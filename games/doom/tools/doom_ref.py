#!/usr/bin/env python3
"""Independent host oracle for the DOOM picture decoders (D2.4) and the BSP
walk (D3.1) of this port.

WHY THIS TOOL EXISTS

``lib/doom/r_data.{h,c}`` decodes the three picture formats a WAD stores: patch
columns, composite textures and flats. Unit tests pin those decoders against
synthetic lumps the test itself wrote, and that is all a CI machine can do -- it
has no WAD. This tool is the other half of the evidence: it reads the real
DOOM1.WAD, decodes the same items in Python from the raw WAD, and prints one
deterministic SHA-256 per item. ``tools/rdat_check.py`` compiles the C decoder,
runs both sides and compares the hashes.

The same tool carries a second oracle. ``--bsp NAME`` parses the map lumps,
builds the fixed-point angle tables, walks the BSP from the player start and
prints every drawseg, every visplane and a hash of both, so
``tools/rbsp_check.py`` can compare it with ``lib/doom/r_bsp.c``. The two
protocols are independent: this file has its own map parser, its own tables,
its own clip list and its own plane set.

A third oracle, ``--segs NAME``, is D3.2's and D3.3's: it composes the wall
textures, reads COLORMAP and the flats, and renders the 20 sampled viewpoints --
four player/deathmatch start positions from THINGS against five view angles --
with its own wall column loop, span maker, plane light, flat lookup and sky path.
It prints the wall hash (unchanged from D3.2), the plane-mask hash and the
finished frame's hash. ``tools/rsegs_check.py`` compares them with
``tools/rsegs_check.c``. It is written from upstream's r_segs.c/r_draw.c,
r_plane.c/r_draw.c and from r_data.h's documented composition and flat
contracts; it does not read the C emitters.

INDEPENDENCE

Nothing here is derived from the C implementation. This file has its own WAD
reader, its own patch walker, its own texture-directory parser, its own
compositor, its own map parser and its own BSP walk; it does not import
``whd_encode.py`` (that tool writes a different format and never decodes
pictures), it does not run the C code, and it does not read the C sources. The
only shared things between the two sides are the formats and the formulas, as
described in the headers and in DOOM's own sources:

  * a patch is ``int16 width, height, leftoffset, topoffset`` then
    ``int32 columnofs[width]``; a column is a chain of
    ``topdelta, length, unused, pixels[length], unused`` posts ending in a
    0xFF topdelta, and post topdelta/length are bytes;
  * a texture is ``char name[8], int32 masked, int16 width, int16 height,
    int32 obsolete, int16 patchcount, patches[10 bytes each]`` inside
    TEXTURE1/TEXTURE2, and its patch indices address PNAMES;
  * a flat is 4096 raw bytes.
  * a map is the marker lump followed by THINGS, LINEDEFS, SIDEDEFS, VERTEXES,
    SEGS, SSECTORS, NODES, SECTORS, REJECT and BLOCKMAP, all packed
    little-endian shorts (see doom_map.h);
  * the angle tables are generated from the formulas in upstream's
    R_InitTables()/R_InitPointToAngle(), and the walk is a port of r_bsp.c,
    R_AddLine/R_ClipSolidWallSegment/R_CheckBBox, and the floor/ceiling marking
    of R_RenderSegLoop.

A disagreement between this file and the C side is therefore either a bug in
one of them or a real-data case the unit tests did not cover. That is the point.

HASH PROTOCOL (identical to tools/rdat_check.c; both are stated here so the two
can be compared without reading each other's code)

  PATCH    for every column x in order: uint16 little-endian length of the
           normalised post stream, then the stream itself. The stream is
           ``[topdelta][length][0][pixels...][0]`` per post plus a final 0xFF --
           the column_t form R_GetColumn() returns.
  TEXTURE  the composed pixel surface (width * height, column-major, byte
           (x * height + y)) followed by the mask plane of the same length,
           0x01 where a patch wrote and 0x00 elsewhere.
  FLAT     the 4096 bytes.

Output is one line per item, stable order, lowercase hex, no timestamps and no
paths: ``PATCH <NAME> <sha256>``, ``TEXTURE <NAME> <sha256>``,
``FLAT <NAME> <sha256>``, plus ``MAP-TEXTURE``/``MAP-FLAT`` listings and
``TEXTURE-UNRESOLVED``/``* -INVALID`` lines. Names come from the WAD and are
not picture data; no decoded byte is printed, so this output is safe to paste
into a report.

Standard library only: argparse, hashlib, struct, sys, pathlib.
"""

from __future__ import annotations

import argparse
import hashlib
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

# --- Format constants (the 1993 WAD, as r_data.h documents it) --------------

WAD_HEADER_SIZE = 12
WAD_DIRECTORY_ENTRY_SIZE = 16
WAD_NAME_SIZE = 8

PATCH_HEADER_SIZE = 8
PATCH_COLUMN_ENTRY_SIZE = 4
POST_HEADER_SIZE = 3
POST_TRAILER_SIZE = 1
POST_END = 0xFF

TEXTURE_NAME_SIZE = 8
TEXTURE_HEADER_SIZE = 22
TEXTURE_PATCH_SIZE = 10
TEXTURE_COUNT_SIZE = 4
TEXTURE_DIRECTORY_ENTRY_SIZE = 4
PNAMES_HEADER_SIZE = 4
PATCH_NAME_SIZE = 8

FLAT_SIZE = 64
FLAT_BYTES = FLAT_SIZE * FLAT_SIZE

TRANSPARENT = 0x00
OPAQUE = 0x01

EXIT_OK = 0
EXIT_INPUT = 1
EXIT_USAGE = 2


class InputError(Exception):
    """The file is not a WAD this tool can read."""


class FormatError(Exception):
    """A lump does not follow the format it claims."""


@dataclass(frozen=True)
class Lump:
    """One WAD directory entry."""

    name: str
    data: bytes


@dataclass(frozen=True)
class Patch:
    """A validated patch: its geometry and its decoded columns."""

    name: str
    width: int
    height: int
    left_offset: int
    top_offset: int
    columns: tuple[bytes, ...]


@dataclass(frozen=True)
class Texture:
    """One validated TEXTURE1/TEXTURE2 definition."""

    name: str
    masked: int
    width: int
    height: int
    patches: tuple[tuple[int, int, int], ...]  # (originx, originy, patch index)


# --- WAD reading ------------------------------------------------------------

def read_wad(path: Path) -> tuple[str, list[Lump]]:
    """Reads a WAD into a name/data list, in directory order.

    Duplicate names are kept: the list is the WAD, not a lookup table. Callers
    that need DOOM's first-match rule use ``first_index``.
    """
    try:
        blob = path.read_bytes()
    except OSError as error:
        raise InputError(f"{path}: {error.strerror}") from error

    if len(blob) < WAD_HEADER_SIZE:
        raise InputError(f"{path}: {len(blob)} bytes is shorter than a WAD header")

    identification = blob[0:4]
    if identification not in (b"IWAD", b"PWAD"):
        raise InputError(f"{path}: identification {identification!r} is neither IWAD nor PWAD")

    count, info_offset = struct.unpack_from("<ii", blob, 4)
    if count < 0:
        raise InputError(f"{path}: negative lump count {count}")
    if info_offset < WAD_HEADER_SIZE or info_offset + count * WAD_DIRECTORY_ENTRY_SIZE > len(blob):
        raise InputError(f"{path}: directory at {info_offset} cannot hold {count} entries")

    lumps: list[Lump] = []
    for index in range(count):
        at = info_offset + index * WAD_DIRECTORY_ENTRY_SIZE
        position, size, raw_name = struct.unpack_from("<ii8s", blob, at)
        if position < 0 or size < 0 or position + size > len(blob):
            raise InputError(f"{path}: lump {index} at {position}+{size} is outside the file")
        name = raw_name.rstrip(b"\0").decode("latin-1").upper()
        lumps.append(Lump(name, blob[position:position + size]))

    return identification.decode("ascii"), lumps


def first_index(lumps: list[Lump], name: str) -> int:
    """DOOM's name lookup: upper case, first match wins, -1 when absent."""
    wanted = name.upper()
    for index, lump in enumerate(lumps):
        if lump.name == wanted:
            return index
    return -1


def lumps_between(lumps: list[Lump], start: str, end: str) -> list[Lump]:
    """The lumps strictly between two markers, as DOOM's S_START/S_END do."""
    first = first_index(lumps, start)
    last = first_index(lumps, end)
    if first < 0 or last < 0 or last < first:
        return []
    return lumps[first + 1:last]


# --- Patches ----------------------------------------------------------------

def parse_patch(name: str, data: bytes) -> Patch:
    """Validates one patch the way r_patch_validate() does and decodes it.

    Every rule the C decoder enforces is stated here independently: the header,
    the column table, each column offset, each post's bytes, the height rule and
    the terminator inside the lump.
    """
    if len(data) < PATCH_HEADER_SIZE:
        raise FormatError(f"{name}: {len(data)} bytes is shorter than a patch header")

    width, height, left_offset, top_offset = struct.unpack_from("<hhhh", data, 0)
    if width < 1:
        raise FormatError(f"{name}: width {width}")
    if height < 1:
        raise FormatError(f"{name}: height {height}")

    table_end = PATCH_HEADER_SIZE + width * PATCH_COLUMN_ENTRY_SIZE
    if table_end > len(data):
        raise FormatError(f"{name}: column table needs {table_end} B of {len(data)}")

    columns: list[bytes] = []
    for column in range(width):
        offset = struct.unpack_from("<i", data, PATCH_HEADER_SIZE + column * 4)[0]
        if offset < table_end or offset >= len(data):
            raise FormatError(f"{name}: column {column} offset {offset} "
                              f"outside {table_end}..{len(data)}")
        columns.append(decode_column(name, column, data, offset, height))

    return Patch(name, width, height, left_offset, top_offset, tuple(columns))


def decode_column(name: str, column: int, data: bytes, start: int, height: int) -> bytes:
    """Normalises one post chain into [topdelta][length][0][pixels][0]... 0xFF."""
    out = bytearray()
    at = start
    while True:
        if at >= len(data):
            raise FormatError(f"{name}: column {column} has no terminator")
        top_delta = data[at]
        if top_delta == POST_END:
            break
        if at + POST_HEADER_SIZE > len(data):
            raise FormatError(f"{name}: column {column} post header past the lump")
        length = data[at + 1]
        if at + POST_HEADER_SIZE + length > len(data):
            raise FormatError(f"{name}: column {column} post pixels past the lump")
        if top_delta + length > height:
            raise FormatError(f"{name}: column {column} post reaches row "
                              f"{top_delta + length} of {height}")
        if at + length + POST_HEADER_SIZE + POST_TRAILER_SIZE >= len(data):
            raise FormatError(f"{name}: column {column} post has no following byte")
        out += bytes((top_delta, length, 0))
        out += data[at + POST_HEADER_SIZE:at + POST_HEADER_SIZE + length]
        out.append(0)
        at += length + POST_HEADER_SIZE + POST_TRAILER_SIZE
    out.append(POST_END)
    return bytes(out)


def patch_digest(patch: Patch) -> str:
    """Hashes the concatenation of length-prefixed normalised columns."""
    digest = hashlib.sha256()
    for column in patch.columns:
        digest.update(struct.pack("<H", len(column)))
        digest.update(column)
    return digest.hexdigest()


# --- Textures ---------------------------------------------------------------

def parse_pnames(data: bytes) -> list[str]:
    """PNAMES: int32 count then count 8-byte names, kept in file case."""
    if len(data) < PNAMES_HEADER_SIZE:
        raise FormatError(f"PNAMES: {len(data)} bytes is shorter than its count")
    count = struct.unpack_from("<i", data, 0)[0]
    if count < 0:
        raise FormatError(f"PNAMES: count {count}")
    if PNAMES_HEADER_SIZE + count * PATCH_NAME_SIZE > len(data):
        raise FormatError(f"PNAMES: {count} names do not fit {len(data)} bytes")
    return [data[PNAMES_HEADER_SIZE + index * PATCH_NAME_SIZE:
                 PNAMES_HEADER_SIZE + (index + 1) * PATCH_NAME_SIZE]
            .rstrip(b"\0").decode("latin-1")
            for index in range(count)]


def parse_textures(lump_name: str, data: bytes) -> list[Texture]:
    """TEXTURE1/TEXTURE2: count, offsets, definitions. Order preserved."""
    if len(data) < TEXTURE_COUNT_SIZE:
        raise FormatError(f"{lump_name}: {len(data)} bytes is shorter than its count")
    count = struct.unpack_from("<i", data, 0)[0]
    if count < 0:
        raise FormatError(f"{lump_name}: negative texture count {count}")
    directory_end = TEXTURE_COUNT_SIZE + count * TEXTURE_DIRECTORY_ENTRY_SIZE
    if directory_end > len(data):
        raise FormatError(f"{lump_name}: directory needs {directory_end} B of {len(data)}")

    textures: list[Texture] = []
    for index in range(count):
        offset = struct.unpack_from("<i", data, TEXTURE_COUNT_SIZE + index * 4)[0]
        if offset < directory_end or offset + TEXTURE_HEADER_SIZE > len(data):
            raise FormatError(f"{lump_name}: texture {index} offset {offset} outside the lump")

        name = data[offset:offset + TEXTURE_NAME_SIZE].rstrip(b"\0").decode("latin-1")
        masked = struct.unpack_from("<i", data, offset + 8)[0]
        width, height = struct.unpack_from("<hh", data, offset + 12)
        patch_count = struct.unpack_from("<h", data, offset + 20)[0]
        if width < 1 or height < 1:
            raise FormatError(f"{lump_name}: texture {name!r} is {width}x{height}")
        if patch_count < 0:
            raise FormatError(f"{lump_name}: texture {name!r} has {patch_count} patches")
        if offset + TEXTURE_HEADER_SIZE + patch_count * TEXTURE_PATCH_SIZE > len(data):
            raise FormatError(f"{lump_name}: texture {name!r} patch list past the lump")

        patches = []
        for patch_index in range(patch_count):
            at = offset + TEXTURE_HEADER_SIZE + patch_index * TEXTURE_PATCH_SIZE
            origin_x, origin_y, index_in_pnames = struct.unpack_from("<hhh", data, at)
            patches.append((origin_x, origin_y, index_in_pnames))

        textures.append(Texture(name, masked, width, height, tuple(patches)))

    return textures


def compose_texture(texture: Texture, resolved: dict[int, Patch]) -> tuple[bytes, bytes]:
    """Blits every patch of a texture into one column-major surface plus mask.

    The fill is TRANSPARENT (0) everywhere and OPAQUE (1) where a post wrote.
    Later patches overwrite earlier ones. Clipping matches R_DrawColumnInCache():
    a post above the surface loses those pixels, one below it is shortened, and
    the source pointer advances with the clipped amount (DOOM's original forgets
    that and shifts the block; r_data.h documents the corrected choice).

    `resolved` maps a texture patch index to its decoded PNAMES patch, so the
    caller owns the "which lump" decision and this function only draws.
    """
    width, height = texture.width, texture.height
    pixels = bytearray([TRANSPARENT]) * (width * height)
    mask = bytearray([0]) * (width * height)

    for origin_x, origin_y, patch_index in texture.patches:
        patch = resolved[patch_index]
        x_first = max(0, origin_x)
        x_last = min(width, origin_x + patch.width)
        for x in range(x_first, x_last):
            stream = patch.columns[x - origin_x]
            at = 0
            while stream[at] != POST_END:
                top_delta = stream[at]
                length = stream[at + 1]
                source = at + POST_HEADER_SIZE
                position = origin_y + top_delta
                count = length
                if position < 0:
                    source += -position
                    count += position
                    position = 0
                if position + count > height:
                    count = height - position
                if count > 0:
                    destination = x * height + position
                    pixels[destination:destination + count] = stream[source:source + count]
                    mask[destination:destination + count] = bytes([OPAQUE]) * count
                at += length + POST_HEADER_SIZE + POST_TRAILER_SIZE

    return bytes(pixels), bytes(mask)


def texture_digest(pixels: bytes, mask: bytes) -> str:
    digest = hashlib.sha256()
    digest.update(pixels)
    digest.update(mask)
    return digest.hexdigest()


# --- Flats ------------------------------------------------------------------

def flat_digest(data: bytes) -> str:
    if len(data) != FLAT_BYTES:
        raise FormatError(f"flat is {len(data)} B, not {FLAT_BYTES}")
    return hashlib.sha256(data).hexdigest()


# --- Map listing ------------------------------------------------------------

MAP_LUMPS = ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
             "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP")

SIDEDEF_SIZE = 30
SECTOR_SIZE = 26


def map_textures_and_flats(lumps: list[Lump], map_name: str) -> tuple[list[str], list[str]]:
    """The texture names of a map's sidedefs and the flat names of its sectors.

    A map is the marker lump followed by the ten lumps in MAP_LUMPS order, so
    the lookup is positional and cannot be confused by a duplicate name.
    """
    marker = first_index(lumps, map_name)
    if marker < 0:
        raise InputError(f"{map_name}: map marker not found")
    if marker + len(MAP_LUMPS) >= len(lumps):
        raise InputError(f"{map_name}: not enough lumps after the marker")

    sidedefs = lumps[marker + 1 + MAP_LUMPS.index("SIDEDEFS")].data
    sectors = lumps[marker + 1 + MAP_LUMPS.index("SECTORS")].data

    textures: list[str] = []
    for at in range(0, len(sidedefs) - SIDEDEF_SIZE + 1, SIDEDEF_SIZE):
        for offset in (4, 12, 20):
            name = sidedefs[at + offset:at + offset + 8].rstrip(b"\0").decode("latin-1").upper()
            if name and name != "-":
                textures.append(name)

    flats: list[str] = []
    for at in range(0, len(sectors) - SECTOR_SIZE + 1, SECTOR_SIZE):
        for offset in (4, 12):
            name = sectors[at + offset:at + offset + 8].rstrip(b"\0").decode("latin-1").upper()
            if name:
                flats.append(name)

    return sorted(dict.fromkeys(textures)), sorted(dict.fromkeys(flats))


# --- Fixed point (the renderer's 16.16, reimplemented independently) --------

FRACBITS = 16
FRACUNIT = 1 << FRACBITS
ANG45 = 0x20000000
ANG90 = 0x40000000
ANG180 = 0x80000000
ANG270 = 0xC0000000
ANGLETOFINESHIFT = 19
FINEANGLES = 8192
SLOPERANGE = 2048
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
FIELDOFVIEW = 2048
HEIGHTBITS = 12
HEIGHTUNIT = 1 << HEIGHTBITS
INT32_MAX = 0x7FFFFFFF
INT32_MIN = -0x80000000

# The BSP half of a map, as doomdata.h packs it. Ten lumps after the marker.
BSP_MAP_LUMPS = ("THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
                 "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP")
MAP_RECORD_SIZE = {
    "THINGS": 10, "LINEDEFS": 14, "SIDEDEFS": 30, "VERTEXES": 4,
    "SEGS": 12, "SSECTORS": 4, "NODES": 28, "SECTORS": 26,
}


def u32(value: int) -> int:
    """C's unsigned 32-bit wrap for angle arithmetic."""
    return value & 0xFFFFFFFF


def i32(value: int) -> int:
    """C's int32 reinterpretation of a wrapped 32-bit value."""
    value &= 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def fixed_mul(a: int, b: int) -> int:
    """(a * b) >> 16 with an arithmetic shift, matching the C inline."""
    return (a * b) >> FRACBITS


def trunc_div(numerator: int, denominator: int) -> int:
    """C integer division: truncates toward zero, unlike Python's //."""
    quotient = abs(numerator) // abs(denominator)
    return -quotient if (numerator < 0) != (denominator < 0) else quotient


def fixed_div2(a: int, b: int) -> int:
    return trunc_div(a << FRACBITS, b)


def fixed_div(a: int, b: int) -> int:
    if (abs(a) >> 14) >= abs(b):
        return INT32_MIN if (a ^ b) < 0 else INT32_MAX
    return fixed_div2(a, b)


def slope_div(num: int, den: int) -> int:
    if den < 512:
        return SLOPERANGE
    answer = (num << 3) // (den >> 8)
    return answer if answer <= SLOPERANGE else SLOPERANGE


# The same formulas tools/gen_r_tables.py uses. Independent of the C arrays:
# this is the oracle's own derivation of the same documented tables.
def _fine_sine() -> tuple[int, ...]:
    return tuple(int(FRACUNIT * math.sin((i + 0.5) * math.pi * 2 / FINEANGLES))
                 for i in range(5 * FINEANGLES // 4))


def _fine_tangent() -> tuple[int, ...]:
    return tuple(int(FRACUNIT * math.tan((i - FINEANGLES // 4 + 0.5) * math.pi * 2 / FINEANGLES))
                 for i in range(FINEANGLES // 2))


def _tan_to_angle() -> tuple[int, ...]:
    def single(value: float) -> float:
        return struct.unpack("f", struct.pack("f", value))[0]

    return tuple(int(0x100000000 * (math.atan(single(i / SLOPERANGE)) / (3.141592657 * 2)))
                 & 0xFFFFFFFF for i in range(SLOPERANGE + 1))


def _view_angle_to_x(tangent: tuple[int, ...]) -> tuple[int, ...]:
    centerx = SCREEN_WIDTH // 2
    centerxfrac = centerx << FRACBITS
    focallength = (centerxfrac << FRACBITS) // tangent[FINEANGLES // 4 + FIELDOFVIEW // 2]
    columns: list[int] = []
    for i in range(FINEANGLES // 2):
        if tangent[i] > 2 * FRACUNIT:
            column = -1
        elif tangent[i] < -2 * FRACUNIT:
            column = SCREEN_WIDTH + 1
        else:
            column = fixed_mul(tangent[i], focallength)
            column = (centerxfrac - column + FRACUNIT - 1) >> FRACBITS
            column = max(-1, min(SCREEN_WIDTH + 1, column))
        columns.append(column)
    return tuple(columns)


def _x_to_view_angle(columns: tuple[int, ...]) -> tuple[int, ...]:
    table: list[int] = []
    for x in range(SCREEN_WIDTH + 1):
        index = 0
        while columns[index] > x:
            index += 1
        table.append(u32((index << ANGLETOFINESHIFT) - ANG90))
    return tuple(table)


FINE_SINE = _fine_sine()
_TANGENT = _fine_tangent()
TAN_TO_ANGLE = _tan_to_angle()
VIEW_ANGLE_TO_X = _view_angle_to_x(_TANGENT)
# The fencepost pass R_InitTextureMapping() applies after xtoviewangle exists.
VIEW_ANGLE_TO_X = tuple(0 if c == -1 else (SCREEN_WIDTH if c == SCREEN_WIDTH + 1 else c)
                        for c in VIEW_ANGLE_TO_X)
X_TO_VIEW_ANGLE = _x_to_view_angle(VIEW_ANGLE_TO_X)
CLIP_ANGLE = X_TO_VIEW_ANGLE[0]


# --- Map data (an independent parse of the same 1993 lumps) -----------------

def _i16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<h", data, offset)[0]


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _fixed16(value: int) -> int:
    return value << FRACBITS


def _name8(data: bytes, offset: int) -> bytes:
    return data[offset:offset + 8]


def has_texture(name: bytes) -> bool:
    """DOOM maps a name starting with '-' (or an empty field) to texture 0."""
    return bool(name) and name[0] not in (0, ord("-"))


@dataclass(frozen=True)
class BspMap:
    """The ten lumps of one level, parsed into the forms the walk consumes."""

    name: str
    things: tuple[dict, ...]
    vertexes: tuple[dict, ...]
    linedefs: tuple[dict, ...]
    sidedefs: tuple[dict, ...]
    segs: tuple[dict, ...]
    ssectors: tuple[dict, ...]
    nodes: tuple[dict, ...]
    sectors: tuple[dict, ...]


def read_bsp_map(lumps: list[Lump], map_name: str) -> BspMap:
    """Validates and parses one map, independently of the C loader."""
    marker = first_index(lumps, map_name)
    if marker < 0:
        raise InputError(f"{map_name}: map marker not found")
    if marker + len(BSP_MAP_LUMPS) >= len(lumps):
        raise InputError(f"{map_name}: not enough lumps after the marker")

    raw: dict[str, bytes] = {}
    for index, lump_name in enumerate(BSP_MAP_LUMPS):
        raw[lump_name] = lumps[marker + 1 + index].data
    for lump_name, size in MAP_RECORD_SIZE.items():
        if len(raw[lump_name]) % size:
            raise FormatError(f"{map_name} {lump_name}: {len(raw[lump_name])} B "
                              f"is not a whole number of {size}-byte records")

    vertexes = tuple({"x": _fixed16(_i16(raw["VERTEXES"], i * 4)),
                      "y": _fixed16(_i16(raw["VERTEXES"], i * 4 + 2))}
                     for i in range(len(raw["VERTEXES"]) // 4))

    sectors = tuple({
        "floor": _fixed16(_i16(raw["SECTORS"], i * 26)),
        "ceiling": _fixed16(_i16(raw["SECTORS"], i * 26 + 2)),
        "floorpic": _name8(raw["SECTORS"], i * 26 + 4),
        "ceilingpic": _name8(raw["SECTORS"], i * 26 + 12),
        "light": _i16(raw["SECTORS"], i * 26 + 20),
        "special": _i16(raw["SECTORS"], i * 26 + 22),
        "tag": _i16(raw["SECTORS"], i * 26 + 24),
    } for i in range(len(raw["SECTORS"]) // 26))

    sidedefs = tuple({
        "textureoffset": _fixed16(_i16(raw["SIDEDEFS"], i * 30)),
        "rowoffset": _fixed16(_i16(raw["SIDEDEFS"], i * 30 + 2)),
        "top": _name8(raw["SIDEDEFS"], i * 30 + 4),
        "bottom": _name8(raw["SIDEDEFS"], i * 30 + 12),
        "mid": _name8(raw["SIDEDEFS"], i * 30 + 20),
        "sector": _i16(raw["SIDEDEFS"], i * 30 + 28),
    } for i in range(len(raw["SIDEDEFS"]) // 30))

    linedefs = tuple({
        "v1": _u16(raw["LINEDEFS"], i * 14),
        "v2": _u16(raw["LINEDEFS"], i * 14 + 2),
        "flags": _i16(raw["LINEDEFS"], i * 14 + 4),
        "special": _i16(raw["LINEDEFS"], i * 14 + 6),
        "tag": _i16(raw["LINEDEFS"], i * 14 + 8),
        "side0": _i16(raw["LINEDEFS"], i * 14 + 10),
        "side1": _i16(raw["LINEDEFS"], i * 14 + 12),
    } for i in range(len(raw["LINEDEFS"]) // 14))

    segs = tuple({
        "v1": _u16(raw["SEGS"], i * 12),
        "v2": _u16(raw["SEGS"], i * 12 + 2),
        "angle": u32(_u16(raw["SEGS"], i * 12 + 4) << 16),
        "linedef": _u16(raw["SEGS"], i * 12 + 6),
        "side": _u16(raw["SEGS"], i * 12 + 8),
        "offset": _fixed16(_i16(raw["SEGS"], i * 12 + 10)),
    } for i in range(len(raw["SEGS"]) // 12))

    ssectors = tuple({
        "numsegs": _u16(raw["SSECTORS"], i * 4),
        "firstseg": _u16(raw["SSECTORS"], i * 4 + 2),
    } for i in range(len(raw["SSECTORS"]) // 4))

    nodes = tuple({
        "x": _fixed16(_i16(raw["NODES"], i * 28)),
        "y": _fixed16(_i16(raw["NODES"], i * 28 + 2)),
        "dx": _fixed16(_i16(raw["NODES"], i * 28 + 4)),
        "dy": _fixed16(_i16(raw["NODES"], i * 28 + 6)),
        "bbox": tuple(tuple(_fixed16(_i16(raw["NODES"], i * 28 + 8 + child * 8 + axis * 2))
                            for axis in range(4)) for child in range(2)),
        "children": (_u16(raw["NODES"], i * 28 + 24),
                     _u16(raw["NODES"], i * 28 + 26)),
    } for i in range(len(raw["NODES"]) // 28))

    things = tuple({
        "x": _i16(raw["THINGS"], i * 10),
        "y": _i16(raw["THINGS"], i * 10 + 2),
        "angle": _i16(raw["THINGS"], i * 10 + 4),
        "type": _i16(raw["THINGS"], i * 10 + 6),
        "options": _i16(raw["THINGS"], i * 10 + 8),
    } for i in range(len(raw["THINGS"]) // 10))

    # The indices the walk follows are proved here, once, exactly as the C
    # loader proves them; a raised FormatError is a bad map, not a walk result.
    for index, line in enumerate(linedefs):
        for vertex in (line["v1"], line["v2"]):
            if vertex >= len(vertexes):
                raise FormatError(f"{map_name} LINEDEFS[{index}]: vertex {vertex} "
                                  f"outside {len(vertexes)}")
        for side in (line["side0"], line["side1"]):
            if side != -1 and side >= len(sidedefs):
                raise FormatError(f"{map_name} LINEDEFS[{index}]: side {side} "
                                  f"outside {len(sidedefs)}")
        if line["flags"] & 4 and (line["side0"] < 0 or line["side1"] < 0):
            raise FormatError(f"{map_name} LINEDEFS[{index}]: two-sided without both sides")

    for index, side in enumerate(sidedefs):
        if side["sector"] < 0 or side["sector"] >= len(sectors):
            raise FormatError(f"{map_name} SIDEDEFS[{index}]: sector {side['sector']} "
                              f"outside {len(sectors)}")

    for index, seg in enumerate(segs):
        if seg["v1"] >= len(vertexes) or seg["v2"] >= len(vertexes):
            raise FormatError(f"{map_name} SEGS[{index}]: vertex outside {len(vertexes)}")
        if seg["linedef"] >= len(linedefs):
            raise FormatError(f"{map_name} SEGS[{index}]: linedef outside {len(linedefs)}")
        if seg["side"] > 1:
            raise FormatError(f"{map_name} SEGS[{index}]: side {seg['side']}")

    for index, subsector in enumerate(ssectors):
        if subsector["numsegs"] == 0 or subsector["firstseg"] + subsector["numsegs"] > len(segs):
            raise FormatError(f"{map_name} SSECTORS[{index}]: seg range outside SEGS")

    for index, node in enumerate(nodes):
        for child in node["children"]:
            if child & 0x8000:
                if (child & 0x7FFF) >= len(ssectors):
                    raise FormatError(f"{map_name} NODES[{index}]: subsector child out of range")
            elif child >= len(nodes):
                raise FormatError(f"{map_name} NODES[{index}]: node child out of range")

    return BspMap(map_name, things, vertexes, linedefs, sidedefs, segs,
                  ssectors, nodes, sectors)


# --- The BSP walk (D3.1) ----------------------------------------------------

class WalkError(Exception):
    """The walk hit one of its documented limits."""


class PlaneOverflow(WalkError):
    """R_FindPlane / R_CheckPlane ran out of plane storage."""


class ClipOverflow(WalkError):
    """The solid clip list ran out of posts."""


def point_on_side(x: int, y: int, node: dict) -> int:
    """DOOM's R_PointOnSide(): 0 for the front half-plane, 1 for the back."""
    if node["dx"] == 0:
        if x <= node["x"]:
            return 1 if node["dy"] > 0 else 0
        return 1 if node["dy"] < 0 else 0
    if node["dy"] == 0:
        if y <= node["y"]:
            return 1 if node["dx"] < 0 else 0
        return 1 if node["dx"] > 0 else 0

    dx = i32(x - node["x"])
    dy = i32(y - node["y"])

    if (node["dy"] ^ node["dx"] ^ dx ^ dy) & 0x80000000:
        return 1 if (node["dy"] ^ dx) & 0x80000000 else 0

    left = fixed_mul(node["dy"] >> FRACBITS, dx)
    right = fixed_mul(dy, node["dx"] >> FRACBITS)
    return 0 if right < left else 1


def point_to_angle(viewx: int, viewy: int, x: int, y: int) -> int:
    """DOOM's R_PointToAngle(): the 32-bit angle from the viewpoint to a point."""
    x = i32(x - viewx)
    y = i32(y - viewy)
    if x == 0 and y == 0:
        return 0

    if x >= 0:
        if y >= 0:
            if x > y:
                return TAN_TO_ANGLE[slope_div(y, x)]
            return u32(ANG90 - 1 - TAN_TO_ANGLE[slope_div(x, y)])
        y = -y
        if x > y:
            return u32(-TAN_TO_ANGLE[slope_div(y, x)])
        return u32(ANG270 + TAN_TO_ANGLE[slope_div(x, y)])

    x = -x
    if y >= 0:
        if x > y:
            return u32(ANG180 - 1 - TAN_TO_ANGLE[slope_div(y, x)])
        return u32(ANG90 + TAN_TO_ANGLE[slope_div(x, y)])
    y = -y
    if x > y:
        return u32(ANG180 + TAN_TO_ANGLE[slope_div(y, x)])
    return u32(ANG270 - 1 - TAN_TO_ANGLE[slope_div(x, y)])


def point_to_dist(viewx: int, viewy: int, x: int, y: int) -> int:
    """DOOM's R_PointToDist(): the distance to a point, in 16.16."""
    dx = abs(x - viewx)
    dy = abs(y - viewy)
    if dy > dx:
        dx, dy = dy, dx
    angle = u32((TAN_TO_ANGLE[fixed_div(dy, dx) >> (FRACBITS - 11)] + ANG90) >> ANGLETOFINESHIFT)
    return fixed_div(dx, FINE_SINE[angle])


def scale_from_global_angle(state: "WalkState", visangle: int) -> int:
    """DOOM's R_ScaleFromGlobalAngle(), with detailshift == 0."""
    anglea = u32(ANG90 + u32(visangle - state.viewangle))
    angleb = u32(ANG90 + u32(visangle - state.rw_normalangle))
    sinea = FINE_SINE[anglea >> ANGLETOFINESHIFT]
    sineb = FINE_SINE[angleb >> ANGLETOFINESHIFT]
    num = fixed_mul(state.projection, sineb)
    den = fixed_mul(state.rw_distance, sinea)

    if den > (num >> 16):
        scale = fixed_div(num, den)
        if scale > 64 * FRACUNIT:
            return 64 * FRACUNIT
        if scale < 256:
            return 256
        return scale
    return 64 * FRACUNIT


class Visplane:
    """One floor/ceiling plane: DOOM's visplane_t, key and column arrays."""

    __slots__ = ("height", "pic", "light", "minx", "maxx", "top", "bottom")

    def __init__(self, height: int, pic: bytes, light: int) -> None:
        self.height = height
        self.pic = pic
        self.light = light
        self.minx = SCREEN_WIDTH
        self.maxx = -1
        self.top = bytearray([0xFF]) * SCREEN_WIDTH
        self.bottom = bytearray([0xFF]) * SCREEN_WIDTH


def find_plane(planes: list[Visplane], capacity: int, height: int, pic: bytes,
               light: int, sky: bytes | None) -> Visplane:
    if pic == sky:
        height = 0
        light = 0
    for plane in planes:
        if plane.height == height and plane.pic == pic and plane.light == light:
            return plane
    if len(planes) >= capacity:
        raise PlaneOverflow(f"visplane capacity {capacity} reached")
    plane = Visplane(height, pic, light)
    planes.append(plane)
    return plane


def check_plane(planes: list[Visplane], capacity: int, plane: Visplane,
                start: int, stop: int) -> Visplane:
    if start < plane.minx:
        intrl, unionl = plane.minx, start
    else:
        unionl, intrl = plane.minx, start
    if stop > plane.maxx:
        intrh, unionh = plane.maxx, stop
    else:
        unionh, intrh = plane.maxx, stop

    x = intrl
    while x <= intrh:
        if plane.top[x] != 0xFF:
            break
        x += 1

    if x > intrh:
        plane.minx = unionl
        plane.maxx = unionh
        return plane

    if len(planes) >= capacity:
        raise PlaneOverflow(f"visplane capacity {capacity} reached")
    split = Visplane(plane.height, plane.pic, plane.light)
    split.minx = start
    split.maxx = stop
    planes.append(split)
    return split


CHECK_COORD = (
    (3, 0, 2, 1), (3, 0, 2, 0), (3, 1, 2, 0), (0,),
    (2, 0, 2, 1), (0, 0, 0, 0), (3, 1, 3, 0), (0,),
    (2, 0, 3, 1), (2, 1, 3, 1), (2, 1, 3, 0),
)
BOXTOP, BOXBOTTOM, BOXLEFT, BOXRIGHT = 0, 1, 2, 3


class WalkState:
    """Everything one view needs; the Python twin of r_view_state_t."""

    def __init__(self, bsp: BspMap, maxdrawsegs: int, maxplanes: int) -> None:
        self.bsp = bsp
        self.maxdrawsegs = maxdrawsegs
        self.planeCap = maxplanes
        self.viewx = 0
        self.viewy = 0
        self.viewz = 0
        self.viewangle = 0
        self.extralight = 0
        self.sky = b"F_SKY1\x00\x00"
        self.projection = (SCREEN_WIDTH // 2) << FRACBITS
        self.centerfrac = (SCREEN_HEIGHT // 2) << FRACBITS
        self.drawsegs: list[dict] = []
        self.drawseg_overflow = False
        self.planes: list[Visplane] = []
        self.solid: list[list[int]] = []
        self.floorclip = [0] * SCREEN_WIDTH
        self.ceilingclip = [0] * SCREEN_WIDTH
        self.frontsector = 0
        self.floorplane: Visplane | None = None
        self.ceilingplane: Visplane | None = None
        self.currentseg = 0
        self.rw_angle1 = 0
        self.rw_normalangle = 0
        self.rw_distance = 0
        self.rw_x = 0
        self.rw_stopx = 0
        self.rw_scale = 0
        self.rw_scalestep = 0

    # --- clipping ------------------------------------------------------

    def clear_clip_segs(self) -> None:
        self.solid = [[-0x7FFFFFFF, -1], [SCREEN_WIDTH, 0x7FFFFFFF]]

    def clip_solid(self, first: int, last: int) -> None:
        solid = self.solid
        start = 0
        while solid[start][1] < first - 1:
            start += 1

        if first < solid[start][0]:
            if last < solid[start][0] - 1:
                self.store_wall_range(first, last)
                if len(solid) >= 32:
                    raise ClipOverflow("solid clip list capacity 32 reached")
                solid.insert(start, [first, last])
                return
            self.store_wall_range(first, solid[start][0] - 1)
            solid[start][0] = first

        if last <= solid[start][1]:
            return

        nxt = start
        while last >= solid[nxt + 1][0] - 1:
            self.store_wall_range(solid[nxt][1] + 1, solid[nxt + 1][0] - 1)
            nxt += 1
            if last <= solid[nxt][1]:
                solid[start][1] = solid[nxt][1]
                if nxt == start:
                    return
                del solid[start + 1:nxt + 1]
                return

        self.store_wall_range(solid[nxt][1] + 1, last)
        solid[start][1] = last
        if nxt == start:
            return
        del solid[start + 1:nxt + 1]

    def clip_pass(self, first: int, last: int) -> None:
        solid = self.solid
        start = 0
        while solid[start][1] < first - 1:
            start += 1

        if first < solid[start][0]:
            if last < solid[start][0] - 1:
                self.store_wall_range(first, last)
                return
            self.store_wall_range(first, solid[start][0] - 1)

        if last <= solid[start][1]:
            return

        while last >= solid[start + 1][0] - 1:
            self.store_wall_range(solid[start][1] + 1, solid[start + 1][0] - 1)
            start += 1
            if last <= solid[start][1]:
                return

        self.store_wall_range(solid[start][1] + 1, last)

    # --- one seg -------------------------------------------------------

    def add_line(self, seg_index: int) -> None:
        bsp = self.bsp
        seg = bsp.segs[seg_index]
        v1 = bsp.vertexes[seg["v1"]]
        v2 = bsp.vertexes[seg["v2"]]

        self.currentseg = seg_index
        angle1 = point_to_angle(self.viewx, self.viewy, v1["x"], v1["y"])
        angle2 = point_to_angle(self.viewx, self.viewy, v2["x"], v2["y"])

        span = u32(angle1 - angle2)
        if span >= ANG180:
            return

        self.rw_angle1 = angle1
        angle1 = u32(angle1 - self.viewangle)
        angle2 = u32(angle2 - self.viewangle)

        tspan = u32(angle1 + CLIP_ANGLE)
        if tspan > u32(2 * CLIP_ANGLE):
            tspan = u32(tspan - u32(2 * CLIP_ANGLE))
            if tspan >= span:
                return
            angle1 = CLIP_ANGLE

        tspan = u32(CLIP_ANGLE - angle2)
        if tspan > u32(2 * CLIP_ANGLE):
            tspan = u32(tspan - u32(2 * CLIP_ANGLE))
            if tspan >= span:
                return
            angle2 = u32(-CLIP_ANGLE)

        x1 = VIEW_ANGLE_TO_X[u32(angle1 + ANG90) >> ANGLETOFINESHIFT]
        x2 = VIEW_ANGLE_TO_X[u32(angle2 + ANG90) >> ANGLETOFINESHIFT]
        if x1 == x2:
            return

        line = bsp.linedefs[seg["linedef"]]
        side_index = line["side0"] if seg["side"] == 0 else line["side1"]
        if line["flags"] & 4:
            back_index = line["side1"] if seg["side"] == 0 else line["side0"]
            back_sector = bsp.sidedefs[back_index]["sector"]
        else:
            back_sector = None

        front_sector = self.frontsector
        front = bsp.sectors[front_sector]
        if back_sector is None:
            self.clip_solid(x1, x2 - 1)
            return

        back = bsp.sectors[back_sector]
        if back["ceiling"] <= front["floor"] or back["floor"] >= front["ceiling"]:
            self.clip_solid(x1, x2 - 1)
            return
        if back["ceiling"] != front["ceiling"] or back["floor"] != front["floor"]:
            self.clip_pass(x1, x2 - 1)
            return
        if (back["ceilingpic"] == front["ceilingpic"]
                and back["floorpic"] == front["floorpic"]
                and back["light"] == front["light"]
                and not has_texture(bsp.sidedefs[side_index]["mid"])):
            return
        self.clip_pass(x1, x2 - 1)

    # --- one wall fragment ---------------------------------------------

    def store_wall_range(self, start: int, stop: int) -> None:
        if len(self.drawsegs) >= self.maxdrawsegs:
            self.drawseg_overflow = True
            return

        bsp = self.bsp
        seg = bsp.segs[self.currentseg]
        line = bsp.linedefs[seg["linedef"]]
        side = bsp.sidedefs[line["side0"] if seg["side"] == 0 else line["side1"]]
        front = bsp.sectors[self.frontsector]
        if line["flags"] & 4:
            back_index = line["side1"] if seg["side"] == 0 else line["side0"]
            back = bsp.sectors[bsp.sidedefs[back_index]["sector"]]
        else:
            back = None

        # rw_distance, for the scale.
        self.rw_normalangle = u32(seg["angle"] + ANG90)
        offsetangle = abs(i32(u32(self.rw_normalangle - self.rw_angle1)))
        if offsetangle > ANG90:
            offsetangle = ANG90
        distangle = u32(ANG90 - offsetangle)
        v1 = bsp.vertexes[seg["v1"]]
        hyp = point_to_dist(self.viewx, self.viewy, v1["x"], v1["y"])
        sineval = FINE_SINE[distangle >> ANGLETOFINESHIFT]
        self.rw_distance = fixed_mul(hyp, sineval)

        self.rw_x = start
        self.rw_stopx = stop + 1
        scale1 = scale_from_global_angle(self, u32(self.viewangle +
                                                   X_TO_VIEW_ANGLE[start]))
        if stop > start:
            scale2 = scale_from_global_angle(self, u32(self.viewangle +
                                                       X_TO_VIEW_ANGLE[stop]))
            scalestep = trunc_div(scale2 - scale1, stop - start)
        else:
            scale2 = scale1
            scalestep = 0
        self.rw_scale = scale1
        self.rw_scalestep = scalestep

        worldtop = front["ceiling"] - self.viewz
        worldbottom = front["floor"] - self.viewz
        worldhigh = worldlow = 0
        wall_mid = wall_top = wall_bottom = 0
        silhouette = 0
        bottom_sil = INT32_MAX
        top_sil = INT32_MIN
        markfloor = markceiling = 0

        if back is None:
            wall_mid = 1 if has_texture(side["mid"]) else 0
            markfloor = markceiling = 1
            silhouette = 3
        else:
            if front["floor"] > back["floor"]:
                silhouette = 1
                bottom_sil = front["floor"]
            elif back["floor"] > self.viewz:
                silhouette = 1
                bottom_sil = INT32_MAX
            if front["ceiling"] < back["ceiling"]:
                silhouette |= 2
                top_sil = front["ceiling"]
            elif back["ceiling"] < self.viewz:
                silhouette |= 2
                top_sil = INT32_MIN
            if back["ceiling"] <= front["floor"]:
                bottom_sil = INT32_MAX
                silhouette |= 1
            if back["floor"] >= front["ceiling"]:
                top_sil = INT32_MIN
                silhouette |= 2

            worldhigh = back["ceiling"] - self.viewz
            worldlow = back["floor"] - self.viewz

            if front["ceilingpic"] == self.sky and back["ceilingpic"] == self.sky:
                worldtop = worldhigh

            if (worldlow != worldbottom or back["floorpic"] != front["floorpic"]
                    or back["light"] != front["light"]):
                markfloor = 1
            if (worldhigh != worldtop or back["ceilingpic"] != front["ceilingpic"]
                    or back["light"] != front["light"]):
                markceiling = 1
            if back["ceiling"] <= front["floor"] or back["floor"] >= front["ceiling"]:
                markceiling = markfloor = 1

            if worldhigh < worldtop:
                wall_top = 1 if has_texture(side["top"]) else 0
            if worldlow > worldbottom:
                wall_bottom = 1 if has_texture(side["bottom"]) else 0

        if front["floor"] >= self.viewz:
            markfloor = 0
        if front["ceiling"] <= self.viewz and front["ceilingpic"] != self.sky:
            markceiling = 0

        # R_StoreWallRange's texture-origin block, for the column emitter (D3.2).
        # The C side stores these three per fragment because only this function
        # can produce them from the seg angle and the sidedefs; the oracle
        # derives them the same way and hands them to its own emitter.
        delta = u32(self.rw_normalangle - self.rw_angle1)
        offset_angle = delta
        if offset_angle > ANG180:
            offset_angle = u32(-offset_angle)
        if offset_angle > ANG90:
            offset_angle = ANG90
        rw_offset = fixed_mul(hyp, FINE_SINE[offset_angle >> ANGLETOFINESHIFT])
        if delta < ANG180:
            rw_offset = -rw_offset
        rw_offset = i32(rw_offset + side["textureoffset"] + seg["offset"])
        rw_centerangle = u32(ANG90 + self.viewangle - self.rw_normalangle)

        # The pre-shift heights: 12.4 below, but the pegging and the sky-aware
        # worldtop the emitter needs are 16.16.
        top16, bottom16, high16, low16 = worldtop, worldbottom, worldhigh, worldlow

        worldtop >>= 4
        worldbottom >>= 4
        self.worldtop = worldtop
        self.worldbottom = worldbottom
        topstep = -fixed_mul(scalestep, worldtop)
        topfrac = (self.centerfrac >> 4) - fixed_mul(worldtop, scale1)
        bottomstep = -fixed_mul(scalestep, worldbottom)
        bottomfrac = (self.centerfrac >> 4) - fixed_mul(worldbottom, scale1)
        self.topstep = topstep
        self.topfrac = topfrac
        self.bottomstep = bottomstep
        self.bottomfrac = bottomfrac
        self.markfloor = markfloor
        self.markceiling = markceiling
        self.wall_mid = wall_mid
        self.wall_top = wall_top
        self.wall_bottom = wall_bottom

        if back is not None:
            worldhigh >>= 4
            worldlow >>= 4
            self.worldhigh = worldhigh
            self.worldlow = worldlow
            if worldhigh < worldtop:
                self.pixhigh = (self.centerfrac >> 4) - fixed_mul(worldhigh, scale1)
                self.pixhighstep = -fixed_mul(scalestep, worldhigh)
            if worldlow > worldbottom:
                self.pixlow = (self.centerfrac >> 4) - fixed_mul(worldlow, scale1)
                self.pixlowstep = -fixed_mul(scalestep, worldlow)

        lightnum = (front["light"] >> 4) + self.extralight
        if bsp.vertexes[seg["v1"]]["y"] == bsp.vertexes[seg["v2"]]["y"]:
            lightnum -= 1
        elif bsp.vertexes[seg["v1"]]["x"] == bsp.vertexes[seg["v2"]]["x"]:
            lightnum += 1
        self.lightnum = lightnum

        if markceiling:
            self.ceilingplane = check_plane(self.planes, self.planeCap,
                                            self.ceilingplane, self.rw_x,
                                            self.rw_stopx - 1)
        if markfloor:
            self.floorplane = check_plane(self.planes, self.planeCap,
                                          self.floorplane, self.rw_x,
                                          self.rw_stopx - 1)

        self.render_seg_loop()

        self.drawsegs.append({
            "seg": self.currentseg,
            "x1": start,
            "x2": stop,
            "scale1": scale1,
            "scale2": scale2,
            "scalestep": scalestep,
            "silhouette": silhouette,
            "bottom": bottom_sil,
            "top": top_sil,
            "light": front["light"],
            "lightnum": lightnum,
            # D3.2: what the column emitter reads.
            "distance": self.rw_distance,
            "offset": rw_offset,
            "centerangle": rw_centerangle,
            "top16": top16,
            "bottom16": bottom16,
            "high16": high16,
            "low16": low16,
            "wall_mid": wall_mid,
            "wall_top": wall_top,
            "wall_bottom": wall_bottom,
            "markfloor": markfloor,
            "markceiling": markceiling,
            "has_back": back is not None,
            "flags": line["flags"],
            "side": side,
        })

    def render_seg_loop(self) -> None:
        x = self.rw_x
        while x < self.rw_stopx:
            yl = (self.topfrac + HEIGHTUNIT - 1) >> HEIGHTBITS
            if yl < self.ceilingclip[x] + 1:
                yl = self.ceilingclip[x] + 1

            if self.markceiling:
                top = self.ceilingclip[x] + 1
                bottom = yl - 1
                if bottom >= self.floorclip[x]:
                    bottom = self.floorclip[x] - 1
                if top <= bottom:
                    self.ceilingplane.top[x] = top
                    self.ceilingplane.bottom[x] = bottom

            yh = self.bottomfrac >> HEIGHTBITS
            if yh >= self.floorclip[x]:
                yh = self.floorclip[x] - 1

            if self.markfloor:
                top = yh + 1
                bottom = self.floorclip[x] - 1
                if top <= self.ceilingclip[x]:
                    top = self.ceilingclip[x] + 1
                if top <= bottom:
                    self.floorplane.top[x] = top
                    self.floorplane.bottom[x] = bottom

            if self.wall_mid:
                self.ceilingclip[x] = SCREEN_HEIGHT
                self.floorclip[x] = -1
            else:
                if self.wall_top:
                    mid = self.pixhigh >> HEIGHTBITS
                    self.pixhigh += self.pixhighstep
                    if mid >= self.floorclip[x]:
                        mid = self.floorclip[x] - 1
                    if mid >= yl:
                        self.ceilingclip[x] = mid
                    else:
                        self.ceilingclip[x] = yl - 1
                elif self.markceiling:
                    self.ceilingclip[x] = yl - 1

                if self.wall_bottom:
                    mid = (self.pixlow + HEIGHTUNIT - 1) >> HEIGHTBITS
                    self.pixlow += self.pixlowstep
                    if mid <= self.ceilingclip[x]:
                        mid = self.ceilingclip[x] + 1
                    if mid <= yh:
                        self.floorclip[x] = mid
                    else:
                        self.floorclip[x] = yh + 1
                elif self.markfloor:
                    self.floorclip[x] = yh + 1

            self.rw_scale += self.rw_scalestep
            self.topfrac += self.topstep
            self.bottomfrac += self.bottomstep
            x += 1

    # --- BSP -----------------------------------------------------------

    def check_bbox(self, bbox: tuple[tuple[int, ...], ...]) -> bool:
        if self.viewx <= bbox[BOXLEFT]:
            boxx = 0
        elif self.viewx < bbox[BOXRIGHT]:
            boxx = 1
        else:
            boxx = 2

        if self.viewy >= bbox[BOXTOP]:
            boxy = 0
        elif self.viewy > bbox[BOXBOTTOM]:
            boxy = 1
        else:
            boxy = 2

        boxpos = (boxy << 2) + boxx
        if boxpos == 5:
            return True

        coords = CHECK_COORD[boxpos]
        x1 = bbox[coords[0]] if len(coords) > 1 else 0
        y1 = bbox[coords[1]] if len(coords) > 1 else 0
        x2 = bbox[coords[2]] if len(coords) > 1 else 0
        y2 = bbox[coords[3]] if len(coords) > 1 else 0

        angle1 = u32(point_to_angle(self.viewx, self.viewy, x1, y1) - self.viewangle)
        angle2 = u32(point_to_angle(self.viewx, self.viewy, x2, y2) - self.viewangle)
        span = u32(angle1 - angle2)
        if span >= ANG180:
            return True

        tspan = u32(angle1 + CLIP_ANGLE)
        if tspan > u32(2 * CLIP_ANGLE):
            tspan = u32(tspan - u32(2 * CLIP_ANGLE))
            if tspan >= span:
                return False
            angle1 = CLIP_ANGLE

        tspan = u32(CLIP_ANGLE - angle2)
        if tspan > u32(2 * CLIP_ANGLE):
            tspan = u32(tspan - u32(2 * CLIP_ANGLE))
            if tspan >= span:
                return False
            angle2 = u32(-CLIP_ANGLE)

        sx1 = VIEW_ANGLE_TO_X[u32(angle1 + ANG90) >> ANGLETOFINESHIFT]
        sx2 = VIEW_ANGLE_TO_X[u32(angle2 + ANG90) >> ANGLETOFINESHIFT]
        if sx1 == sx2:
            return False
        sx2 -= 1

        start = 0
        while self.solid[start][1] < sx2:
            start += 1
        return not (sx1 >= self.solid[start][0] and sx2 <= self.solid[start][1])

    def subsector(self, number: int, depth: int) -> None:
        if depth > 64:
            raise WalkError("BSP depth guard reached")
        bsp = self.bsp
        subsector = bsp.ssectors[number]
        first_seg = subsector["firstseg"]
        seg = bsp.segs[first_seg]
        line = bsp.linedefs[seg["linedef"]]
        side = bsp.sidedefs[line["side0"] if seg["side"] == 0 else line["side1"]]
        self.frontsector = side["sector"]
        sector = bsp.sectors[self.frontsector]

        if sector["floor"] < self.viewz:
            self.floorplane = find_plane(self.planes, self.planeCap,
                                         sector["floor"], sector["floorpic"],
                                         sector["light"], self.sky)
        else:
            self.floorplane = None

        if sector["ceiling"] > self.viewz or sector["ceilingpic"] == self.sky:
            self.ceilingplane = find_plane(self.planes, self.planeCap,
                                           sector["ceiling"], sector["ceilingpic"],
                                           sector["light"], self.sky)
        else:
            self.ceilingplane = None

        for index in range(first_seg, first_seg + subsector["numsegs"]):
            self.add_line(index)

    def render_bsp_node(self, bspnum: int, depth: int) -> None:
        if depth > 64:
            raise WalkError("BSP depth guard reached")
        if bspnum & 0x8000:
            self.subsector(0 if bspnum == -1 else (bspnum & ~0x8000), depth)
            return

        node = self.bsp.nodes[bspnum]
        side = point_on_side(self.viewx, self.viewy, node)
        self.render_bsp_node(node["children"][side], depth + 1)
        if self.check_bbox(node["bbox"][side ^ 1]):
            self.render_bsp_node(node["children"][side ^ 1], depth + 1)

    def render(self, params: dict) -> None:
        self.viewx = params["x"]
        self.viewy = params["y"]
        self.viewz = params["z"]
        self.viewangle = params["angle"]
        self.extralight = params["extralight"]
        self.sky = params["sky"]

        self.clear_clip_segs()
        for column in range(SCREEN_WIDTH):
            self.floorclip[column] = SCREEN_HEIGHT
            self.ceilingclip[column] = -1

        if not self.bsp.nodes:
            self.subsector(0, 0)
        else:
            self.render_bsp_node(len(self.bsp.nodes) - 1, 0)


def point_in_subsector(bsp: BspMap, x: int, y: int) -> int:
    """DOOM's R_PointInSubsector()."""
    if not bsp.nodes:
        return 0
    nodenum = len(bsp.nodes) - 1
    depth = 0
    while not (nodenum & 0x8000):
        depth += 1
        if depth > 64:
            raise WalkError("BSP depth guard reached")
        node = bsp.nodes[nodenum]
        nodenum = node["children"][point_on_side(x, y, node)]
    return nodenum & ~0x8000


def spawn_view(bsp: BspMap, player: int = 0) -> dict:
    """P_SpawnPlayer()'s viewpoint, exactly: last start of type player + 1."""
    thing = None
    for candidate in bsp.things:
        if candidate["type"] == player + 1:
            thing = candidate
    if thing is None:
        raise InputError(f"{bsp.name}: no player {player + 1} start")

    x = _fixed16(thing["x"])
    y = _fixed16(thing["y"])
    angle = u32(ANG45 * trunc_div(thing["angle"], 45))
    subsector = bsp.ssectors[point_in_subsector(bsp, x, y)]
    seg = bsp.segs[subsector["firstseg"]]
    line = bsp.linedefs[seg["linedef"]]
    side = bsp.sidedefs[line["side0"] if seg["side"] == 0 else line["side1"]]
    z = bsp.sectors[side["sector"]]["floor"] + (41 << FRACBITS)

    return {"x": x, "y": y, "z": z, "angle": angle, "extralight": 0,
            "sky": b"F_SKY1\x00\x00"}


# --- BSP reporting ----------------------------------------------------------

def drawseg_digest(drawsegs: list[dict]) -> str:
    digest = hashlib.sha256()
    for entry in drawsegs:
        digest.update(struct.pack("<iiB", entry["x1"], entry["x2"],
                                  entry["silhouette"]))
    return digest.hexdigest()


def visplane_digest(planes: list[Visplane]) -> str:
    digest = hashlib.sha256()
    for plane in planes:
        digest.update(struct.pack("<i", plane.height))
        digest.update(plane.pic)
        digest.update(struct.pack("<hhh", plane.light, plane.minx, plane.maxx))
        digest.update(bytes(plane.top))
        digest.update(bytes(plane.bottom))
    return digest.hexdigest()


def report_bsp(lumps: list[Lump], map_name: str, maxdrawsegs: int,
               maxplanes: int) -> None:
    bsp = read_bsp_map(lumps, map_name)
    params = spawn_view(bsp)
    state = WalkState(bsp, maxdrawsegs, maxplanes)
    state.render(params)

    print(f"BSP {map_name} x={params['x']} y={params['y']} z={params['z']} "
          f"angle=0x{params['angle']:08x}")
    for index, entry in enumerate(state.drawsegs):
        print(f"DRAWSEG {index} {entry['x1']},{entry['x2']},{entry['silhouette']}")
    print(f"DRAWSEG-COUNT {len(state.drawsegs)}")
    if state.drawseg_overflow:
        print("DRAWSEG-OVERFLOW")
    marked = 0
    for index, plane in enumerate(state.planes):
        name = plane.pic.rstrip(b"\0").decode("latin-1").upper()
        print(f"VISPLANE {index} {plane.height} {name} {plane.light} "
              f"{plane.minx} {plane.maxx}")
        if plane.minx <= plane.maxx:
            marked += 1
    print(f"VISPLANE-COUNT {len(state.planes)}")
    print(f"VISPLANE-MARKED {marked}")

    drawseg_hash = drawseg_digest(state.drawsegs)
    visplane_hash = visplane_digest(state.planes)
    combined = hashlib.sha256()
    for entry in state.drawsegs:
        combined.update(struct.pack("<iiB", entry["x1"], entry["x2"],
                                    entry["silhouette"]))
    for plane in state.planes:
        combined.update(struct.pack("<i", plane.height))
        combined.update(plane.pic)
        combined.update(struct.pack("<hhh", plane.light, plane.minx, plane.maxx))
        combined.update(bytes(plane.top))
        combined.update(bytes(plane.bottom))
    print(f"HASH DRAWSEGS {drawseg_hash}")
    print(f"HASH VISPLANES {visplane_hash}")
    print(f"HASH BOTH {combined.hexdigest()}")


# --- Reporting --------------------------------------------------------------

def patch_names(lumps: list[Lump], pnames: list[str]) -> list[str]:
    """Every name this tool treats as a patch, in WAD order, deduplicated.

    Two sources, both documented by the WAD itself: the names PNAMES references
    (wall textures and switches) and the sprite lumps between S_START and
    S_END. A name that appears in both is hashed once.
    """
    order: list[str] = []
    seen: set[str] = set()

    for name in pnames:
        upper = name.upper()
        if upper in seen:
            continue
        if first_index(lumps, upper) >= 0:
            seen.add(upper)
            order.append(upper)

    for lump in lumps_between(lumps, "S_START", "S_END"):
        if lump.name in seen:
            continue
        seen.add(lump.name)
        order.append(lump.name)

    return order


def report_patches(lumps: list[Lump], pnames: list[str], limit: int | None) -> None:
    names = patch_names(lumps, pnames)
    if limit is not None:
        names = names[:limit]

    for name in names:
        index = first_index(lumps, name)
        try:
            patch = parse_patch(name, lumps[index].data)
        except FormatError as error:
            print(f"PATCH-INVALID {name} {error}")
            continue
        print(f"PATCH {name} {patch_digest(patch)}")


def report_flats(lumps: list[Lump], limit: int | None) -> None:
    hashed = 0

    for lump in lumps_between(lumps, "F_START", "F_END"):
        if len(lump.data) != FLAT_BYTES:
            # The nested F1_START/F1_END markers are directory entries with no
            # bytes; DOOM counts them as flats, this oracle reports and skips.
            print(f"FLAT-SKIP {lump.name} {len(lump.data)}")
            continue
        if limit is not None and hashed >= limit:
            break
        print(f"FLAT {lump.name} {flat_digest(lump.data)}")
        hashed += 1


def report_textures(lumps: list[Lump], limit: int | None) -> None:
    texture_lumps: list[tuple[str, list[Texture]]] = []
    for lump_name in ("TEXTURE1", "TEXTURE2"):
        index = first_index(lumps, lump_name)
        if index >= 0:
            texture_lumps.append((lump_name, parse_textures(lump_name, lumps[index].data)))

    pnames_index = first_index(lumps, "PNAMES")
    pnames = parse_pnames(lumps[pnames_index].data) if pnames_index >= 0 else []

    emitted = 0
    for _, textures in texture_lumps:
        for texture in textures:
            if limit is not None and emitted >= limit:
                return

            resolved: dict[int, Patch] = {}
            unresolved: str | None = None
            invalid: str | None = None

            for _, _, patch_index in texture.patches:
                if patch_index in resolved:
                    continue
                if patch_index < 0 or patch_index >= len(pnames):
                    unresolved = f"index {patch_index} outside PNAMES"
                    break
                patch_name = pnames[patch_index].upper()
                lump_index = first_index(lumps, patch_name)
                if lump_index < 0:
                    unresolved = patch_name
                    break
                try:
                    resolved[patch_index] = parse_patch(patch_name, lumps[lump_index].data)
                except FormatError as error:
                    invalid = str(error)
                    break

            if unresolved is not None:
                print(f"TEXTURE-UNRESOLVED {texture.name} {unresolved}")
                continue
            if invalid is not None:
                print(f"TEXTURE-INVALID {texture.name} {invalid}")
                continue

            pixels, mask = compose_texture(texture, resolved)
            print(f"TEXTURE {texture.name} {texture_digest(pixels, mask)}")
            emitted += 1


def report_stats(lumps: list[Lump], pnames: list[str]) -> None:
    """Reproduces the real-data numbers r_data.h quotes about DOOM1.WAD.

    Printed as ``STAT`` lines so they can be checked without reading a texture
    by hand:

      STAT holes <NAME> <unwritten pixels>          a composed surface with gaps
      STAT negative-origin <NAME> <single> <multi>  columns whose only patch, or
                                                    one of several, sits at a
                                                    negative originy
      STAT clipped-posts <n>                        posts whose first pixel is
                                                    above the surface
      STAT masked-textures <n>                      definitions with masked != 0

    The header uses these to state, rather than assert, which shareware textures
    the documented clipping and origin choices actually affect.
    """
    texture_lumps = []
    for lump_name in ("TEXTURE1", "TEXTURE2"):
        index = first_index(lumps, lump_name)
        if index >= 0:
            texture_lumps.append(parse_textures(lump_name, lumps[index].data))

    masked = 0
    clipped = 0

    for textures in texture_lumps:
        for texture in textures:
            resolved: dict[int, Patch] = {}
            for _, _, patch_index in texture.patches:
                if patch_index in resolved or patch_index < 0 or patch_index >= len(pnames):
                    continue
                patch_name = pnames[patch_index].upper()
                lump_index = first_index(lumps, patch_name)
                if lump_index >= 0:
                    resolved[patch_index] = parse_patch(patch_name, lumps[lump_index].data)

            if texture.masked != 0:
                masked += 1

            per_column: list[list[int]] = [[] for _ in range(texture.width)]
            for origin_x, origin_y, patch_index in texture.patches:
                patch = resolved.get(patch_index)
                if patch is None:
                    continue
                for column in range(patch.width):
                    x = origin_x + column
                    if 0 <= x < texture.width:
                        per_column[x].append(origin_y)

            single = sum(1 for origins in per_column
                         if len(origins) == 1 and origins[0] < 0)
            multi = sum(1 for origins in per_column
                        if len(origins) > 1 and any(origin < 0 for origin in origins))
            if single or multi:
                print(f"STAT negative-origin {texture.name} {single} {multi}")
                for origin_x, origin_y, patch_index in texture.patches:
                    if origin_y >= 0 or patch_index not in resolved:
                        continue
                    for stream in resolved[patch_index].columns:
                        at = 0
                        while stream[at] != POST_END:
                            if origin_y + stream[at] < 0:
                                clipped += 1
                            at += stream[at + 1] + POST_HEADER_SIZE + POST_TRAILER_SIZE

            if len(resolved) == len({index for _, _, index in texture.patches}):
                _, mask = compose_texture(texture, resolved)
                unwritten = len(mask) - sum(mask)
                if unwritten:
                    print(f"STAT holes {texture.name} {unwritten}")

    print(f"STAT clipped-posts {clipped}")
    print(f"STAT masked-textures {masked}")


# --- The wall column emitter (D3.2) -----------------------------------------

# r_data.h's light constants. The view this port renders is fixed, so the wall
# light table is a constant derived from the same formula R_ExecuteSetViewSize()
# uses, exactly as the C side generates it into r_tables.c.
LIGHTLEVELS = 16
NUMCOLORMAPS = 32
MAXLIGHTSCALE = 48
MAXLIGHTSCALE_INDEX = MAXLIGHTSCALE - 1
LIGHTSCALESHIFT = 12
DISTMAP = 2
LIGHTSEGSHIFT = 4
VIEW_HEIGHT = SCREEN_HEIGHT
CENTER_Y = SCREEN_HEIGHT // 2
CENTER_Y_FRAC = CENTER_Y << FRACBITS
UNCOVERED = 0xFF

# DOOM's R_DrawColumn() wraps a texture column with a hard-coded `& 127`, so a
# stored column is 128 rows whatever the texture's declared height.
STORED_COLUMN_HEIGHT = 128

# linetype_sidedef flags the wall pegging reads.
ML_DONTPEGTOP = 8
ML_DONTPEGBOTTOM = 16

# R_InitLightTables()/R_ExecuteSetViewSize() constants for the plane pass (D3.3).
LIGHTZSHIFT = 20
MAXLIGHTZ = 128
ANGLETOSKYSHIFT = 22
SKY_ISCALE = FRACUNIT
SKY_TEXTUREMID = CENTER_Y << FRACBITS
FLAT_SIZE = 64

# The THINGS types that are a player or deathmatch start.
PLAYER_START_TYPES = (1, 2, 3, 4, 11)


def _light_levels() -> tuple[tuple[int, ...], ...]:
    """R_ExecuteSetViewSize()'s scalelight[,] as COLORMAP map indices."""
    table: list[tuple[int, ...]] = []
    for i in range(LIGHTLEVELS):
        start_map = ((LIGHTLEVELS - 1 - i) * 2) * NUMCOLORMAPS // LIGHTLEVELS
        row: list[int] = []
        for j in range(MAXLIGHTSCALE):
            level = start_map - j * SCREEN_WIDTH // SCREEN_WIDTH // DISTMAP
            row.append(0 if level < 0 else (NUMCOLORMAPS - 1 if level >= NUMCOLORMAPS else level))
        table.append(tuple(row))
    return tuple(table)


LIGHT_LEVEL = _light_levels()


def _plane_light() -> tuple[tuple[int, ...], ...]:
    """R_InitLightTables()'s zlight[,] as COLORMAP map indices (D3.3)."""
    table: list[tuple[int, ...]] = []
    for i in range(LIGHTLEVELS):
        start_map = ((LIGHTLEVELS - 1 - i) * 2) * NUMCOLORMAPS // LIGHTLEVELS
        row: list[int] = []
        for j in range(MAXLIGHTZ):
            scale = fixed_div((SCREEN_WIDTH // 2) * FRACUNIT, (j + 1) << LIGHTZSHIFT)
            scale >>= LIGHTSCALESHIFT
            level = start_map - scale // DISTMAP
            row.append(0 if level < 0 else (NUMCOLORMAPS - 1 if level >= NUMCOLORMAPS else level))
        table.append(tuple(row))
    return tuple(table)


def _plane_yslope() -> tuple[int, ...]:
    """R_ExecuteSetViewSize()'s yslope[] (D3.3)."""
    table: list[int] = []
    for i in range(SCREEN_HEIGHT):
        dy = abs(((i - SCREEN_HEIGHT // 2) << FRACBITS) + FRACUNIT // 2)
        table.append(fixed_div((SCREEN_WIDTH // 2) * FRACUNIT, dy))
    return tuple(table)


def _plane_dist_scale() -> tuple[int, ...]:
    """R_ExecuteSetViewSize()'s distscale[] (D3.3)."""
    table: list[int] = []
    for x in range(SCREEN_WIDTH):
        cosadj = abs(FINE_SINE[(X_TO_VIEW_ANGLE[x] >> ANGLETOFINESHIFT)
                               + FINEANGLES // 4])
        table.append(fixed_div(FRACUNIT, cosadj))
    return tuple(table)


PLANE_LIGHT = _plane_light()
YSLOPE = _plane_yslope()
DISTSCALE = _plane_dist_scale()


def light_row(lightnum: int) -> int:
    """R_StoreWallRange()'s three-way clamp of the wall light number."""
    if lightnum < 0:
        return 0
    if lightnum >= LIGHTLEVELS:
        return LIGHTLEVELS - 1
    return lightnum


def light_column(scale: int) -> int:
    """R_RenderSegLoop()'s `rw_scale >> LIGHTSCALESHIFT`, clamped; unsigned."""
    index = (scale & 0xFFFFFFFF) >> LIGHTSCALESHIFT
    return MAXLIGHTSCALE_INDEX if index >= MAXLIGHTSCALE else index


def width_mask(width: int) -> int:
    """R_InitTextures(): the largest power of two that does not exceed width, -1."""
    power = 1
    while power * 2 <= width:
        power <<= 1
    return power - 1


class WallTexture:
    """One composed wall texture in the column loop's storage form."""

    __slots__ = ("width", "height", "pixels", "width_mask", "column_height")

    def __init__(self, width: int, height: int, composite: bytes) -> None:
        self.width = width
        self.height = height                 # declared, for the pegging
        self.column_height = STORED_COLUMN_HEIGHT
        self.width_mask = width_mask(width)
        pixels = bytearray(width * self.column_height)
        for x in range(width):
            pixels[x * self.column_height:x * self.column_height + height] = \
                composite[x * height:(x + 1) * height]
        self.pixels = bytes(pixels)


class TextureLibrary:
    """Composes wall textures on demand from TEXTURE1/2, PNAMES and the patches.

    This is the residency policy of the oracle: nothing is composed until a
    fragment asks for it, and every patch is decoded with the same rules
    r_data.h documents (the decoder D2.4 was verified against).
    """

    def __init__(self, lumps: list[Lump]) -> None:
        self.lumps = lumps
        pnames_index = first_index(lumps, "PNAMES")
        if pnames_index < 0:
            raise FormatError("PNAMES is not in this WAD")
        self.pnames = parse_pnames(lumps[pnames_index].data)
        self.definitions: dict[str, Texture] = {}
        for lump_name in ("TEXTURE1", "TEXTURE2"):
            index = first_index(lumps, lump_name)
            if index < 0:
                continue
            for texture in parse_textures(lump_name, lumps[index].data):
                self.definitions.setdefault(texture.name.upper(), texture)
        self.patches: dict[int, Patch] = {}
        self.cache: dict[str, WallTexture] = {}

    def _patch(self, index: int) -> Patch:
        cached = self.patches.get(index)
        if cached is not None:
            return cached
        if index < 0 or index >= len(self.pnames):
            raise FormatError(f"PNAMES index {index} outside {len(self.pnames)}")
        name = self.pnames[index]
        lump = first_index(self.lumps, name)
        if lump < 0:
            raise FormatError(f"patch {name!r} is not a lump in this WAD")
        patch = parse_patch(name, self.lumps[lump].data)
        self.patches[index] = patch
        return patch

    def get(self, name: str) -> WallTexture:
        key = name.upper()
        cached = self.cache.get(key)
        if cached is not None:
            return cached
        texture = self.definitions.get(key)
        if texture is None:
            raise FormatError(f"texture {name!r} is not in TEXTURE1/TEXTURE2")
        resolved = {index: self._patch(index) for _, _, index in texture.patches}
        pixels, _mask = compose_texture(texture, resolved)
        wall = WallTexture(texture.width, texture.height, pixels)
        self.cache[key] = wall
        return wall


class Flat:
    """One flat: 64 x 64 raw palette indices, row-major (r_data.h's format)."""

    __slots__ = ("pixels",)

    def __init__(self, name: str, data: bytes) -> None:
        if len(data) != FLAT_SIZE * FLAT_SIZE:
            raise FormatError(f"flat {name!r} is {len(data)} B, not 64 * 64")
        self.pixels = data


class FlatLibrary:
    """Resolves flats by name from the WAD directory, like R_FlatNumForName()."""

    def __init__(self, lumps: list[Lump]) -> None:
        self.lumps = lumps
        self.cache: dict[str, Flat] = {}

    def get(self, name: str) -> Flat:
        key = name.upper()
        cached = self.cache.get(key)
        if cached is not None:
            return cached
        index = first_index(self.lumps, key)
        if index < 0:
            raise FormatError(f"flat {name!r} is not a lump in this WAD")
        flat = Flat(name, self.lumps[index].data)
        self.cache[key] = flat
        return flat


def name_of(field: bytes) -> str:
    return field.rstrip(b"\0").decode("latin-1")


def emit_walls(bsp: BspMap, drawsegs: list[dict], params: dict,
               textures: TextureLibrary, colormaps: bytes) -> dict:
    """R_RenderSegLoop()'s drawing half, over the walk's drawsegs.

    Written from r_segs.c/r_draw.c and from r_light.h's documented wall table;
    it reads the walk's stored fragment values, never r_segs.c's C text.
    """
    pixels = bytearray([UNCOVERED]) * (SCREEN_WIDTH * SCREEN_HEIGHT)
    covered = bytearray(SCREEN_WIDTH)
    floorclip = [VIEW_HEIGHT] * SCREEN_WIDTH
    ceilingclip = [-1] * SCREEN_WIDTH
    counts = {"mid": 0, "top": 0, "bottom": 0}
    tangent = _TANGENT
    half_angles = FINEANGLES // 2
    half_mask = half_angles - 1
    width = SCREEN_WIDTH

    def draw(tier: str, texture: WallTexture, texture_mid: int, y0: int, y1: int,
             colormap: int, iscale: int, texture_column: int, x: int) -> None:
        if y0 > y1:
            return
        if y0 < 0:
            y0 = 0
        if y1 > VIEW_HEIGHT - 1:
            y1 = VIEW_HEIGHT - 1
        if y0 > y1:
            return
        column = (texture_column & texture.width_mask) * texture.column_height
        frac = i32(texture_mid + (y0 - CENTER_Y) * iscale)
        written = 0
        for y in range(y0, y1 + 1):
            pixels[y * width + x] = colormaps[colormap + texture.pixels[column + ((frac >> FRACBITS) & (texture.column_height - 1))]]
            frac = i32(frac + iscale)
            written += 1
        if written:
            covered[x] = 1
            counts[tier] += written

    for ds in drawsegs:
        side = ds["side"]
        flags = ds["flags"]
        top16 = ds["top16"]
        bottom16 = ds["bottom16"]
        high16 = ds["high16"]
        low16 = ds["low16"]

        mid = top = bottom = None
        mid_height = top_height = 0
        if ds["wall_mid"]:
            mid = textures.get(name_of(side["mid"]))
            mid_height = mid.height << FRACBITS
        if ds["wall_top"]:
            top = textures.get(name_of(side["top"]))
            top_height = top.height << FRACBITS
        if ds["wall_bottom"]:
            bottom = textures.get(name_of(side["bottom"]))
        row_offset = side["rowoffset"]

        mid_mid = top_mid = bottom_mid = 0
        if ds["wall_mid"]:
            mid_mid = bottom16 + mid_height if flags & ML_DONTPEGBOTTOM else top16
            mid_mid += row_offset
        if ds["has_back"]:
            if ds["wall_top"]:
                top_mid = top16 if flags & ML_DONTPEGTOP else high16 + top_height
                top_mid += row_offset
            if ds["wall_bottom"]:
                bottom_mid = top16 if flags & ML_DONTPEGBOTTOM else low16
                bottom_mid += row_offset

        scale = ds["scale1"]
        scalestep = ds["scalestep"]
        top4 = top16 >> 4
        bottom4 = bottom16 >> 4
        top_step = -fixed_mul(scalestep, top4)
        top_frac = (CENTER_Y_FRAC >> 4) - fixed_mul(top4, scale)
        bottom_step = -fixed_mul(scalestep, bottom4)
        bottom_frac = (CENTER_Y_FRAC >> 4) - fixed_mul(bottom4, scale)
        pix_high = pix_low = pix_high_step = pix_low_step = 0
        if ds["has_back"]:
            high4 = high16 >> 4
            low4 = low16 >> 4
            if high4 < top4:
                pix_high = (CENTER_Y_FRAC >> 4) - fixed_mul(high4, scale)
                pix_high_step = -fixed_mul(scalestep, high4)
            if low4 > bottom4:
                pix_low = (CENTER_Y_FRAC >> 4) - fixed_mul(low4, scale)
                pix_low_step = -fixed_mul(scalestep, low4)

        light = LIGHT_LEVEL[light_row(ds["lightnum"])]
        texture_offset = ds["offset"]
        distance = ds["distance"]
        center_angle = ds["centerangle"]
        wall_mid = ds["wall_mid"]
        wall_top = ds["wall_top"]
        wall_bottom = ds["wall_bottom"]

        for x in range(ds["x1"], ds["x2"] + 1):
            yl = (top_frac + HEIGHTUNIT - 1) >> HEIGHTBITS
            if yl < ceilingclip[x] + 1:
                yl = ceilingclip[x] + 1
            yh = bottom_frac >> HEIGHTBITS
            if yh >= floorclip[x]:
                yh = floorclip[x] - 1

            colormap = 0
            iscale = 0
            texture_column = 0
            if wall_mid or wall_top or wall_bottom:
                angle = u32(center_angle + X_TO_VIEW_ANGLE[x])
                texture_column = i32(texture_offset - fixed_mul(
                    tangent[(angle >> ANGLETOFINESHIFT) & half_mask], distance)) >> FRACBITS
                colormap = light[light_column(scale)] * 256
                iscale = 0xFFFFFFFF // (scale & 0xFFFFFFFF) if (scale & 0xFFFFFFFF) else INT32_MAX

            if wall_mid:
                draw("mid", mid, mid_mid, yl, yh, colormap, iscale, texture_column, x)
                ceilingclip[x] = VIEW_HEIGHT
                floorclip[x] = -1
            else:
                if wall_top:
                    mid_row = pix_high >> HEIGHTBITS
                    pix_high += pix_high_step
                    if mid_row >= floorclip[x]:
                        mid_row = floorclip[x] - 1
                    if mid_row >= yl:
                        draw("top", top, top_mid, yl, mid_row, colormap, iscale,
                             texture_column, x)
                        ceilingclip[x] = mid_row
                    else:
                        ceilingclip[x] = yl - 1
                elif ds["markceiling"]:
                    ceilingclip[x] = yl - 1

                if wall_bottom:
                    mid_row = (pix_low + HEIGHTUNIT - 1) >> HEIGHTBITS
                    pix_low += pix_low_step
                    if mid_row <= ceilingclip[x]:
                        mid_row = ceilingclip[x] + 1
                    if mid_row <= yh:
                        draw("bottom", bottom, bottom_mid, mid_row, yh, colormap,
                             iscale, texture_column, x)
                        floorclip[x] = mid_row
                    else:
                        floorclip[x] = yh + 1
                elif ds["markfloor"]:
                    floorclip[x] = yh + 1

            scale += scalestep
            top_frac += top_step
            bottom_frac += bottom_step

    digest = hashlib.sha256()
    digest.update(bytes(pixels))
    digest.update(bytes(covered))
    frame = {
        "pixels": bytes(pixels),
        "covered": bytes(covered),
        "covered_columns": sum(1 for value in covered if value),
        "drawsegs": len(drawsegs),
        "mid": counts["mid"],
        "top": counts["top"],
        "bottom": counts["bottom"],
        "digest": digest.hexdigest(),
    }
    frame["pixels_written"] = counts["mid"] + counts["top"] + counts["bottom"]
    return frame


def emit_planes(planes: list[Visplane], params: dict, textures: TextureLibrary,
                flats: FlatLibrary, colormaps: bytes, wall_pixels: bytes,
                wall_covered: bytes) -> dict:
    """R_DrawPlanes(): the span maker, R_MapPlane, R_DrawSpan and the sky path.

    Written from upstream's r_plane.c (R_DrawPlanes, R_MakeSpans, R_MapPlane),
    r_draw.c (R_DrawSpan), r_sky.c/r_main.c (the sky constants) and r_data.h's
    flat contract. It reads the walk's plane records, never r_plane.c's C text.

    The input surface is the wall pass's output, which this pass draws over. The
    returned `mask` is one byte per pixel and says exactly where this pass wrote,
    so a frame comparison can attribute a divergent pixel to a pass.
    """
    pixels = bytearray(wall_pixels)
    covered = bytearray(wall_covered)
    mask = bytearray(SCREEN_WIDTH * SCREEN_HEIGHT)

    viewx = params["x"]
    viewy = params["y"]
    viewz = params["z"]
    viewangle = params["angle"]
    extralight = params["extralight"]
    skyflat = params["sky"]
    # E1M1 is episode 1, so the level's sky texture is SKY1 (the episode rule
    # that sets upstream's `skytexture`). The C harness receives the same name
    # through r_tex_source_t::skyTexture.
    sky = textures.get("SKY1")

    centerxfrac = (SCREEN_WIDTH // 2) << FRACBITS
    base_angle = u32(viewangle - ANG90) >> ANGLETOFINESHIFT
    basexscale = fixed_div(FINE_SINE[base_angle + FINEANGLES // 4], centerxfrac)
    baseyscale = -fixed_div(FINE_SINE[base_angle], centerxfrac)

    spanstart = [0] * SCREEN_HEIGHT
    counters = {"planes": 0, "spans": 0, "pixels": 0}

    def map_plane(y: int, x1: int, x2: int, planeheight: int, lightrow: int,
                  flat: Flat) -> None:
        if y < 0 or y >= SCREEN_HEIGHT or x1 < 0 or x2 >= SCREEN_WIDTH or x1 > x2:
            return
        distance = i32(fixed_mul(planeheight, YSLOPE[y]))
        xstep = i32(fixed_mul(distance, basexscale))
        ystep = i32(fixed_mul(distance, baseyscale))
        length = i32(fixed_mul(distance, DISTSCALE[x1]))
        index = u32(viewangle + X_TO_VIEW_ANGLE[x1]) >> ANGLETOFINESHIFT
        xfrac = i32(viewx + i32(fixed_mul(FINE_SINE[index + FINEANGLES // 4], length)))
        yfrac = i32(-viewy - i32(fixed_mul(FINE_SINE[index], length)))
        z = distance >> LIGHTZSHIFT
        if z >= MAXLIGHTZ:
            z = MAXLIGHTZ - 1
        colormap = PLANE_LIGHT[lightrow][z] * 256
        counters["spans"] += 1
        for x in range(x1, x2 + 1):
            spot = ((yfrac >> (FRACBITS - 6)) & (63 * 64)) + ((xfrac >> FRACBITS) & 63)
            at = y * SCREEN_WIDTH + x
            pixels[at] = colormaps[colormap + flat.pixels[spot]]
            mask[at] = 1
            covered[x] = 1
            counters["pixels"] += 1
            xfrac = i32(xfrac + xstep)
            yfrac = i32(yfrac + ystep)

    def make_spans(x: int, t1: int, b1: int, t2: int, b2: int,
                   planeheight: int, lightrow: int, flat: Flat) -> None:
        # Upstream writes top[minx-1] and top[maxx+1] as 0xff sentinels; this
        # pass passes the outside columns in as empty instead. An empty column's
        # bottom is zero, which is what upstream's zero-initialised array holds
        # where top is 0xff.
        if t1 == 0xFF:
            b1 = 0
        if t2 == 0xFF:
            b2 = 0
        while t1 < t2 and t1 <= b1:
            map_plane(t1, spanstart[t1], x - 1, planeheight, lightrow, flat)
            t1 += 1
        while b1 > b2 and b1 >= t1:
            map_plane(b1, spanstart[b1], x - 1, planeheight, lightrow, flat)
            b1 -= 1
        while t2 < t1 and t2 <= b2:
            spanstart[t2] = x
            t2 += 1
        while b2 > b1 and b2 >= t2:
            spanstart[b2] = x
            b2 -= 1

    for plane in planes:
        if plane.minx > plane.maxx:
            continue
        counters["planes"] += 1

        if plane.pic == skyflat:
            # Sky is always full bright (colormaps[0]) and does not go through
            # the span maker: one R_DrawColumn per marked column.
            for x in range(plane.minx, plane.maxx + 1):
                yl = plane.top[x]
                yh = plane.bottom[x]
                if yl == 0xFF or yl > yh:
                    continue
                if yl < 0:
                    yl = 0
                if yh > SCREEN_HEIGHT - 1:
                    yh = SCREEN_HEIGHT - 1
                if yl > yh:
                    continue
                angle = u32(viewangle + X_TO_VIEW_ANGLE[x]) >> ANGLETOSKYSHIFT
                column = (angle & sky.width_mask) * sky.column_height
                frac = SKY_TEXTUREMID + (yl - CENTER_Y) * SKY_ISCALE
                for row in range(yl, yh + 1):
                    at = row * SCREEN_WIDTH + x
                    pixels[at] = sky.pixels[
                        column + ((frac >> FRACBITS) & (sky.column_height - 1))]
                    mask[at] = 1
                    covered[x] = 1
                    counters["pixels"] += 1
                    frac = i32(frac + SKY_ISCALE)
            continue

        flat = flats.get(name_of(plane.pic))
        planeheight = abs(i32(plane.height - viewz))
        light = (plane.light >> LIGHTSEGSHIFT) + extralight
        if light >= LIGHTLEVELS:
            light = LIGHTLEVELS - 1
        if light < 0:
            light = 0

        stop = plane.maxx + 1
        for x in range(plane.minx, stop + 1):
            t1 = plane.top[x - 1] if x > plane.minx else 0xFF
            b1 = plane.bottom[x - 1] if x > plane.minx else 0
            t2 = plane.top[x] if x <= plane.maxx else 0xFF
            b2 = plane.bottom[x] if x <= plane.maxx else 0
            make_spans(x, t1, b1, t2, b2, planeheight, light, flat)

    frame_hash = hashlib.sha256(bytes(pixels) + bytes(covered)).hexdigest()
    plane_hash = hashlib.sha256(bytes(mask)).hexdigest()
    return {
        "pixels": bytes(pixels),
        "covered": bytes(covered),
        "mask": bytes(mask),
        "planes": counters["planes"],
        "spans": counters["spans"],
        "plane_pixels": counters["pixels"],
        "planehash": plane_hash,
        "framehash": frame_hash,
    }


def floor_view(bsp: BspMap, x: int, y: int) -> dict:
    """A viewpoint at (x, y) with P_SpawnPlayer()'s z rule, angle 0."""
    subsector = bsp.ssectors[point_in_subsector(bsp, x, y)]
    seg = bsp.segs[subsector["firstseg"]]
    side = bsp.sidedefs[bsp.linedefs[seg["linedef"]]["side0"] if seg["side"] == 0
                        else bsp.linedefs[seg["linedef"]]["side1"]]
    return {"x": x, "y": y, "z": bsp.sectors[side["sector"]]["floor"] + (41 << FRACBITS),
            "angle": 0, "extralight": 0, "sky": b"F_SKY1\x00\x00"}


def frame_views(bsp: BspMap) -> list[dict]:
    """The 20 sampled viewpoints: 4 positions x 5 angles, in that order.

    Positions are the first four THINGS records in file order whose type is a
    player or deathmatch start, padded by repeating the spawn viewpoint. Angles
    are k * 2^32 / 5 for k in 0..4, and frame index = position * 5 + angle.
    """
    spawn = spawn_view(bsp)
    positions: list[dict] = []
    for thing in bsp.things:
        if thing["type"] not in PLAYER_START_TYPES:
            continue
        positions.append(floor_view(bsp, _fixed16(thing["x"]), _fixed16(thing["y"])))
        if len(positions) == 4:
            break
    while len(positions) < 4:
        positions.append(dict(spawn))

    views: list[dict] = []
    for position in positions:
        for k in range(5):
            view = dict(position)
            view["angle"] = u32(k * (1 << 32) // 5 & 0xFFFFFFFF)
            views.append(view)
    return views


def report_segs(lumps: list[Lump], map_name: str, drawsegs: int, planes: int,
                detail: int | None = None,
                column: tuple[int, int] | None = None) -> None:
    bsp = read_bsp_map(lumps, map_name)
    textures = TextureLibrary(lumps)
    flats = FlatLibrary(lumps)
    colormap_index = first_index(lumps, "COLORMAP")
    if colormap_index < 0:
        raise FormatError("COLORMAP is not in this WAD")
    colormaps = lumps[colormap_index].data
    if len(colormaps) != 34 * 256:
        raise FormatError(f"COLORMAP is {len(colormaps)} B, not 34 * 256")

    views = frame_views(bsp)
    print(f"SEGS {map_name} frames={len(views)}")
    frames: list[dict] = []
    for index, params in enumerate(views):
        state = WalkState(bsp, drawsegs, planes)
        state.render(params)
        wall = emit_walls(bsp, state.drawsegs, params, textures, colormaps)
        plane = emit_planes(state.planes, params, textures, flats, colormaps,
                            wall["pixels"], wall["covered"])
        frame = {
            "wall": wall,
            "plane": plane,
        }
        frames.append(frame)
        wall_hash = wall["digest"]
        pixel_digest = hashlib.sha256(wall["pixels"]).hexdigest()
        cover_digest = hashlib.sha256(wall["covered"]).hexdigest()
        print(f"frame {index:02d}: {wall_hash}")
        print(f"FRAMESTAT {index:02d} drawsegs={wall['drawsegs']} "
              f"columns={wall['covered_columns']} pixels={wall['pixels_written']} "
              f"mid={wall['mid']} top={wall['top']} bottom={wall['bottom']} "
              f"pixelhash={pixel_digest} coverhash={cover_digest}")
        print(f"PLANESTAT {index:02d} planes={plane['planes']} spans={plane['spans']} "
              f"plane_pixels={plane['plane_pixels']} wallhash={wall_hash} "
              f"planehash={plane['planehash']} framehash={plane['framehash']}")

    print(f"SEGS-SUMMARY frames={len(frames)} "
          f"drawsegs={sum(f['wall']['drawsegs'] for f in frames)} "
          f"columns={sum(f['wall']['covered_columns'] for f in frames)} "
          f"pixels={sum(f['wall']['pixels_written'] for f in frames)} "
          f"mid={sum(f['wall']['mid'] for f in frames)} "
          f"top={sum(f['wall']['top'] for f in frames)} "
          f"bottom={sum(f['wall']['bottom'] for f in frames)}")
    print(f"PLANE-SUMMARY frames={len(frames)} "
          f"planes={sum(f['plane']['planes'] for f in frames)} "
          f"spans={sum(f['plane']['spans'] for f in frames)} "
          f"plane_pixels={sum(f['plane']['plane_pixels'] for f in frames)}")

    if detail is not None:
        if detail < 0 or detail >= len(frames):
            raise InputError(f"--segs-detail {detail} is not a frame of this run")
        frame = frames[detail]
        for x in range(SCREEN_WIDTH):
            wall_hash = hashlib.sha256(
                frame["wall"]["pixels"][x::SCREEN_WIDTH]
                + frame["wall"]["covered"][x:x + 1]).hexdigest()
            frame_hash = hashlib.sha256(
                frame["plane"]["pixels"][x::SCREEN_WIDTH]
                + frame["plane"]["covered"][x:x + 1]).hexdigest()
            print(f"COLUMN {detail:02d} {x} {wall_hash} {frame_hash}")

    if column is not None:
        frame_index, x = column
        if frame_index < 0 or frame_index >= len(frames):
            raise InputError(f"--segs-column frame {frame_index} is not in this run")
        if x < 0 or x >= SCREEN_WIDTH:
            raise InputError(f"--segs-column {x} is not a screen column")
        frame = frames[frame_index]
        for y in range(SCREEN_HEIGHT):
            at = y * SCREEN_WIDTH + x
            print(f"ROW {frame_index:02d} {x} {y} "
                  f"{frame['wall']['pixels'][at]:02x} "
                  f"{frame['plane']['pixels'][at]:02x} "
                  f"{frame['wall']['covered'][x]:02x} "
                  f"{frame['plane']['mask'][at]:02x}")


# --- Command line -----------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    epilog = """\
exit codes
  0  every requested item was decoded and hashed
  1  the input could not be read, or a requested map was not found
  2  the command line is wrong (argparse)

hash protocol, identical to tools/rdat_check.c
  PATCH    uint16 LE column length + normalised post stream, per column
  TEXTURE  pixel surface (column-major) then mask plane, both width*height
  FLAT     the 4096 bytes

The tool never prints a decoded byte: only names from the WAD directory and
SHA-256 hashes, so its output is safe to quote in a report or a commit message.
"""
    parser = argparse.ArgumentParser(
        prog="doom_ref.py",
        description="Host oracle for the DOOM picture decoders: read the raw WAD, "
                    "decode patches, textures and flats independently of the C code, "
                    "and print one SHA-256 per item.",
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, metavar="PATH", required=True,
                        help="IWAD or PWAD to read")
    parser.add_argument("--patches", action="store_true",
                        help="hash every patch: the PNAMES entries and the sprites "
                             "between S_START and S_END")
    parser.add_argument("--textures", action="store_true",
                        help="hash every TEXTURE1/TEXTURE2 texture whose patches resolve")
    parser.add_argument("--flats", action="store_true",
                        help="hash every 4096-byte flat between F_START and F_END")
    parser.add_argument("--all", action="store_true",
                        help="--patches --textures --flats")
    parser.add_argument("--map", metavar="NAME",
                        help="list the textures and flats a map references "
                             "(for example E1M1)")
    parser.add_argument("--bsp", metavar="NAME",
                        help="walk the map's BSP from its player start and print the "
                             "drawsegs, the visplanes and their hashes (D3.1)")
    parser.add_argument("--drawsegs", type=int, default=256, metavar="N",
                        help="drawseg capacity for --bsp (default: 256, DOOM's "
                             "MAXDRAWSEGS)")
    parser.add_argument("--planes", type=int, default=128, metavar="N",
                        help="visplane capacity for --bsp (default: 128, DOOM's "
                             "MAXVISPLANES)")
    parser.add_argument("--stats", action="store_true",
                        help="print STAT lines about the real data: composing "
                             "holes, negative patch origins, clipped posts and "
                             "the masked flag, the numbers r_data.h quotes")
    parser.add_argument("--segs", metavar="NAME",
                        help="render the 20 sampled wall-only frames of a map "
                             "(D3.2) and print one hash per frame")
    parser.add_argument("--segs-detail", type=int, metavar="N",
                        help="with --segs: also print a hash per screen column "
                             "of frame N (the first divergent column)")
    parser.add_argument("--segs-column", metavar="N,X",
                        help="with --segs: also print the 200 pixels of column "
                             "X of frame N (the first divergent row)")
    parser.add_argument("--limit", type=int, metavar="N",
                        help="hash at most N items of each requested kind")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    patches_wanted = arguments.patches or arguments.all
    textures_wanted = arguments.textures or arguments.all
    flats_wanted = arguments.flats or arguments.all
    if not (patches_wanted or textures_wanted or flats_wanted or arguments.map
            or arguments.stats or arguments.bsp or arguments.segs):
        parser.error("nothing to do: pass --patches, --textures, --flats, --all, "
                     "--map, --stats, --bsp or --segs")
    if arguments.limit is not None and arguments.limit < 0:
        parser.error("--limit cannot be negative")
    if arguments.drawsegs < 1:
        parser.error("--drawsegs must be at least 1")
    if arguments.planes < 1:
        parser.error("--planes must be at least 1")
    if arguments.segs_detail is not None and not arguments.segs:
        parser.error("--segs-detail needs --segs")
    if arguments.segs_column is not None and not arguments.segs:
        parser.error("--segs-column needs --segs")
    segs_column = None
    if arguments.segs_column is not None:
        parts = arguments.segs_column.split(",")
        if len(parts) != 2:
            parser.error("--segs-column wants FRAME,COLUMN")
        try:
            segs_column = (int(parts[0]), int(parts[1]))
        except ValueError:
            parser.error("--segs-column wants two integers")

    try:
        identification, lumps = read_wad(arguments.wad)
    except InputError as error:
        print(f"doom_ref: {error}", file=sys.stderr)
        return EXIT_INPUT

    pnames_index = first_index(lumps, "PNAMES")
    pnames = []
    if pnames_index >= 0:
        try:
            pnames = parse_pnames(lumps[pnames_index].data)
        except FormatError as error:
            print(f"doom_ref: {error}", file=sys.stderr)
            return EXIT_INPUT

    print(f"WAD {identification} {len(lumps)} lumps")

    try:
        if patches_wanted:
            report_patches(lumps, pnames, arguments.limit)
        if textures_wanted:
            report_textures(lumps, arguments.limit)
        if flats_wanted:
            report_flats(lumps, arguments.limit)
        if arguments.map:
            textures, flats = map_textures_and_flats(lumps, arguments.map.upper())
            for name in textures:
                print(f"MAP-TEXTURE {name}")
            for name in flats:
                print(f"MAP-FLAT {name}")
        if arguments.stats:
            report_stats(lumps, pnames)
        if arguments.bsp:
            report_bsp(lumps, arguments.bsp.upper(), arguments.drawsegs,
                       arguments.planes)
        if arguments.segs:
            report_segs(lumps, arguments.segs.upper(), arguments.drawsegs,
                        arguments.planes, arguments.segs_detail, segs_column)
    except FormatError as error:
        print(f"doom_ref: {error}", file=sys.stderr)
        return EXIT_INPUT
    except WalkError as error:
        print(f"doom_ref: {arguments.bsp or arguments.segs}: {error}", file=sys.stderr)
        return EXIT_INPUT

    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
