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
import os
import struct
import sys
from dataclasses import dataclass, replace
from pathlib import Path

import e1m1_sprites

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
FINEMASK = FINEANGLES - 1
SLOPERANGE = 2048
SCREEN_WIDTH = 320
SCREEN_HEIGHT = 200
# The 3D window's rows. 200 is the full surface (the host gates' frames); the
# DOOM_HUD build (D5.1) renders DOOM's screen size 9, 168 rows above the status
# bar. The environment variable is how a gate asks the oracle for that window.
VIEW_HEIGHT = int(os.environ.get("DOOM_REF_VIEW_HEIGHT", SCREEN_HEIGHT))
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
    reject: bytes


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
                  ssectors, nodes, sectors, raw["REJECT"])


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
        self.centerfrac = (VIEW_HEIGHT // 2) << FRACBITS
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
        clip_top_solid = clip_bottom_solid = 0

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
                clip_bottom_solid = 1
            if back["floor"] >= front["ceiling"]:
                top_sil = INT32_MIN
                silhouette |= 2
                clip_top_solid = 1

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
            "clip_top_solid": clip_top_solid,
            "clip_bottom_solid": clip_bottom_solid,
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
                self.ceilingclip[x] = VIEW_HEIGHT
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
            self.floorclip[column] = VIEW_HEIGHT
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


# --- The THINGS run (D4.1a) -------------------------------------------------
#
# P_SpawnMapThing()'s filter chain, re-derived from linuxdoom-1.10 p_mobj.c:
# the deathmatch-start branch, the player-start branch, the single-player
# options bit, the skill bit, the doomednum scan, MF_NOTDMATCH, -nomonsters,
# MF_SPAWNCEILING, and the ONFLOORZ/ONCEILINGZ rules (which do not clamp).
#
# The table below is a *second* reading of upstream's info.c: tools/gen_info.py
# parses that file with an expression engine and emits lib/doom/info_tables.h;
# this dict was transcribed from the same records by a separate, cruder parse.
# A disagreement between them is therefore a real defect in one of the two, not
# a tautology. Only the columns the dump prints are carried; sounds, mass, speed
# and the rest would be unused weight.
#
#   doomednum: (mobjtype, flags, height, spawnhealth, spawnstate,
#               spawnstate_has_tics)

MAXPLAYERS = 4

MF_SPAWNCEILING = 0x00000100
MF_COUNTKILL = 0x00400000
MF_COUNTITEM = 0x00800000
MF_NOTDMATCH = 0x02000000
MF_AMBUSH = 0x00000020
MF_TRANSSHIFT = 26

# The THINGS options bit that makes a monster deaf (upstream's MTF_AMBUSH).
MTF_AMBUSH = 8

SKILLS = ("baby", "easy", "medium", "hard", "nightmare")

# MT_PLAYER's doomednum is -1, so it is not addressable from a THINGS record and
# is not in the table: (flags, height, spawnhealth, state, timed). Its mobjtype
# index is 0.
PLAYER_INFO = (0x02000C06, 3670016, 100, 149, 0)

MOBJINFO = {
    3004: (  1, 0x00400006,   3670016,    20, 174, 1),  # MT_POSSESSED
    9: (  2, 0x00400006,   3670016,    30, 207, 1),  # MT_SHOTGUY
    64: (  3, 0x00400006,   3670016,   700, 241, 1),  # MT_VILE
    66: (  5, 0x00400006,   3670016,   300, 321, 1),  # MT_UNDEAD
    67: (  8, 0x00400006,   4194304,   600, 362, 1),  # MT_FATSO
    65: ( 10, 0x00400006,   3670016,    70, 406, 1),  # MT_CHAINGUY
    3001: ( 11, 0x00400006,   3670016,    60, 442, 1),  # MT_TROOP
    3002: ( 12, 0x00400006,   3670016,   150, 475, 1),  # MT_SERGEANT
    58: ( 13, 0x00440006,   3670016,   150, 475, 1),  # MT_SHADOWS
    3005: ( 14, 0x00404206,   3670016,   400, 502, 1),  # MT_HEAD
    3003: ( 15, 0x00400006,   4194304,  1000, 527, 1),  # MT_BRUISER
    69: ( 17, 0x00400006,   4194304,   500, 556, 1),  # MT_KNIGHT
    3006: ( 18, 0x00004206,   3670016,   100, 585, 1),  # MT_SKULL
    7: ( 19, 0x00400006,   6553600,  3000, 601, 1),  # MT_SPIDER
    68: ( 20, 0x00400006,   4194304,   500, 632, 1),  # MT_BABY
    16: ( 21, 0x00400006,   7208960,  4000, 674, 1),  # MT_CYBORG
    71: ( 22, 0x00404206,   3670016,   400, 701, 1),  # MT_PAIN
    84: ( 23, 0x00400006,   3670016,    50, 726, 1),  # MT_WOLFSS
    72: ( 24, 0x00400306,   4718592,   100, 763, 0),  # MT_KEEN
    88: ( 25, 0x00000006,   1048576,   250, 778, 0),  # MT_BOSSBRAIN
    89: ( 26, 0x00000018,   2097152,  1000, 784, 1),  # MT_BOSSSPIT
    87: ( 27, 0x00000018,   2097152,  1000,   0, 0),  # MT_BOSSTARGET
    2035: ( 30, 0x00080006,   2752512,    20, 806, 1),  # MT_BARREL
    14: ( 41, 0x00000018,   1048576,  1000,   0, 0),  # MT_TELEPORTMAN
    2018: ( 43, 0x00000001,   1048576,  1000, 802, 1),  # MT_MISC0
    2019: ( 44, 0x00000001,   1048576,  1000, 804, 1),  # MT_MISC1
    2014: ( 45, 0x00800001,   1048576,  1000, 816, 1),  # MT_MISC2
    2015: ( 46, 0x00800001,   1048576,  1000, 822, 1),  # MT_MISC3
    5: ( 47, 0x02000001,   1048576,  1000, 828, 1),  # MT_MISC4
    13: ( 48, 0x02000001,   1048576,  1000, 830, 1),  # MT_MISC5
    6: ( 49, 0x02000001,   1048576,  1000, 832, 1),  # MT_MISC6
    39: ( 50, 0x02000001,   1048576,  1000, 838, 1),  # MT_MISC7
    38: ( 51, 0x02000001,   1048576,  1000, 836, 1),  # MT_MISC8
    40: ( 52, 0x02000001,   1048576,  1000, 834, 1),  # MT_MISC9
    2011: ( 53, 0x00000001,   1048576,  1000, 840, 0),  # MT_MISC10
    2012: ( 54, 0x00000001,   1048576,  1000, 841, 0),  # MT_MISC11
    2013: ( 55, 0x00800001,   1048576,  1000, 842, 1),  # MT_MISC12
    2022: ( 56, 0x00800001,   1048576,  1000, 848, 1),  # MT_INV
    2023: ( 57, 0x00800001,   1048576,  1000, 852, 0),  # MT_MISC13
    2024: ( 58, 0x00800001,   1048576,  1000, 853, 1),  # MT_INS
    2025: ( 59, 0x00000001,   1048576,  1000, 861, 0),  # MT_MISC14
    2026: ( 60, 0x00800001,   1048576,  1000, 862, 1),  # MT_MISC15
    2045: ( 61, 0x00800001,   1048576,  1000, 868, 1),  # MT_MISC16
    83: ( 62, 0x00800001,   1048576,  1000, 857, 1),  # MT_MEGA
    2007: ( 63, 0x00000001,   1048576,  1000, 870, 0),  # MT_CLIP
    2048: ( 64, 0x00000001,   1048576,  1000, 871, 0),  # MT_MISC17
    2010: ( 65, 0x00000001,   1048576,  1000, 872, 0),  # MT_MISC18
    2046: ( 66, 0x00000001,   1048576,  1000, 873, 0),  # MT_MISC19
    2047: ( 67, 0x00000001,   1048576,  1000, 874, 0),  # MT_MISC20
    17: ( 68, 0x00000001,   1048576,  1000, 875, 0),  # MT_MISC21
    2008: ( 69, 0x00000001,   1048576,  1000, 876, 0),  # MT_MISC22
    2049: ( 70, 0x00000001,   1048576,  1000, 877, 0),  # MT_MISC23
    8: ( 71, 0x00000001,   1048576,  1000, 878, 0),  # MT_MISC24
    2006: ( 72, 0x00000001,   1048576,  1000, 879, 0),  # MT_MISC25
    2002: ( 73, 0x00000001,   1048576,  1000, 880, 0),  # MT_CHAINGUN
    2005: ( 74, 0x00000001,   1048576,  1000, 881, 0),  # MT_MISC26
    2003: ( 75, 0x00000001,   1048576,  1000, 882, 0),  # MT_MISC27
    2004: ( 76, 0x00000001,   1048576,  1000, 883, 0),  # MT_MISC28
    2001: ( 77, 0x00000001,   1048576,  1000, 884, 0),  # MT_SHOTGUN
    82: ( 78, 0x00000001,   1048576,  1000, 885, 0),  # MT_SUPERSHOTGUN
    85: ( 79, 0x00000002,   1048576,  1000, 959, 1),  # MT_MISC29
    86: ( 80, 0x00000002,   1048576,  1000, 963, 1),  # MT_MISC30
    2028: ( 81, 0x00000002,   1048576,  1000, 886, 0),  # MT_MISC31
    30: ( 82, 0x00000002,   1048576,  1000, 907, 0),  # MT_MISC32
    31: ( 83, 0x00000002,   1048576,  1000, 908, 0),  # MT_MISC33
    32: ( 84, 0x00000002,   1048576,  1000, 909, 0),  # MT_MISC34
    33: ( 85, 0x00000002,   1048576,  1000, 910, 0),  # MT_MISC35
    37: ( 86, 0x00000002,   1048576,  1000, 913, 0),  # MT_MISC36
    36: ( 87, 0x00000002,   1048576,  1000, 924, 1),  # MT_MISC37
    41: ( 88, 0x00000002,   1048576,  1000, 917, 1),  # MT_MISC38
    42: ( 89, 0x00000002,   1048576,  1000, 921, 1),  # MT_MISC39
    43: ( 90, 0x00000002,   1048576,  1000, 914, 0),  # MT_MISC40
    44: ( 91, 0x00000002,   1048576,  1000, 926, 1),  # MT_MISC41
    45: ( 92, 0x00000002,   1048576,  1000, 930, 1),  # MT_MISC42
    46: ( 93, 0x00000002,   1048576,  1000, 934, 1),  # MT_MISC43
    55: ( 94, 0x00000002,   1048576,  1000, 938, 1),  # MT_MISC44
    56: ( 95, 0x00000002,   1048576,  1000, 942, 1),  # MT_MISC45
    57: ( 96, 0x00000002,   1048576,  1000, 946, 1),  # MT_MISC46
    47: ( 97, 0x00000002,   1048576,  1000, 906, 0),  # MT_MISC47
    48: ( 98, 0x00000002,   1048576,  1000, 916, 0),  # MT_MISC48
    34: ( 99, 0x00000000,   1048576,  1000, 911, 0),  # MT_MISC49
    35: (100, 0x00000002,   1048576,  1000, 912, 0),  # MT_MISC50
    49: (101, 0x00000302,   4456448,  1000, 888, 1),  # MT_MISC51
    50: (102, 0x00000302,   5505024,  1000, 902, 0),  # MT_MISC52
    51: (103, 0x00000302,   5505024,  1000, 903, 0),  # MT_MISC53
    52: (104, 0x00000302,   4456448,  1000, 904, 0),  # MT_MISC54
    53: (105, 0x00000302,   3407872,  1000, 905, 0),  # MT_MISC55
    59: (106, 0x00000300,   5505024,  1000, 902, 0),  # MT_MISC56
    60: (107, 0x00000300,   4456448,  1000, 904, 0),  # MT_MISC57
    61: (108, 0x00000300,   3407872,  1000, 903, 0),  # MT_MISC58
    62: (109, 0x00000300,   3407872,  1000, 905, 0),  # MT_MISC59
    63: (110, 0x00000300,   4456448,  1000, 888, 1),  # MT_MISC60
    22: (111, 0x00000000,   1048576,  1000, 515, 0),  # MT_MISC61
    15: (112, 0x00000000,   1048576,  1000, 164, 0),  # MT_MISC62
    18: (113, 0x00000000,   1048576,  1000, 193, 0),  # MT_MISC63
    21: (114, 0x00000000,   1048576,  1000, 495, 0),  # MT_MISC64
    23: (115, 0x00000000,   1048576,  1000, 600, 1),  # MT_MISC65
    20: (116, 0x00000000,   1048576,  1000, 461, 0),  # MT_MISC66
    19: (117, 0x00000000,   1048576,  1000, 226, 0),  # MT_MISC67
    10: (118, 0x00000000,   1048576,  1000, 173, 0),  # MT_MISC68
    12: (119, 0x00000000,   1048576,  1000, 173, 0),  # MT_MISC69
    28: (120, 0x00000002,   1048576,  1000, 894, 0),  # MT_MISC70
    24: (121, 0x00000000,   1048576,  1000, 895, 0),  # MT_MISC71
    27: (122, 0x00000002,   1048576,  1000, 896, 0),  # MT_MISC72
    29: (123, 0x00000002,   1048576,  1000, 897, 1),  # MT_MISC73
    25: (124, 0x00000002,   1048576,  1000, 899, 0),  # MT_MISC74
    26: (125, 0x00000002,   1048576,  1000, 900, 1),  # MT_MISC75
    54: (126, 0x00000002,   1048576,  1000, 915, 0),  # MT_MISC76
    70: (127, 0x00000002,   1048576,  1000, 813, 1),  # MT_MISC77
    73: (128, 0x00000302,   5767168,  1000, 950, 0),  # MT_MISC78
    74: (129, 0x00000302,   5767168,  1000, 951, 0),  # MT_MISC79
    75: (130, 0x00000302,   4194304,  1000, 952, 0),  # MT_MISC80
    76: (131, 0x00000302,   4194304,  1000, 953, 0),  # MT_MISC81
    77: (132, 0x00000302,   4194304,  1000, 954, 0),  # MT_MISC82
    78: (133, 0x00000302,   4194304,  1000, 955, 0),  # MT_MISC83
    79: (134, 0x00000010,   1048576,  1000, 956, 0),  # MT_MISC84
    80: (135, 0x00000010,   1048576,  1000, 957, 0),  # MT_MISC85
    81: (136, 0x00000010,   1048576,  1000, 958, 0),  # MT_MISC86
}


def sector_of_subsector(bsp: BspMap, index: int) -> int:
    """P_GroupLines' `ss->sector = seg[ss->firstline].sidedef->sector`."""
    subsector = bsp.ssectors[index]
    seg = bsp.segs[subsector["firstseg"]]
    line = bsp.linedefs[seg["linedef"]]
    side = bsp.sidedefs[line["side0"] if seg["side"] == 0 else line["side1"]]
    return side["sector"]


class ThingRun:
    """One level's THINGS pass: the mobjs, the counters and the RNG position.

    The RNG is tracked as a position, not as values: every spawn consumes one
    draw for `lastlook` and one more when the spawn state has tics, and the
    position at the end is what the C side prints as RNG <index>. The values
    themselves are pinned by test/unit/test_world, so a second copy of the table
    here would test nothing new.
    """

    def __init__(self, bsp: BspMap, skill: int) -> None:
        self.bsp = bsp
        self.skill = skill
        self.mobjs: list[dict] = []
        self.rng = 0
        self.player_starts: list[dict | None] = [None] * MAXPLAYERS
        self.deathmatch_starts: list[dict] = []
        self.player_index = -1
        self.counters = {
            "things": 0, "spawned": 0, "kills": 0, "items": 0, "secrets": 0,
            "skill": 0, "single": 0, "nomonsters": 0, "dmskip": 0,
            "unknown": 0, "invalid": 0, "failed": 0,
            "playerstarts": 0, "deathmatchstarts": 0,
        }

    def draw(self) -> None:
        """One P_Random() call: only the position matters here."""
        self.rng = (self.rng + 1) & 0xFF

    def spawn(self, mobj_type: int, x: int, y: int, z_rule: str, info: tuple,
              angle_degrees: int, extra_flags: int = 0) -> int:
        """P_SpawnMobj() with `z` resolved by the rule, then the tics draw."""
        flags, height, health, spawnstate, timed = info
        self.draw()                                     # lastlook
        subsector = point_in_subsector(self.bsp, x, y)
        sector = sector_of_subsector(self.bsp, subsector)
        floor = self.bsp.sectors[sector]["floor"]
        ceiling = self.bsp.sectors[sector]["ceiling"]
        if z_rule == "floor":
            z = floor
        elif z_rule == "ceiling":
            z = ceiling - height
        else:
            raise FormatError(f"unknown z rule {z_rule!r}")
        if timed:
            self.draw()                                 # `1 + P_Random() % tics`
        self.mobjs.append({
            "type": mobj_type,
            "x": x,
            "y": y,
            "z": i32(z),
            "angle": u32(ANG45 * trunc_div(angle_degrees, 45)),
            "subsector": subsector,
            "sector": sector,
            "flags": u32(flags | extra_flags),
            "health": health,
            "state": spawnstate,
            "height": height,
        })
        return len(self.mobjs) - 1


def run_things(bsp: BspMap, skill: int) -> ThingRun:
    """The THINGS pass without printing.

    report_things() prints it; report_specials() uses the RNG position it leaves,
    because P_SpawnSpecials() consumes the sequence *after* P_LoadThings().
    """
    if skill == 0:
        bit = 1
    elif skill == 4:
        bit = 4
    else:
        bit = 1 << (skill - 1)

    run = ThingRun(bsp, skill)
    player_in_game = [True, False, False, False]

    for record in bsp.things:
        kind = record["type"]
        run.counters["things"] += 1

        # 1. Deathmatch starts.
        if kind == 11:
            if len(run.deathmatch_starts) < 10:
                run.deathmatch_starts.append(record)
            run.counters["deathmatchstarts"] += 1
            continue

        # 2. Player starts.
        if 1 <= kind <= MAXPLAYERS:
            run.player_starts[kind - 1] = record
            run.counters["playerstarts"] += 1
            if player_in_game[kind - 1]:
                extra = (kind - 1) << MF_TRANSSHIFT if kind > 1 else 0
                index = run.spawn(0, _fixed16(record["x"]), _fixed16(record["y"]),
                                  "floor", PLAYER_INFO, record["angle"], extra)
                run.counters["spawned"] += 1
                if kind == 1 and run.player_index < 0:
                    run.player_index = index
            continue

        # 3. Not a thing.
        if kind <= 0:
            run.counters["invalid"] += 1
            continue

        # 4. The single-player options bit.
        if record["options"] & 16:
            run.counters["single"] += 1
            continue

        # 5. The skill bit.
        if not record["options"] & bit:
            run.counters["skill"] += 1
            continue

        # 6. The doomednum scan.
        row = MOBJINFO.get(kind)
        if row is None:
            run.counters["unknown"] += 1
            continue
        mobj_type, flags, height, health, spawnstate, timed = row

        # 7. MF_NOTDMATCH only filters in deathmatch and 8. -nomonsters is off,
        #    so neither branch can fire in this single-player run. gen_info's
        #    table and the C loader still carry both; see p_mobj.c.

        z_rule = "ceiling" if flags & MF_SPAWNCEILING else "floor"
        ambush = MF_AMBUSH if record["options"] & MTF_AMBUSH else 0
        run.spawn(mobj_type, _fixed16(record["x"]), _fixed16(record["y"]), z_rule,
                  (flags, height, health, spawnstate, timed), record["angle"],
                  ambush)
        run.counters["spawned"] += 1
        if flags & MF_COUNTKILL:
            run.counters["kills"] += 1
        if flags & MF_COUNTITEM:
            run.counters["items"] += 1

    return run


def report_things(lumps: list[Lump], map_name: str, skill_name: str) -> None:
    """The `--things` protocol: P_LoadThings() through P_SpawnMapThing()."""
    if skill_name not in SKILLS:
        raise FormatError(f"--skill must be one of {', '.join(SKILLS)}")
    skill = SKILLS.index(skill_name)
    bsp = read_bsp_map(lumps, map_name)
    run = run_things(bsp, skill)

    print(f"THINGS {map_name} skill={skill} things={len(bsp.things)}")
    for index, mobj in enumerate(run.mobjs):
        print(f"MOBJ {index} type={mobj['type']} x={mobj['x']} y={mobj['y']} "
              f"z={mobj['z']} angle=0x{mobj['angle']:08x} "
              f"subsector={mobj['subsector']} sector={mobj['sector']} "
              f"flags=0x{mobj['flags']:08x} health={mobj['health']} "
              f"state={mobj['state']}")
    print(f"MOBJ-COUNT {len(run.mobjs)}")

    ordered = ["things", "spawned", "kills", "items", "secrets", "skill", "single",
               "nomonsters", "dmskip", "unknown", "invalid", "failed",
               "playerstarts", "deathmatchstarts"]
    print("COUNTERS " + " ".join(f"{name}={run.counters[name]}" for name in ordered))

    for slot, record in enumerate(run.player_starts):
        if record is not None:
            print(f"PLAYERSTART {slot} x={record['x']} y={record['y']} "
                  f"angle={record['angle']} type={record['type']} "
                  f"options={record['options']}")
    for index, record in enumerate(run.deathmatch_starts):
        print(f"DEATHMATCHSTART {index} x={record['x']} y={record['y']} "
              f"angle={record['angle']} type={record['type']} "
              f"options={record['options']}")

    print(f"PLAYER {run.player_index}")
    print(f"RNG {run.rng}")


# --- The specials run (D4.1b) ----------------------------------------------
#
# A second implementation of linuxdoom-1.10's sector specials, written from the
# state machines in p_spec.c, p_floor.c, p_doors.c, p_plats.c, p_ceilng.c and
# p_lights.c -- not from the C in this repository. It reads the map itself, owns
# its own sector array and thinker list, and steps the same tics.
#
# The RNG table is upstream data (m_random.c) transcribed once; the C carries
# the same 256 bytes in lib/doom/m_random.c. Every value below is DOOM's, not a
# recording of the C output.

RNDTABLE = (
    0, 8, 109, 220, 222, 241, 149, 107, 75, 248, 254, 140, 16, 66,
    74, 21, 211, 47, 80, 242, 154, 27, 205, 128, 161, 89, 77, 36,
    95, 110, 85, 48, 212, 140, 211, 249, 22, 79, 200, 50, 28, 188,
    52, 140, 202, 120, 68, 145, 62, 70, 184, 190, 91, 197, 152, 224,
    149, 104, 25, 178, 252, 182, 202, 182, 141, 197, 4, 81, 181, 242,
    145, 42, 39, 227, 156, 198, 225, 193, 219, 93, 122, 175, 249, 0,
    175, 143, 70, 239, 46, 246, 163, 53, 163, 109, 168, 135, 2, 235,
    25, 92, 20, 145, 138, 77, 69, 166, 78, 176, 173, 212, 166, 113,
    94, 161, 41, 50, 239, 49, 111, 164, 70, 60, 2, 37, 171, 75,
    136, 156, 11, 56, 42, 146, 138, 229, 73, 146, 77, 61, 98, 196,
    135, 106, 63, 197, 195, 86, 96, 203, 113, 101, 170, 247, 181, 113,
    80, 250, 108, 7, 255, 237, 129, 226, 79, 107, 112, 166, 103, 241,
    24, 223, 239, 120, 198, 58, 60, 82, 128, 3, 184, 66, 143, 224,
    145, 224, 81, 206, 163, 45, 63, 90, 168, 114, 59, 33, 159, 95,
    28, 139, 123, 98, 125, 196, 15, 70, 194, 253, 54, 14, 109, 226,
    71, 17, 161, 93, 186, 87, 244, 138, 20, 52, 123, 251, 26, 36,
    17, 46, 52, 231, 232, 76, 31, 221, 84, 37, 216, 165, 212, 106,
    197, 242, 98, 43, 39, 175, 254, 145, 190, 84, 118, 222, 187, 136,
    120, 163, 236, 249,
)

FRAC = 1 << FRACBITS

VDOORSPEED = FRAC * 2
VDOORWAIT = 150
FLOORSPEED = FRAC
CEILSPEED = FRAC
PLATSPEED = FRAC
PLATWAIT = 3
GLOWSPEED = 8
STROBEBRIGHT = 5
FASTDARK = 15
SLOWDARK = 35

# floor_e (p_spec.h)
FLOOR_LOWER = 0
FLOOR_LOWER_TO_LOWEST = 1
FLOOR_TURBO_LOWER = 2
FLOOR_RAISE = 3
FLOOR_RAISE_TO_NEAREST = 4
FLOOR_RAISE_TO_TEXTURE = 5
FLOOR_LOWER_AND_CHANGE = 6
FLOOR_RAISE_24 = 7
FLOOR_RAISE_24_AND_CHANGE = 8
FLOOR_RAISE_CRUSH = 9
FLOOR_RAISE_TURBO = 10
FLOOR_DONUT_RAISE = 11
FLOOR_RAISE_512 = 12

# stair_e (p_spec.h), in upstream's order.
STAIR_BUILD8 = 0
STAIR_TURBO16 = 1

# EV_Teleport's two constants (p_spec.h P_TELEPORT_*).
TELEPORT_REACTIONTIME = 18
TELEPORT_FOG_DISTANCE = 20

# vldoor_e
DOOR_NORMAL = 0
DOOR_CLOSE30 = 1
DOOR_CLOSE = 2
DOOR_OPEN = 3
DOOR_RAISE5 = 4
DOOR_BLAZE_RAISE = 5
DOOR_BLAZE_OPEN = 6
DOOR_BLAZE_CLOSE = 7

# plat_e / plattype_e
PLAT_UP = 0
PLAT_DOWN = 1
PLAT_WAITING = 2
PLAT_IN_STASIS = 3
PLAT_PERPETUAL = 0
PLAT_DWUS = 1
PLAT_RAISE_AND_CHANGE = 2
PLAT_RAISE_NEAREST = 3
PLAT_BLAZE_DWUS = 4

# ceiling_e
CEIL_LOWER_TO_FLOOR = 0
CEIL_RAISE_TO_HIGHEST = 1
CEIL_LOWER_AND_CRUSH = 2
CEIL_CRUSH_AND_RAISE = 3
CEIL_FAST_CRUSH = 4
CEIL_SILENT_CRUSH = 5

# p_light_type_t
LIGHT_FLASH = 0
LIGHT_STROBE = 1
LIGHT_GLOW = 2
LIGHT_FIRE = 3


class RandomState:
    """Upstream P_Random(): pre-increment the index, then read the table."""

    def __init__(self, index: int) -> None:
        self.index = index & 0xFF

    def next(self) -> int:
        self.index = (self.index + 1) & 0xFF
        return RNDTABLE[self.index]


# The sound-blocking linedef flag (doomdef.h's ML_SOUNDBLOCK).
ML_SOUNDBLOCK = 64


class SpecLevel:
    """One level's mutable sector state and thinker list."""

    def __init__(self, bsp: BspMap, rng: RandomState) -> None:
        self.bsp = bsp
        self.sectors = []
        for sector in bsp.sectors:
            self.sectors.append({
                "floor": sector["floor"],
                "ceiling": sector["ceiling"],
                "floorpic": sector["floorpic"],
                "ceilingpic": sector["ceilingpic"],
                "light": sector["light"],
                "special": sector["special"],
                "tag": sector["tag"],
                "specialdata": None,
            })
        self.line_special = [line["special"] for line in bsp.linedefs]
        self.thinkers: list[dict] = []
        self.rng = rng
        self.total_secrets = 0
        self.leveltime = 0
        # D4.5: G_ExitLevel()/G_SecretExitLevel()'s write. Upstream sets
        # `secretexit` and `gameaction = ga_completed`; the oracle keeps the
        # request so a comparison can see which exit a trigger asked for.
        self.exit_request = "none"
        # T15a: the per-sector sound flood state (upstream's sector_t
        # soundtarget/soundtraversed/validcount) and the one epoch counter shared
        # by every flood, exactly as p_sector_sound_t and
        # p_mobj_context_t::soundValidCount hold them in the C.
        self.sound = [{"target": None, "traversed": 0, "valid": 0}
                      for _ in bsp.sectors]
        self.sound_validcount = 0
        # playeringame[]: single player is slot 0, which is what
        # p_spawn_rules_default() leaves and what the C's P_LookForPlayers reads.
        self.player_in_game = [True, False, False, False]
        self.stats = {
            "spawned": 0, "secrets": 0, "changeSector": 0,
            "unsupportedSector": 0, "unsupportedLine": 0,
            "deferredLine": 0, "lineScrolls": 0, "triggers": 0, "refused": 0,
        }


def _line(level: SpecLevel, index: int) -> dict:
    return level.bsp.linedefs[index]


def _front_sector(level: SpecLevel, line: dict) -> int | None:
    if line["side0"] < 0:
        return None
    return level.bsp.sidedefs[line["side0"]]["sector"]


def _back_sector(level: SpecLevel, line: dict) -> int | None:
    if not (line["flags"] & 4) or line["side1"] < 0:
        return None
    return level.bsp.sidedefs[line["side1"]]["sector"]


def _other_sector(level: SpecLevel, line: dict, sector: int) -> int | None:
    """getNextSector(): front -> back, otherwise front; None when one-sided."""
    if not (line["flags"] & 4):
        return None
    front = _front_sector(level, line)
    back = _back_sector(level, line)
    if front is None or back is None:
        return None
    return back if front == sector else front


def _neighbours(level: SpecLevel, sector: int):
    """P_GroupLines' per-sector line list, reduced to the other sectors.

    Only lines that actually touch @p sector count; `_other_sector` alone would
    answer every two-sided line's front sector, which is upstream's
    getNextSector() quirk and not this loop's input.
    """
    for line in level.bsp.linedefs:
        if not (line["flags"] & 4):
            continue
        front = _front_sector(level, line)
        back = _back_sector(level, line)
        if front is None or back is None:
            continue
        if front == sector:
            yield back
        elif back == sector:
            yield front


def find_sector_from_tag(level: SpecLevel, tag: int, start: int) -> int:
    for index in range(start + 1, len(level.sectors)):
        if level.sectors[index]["tag"] == tag:
            return index
    return -1


def find_lowest_floor_surrounding(level: SpecLevel, sector: int) -> int:
    floor = level.sectors[sector]["floor"]
    for other in _neighbours(level, sector):
        if level.sectors[other]["floor"] < floor:
            floor = level.sectors[other]["floor"]
    return floor


def find_highest_floor_surrounding(level: SpecLevel, sector: int) -> int:
    floor = -500 * FRAC
    for other in _neighbours(level, sector):
        if level.sectors[other]["floor"] > floor:
            floor = level.sectors[other]["floor"]
    return floor


def find_next_highest_floor(level: SpecLevel, sector: int, current: int) -> int:
    heights = []
    for other in _neighbours(level, sector):
        height = level.sectors[other]["floor"]
        if height > current:
            heights.append(height)
    if not heights:
        return current
    return min(heights[:20])


def find_lowest_ceiling_surrounding(level: SpecLevel, sector: int) -> int:
    height = 0x7FFFFFFF
    for other in _neighbours(level, sector):
        if level.sectors[other]["ceiling"] < height:
            height = level.sectors[other]["ceiling"]
    return height


def find_highest_ceiling_surrounding(level: SpecLevel, sector: int) -> int:
    height = 0
    for other in _neighbours(level, sector):
        if level.sectors[other]["ceiling"] > height:
            height = level.sectors[other]["ceiling"]
    return height


def find_min_surrounding_light(level: SpecLevel, sector: int, maximum: int) -> int:
    minimum = maximum
    for other in _neighbours(level, sector):
        if level.sectors[other]["light"] < minimum:
            minimum = level.sectors[other]["light"]
    return minimum


def change_sector(level: SpecLevel) -> bool:
    """p_spec_p_change_sector(): the counted stub, always 'clear'."""
    level.stats["changeSector"] += 1
    return False


def move_plane(level: SpecLevel, sector_index: int, speed: int, dest: int,
               crush: bool, floor_or_ceiling: int, direction: int) -> str:
    """T_MovePlane() (p_floor.c)."""
    sector = level.sectors[sector_index]
    if floor_or_ceiling == 0:
        if direction == -1:
            if sector["floor"] - speed < dest:
                last = sector["floor"]
                sector["floor"] = dest
                if change_sector(level):
                    sector["floor"] = last
                    change_sector(level)
                return "pastdest"
            last = sector["floor"]
            sector["floor"] -= speed
            if change_sector(level):
                sector["floor"] = last
                change_sector(level)
                return "crushed"
        elif direction == 1:
            if sector["floor"] + speed > dest:
                last = sector["floor"]
                sector["floor"] = dest
                if change_sector(level):
                    sector["floor"] = last
                    change_sector(level)
                return "pastdest"
            last = sector["floor"]
            sector["floor"] += speed
            if change_sector(level):
                if crush:
                    return "crushed"
                sector["floor"] = last
                change_sector(level)
                return "crushed"
    else:
        if direction == -1:
            if sector["ceiling"] - speed < dest:
                last = sector["ceiling"]
                sector["ceiling"] = dest
                if change_sector(level):
                    sector["ceiling"] = last
                    change_sector(level)
                return "pastdest"
            last = sector["ceiling"]
            sector["ceiling"] -= speed
            if change_sector(level):
                if crush:
                    return "crushed"
                sector["ceiling"] = last
                change_sector(level)
                return "crushed"
        elif direction == 1:
            if sector["ceiling"] + speed > dest:
                last = sector["ceiling"]
                sector["ceiling"] = dest
                if change_sector(level):
                    sector["ceiling"] = last
                    change_sector(level)
                return "pastdest"
            sector["ceiling"] += speed
            change_sector(level)     # upstream's revert is #if 0
    return "ok"


def new_thinker(level: SpecLevel, kind: str, **fields) -> dict:
    thinker = {"kind": kind, "removed": False}
    thinker.update(fields)
    level.thinkers.append(thinker)
    return thinker


def finish(level: SpecLevel, thinker: dict) -> None:
    sector = level.sectors[thinker["sector"]]
    if sector["specialdata"] is thinker:
        sector["specialdata"] = None
    thinker["removed"] = True


# --- Light spawns ------------------------------------------------------------

def spawn_light_flash(level: SpecLevel, sector_index: int) -> bool:
    sector = level.sectors[sector_index]
    sector["special"] = 0
    new_thinker(level, "light", sector=sector_index, type=LIGHT_FLASH, dir=0,
                speed=0, count=(level.rng.next() & 64) + 1, maxLight=sector["light"],
                minLight=find_min_surrounding_light(level, sector_index, sector["light"]),
                maxTime=64, minTime=7, darkTime=0, brightTime=0)
    return True


def spawn_strobe_flash(level: SpecLevel, sector_index: int, fast_or_slow: int,
                       in_sync: bool) -> bool:
    sector = level.sectors[sector_index]
    maxlight = sector["light"]
    minlight = find_min_surrounding_light(level, sector_index, maxlight)
    if minlight == maxlight:
        minlight = 0
    sector["special"] = 0
    count = 1 if in_sync else (level.rng.next() & 7) + 1
    new_thinker(level, "light", sector=sector_index, type=LIGHT_STROBE, dir=0,
                speed=0, count=count, maxLight=maxlight, minLight=minlight,
                maxTime=0, minTime=0, darkTime=fast_or_slow,
                brightTime=STROBEBRIGHT)
    return True


def spawn_glowing_light(level: SpecLevel, sector_index: int) -> bool:
    sector = level.sectors[sector_index]
    minlight = find_min_surrounding_light(level, sector_index, sector["light"])
    sector["special"] = 0
    new_thinker(level, "light", sector=sector_index, type=LIGHT_GLOW, dir=-1,
                speed=0, count=0, maxLight=sector["light"], minLight=minlight,
                maxTime=0, minTime=0, darkTime=0, brightTime=0)
    return True


def spawn_fire_flicker(level: SpecLevel, sector_index: int) -> bool:
    sector = level.sectors[sector_index]
    sector["special"] = 0
    new_thinker(level, "light", sector=sector_index, type=LIGHT_FIRE, dir=0,
                speed=0, count=4, maxLight=sector["light"],
                minLight=find_min_surrounding_light(level, sector_index,
                                                    sector["light"]) + 16,
                maxTime=0, minTime=0, darkTime=0, brightTime=0)
    return True


# --- Doors ------------------------------------------------------------------

def new_door(level: SpecLevel, sector_index: int) -> dict:
    thinker = new_thinker(level, "door", sector=sector_index, type=DOOR_NORMAL,
                          dir=0, topHeight=0, speed=VDOORSPEED,
                          topWait=VDOORWAIT, count=0)
    level.sectors[sector_index]["specialdata"] = thinker
    return thinker


def door_top_height(level: SpecLevel, sector_index: int) -> int:
    return find_lowest_ceiling_surrounding(level, sector_index) - 4 * FRAC


def do_door(level: SpecLevel, line_index: int, door_type: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        sector = level.sectors[secnum]
        if sector["specialdata"] is not None:
            continue
        door = new_door(level, secnum)
        rtn = True
        door["type"] = door_type
        door["topWait"] = VDOORWAIT
        door["speed"] = VDOORSPEED
        if door_type == DOOR_BLAZE_CLOSE:
            door["topHeight"] = door_top_height(level, secnum)
            door["dir"] = -1
            door["speed"] = VDOORSPEED * 4
        elif door_type == DOOR_CLOSE:
            door["topHeight"] = door_top_height(level, secnum)
            door["dir"] = -1
        elif door_type == DOOR_CLOSE30:
            door["topHeight"] = sector["ceiling"]
            door["dir"] = -1
        elif door_type in (DOOR_BLAZE_RAISE, DOOR_BLAZE_OPEN):
            door["dir"] = 1
            door["topHeight"] = door_top_height(level, secnum)
            door["speed"] = VDOORSPEED * 4
        elif door_type in (DOOR_NORMAL, DOOR_OPEN):
            door["dir"] = 1
            door["topHeight"] = door_top_height(level, secnum)
    return rtn


def vertical_door(level: SpecLevel, line_index: int, by_player: bool) -> bool:
    line = _line(level, line_index)
    sector_index = _back_sector(level, line)
    if sector_index is None:
        return False
    sector = level.sectors[sector_index]
    special = level.line_special[line_index]

    if sector["specialdata"] is not None:
        door = sector["specialdata"]
        if door["kind"] != "door":
            level.stats["refused"] += 1
            return False
        if special in (1, 26, 27, 28, 117):
            if door["dir"] == -1:
                door["dir"] = 1
            else:
                if not by_player:
                    return False
                door["dir"] = -1
            return True
        level.stats["refused"] += 1
        return False

    door = new_door(level, sector_index)
    door["dir"] = 1
    door["speed"] = VDOORSPEED
    door["topWait"] = VDOORWAIT
    if special in (1, 26, 27, 28):
        door["type"] = DOOR_NORMAL
    elif special in (31, 32, 33, 34):
        door["type"] = DOOR_OPEN
        level.line_special[line_index] = 0
    elif special == 117:
        door["type"] = DOOR_BLAZE_RAISE
        door["speed"] = VDOORSPEED * 4
    elif special == 118:
        door["type"] = DOOR_BLAZE_OPEN
        door["speed"] = VDOORSPEED * 4
        level.line_special[line_index] = 0
    else:
        door["type"] = DOOR_NORMAL
    door["topHeight"] = door_top_height(level, sector_index)
    return True


def spawn_door_close_in_30(level: SpecLevel, sector_index: int) -> bool:
    door = new_door(level, sector_index)
    level.sectors[sector_index]["special"] = 0
    door["dir"] = 0
    door["type"] = DOOR_NORMAL
    door["speed"] = VDOORSPEED
    door["count"] = 30 * 35
    return True


def spawn_door_raise_in_5_mins(level: SpecLevel, sector_index: int) -> bool:
    door = new_door(level, sector_index)
    level.sectors[sector_index]["special"] = 0
    door["dir"] = 2
    door["type"] = DOOR_RAISE5
    door["speed"] = VDOORSPEED
    door["topHeight"] = door_top_height(level, sector_index)
    door["topWait"] = VDOORWAIT
    door["count"] = 5 * 60 * 35
    return True


# --- Floors -----------------------------------------------------------------

def new_floor(level: SpecLevel, sector_index: int, floor_type: int) -> dict:
    thinker = new_thinker(level, "floor", sector=sector_index, type=floor_type,
                          dir=0, speed=FLOORSPEED, crush=0, newspecial=0,
                          texture=b"", dest=0)
    level.sectors[sector_index]["specialdata"] = thinker
    return thinker


def start_floor(level: SpecLevel, sector_index: int, line_index: int,
                floor_type: int) -> bool:
    sector = level.sectors[sector_index]
    if floor_type == FLOOR_RAISE_TO_TEXTURE:
        # textureheight[] is r_data's and is not this layer's.
        level.stats["deferredLine"] += 1
        return False
    floor = new_floor(level, sector_index, floor_type)
    if floor_type == FLOOR_LOWER:
        floor["dir"] = -1
        floor["dest"] = find_highest_floor_surrounding(level, sector_index)
    elif floor_type == FLOOR_LOWER_TO_LOWEST:
        floor["dir"] = -1
        floor["dest"] = find_lowest_floor_surrounding(level, sector_index)
    elif floor_type == FLOOR_TURBO_LOWER:
        floor["dir"] = -1
        floor["speed"] = FLOORSPEED * 4
        floor["dest"] = find_highest_floor_surrounding(level, sector_index)
        if floor["dest"] != sector["floor"]:
            floor["dest"] += 8 * FRAC
    elif floor_type in (FLOOR_RAISE, FLOOR_RAISE_CRUSH):
        if floor_type == FLOOR_RAISE_CRUSH:
            floor["crush"] = 1
        floor["dir"] = 1
        floor["dest"] = find_lowest_ceiling_surrounding(level, sector_index)
        if floor["dest"] > sector["ceiling"]:
            floor["dest"] = sector["ceiling"]
        if floor_type == FLOOR_RAISE_CRUSH:
            floor["dest"] -= 8 * FRAC
    elif floor_type == FLOOR_RAISE_TURBO:
        floor["dir"] = 1
        floor["speed"] = FLOORSPEED * 4
        floor["dest"] = find_next_highest_floor(level, sector_index, sector["floor"])
    elif floor_type == FLOOR_RAISE_TO_NEAREST:
        floor["dir"] = 1
        floor["dest"] = find_next_highest_floor(level, sector_index, sector["floor"])
    elif floor_type == FLOOR_RAISE_24:
        floor["dir"] = 1
        floor["dest"] = sector["floor"] + 24 * FRAC
    elif floor_type == FLOOR_RAISE_512:
        floor["dir"] = 1
        floor["dest"] = sector["floor"] + 512 * FRAC
    elif floor_type == FLOOR_RAISE_24_AND_CHANGE:
        floor["dir"] = 1
        floor["dest"] = sector["floor"] + 24 * FRAC
        front = _front_sector(level, _line(level, line_index))
        if front is not None:
            sector["floorpic"] = level.sectors[front]["floorpic"]
            sector["special"] = level.sectors[front]["special"]
    elif floor_type == FLOOR_LOWER_AND_CHANGE:
        floor["dir"] = -1
        floor["dest"] = find_lowest_floor_surrounding(level, sector_index)
        floor["texture"] = sector["floorpic"]
        for line in level.bsp.linedefs:
            if not (line["flags"] & 4):
                continue
            if line["side0"] < 0 or line["side1"] < 0:
                continue
            side0 = level.bsp.sidedefs[line["side0"]]["sector"]
            side1 = level.bsp.sidedefs[line["side1"]]["sector"]
            if side0 == sector_index:
                other = side1
            elif side1 == sector_index:
                other = side0
            else:
                continue
            if level.sectors[other]["floor"] == floor["dest"]:
                floor["texture"] = level.sectors[other]["floorpic"]
                floor["newspecial"] = level.sectors[other]["special"]
                break
    return True


def do_floor(level: SpecLevel, line_index: int, floor_type: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        if level.sectors[secnum]["specialdata"] is not None:
            continue
        rtn = start_floor(level, secnum, line_index, floor_type)
    return rtn


# --- Plats ------------------------------------------------------------------

def new_plat(level: SpecLevel, sector_index: int, tag: int) -> dict:
    thinker = new_thinker(level, "plat", sector=sector_index, type=PLAT_PERPETUAL,
                          status=PLAT_WAITING, oldStatus=PLAT_WAITING, crush=0,
                          tag=tag, speed=PLATSPEED, low=0, high=0, wait=0,
                          count=0)
    level.sectors[sector_index]["specialdata"] = thinker
    return thinker


def activate_in_stasis_plats(level: SpecLevel, tag: int) -> bool:
    rtn = False
    for thinker in level.thinkers:
        if (thinker["kind"] == "plat" and thinker["tag"] == tag and
                thinker["status"] == PLAT_IN_STASIS):
            thinker["status"] = thinker["oldStatus"]
            rtn = True
    return rtn


def do_plat(level: SpecLevel, line_index: int, plat_type: int, amount: int) -> bool:
    line = _line(level, line_index)
    front = _front_sector(level, line)
    tag = line["tag"]
    if plat_type == PLAT_PERPETUAL:
        activate_in_stasis_plats(level, tag)
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        sector = level.sectors[secnum]
        if sector["specialdata"] is not None:
            continue
        plat = new_plat(level, secnum, tag)
        rtn = True
        plat["type"] = plat_type
        if plat_type == PLAT_RAISE_NEAREST:
            plat["speed"] = PLATSPEED // 2
            if front is not None:
                sector["floorpic"] = level.sectors[front]["floorpic"]
            plat["high"] = find_next_highest_floor(level, secnum, sector["floor"])
            plat["wait"] = 0
            plat["status"] = PLAT_UP
            sector["special"] = 0
        elif plat_type == PLAT_RAISE_AND_CHANGE:
            plat["speed"] = PLATSPEED // 2
            if front is not None:
                sector["floorpic"] = level.sectors[front]["floorpic"]
            plat["high"] = sector["floor"] + amount * FRAC
            plat["wait"] = 0
            plat["status"] = PLAT_UP
        elif plat_type == PLAT_DWUS:
            plat["speed"] = PLATSPEED * 4
            plat["low"] = find_lowest_floor_surrounding(level, secnum)
            if plat["low"] > sector["floor"]:
                plat["low"] = sector["floor"]
            plat["high"] = sector["floor"]
            plat["wait"] = 35 * PLATWAIT
            plat["status"] = PLAT_DOWN
        elif plat_type == PLAT_BLAZE_DWUS:
            plat["speed"] = PLATSPEED * 8
            plat["low"] = find_lowest_floor_surrounding(level, secnum)
            if plat["low"] > sector["floor"]:
                plat["low"] = sector["floor"]
            plat["high"] = sector["floor"]
            plat["wait"] = 35 * PLATWAIT
            plat["status"] = PLAT_DOWN
        elif plat_type == PLAT_PERPETUAL:
            plat["speed"] = PLATSPEED
            plat["low"] = find_lowest_floor_surrounding(level, secnum)
            if plat["low"] > sector["floor"]:
                plat["low"] = sector["floor"]
            plat["high"] = find_highest_floor_surrounding(level, secnum)
            if plat["high"] < sector["floor"]:
                plat["high"] = sector["floor"]
            plat["wait"] = 35 * PLATWAIT
            plat["status"] = level.rng.next() & 1
    return rtn


def stop_plat(level: SpecLevel, line_index: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    for thinker in level.thinkers:
        if (thinker["kind"] == "plat" and thinker["tag"] == tag and
                thinker["status"] != PLAT_IN_STASIS):
            thinker["oldStatus"] = thinker["status"]
            thinker["status"] = PLAT_IN_STASIS
            rtn = True
    return rtn


# --- Ceilings ---------------------------------------------------------------

def new_ceiling(level: SpecLevel, sector_index: int, tag: int) -> dict:
    thinker = new_thinker(level, "ceiling", sector=sector_index,
                          type=CEIL_LOWER_TO_FLOOR, dir=0, crush=0, tag=tag,
                          oldDir=0, topHeight=0, bottomHeight=0, speed=CEILSPEED)
    level.sectors[sector_index]["specialdata"] = thinker
    return thinker


def activate_in_stasis_ceilings(level: SpecLevel, tag: int) -> bool:
    rtn = False
    for thinker in level.thinkers:
        if (thinker["kind"] == "ceiling" and thinker["tag"] == tag and
                thinker["dir"] == 0):
            thinker["dir"] = thinker["oldDir"]
            rtn = True
    return rtn


def ceiling_crush_stop(level: SpecLevel, line_index: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    for thinker in level.thinkers:
        if (thinker["kind"] == "ceiling" and thinker["tag"] == tag and
                thinker["dir"] != 0):
            thinker["oldDir"] = thinker["dir"]
            thinker["dir"] = 0
            rtn = True
    return rtn


def do_ceiling(level: SpecLevel, line_index: int, ceiling_type: int) -> bool:
    tag = _line(level, line_index)["tag"]
    if ceiling_type in (CEIL_FAST_CRUSH, CEIL_SILENT_CRUSH, CEIL_CRUSH_AND_RAISE):
        activate_in_stasis_ceilings(level, tag)
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        sector = level.sectors[secnum]
        if sector["specialdata"] is not None:
            continue
        ceiling = new_ceiling(level, secnum, tag)
        rtn = True
        ceiling["crush"] = 0
        if ceiling_type == CEIL_FAST_CRUSH:
            ceiling["crush"] = 1
            ceiling["topHeight"] = sector["ceiling"]
            ceiling["bottomHeight"] = sector["floor"] + 8 * FRAC
            ceiling["dir"] = -1
            ceiling["speed"] = CEILSPEED * 2
        elif ceiling_type in (CEIL_SILENT_CRUSH, CEIL_CRUSH_AND_RAISE):
            ceiling["crush"] = 1
            ceiling["topHeight"] = sector["ceiling"]
            ceiling["bottomHeight"] = sector["floor"] + 8 * FRAC
            ceiling["dir"] = -1
            ceiling["speed"] = CEILSPEED
        elif ceiling_type == CEIL_LOWER_AND_CRUSH:
            ceiling["bottomHeight"] = sector["floor"] + 8 * FRAC
            ceiling["dir"] = -1
            ceiling["speed"] = CEILSPEED
        elif ceiling_type == CEIL_LOWER_TO_FLOOR:
            ceiling["bottomHeight"] = sector["floor"]
            ceiling["dir"] = -1
            ceiling["speed"] = CEILSPEED
        elif ceiling_type == CEIL_RAISE_TO_HIGHEST:
            ceiling["topHeight"] = find_highest_ceiling_surrounding(level, secnum)
            ceiling["dir"] = 1
            ceiling["speed"] = CEILSPEED
        ceiling["tag"] = sector["tag"]
        ceiling["type"] = ceiling_type
    return rtn


# --- The light EV helpers ---------------------------------------------------

def start_light_strobing(level: SpecLevel, line_index: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        if level.sectors[secnum]["specialdata"] is not None:
            continue
        if spawn_strobe_flash(level, secnum, SLOWDARK, False):
            rtn = True
    return rtn


def light_turn_on(level: SpecLevel, line_index: int, bright: int) -> bool:
    tag = _line(level, line_index)["tag"]
    for index, sector in enumerate(level.sectors):
        if sector["tag"] != tag:
            continue
        if bright == 0:
            for other in _neighbours(level, index):
                if level.sectors[other]["light"] > bright:
                    bright = level.sectors[other]["light"]
        sector["light"] = bright
    return True


def turn_tag_lights_off(level: SpecLevel, line_index: int) -> bool:
    tag = _line(level, line_index)["tag"]
    for index, sector in enumerate(level.sectors):
        if sector["tag"] != tag:
            continue
        minimum = sector["light"]
        for other in _neighbours(level, index):
            if level.sectors[other]["light"] < minimum:
                minimum = level.sectors[other]["light"]
        sector["light"] = minimum
    return True


def do_donut(level: SpecLevel, line_index: int) -> bool:
    tag = _line(level, line_index)["tag"]
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        s1 = secnum
        if level.sectors[s1]["specialdata"] is not None:
            continue
        s2 = None
        for other in _neighbours(level, s1):
            if other != s1:
                s2 = other
                break
        if s2 is None:
            continue
        s3 = None
        for other in _neighbours(level, s2):
            if other not in (s1, s2):
                s3 = other
                break
        if s3 is None:
            continue
        rtn = True
        up = new_floor(level, s2, FLOOR_DONUT_RAISE)
        up["dest"] = level.sectors[s3]["floor"]
        up["dir"] = 1
        up["speed"] = FLOORSPEED // 2
        up["texture"] = level.sectors[s3]["floorpic"]
        down = new_floor(level, s1, FLOOR_LOWER)
        down["dest"] = level.sectors[s3]["floor"]
        down["dir"] = -1
        down["speed"] = FLOORSPEED // 2
    return rtn


# --- EV_BuildStairs and EV_Teleport (p_floor.c / p_telept.c) ----------------

def do_stairs(level: SpecLevel, line_index: int, stair_type: int) -> bool:
    """EV_BuildStairs(): the chain walk, upstream's order and arithmetic.

    `height += stairsize` sits before the "already moving" test because that is
    where upstream put it: a neighbour that is skipped still raises the height
    the next accepted neighbour is given. `type` stays FLOOR_LOWER (0), which is
    what the Z_Malloc'd floormove_t holds upstream because EV_BuildStairs never
    assigns it.
    """
    if stair_type == STAIR_BUILD8:
        speed = FLOORSPEED // 4
        stairsize = (8 + STAIR_PERTURB["stairs"]) * FRACUNIT
    elif stair_type == STAIR_TURBO16:
        speed = FLOORSPEED * 4
        stairsize = (16 + STAIR_PERTURB["stairs"]) * FRACUNIT
    else:
        return False

    tag = _line(level, line_index)["tag"]
    rtn = False
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            break
        sec = level.sectors[secnum]
        if sec["specialdata"] is not None:
            continue
        rtn = True
        height = sec["floor"] + stairsize
        texture = sec["floorpic"]
        floor = new_floor(level, secnum, FLOOR_LOWER)
        floor["dir"] = 1
        floor["speed"] = speed
        floor["dest"] = height
        current = secnum
        while True:
            found = None
            for line in level.bsp.linedefs:
                if not (line["flags"] & 4):
                    continue
                if _front_sector(level, line) != current:
                    continue
                back = _back_sector(level, line)
                if back is None:
                    continue
                if level.sectors[back]["floorpic"] != texture:
                    continue
                height += stairsize
                if level.sectors[back]["specialdata"] is not None:
                    continue
                found = back
                break
            if found is None:
                break
            current = found
            floor = new_floor(level, current, FLOOR_LOWER)
            floor["dir"] = 1
            floor["speed"] = speed
            floor["dest"] = height
    return rtn


#: P_CrossSpecialLine's missile list (`switch(thing->type)`).
CROSS_MISSILE_TYPES = (32, 33, 34, 35, 30, 31)


def spec_teleportmen(level: SpecLevel) -> list[dict]:
    """The MT_TELEPORTMAN mobjs, in THINGS (= spawn = thinker) order.

    p_spawn_specials never creates one: P_LoadThings does, one per DoomEd-14
    record, and the search in EV_Teleport walks that list. The sector is the one
    R_PointInSubsector answers for the record's own position, not the record's
    `sector` field, exactly as the C reads `m->subsector->sector`.
    """
    if getattr(level, "teleportmen", None) is None:
        men = []
        for record in level.bsp.things:
            if record["type"] != 14:
                continue
            x = _fixed16(record["x"])
            y = _fixed16(record["y"])
            subsector = point_in_subsector(level.bsp, x, y)
            men.append({
                "x": x, "y": y,
                "angle": u32(ANG45 * (record["angle"] // 45)),
                "sector": sector_of_subsector(level.bsp, subsector),
                "type": 14,
            })
        level.teleportmen = men
    return level.teleportmen


def teleport_move(level: SpecLevel, mo: dict, x: int, y: int) -> bool:
    """P_TeleportMove(): PIT_StompThing, then the write, no line checks."""
    subsector = point_in_subsector(level.bsp, x, y)
    sector = sector_of_subsector(level.bsp, subsector)

    for other in getattr(level, "mobjs", []):
        if not (other["flags"] & MF_SHOOTABLE):
            continue
        blockdist = other["radius"] + mo["radius"]
        if abs(other["x"] - x) >= blockdist or abs(other["y"] - y) >= blockdist:
            continue
        if other is mo:
            continue
        # `if ( !tmthing->player && gamemap != 30) return false;` -- DOOM1's
        # episode 1 has no gamemap 30.
        if not mo["player"]:
            return False
        damage_damage(level, other, mo, mo, 10000)

    mo["floorz"] = level.sectors[sector]["floor"]
    mo["ceilingz"] = level.sectors[sector]["ceiling"]
    mo["subsector"] = subsector
    mo["sector"] = sector
    mo["x"] = x
    mo["y"] = y
    return True


def do_teleport(level: SpecLevel, line_index: int, mo, side: int) -> bool:
    """EV_Teleport(): the tagged sector's teleportman, the fogs, the write."""
    if mo is None:
        return False
    if mo["flags"] & MF_MISSILE:
        return False
    if side == 1:
        return False

    tag = _line(level, line_index)["tag"]
    secnum = -1
    while True:
        secnum = find_sector_from_tag(level, tag, secnum)
        if secnum < 0:
            return False
        for man in spec_teleportmen(level):
            if man["sector"] != secnum:
                continue
            oldx, oldy, oldz = mo["x"], mo["y"], mo["z"]
            if not teleport_move(level, mo, man["x"], man["y"]):
                return False
            mo["z"] = mo["floorz"]
            if mo["player"] and getattr(level, "player_record", None) is not None:
                level.player_record["viewz"] = mo["z"] + level.player_record["viewheight"]
            an = man["angle"] >> ANGLETOFINESHIFT
            distance = TELEPORT_FOG_DISTANCE + STAIR_PERTURB["teleport"]
            fogs = [
                (oldx, oldy, oldz),
                (man["x"] + distance * FINE_SINE[an + FINEANGLES // 4],
                 man["y"] + distance * FINE_SINE[an],
                 mo["z"]),
            ]
            level.fogs.extend(fogs)
            if mo["player"]:
                mo["reactiontime"] = TELEPORT_REACTIONTIME
            mo["angle"] = man["angle"]
            mo["momx"] = mo["momy"] = mo["momz"] = 0
            return True
    return False


# --- P_SpawnSpecials --------------------------------------------------------

def spawn_specials(level: SpecLevel) -> None:
    for index, sector in enumerate(level.sectors):
        special = sector["special"]
        if not special:
            continue
        if special == 1:
            if spawn_light_flash(level, index):
                level.stats["spawned"] += 1
        elif special == 2:
            if spawn_strobe_flash(level, index, FASTDARK, False):
                level.stats["spawned"] += 1
        elif special == 3:
            if spawn_strobe_flash(level, index, SLOWDARK, False):
                level.stats["spawned"] += 1
        elif special == 4:
            if spawn_strobe_flash(level, index, FASTDARK, False):
                level.stats["spawned"] += 1
            level.sectors[index]["special"] = 4
        elif special == 8:
            if spawn_glowing_light(level, index):
                level.stats["spawned"] += 1
        elif special == 9:
            level.stats["secrets"] += 1
            level.total_secrets += 1
        elif special == 10:
            if spawn_door_close_in_30(level, index):
                level.stats["spawned"] += 1
        elif special == 12:
            if spawn_strobe_flash(level, index, SLOWDARK, True):
                level.stats["spawned"] += 1
        elif special == 13:
            if spawn_strobe_flash(level, index, FASTDARK, True):
                level.stats["spawned"] += 1
        elif special == 14:
            if spawn_door_raise_in_5_mins(level, index):
                level.stats["spawned"] += 1
        elif special == 17:
            if spawn_fire_flicker(level, index):
                level.stats["spawned"] += 1
        elif special in (5, 7, 11, 16):
            # P_PlayerInSpecialSector's damage floors: they spawn nothing and
            # keep their number, because the player effect reads it every tic.
            pass
        else:
            level.stats["unsupportedSector"] += 1

    for special in level.line_special:
        if special == 48:
            level.stats["lineScrolls"] += 1


# --- Ticking ----------------------------------------------------------------

def tick_door(level: SpecLevel, door: dict) -> None:
    sector = level.sectors[door["sector"]]
    if door["dir"] == 0:
        door["count"] -= 1
        if door["count"] != 0:
            return
        if door["type"] in (DOOR_BLAZE_RAISE, DOOR_NORMAL):
            door["dir"] = -1
        elif door["type"] == DOOR_CLOSE30:
            door["dir"] = 1
    elif door["dir"] == 2:
        door["count"] -= 1
        if door["count"] != 0:
            return
        if door["type"] == DOOR_RAISE5:
            door["dir"] = 1
            door["type"] = DOOR_NORMAL
    elif door["dir"] == -1:
        result = move_plane(level, door["sector"], door["speed"], sector["floor"],
                            False, 1, -1)
        if result == "pastdest":
            if door["type"] in (DOOR_BLAZE_RAISE, DOOR_BLAZE_CLOSE,
                                DOOR_NORMAL, DOOR_CLOSE):
                finish(level, door)
            elif door["type"] == DOOR_CLOSE30:
                door["dir"] = 0
                door["count"] = 35 * 30
        elif result == "crushed":
            if door["type"] not in (DOOR_BLAZE_CLOSE, DOOR_CLOSE):
                door["dir"] = 1
    elif door["dir"] == 1:
        result = move_plane(level, door["sector"], door["speed"],
                            door["topHeight"], False, 1, 1)
        if result == "pastdest":
            if door["type"] in (DOOR_BLAZE_RAISE, DOOR_NORMAL):
                door["dir"] = 0
                door["count"] = door["topWait"]
            elif door["type"] in (DOOR_CLOSE30, DOOR_BLAZE_OPEN, DOOR_OPEN):
                finish(level, door)


def tick_floor(level: SpecLevel, floor: dict) -> None:
    sector = level.sectors[floor["sector"]]
    result = move_plane(level, floor["sector"], floor["speed"], floor["dest"],
                        floor["crush"], 0, floor["dir"])
    if result != "pastdest":
        return
    sector["specialdata"] = None
    if floor["dir"] == 1 and floor["type"] == FLOOR_DONUT_RAISE:
        sector["special"] = floor["newspecial"]
        sector["floorpic"] = floor["texture"]
    elif floor["dir"] == -1 and floor["type"] == FLOOR_LOWER_AND_CHANGE:
        sector["special"] = floor["newspecial"]
        sector["floorpic"] = floor["texture"]
    floor["removed"] = True


def tick_plat(level: SpecLevel, plat: dict) -> None:
    sector = level.sectors[plat["sector"]]
    if plat["status"] == PLAT_UP:
        result = move_plane(level, plat["sector"], plat["speed"], plat["high"],
                            bool(plat["crush"]), 0, 1)
        if result == "crushed" and not plat["crush"]:
            plat["count"] = plat["wait"]
            plat["status"] = PLAT_DOWN
        elif result == "pastdest":
            plat["count"] = plat["wait"]
            plat["status"] = PLAT_WAITING
            if plat["type"] in (PLAT_BLAZE_DWUS, PLAT_DWUS,
                                PLAT_RAISE_AND_CHANGE, PLAT_RAISE_NEAREST):
                finish(level, plat)
    elif plat["status"] == PLAT_DOWN:
        result = move_plane(level, plat["sector"], plat["speed"], plat["low"],
                            False, 0, -1)
        if result == "pastdest":
            plat["count"] = plat["wait"]
            plat["status"] = PLAT_WAITING
    elif plat["status"] == PLAT_WAITING:
        plat["count"] -= 1
        if plat["count"] == 0:
            plat["status"] = PLAT_UP if sector["floor"] == plat["low"] else PLAT_DOWN


def tick_ceiling(level: SpecLevel, ceiling: dict) -> None:
    if ceiling["dir"] == 1:
        result = move_plane(level, ceiling["sector"], ceiling["speed"],
                            ceiling["topHeight"], False, 1, 1)
        if result == "pastdest":
            if ceiling["type"] == CEIL_RAISE_TO_HIGHEST:
                finish(level, ceiling)
            elif ceiling["type"] in (CEIL_SILENT_CRUSH, CEIL_FAST_CRUSH,
                                     CEIL_CRUSH_AND_RAISE):
                ceiling["dir"] = -1
    elif ceiling["dir"] == -1:
        result = move_plane(level, ceiling["sector"], ceiling["speed"],
                            ceiling["bottomHeight"], bool(ceiling["crush"]), 1, -1)
        if result == "pastdest":
            if ceiling["type"] in (CEIL_SILENT_CRUSH, CEIL_CRUSH_AND_RAISE,
                                   CEIL_FAST_CRUSH):
                if ceiling["type"] in (CEIL_SILENT_CRUSH, CEIL_CRUSH_AND_RAISE):
                    ceiling["speed"] = CEILSPEED
                ceiling["dir"] = 1
            elif ceiling["type"] in (CEIL_LOWER_AND_CRUSH, CEIL_LOWER_TO_FLOOR):
                finish(level, ceiling)
        elif result == "crushed":
            if ceiling["type"] in (CEIL_SILENT_CRUSH, CEIL_CRUSH_AND_RAISE,
                                   CEIL_LOWER_AND_CRUSH):
                ceiling["speed"] = CEILSPEED // 8


def tick_light(level: SpecLevel, light: dict) -> None:
    sector = level.sectors[light["sector"]]
    if light["type"] == LIGHT_FLASH:
        light["count"] -= 1
        if light["count"] != 0:
            return
        if sector["light"] == light["maxLight"]:
            sector["light"] = light["minLight"]
            light["count"] = (level.rng.next() & light["maxTime"]) + 1
        else:
            sector["light"] = light["maxLight"]
            light["count"] = (level.rng.next() & light["minTime"]) + 1
    elif light["type"] == LIGHT_STROBE:
        light["count"] -= 1
        if light["count"] != 0:
            return
        if sector["light"] == light["minLight"]:
            sector["light"] = light["maxLight"]
            light["count"] = light["brightTime"]
        else:
            sector["light"] = light["minLight"]
            light["count"] = light["darkTime"]
    elif light["type"] == LIGHT_GLOW:
        if light["dir"] == -1:
            sector["light"] -= GLOWSPEED
            if sector["light"] <= light["minLight"]:
                sector["light"] += GLOWSPEED
                light["dir"] = 1
        elif light["dir"] == 1:
            sector["light"] += GLOWSPEED
            if sector["light"] >= light["maxLight"]:
                sector["light"] -= GLOWSPEED
                light["dir"] = -1
    elif light["type"] == LIGHT_FIRE:
        light["count"] -= 1
        if light["count"] != 0:
            return
        amount = (level.rng.next() & 3) * 16
        if sector["light"] - amount < light["minLight"]:
            sector["light"] = light["minLight"]
        else:
            sector["light"] = light["maxLight"] - amount
        light["count"] = 4


def run_tic(level: SpecLevel) -> None:
    index = 0
    while index < len(level.thinkers):
        thinker = level.thinkers[index]
        if thinker["removed"]:
            del level.thinkers[index]
            continue
        if thinker["kind"] == "door":
            tick_door(level, thinker)
        elif thinker["kind"] == "floor":
            tick_floor(level, thinker)
        elif thinker["kind"] == "plat":
            tick_plat(level, thinker)
        elif thinker["kind"] == "ceiling":
            tick_ceiling(level, thinker)
        elif thinker["kind"] == "light":
            tick_light(level, thinker)
        if thinker["removed"]:
            del level.thinkers[index]
            continue
        index += 1


# --- The trigger script -----------------------------------------------------

USE_SPECIALS = {
    1, 7, 9, 11, 14, 15, 18, 20, 21, 23, 26, 27, 28, 29, 31, 32, 33, 34, 41,
    42, 43, 45, 49, 50, 51, 55, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
    99, 101, 102, 103, 111, 112, 113, 114, 115, 116, 117, 118, 122, 123, 127,
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140,
}

MANUAL_DOORS = {1, 26, 27, 28, 31, 32, 33, 34, 117, 118}

# linuxdoom-1.10's three trigger switches, copied whole. A number outside the
# table for a kind is a no-op upstream -- P_CrossSpecialLine falls out of its
# switch, P_UseSpecialLine returns true without acting, P_ShootSpecialLine
# returns -- so a port that does nothing is faithful. Only a number *inside* a
# table that the port cannot execute is an unsupported special.
CROSS_SPECIALS = {
    2, 3, 4, 5, 6, 8, 10, 12, 13, 16, 17, 19, 22, 25, 30, 35, 36, 37, 38, 39,
    40, 44, 52, 53, 54, 56, 57, 58, 59, 72, 73, 74, 75, 76, 77, 79, 80, 81,
    82, 83, 84, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 100, 104,
    105, 106, 107, 108, 109, 110, 119, 120, 121, 124, 125, 126, 128, 129, 130,
    141,
}
SHOOT_SPECIALS = {24, 46, 47}
UPSTREAM_SPECIALS = {
    "use": USE_SPECIALS,
    "cross": CROSS_SPECIALS,
    "shoot": SHOOT_SPECIALS,
}


def trigger_kind_for(special: int) -> str:
    return "use" if special in USE_SPECIALS else "cross"


def upstream_handles(kind: str, special: int) -> bool:
    """Does linuxdoom-1.10's trigger function act on this number at all?"""
    return special in UPSTREAM_SPECIALS[kind]


def _trigger_special_line(level: SpecLevel, line_index: int, kind: str,
                          mo, side: int) -> int:
    special = level.line_special[line_index]
    if special == 0:
        return 0
    by_player = bool(mo is not None and mo.get("player"))
    if kind == "use":
        # `if (side) { switch(line->special) { case 124: break; default: return
        # false; } }` -- 124 is the sliding door upstream compiled out, so it
        # reaches the switch and does nothing.
        if side != 0 and special != 124:
            return 0
        if not by_player and special not in (1, 32, 33, 34):
            level.stats["refused"] += 1
            return 0
        if special in MANUAL_DOORS:
            return 1 if vertical_door(level, line_index, by_player) else 0
        if special in (11, 51):
            # P_ChangeSwitchTexture(line, 0) clears the line and starts the
            # switch sound, then G_ExitLevel()/G_SecretExitLevel() set
            # `secretexit` and `gameaction = ga_completed`. The texture half is
            # not modelled: the container holds no SW2 partner (D4.5).
            level.line_special[line_index] = 0
            level.exit_request = "normal" if special == 11 else "secret"
            return 1
        # `if (EV_BuildStairs(line, type)) P_ChangeSwitchTexture(line, 0);` --
        # the one switch in P_UseSpecialLine whose clear the EV result gates.
        if special in (7, 127):
            if do_stairs(level, line_index,
                         STAIR_BUILD8 if special == 7 else STAIR_TURBO16):
                level.stats["deferredLine"] += 1
                level.line_special[line_index] = 0
            return 1
        use = {
            9: ("donut",), 14: ("plat", PLAT_RAISE_AND_CHANGE, 32),
            15: ("plat", PLAT_RAISE_AND_CHANGE, 24),
            18: ("floor", FLOOR_RAISE_TO_NEAREST),
            20: ("plat", PLAT_RAISE_NEAREST, 0),
            21: ("plat", PLAT_DWUS, 0),
            23: ("floor", FLOOR_LOWER_TO_LOWEST),
            29: ("door", DOOR_NORMAL),
            41: ("ceiling", CEIL_LOWER_TO_FLOOR),
            49: ("ceiling", CEIL_CRUSH_AND_RAISE),
            50: ("door", DOOR_CLOSE),
            55: ("floor", FLOOR_RAISE_CRUSH),
            71: ("floor", FLOOR_TURBO_LOWER),
            101: ("floor", FLOOR_RAISE),
            102: ("floor", FLOOR_LOWER),
            103: ("door", DOOR_OPEN),
            111: ("door", DOOR_BLAZE_RAISE),
            112: ("door", DOOR_BLAZE_OPEN),
            113: ("door", DOOR_BLAZE_CLOSE),
            122: ("plat", PLAT_BLAZE_DWUS, 0),
            131: ("floor", FLOOR_RAISE_TURBO),
            133: ("door", DOOR_BLAZE_OPEN),
            135: ("door", DOOR_BLAZE_OPEN),
            137: ("door", DOOR_BLAZE_OPEN),
            140: ("floor", FLOOR_RAISE_512),
        }
        button = {
            42: ("door", DOOR_CLOSE), 43: ("ceiling", CEIL_LOWER_TO_FLOOR),
            45: ("floor", FLOOR_LOWER),
            60: ("floor", FLOOR_LOWER_TO_LOWEST), 61: ("door", DOOR_OPEN),
            62: ("plat", PLAT_DWUS, 1), 63: ("door", DOOR_NORMAL),
            64: ("floor", FLOOR_RAISE), 65: ("floor", FLOOR_RAISE_CRUSH),
            66: ("plat", PLAT_RAISE_AND_CHANGE, 24),
            67: ("plat", PLAT_RAISE_AND_CHANGE, 32),
            68: ("plat", PLAT_RAISE_NEAREST, 0),
            69: ("floor", FLOOR_RAISE_TO_NEAREST),
            70: ("floor", FLOOR_TURBO_LOWER),
            99: ("door", DOOR_BLAZE_OPEN), 114: ("door", DOOR_BLAZE_RAISE),
            115: ("door", DOOR_BLAZE_OPEN), 116: ("door", DOOR_BLAZE_CLOSE),
            123: ("plat", PLAT_BLAZE_DWUS, 0),
            132: ("floor", FLOOR_RAISE_TURBO),
            134: ("door", DOOR_BLAZE_OPEN), 136: ("door", DOOR_BLAZE_OPEN),
            138: ("light_on", 255), 139: ("light_on", 35),
        }
        if special in use:
            effect = use[special]
            if effect[0] == "donut":
                do_donut(level, line_index)
            else:
                run_effect(level, line_index, effect)
            level.stats["deferredLine"] += 1
            level.line_special[line_index] = 0
            return 1
        if special in button:
            run_effect(level, line_index, button[special])
            level.stats["deferredLine"] += 1
            return 1
        if upstream_handles("use", special):
            level.stats["unsupportedLine"] += 1
        return 0

    # CROSS
    if not by_player:
        missile = mo is not None and mo.get("type") in CROSS_MISSILE_TYPES
        if missile or special not in (4, 10, 39, 88, 97, 125, 126):
            level.stats["refused"] += 1
            return 0
    # EXIT! / SECRET EXIT: G_ExitLevel()/G_SecretExitLevel() and, unlike the
    # one-shot cases below, no `line->special = 0` in P_CrossSpecialLine.
    if special == 52:
        level.exit_request = "normal"
        return 1
    if special == 124:
        level.exit_request = "secret"
        return 1
    # One-shot stairs: EV_BuildStairs' answer is ignored and the line is
    # cleared either way. 39/125 are the same shape for EV_Teleport.
    if special in (8, 100):
        do_stairs(level, line_index,
                  STAIR_BUILD8 if special == 8 else STAIR_TURBO16)
        level.line_special[line_index] = 0
        return 1
    if special == 39:
        do_teleport(level, line_index, mo, side)
        level.line_special[line_index] = 0
        return 1
    if special == 125:
        # `if (!thing->player) { EV_Teleport(...); line->special = 0; }`
        if by_player:
            return 0
        do_teleport(level, line_index, mo, side)
        level.line_special[line_index] = 0
        return 1
    if special == 97:
        do_teleport(level, line_index, mo, side)
        return 1
    if special == 126:
        if by_player:
            return 0
        do_teleport(level, line_index, mo, side)
        return 1
    one_shot = {
        2: ("door", DOOR_OPEN), 3: ("door", DOOR_CLOSE),
        4: ("door", DOOR_NORMAL), 5: ("floor", FLOOR_RAISE),
        6: ("ceiling", CEIL_FAST_CRUSH), 10: ("plat", PLAT_DWUS, 0),
        12: ("light_on", 0), 13: ("light_on", 255),
        16: ("door", DOOR_CLOSE30), 17: ("strobe",),
        19: ("floor", FLOOR_LOWER), 22: ("plat", PLAT_RAISE_NEAREST, 0),
        25: ("ceiling", CEIL_CRUSH_AND_RAISE),
        30: ("floor", FLOOR_RAISE_TO_TEXTURE), 35: ("light_on", 35),
        36: ("floor", FLOOR_TURBO_LOWER),
        37: ("floor", FLOOR_LOWER_AND_CHANGE),
        38: ("floor", FLOOR_LOWER_TO_LOWEST), 40: ("both40",),
        44: ("ceiling", CEIL_LOWER_AND_CRUSH),
        53: ("plat", PLAT_PERPETUAL, 0), 54: ("stop_plat",),
        56: ("floor", FLOOR_RAISE_CRUSH), 57: ("ceiling_stop",),
        58: ("floor", FLOOR_RAISE_24),
        59: ("floor", FLOOR_RAISE_24_AND_CHANGE), 104: ("lights_off",),
        108: ("door", DOOR_BLAZE_RAISE), 109: ("door", DOOR_BLAZE_OPEN),
        110: ("door", DOOR_BLAZE_CLOSE),
        119: ("floor", FLOOR_RAISE_TO_NEAREST), 121: ("plat", PLAT_BLAZE_DWUS, 0),
        130: ("floor", FLOOR_RAISE_TURBO),
        141: ("ceiling", CEIL_SILENT_CRUSH),
    }
    retrigger = {
        72: ("ceiling", CEIL_LOWER_AND_CRUSH),
        73: ("ceiling", CEIL_CRUSH_AND_RAISE), 74: ("ceiling_stop",),
        75: ("door", DOOR_CLOSE), 76: ("door", DOOR_CLOSE30),
        77: ("ceiling", CEIL_FAST_CRUSH), 79: ("light_on", 35),
        80: ("light_on", 0), 81: ("light_on", 255),
        82: ("floor", FLOOR_LOWER_TO_LOWEST), 83: ("floor", FLOOR_LOWER),
        84: ("floor", FLOOR_LOWER_AND_CHANGE), 86: ("door", DOOR_OPEN),
        87: ("plat", PLAT_PERPETUAL, 0), 88: ("plat", PLAT_DWUS, 0),
        89: ("stop_plat",), 90: ("door", DOOR_NORMAL),
        91: ("floor", FLOOR_RAISE), 92: ("floor", FLOOR_RAISE_24),
        93: ("floor", FLOOR_RAISE_24_AND_CHANGE),
        94: ("floor", FLOOR_RAISE_CRUSH),
        95: ("plat", PLAT_RAISE_NEAREST, 0),
        96: ("floor", FLOOR_RAISE_TO_TEXTURE),
        98: ("floor", FLOOR_TURBO_LOWER),
        105: ("door", DOOR_BLAZE_RAISE), 106: ("door", DOOR_BLAZE_OPEN),
        107: ("door", DOOR_BLAZE_CLOSE), 120: ("plat", PLAT_BLAZE_DWUS, 0),
        128: ("floor", FLOOR_RAISE_TO_NEAREST),
        129: ("floor", FLOOR_RAISE_TURBO),
    }
    if special in one_shot:
        run_effect(level, line_index, one_shot[special])
        level.line_special[line_index] = 0
        return 1
    if special in retrigger:
        run_effect(level, line_index, retrigger[special])
        return 1
    if upstream_handles("cross", special):
        level.stats["unsupportedLine"] += 1
    return 0


def trigger_special_line(level: SpecLevel, line_index: int, kind: str,
                         mo, side: int = 0) -> int:
    """The trigger entry point: runs the special and counts a success.

    ``mo`` is upstream's actor (None is the gate's "no actor"), ``side`` is
    P_PointOnLineSide() of the position the thing came from for a crossing and
    the user's side for a use.
    """
    result = _trigger_special_line(level, line_index, kind, mo, side)
    if result:
        level.stats["triggers"] += 1
    return result


def run_effect(level: SpecLevel, line_index: int, effect: tuple) -> None:
    name = effect[0]
    if name == "door":
        do_door(level, line_index, effect[1])
    elif name == "floor":
        do_floor(level, line_index, effect[1])
    elif name == "plat":
        do_plat(level, line_index, effect[1], effect[2] if len(effect) > 2 else 0)
    elif name == "ceiling":
        do_ceiling(level, line_index, effect[1])
    elif name == "light_on":
        light_turn_on(level, line_index, effect[1])
    elif name == "strobe":
        start_light_strobing(level, line_index)
    elif name == "stop_plat":
        stop_plat(level, line_index)
    elif name == "ceiling_stop":
        ceiling_crush_stop(level, line_index)
    elif name == "lights_off":
        turn_tag_lights_off(level, line_index)
    elif name == "both40":
        do_ceiling(level, line_index, CEIL_RAISE_TO_HIGHEST)
        do_floor(level, line_index, FLOOR_LOWER_TO_LOWEST)


# --- The dump ---------------------------------------------------------------

def print_thinker(tic: int, slot: int, thinker: dict) -> None:
    kind = thinker["kind"]
    if kind == "door":
        type_, direction, speed = thinker["type"], thinker["dir"], thinker["speed"]
        count, a, b, c = thinker["count"], thinker["topHeight"], thinker["topWait"], 0
    elif kind == "floor":
        type_, direction, speed = thinker["type"], thinker["dir"], thinker["speed"]
        count, a, b, c = 0, thinker["dest"], thinker["crush"], thinker["newspecial"]
    elif kind == "plat":
        type_, direction, speed = thinker["type"], thinker["status"], thinker["speed"]
        count, a, b, c = thinker["count"], thinker["high"], thinker["low"], thinker["wait"]
    elif kind == "ceiling":
        type_, direction, speed = thinker["type"], thinker["dir"], thinker["speed"]
        count, a, b, c = 0, thinker["topHeight"], thinker["bottomHeight"], thinker["tag"]
    else:
        type_, direction = thinker["type"], thinker["dir"]
        speed, count = 0, thinker["count"]
        a, b = thinker["maxLight"], thinker["minLight"]
        c = thinker["darkTime"] if thinker["type"] == LIGHT_STROBE else thinker["maxTime"]
    print(f"THINKER {tic} {slot} kind={kind} sector={thinker['sector']} "
          f"type={type_} dir={direction} speed={speed} count={count} "
          f"a={a} b={b} c={c}")


def dump_specials_tic(level: SpecLevel, tic: int) -> None:
    for index, sector in enumerate(level.sectors):
        print(f"SECTOR {tic} {index} floor={sector['floor']} "
              f"ceil={sector['ceiling']} light={sector['light']}")
    for slot, thinker in enumerate(level.thinkers):
        print_thinker(tic, slot, thinker)


def report_specials(lumps: list[Lump], map_name: str, tics: int) -> None:
    """The `--specials` protocol: P_SpawnSpecials and the E1M1 trigger script."""
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)         # the level's default skill
    level = SpecLevel(bsp, RandomState(things.rng))

    print(f"SPECIALS {map_name} tics={tics} sectors={len(bsp.sectors)} "
          f"lines={len(bsp.linedefs)}")

    spawn_specials(level)
    actor = movement_player(level, use_start_angle=True)
    level.mobjs = [actor]

    # Tic 0: every special line, ascending, as the C harness documents.
    for line_index, special in enumerate(level.line_special):
        if special == 0:
            continue
        kind = trigger_kind_for(special)
        result = trigger_special_line(level, line_index, kind, actor)
        print(f"TRIGGER 0 line={line_index} special={special} kind={kind} "
              f"result={result}")

    dump_specials_tic(level, 0)
    for tic in range(1, tics + 1):
        run_tic(level)
        dump_specials_tic(level, tic)

    stats = level.stats
    print(f"SUMMARY spawned={stats['spawned']} secrets={stats['secrets']} "
          f"totalSecrets={level.total_secrets} "
          f"changeSector={stats['changeSector']} "
          f"unsupportedSector={stats['unsupportedSector']} "
          f"unsupportedLine={stats['unsupportedLine']} "
          f"deferredLine={stats['deferredLine']} "
          f"lineScrolls={stats['lineScrolls']} triggers={stats['triggers']} "
          f"refused={stats['refused']}")

    # The protocol continues with P_PlayerInSpecialSector's fixtures, which the
    # C harness prints after its SUMMARY: they are part of the same report.
    report_sector_damage(lumps, map_name)


# --- P_UpdateSpecials: scroll and animation (T35) ---------------------------
#
# linuxdoom-1.10's animdefs[], verbatim and in its order: (isTexture, endname,
# startname, speed). P_InitPicAnims() skips an entry whose start name the WAD
# does not carry, numbers a family by lump index (`numpics = end - start + 1`)
# and P_UpdateSpecials() selects
#
#     pic = basepic + ((leveltime / speed + i) % numpics)
#
# for every index i in the family. The port carries names, not numbers, and the
# same rule applies to a name. Note that `i` is the absolute index, so a
# family's *base* is part of the phase: NUKAGE1 is flat 51 (51 % 3 == 0, the
# frame is `(leveltime/8) % 3`) and SLADRIP1 is texture 61 (61 % 3 == 1, one
# frame further on). This is why the frame is not simply
# `(leveltime/speed) % frames` for every family.
ANIMDEFS = (
    (False, "NUKAGE3", "NUKAGE1", 8),
    (False, "FWATER4", "FWATER1", 8),
    (False, "SWATER4", "SWATER1", 8),
    (False, "LAVA4", "LAVA1", 8),
    (False, "BLOOD3", "BLOOD1", 8),
    (False, "RROCK08", "RROCK05", 8),
    (False, "SLIME04", "SLIME01", 8),
    (False, "SLIME08", "SLIME05", 8),
    (False, "SLIME12", "SLIME09", 8),
    (True, "BLODGR4", "BLODGR1", 8),
    (True, "SLADRIP3", "SLADRIP1", 8),
    (True, "BLODRIP4", "BLODRIP1", 8),
    (True, "FIREWALL", "FIREWALA", 8),
    (True, "GSTFONT3", "GSTFONT1", 8),
    (True, "FIRELAVA", "FIRELAV3", 8),
    (True, "FIREMAG3", "FIREMAG1", 8),
    (True, "FIREBLU2", "FIREBLU1", 8),
    (True, "ROCKRED3", "ROCKRED1", 8),
    (True, "BFALL4", "BFALL1", 8),
    (True, "SFALL4", "SFALL1", 8),
    (True, "WFALL4", "WFALL1", 8),
    (True, "DBRAIN4", "DBRAIN1", 8),
)

#: The scroll step: upstream's `textureoffset += FRACUNIT`.
SCROLL_STEP = FRACUNIT


@dataclass(frozen=True)
class AnimFamily:
    """One active animdefs[] entry, measured from the WAD it was built on."""

    is_texture: bool
    frames: tuple[str, ...]
    speed: int
    base: int


def flat_order(lumps: list[Lump]) -> list[str]:
    """The flat names in R_FlatNumForName's order: between F_START and F_END."""
    start = first_index(lumps, "F_START")
    end = first_index(lumps, "F_END")
    if start < 0 or end < 0 or end <= start:
        raise FormatError("the WAD has no F_START..F_END range")
    return [lump.name.upper() for lump in lumps[start + 1:end]]


def texture_order(lumps: list[Lump]) -> list[str]:
    """The texture names in R_TextureNumForName's order: TEXTURE1 then TEXTURE2."""
    names: list[str] = []
    for lump_name in ("TEXTURE1", "TEXTURE2"):
        index = first_index(lumps, lump_name)
        if index < 0:
            continue
        for texture in parse_textures(lump_name, lumps[index].data):
            names.append(texture.name.upper())
    return names


def animation_families(lumps: list[Lump],
                       perturb: str = "none") -> list[AnimFamily]:
    """The animdefs[] entries this WAD can animate, with their measured range.

    `perturb` moves one thing in *this* implementation only, so the gate can
    show its comparison fails: "speed" halves every speed and "phase" drops the
    base index from the frame arithmetic.
    """
    flats = flat_order(lumps)
    textures = texture_order(lumps)
    flat_index: dict[str, int] = {}
    for at, name in enumerate(flats):
        flat_index.setdefault(name, at)
    texture_index: dict[str, int] = {}
    for at, name in enumerate(textures):
        texture_index.setdefault(name, at)

    families: list[AnimFamily] = []
    for is_texture, end_name, start_name, speed in ANIMDEFS:
        table = texture_index if is_texture else flat_index
        source = textures if is_texture else flats
        if start_name not in table or end_name not in table:
            continue  # P_InitPicAnims' `if (... == -1) continue;`
        base = table[start_name]
        top = table[end_name]
        if top < base:
            raise FormatError(f"animation {start_name}..{end_name} runs backwards")
        if perturb == "speed":
            speed = max(1, speed // 2)
        # The perturbed phase moves only the frame index, never the family's
        # name range: `base` selects frames, `phase` only shifts the cycle.
        phase = 0 if perturb == "phase" else base
        families.append(AnimFamily(is_texture, tuple(source[base:top + 1]),
                                   speed, phase))
    return families


def animated(name: str, families: list[AnimFamily]) -> bool:
    return any(name in family.frames for family in families)


def name_text(raw) -> str:
    """A WAD name field as text: the eight bytes up to the first NUL."""
    if isinstance(raw, str):
        return raw
    return raw.rstrip(b"\0").decode("latin-1")


def anim_frame(name: str, anim_time: int, families: list[AnimFamily]) -> str:
    """`texturetranslation[]`/`flattranslation[]` for one name, as a string."""
    if anim_time < 0:
        return name
    for family in families:
        if name not in family.frames:
            continue
        member = family.frames.index(name)
        selected = (anim_time // family.speed + family.base + member) \
            % len(family.frames)
        return family.frames[selected]
    return name


def report_scrolls(lumps: list[Lump], map_name: str, tics: int,
                   perturb: str = "none") -> None:
    """The `--scrolls` protocol: P_UpdateSpecials on one map.

    The C harness prints the same records; see tools/specials_check.c for the
    documented script. `--scrolls-perturb` moves one constant here only.
    """
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)         # the level's default skill
    level = SpecLevel(bsp, RandomState(things.rng))
    families = animation_families(lumps, perturb)
    step = 2 * SCROLL_STEP if perturb == "step" else SCROLL_STEP

    spawn_specials(level)
    level.scrolls = [{"line": index, "side": line["side0"], "dx": 0, "dy": 0}
                     for index, line in enumerate(bsp.linedefs)
                     if line["special"] == 48]
    level.anim_time = -1

    # The animated sidedefs, measured from the immutable records: exactly what
    # the C harness collects before it binds the seams.
    textures: list[tuple[int, int, str]] = []
    for side_index, side in enumerate(bsp.sidedefs):
        for field, key in enumerate(("top", "bottom", "mid")):
            base = name_text(side[key])
            if animated(base, families):
                textures.append((side_index, field, base))

    animated_fields = 0
    for sector in level.sectors:
        if animated(name_text(sector["floorpic"]), families):
            animated_fields += 1
        if animated(name_text(sector["ceilingpic"]), families):
            animated_fields += 1
    animated_fields += len(textures)

    print(f"SCROLLS {map_name} tics={tics} scrolls={len(level.scrolls)}")

    def dump(tic: int) -> None:
        for scroll in level.scrolls:
            offset = bsp.sidedefs[scroll["side"]]["textureoffset"] + scroll["dx"]
            print(f"SCROLL {tic} line={scroll['line']} side={scroll['side']} "
                  f"dx={scroll['dx']} dy={scroll['dy']} offset={offset}")
        for index, sector in enumerate(level.sectors):
            floor_pic = name_text(sector["floorpic"])
            ceiling_pic = name_text(sector["ceilingpic"])
            if animated(floor_pic, families):
                print(f"ANIM {tic} sector={index} field=floor name={floor_pic} "
                      f"frame={anim_frame(floor_pic, level.anim_time, families)}")
            if animated(ceiling_pic, families):
                print(f"ANIM {tic} sector={index} field=ceiling name={ceiling_pic} "
                      f"frame={anim_frame(ceiling_pic, level.anim_time, families)}")
        for side_index, field, base in textures:
            name = ("top", "bottom", "mid")[field]
            print(f"ANIM {tic} side={side_index} field={name} name={base} "
                  f"frame={anim_frame(base, level.anim_time, families)}")

    dump(0)
    for tic in range(1, tics + 1):
        run_tic(level)
        # P_UpdateSpecials: scrolls advance and the picture clock is stamped,
        # both before `leveltime++` (P_Ticker's order).
        for scroll in level.scrolls:
            if level.line_special[scroll["line"]] == 48:
                scroll["dx"] += step
        level.anim_time = level.leveltime
        level.leveltime += 1
        dump(tic)

    print(f"SUMMARY scrolls={len(level.scrolls)} animated={animated_fields}")


# --- Stairs and teleports on a real map (T34) -------------------------------

#: The eight numbers this stage measures. 7/8/100/127 are EV_BuildStairs' two
#: types over the two trigger functions; 39/97/125/126 are EV_Teleport's.
STAIR_SPECIALS = frozenset({7, 8, 100, 127})
TELEPORT_SPECIALS = frozenset({39, 97, 125, 126})
STAIR_TELEPORT_SPECIALS = STAIR_SPECIALS | TELEPORT_SPECIALS


def print_teleport(level: SpecLevel, line_index: int, result: int, before,
                   before_fogs: int) -> None:
    """TELEPORT: the actor's pose on both ends and the destination fog."""
    new = level.fogs[before_fogs:]
    actor = level.mobjs[0] if level.mobjs else None
    if actor is None:
        print(f"TELEPORT {line_index} result={result} from=none to=none "
              f"angle=0 fogs=0 last=none")
        return
    fromtext = ",".join(str(value) for value in before)
    print(f"TELEPORT {line_index} result={result} from={fromtext} "
          f"to={actor['x']},{actor['y']},{actor['z']} angle={actor['angle']} "
          f"fogs={len(new)} "
          f"last={','.join(str(value) for value in new[-1]) if new else 'none'}")


def report_stairs(lumps: list[Lump], map_name: str, line_index: int,
                  special: int, side: int, tics: int) -> None:
    """The `--stairs` protocol: one fixture, one line, compared line for line.

    The header names the fixture; LINE is the trigger's answer; TELEPORT is
    printed for the four teleport numbers and carries the fog positions; the
    SECTOR/THINKER dumps are the stair mover's evidence. `synthetic` says
    whether the number was written into a line the map carries for another
    special, the same disclosure the sector-damage fixtures make.

    The actor is the player. The monster half of EV_Teleport is a unit test:
    a spawned imp runs A_Look, whose `lastlook` draw moves the map's light
    flashes off the line this fixture compares.
    """
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)
    level = SpecLevel(bsp, RandomState(things.rng))
    level.fogs = []
    spawn_specials(level)

    synthetic = int(level.line_special[line_index] != special)
    kind = trigger_kind_for(special)
    level.line_special[line_index] = special
    actor = movement_player(level, use_start_angle=True)
    level.mobjs = [actor]

    print(f"STAIRS {map_name} line={line_index} special={special} kind={kind} "
          f"synthetic={synthetic} side={side} tics={tics}")

    before = (actor["x"], actor["y"], actor["z"], actor["angle"])
    before_fogs = len(level.fogs)
    result = trigger_special_line(level, line_index, kind, actor, side)
    print(f"LINE {line_index} result={result} "
          f"consumed={1 if level.line_special[line_index] == 0 else 0}")

    if special in TELEPORT_SPECIALS:
        print_teleport(level, line_index, result, before, before_fogs)

    dump_specials_tic(level, 0)
    for tic in range(1, tics + 1):
        run_tic(level)
        dump_specials_tic(level, tic)

    floors = sum(1 for thinker in level.thinkers if thinker["kind"] == "floor")
    print(f"SUMMARY floors={floors} fogs={len(level.fogs)} "
          f"triggers={level.stats['triggers']} refused={level.stats['refused']} "
          f"unsupportedLine={level.stats['unsupportedLine']} "
          f"deferredLine={level.stats['deferredLine']}")



# A second implementation of linuxdoom-1.10 p_map.c / p_maputl.c / p_mobj.c's
# movement half: P_CheckPosition, P_TryMove, P_SlideMove, P_HitSlideLine,
# P_LineOpening, P_ThingHeightClip, P_XYMovement and P_ZMovement. It shares this
# file's fixed-point helpers and its map parse and nothing else; it never calls
# the C.
#
# There is no blockmap here either: every linedef and every thing is examined,
# which is what the C harness does and why the two can be compared at all.

MOVEMENT_MAXMOVE = 30 * FRACUNIT
MOVEMENT_STOPSPEED = 0x1000
MOVEMENT_FRICTION = 0xe800
MOVEMENT_GRAVITY = FRACUNIT
MOVEMENT_FLOATSPEED = 4 * FRACUNIT
MOVEMENT_STEPUP = 24 * FRACUNIT
MOVEMENT_MAXSPECIALCROSS = 8
MOVEMENT_MAXRADIUS = 128 * FRACUNIT
MOVEMENT_USERANGE = 64 * FRACUNIT

MOVEMENT_ML_BLOCKING = 1
MOVEMENT_ML_TWOSIDED = 4
MOVEMENT_ML_BLOCKMONSTERS = 16

MOVEMENT_BOXTOP = 0
MOVEMENT_BOXBOTTOM = 1
MOVEMENT_BOXLEFT = 2
MOVEMENT_BOXRIGHT = 3

MOVEMENT_SCRIPT = "walk-slide-corner-friction-return"
MOVEMENT_PLAYER_RADIUS = 16 << FRACBITS
MOVEMENT_PLAYER_HEIGHT = 56 << FRACBITS

# --- The player (D4.2c) -----------------------------------------------------
#
# A second implementation of linuxdoom-1.10 p_user.c: P_PlayerThink,
# P_MovePlayer, P_CalcHeight and P_Thrust, plus the P_UseLines half of p_map.c
# the BT_USE press needs. It reuses this file's movement core and special
# dispatch; it never calls the C.

PLAYER_VIEWHEIGHT = 41 * FRACUNIT          # p_local.h VIEWHEIGHT
PLAYER_MAXBOB = 0x100000                   # p_user.c MAXBOB
PLAYER_MAXHEALTH = 100                     # p_local.h MAXHEALTH
PLAYER_BOB_FINE_STEP = FINEANGLES // 20    # `FINEANGLES/20*leveltime`
PLAYER_SCRIPT = "player-walk-turn-strafe-use"

# d_player.h playerstate_t and doomdef.h buttoncode_t.
PST_LIVE = 0
PST_DEAD = 1
PST_REBORN = 2

BT_ATTACK = 1
BT_USE = 2
BT_CHANGE = 4
BT_WEAPONMASK = 8 + 16 + 32
BT_WEAPONSHIFT = 3
BT_SPECIAL = 128
BT_SPECIALMASK = 3

# The mobj flag word (doomdef.h) and the three mobjtype indices the missile
# species test names. The values are the 1993 ones; the C side reads them from
# info_tables.h, this side from a second reading of info.c (see MOBJINFO).
MF_SPECIAL = 0x00000001
MF_SOLID = 0x00000002
MF_SHOOTABLE = 0x00000004
MF_NOGRAVITY = 0x00000200
MF_DROPOFF = 0x00000400
MF_NOCLIP = 0x00001000
MF_FLOAT = 0x00004000
MF_TELEPORT = 0x00008000
MF_MISSILE = 0x00010000
MF_CORPSE = 0x00100000
MF_INFLOAT = 0x00200000
MF_SKULLFLY = 0x01000000

MT_PLAYER = 0
MT_BRUISER = 15
MT_KNIGHT = 17

# p_mobj.c's missile branch reads `tmthing->info->damage`: a second reading of
# info.c's rows for the four missile types this port can spawn (the imp's
# fireball and the player's three). Movement and the player gate spawn no
# missile, so only the fireball is reached today.
MOVEMENT_MISSILE_DAMAGE = {
    31: 3,      # MT_TROOPSHOT
    33: 20,     # MT_ROCKET
    34: 5,      # MT_PLASMA
    35: 100,    # MT_BFG
}


def movement_abs(value: int) -> int:
    return -value if value < 0 else value


def movement_aprox_distance(dx: int, dy: int) -> int:
    dx = movement_abs(dx)
    dy = movement_abs(dy)
    if dx < dy:
        return dx + dy - (dx >> 1)
    return dx + dy - (dy >> 1)


def movement_line_geometry(bsp: BspMap, line: dict) -> tuple[dict, dict, int, int]:
    v1 = bsp.vertexes[line["v1"]]
    v2 = bsp.vertexes[line["v2"]]
    return v1, v2, v2["x"] - v1["x"], v2["y"] - v1["y"]


def movement_slope_type(dx: int, dy: int) -> str:
    if dx == 0:
        return "vertical"
    if dy == 0:
        return "horizontal"
    return "negative" if (dx < 0) != (dy < 0) else "positive"


def movement_point_on_line_side(x: int, y: int, v1: dict, dx: int, dy: int) -> int:
    if dx == 0:
        if x <= v1["x"]:
            return 1 if dy > 0 else 0
        return 1 if dy < 0 else 0
    if dy == 0:
        if y <= v1["y"]:
            return 1 if dx < 0 else 0
        return 1 if dx > 0 else 0

    px = x - v1["x"]
    py = y - v1["y"]
    left = fixed_mul(dy >> FRACBITS, px)
    right = fixed_mul(py, dx >> FRACBITS)
    return 0 if right < left else 1


def movement_point_on_divline_side(x: int, y: int, lx: int, ly: int,
                                   ldx: int, ldy: int) -> int:
    if ldx == 0:
        if x <= lx:
            return 1 if ldy > 0 else 0
        return 1 if ldy < 0 else 0
    if ldy == 0:
        if y <= ly:
            return 1 if ldx < 0 else 0
        return 1 if ldx > 0 else 0

    dx = i32(x - lx)
    dy = i32(y - ly)
    if (ldy ^ ldx ^ dx ^ dy) & 0x80000000:
        return 1 if (ldy ^ dx) & 0x80000000 else 0

    left = fixed_mul(ldy >> 8, dx >> 8)
    right = fixed_mul(dy >> 8, ldx >> 8)
    return 0 if right < left else 1


def movement_box_on_line_side(box: list[int], v1: dict, dx: int, dy: int) -> int:
    slope = movement_slope_type(dx, dy)
    if slope == "horizontal":
        p1 = 1 if box[MOVEMENT_BOXTOP] > v1["y"] else 0
        p2 = 1 if box[MOVEMENT_BOXBOTTOM] > v1["y"] else 0
        if dx < 0:
            p1 ^= 1
            p2 ^= 1
    elif slope == "vertical":
        p1 = 1 if box[MOVEMENT_BOXRIGHT] < v1["x"] else 0
        p2 = 1 if box[MOVEMENT_BOXLEFT] < v1["x"] else 0
        if dy < 0:
            p1 ^= 1
            p2 ^= 1
    elif slope == "positive":
        p1 = movement_point_on_line_side(box[MOVEMENT_BOXLEFT],
                                         box[MOVEMENT_BOXTOP], v1, dx, dy)
        p2 = movement_point_on_line_side(box[MOVEMENT_BOXRIGHT],
                                         box[MOVEMENT_BOXBOTTOM], v1, dx, dy)
    else:
        p1 = movement_point_on_line_side(box[MOVEMENT_BOXRIGHT],
                                         box[MOVEMENT_BOXTOP], v1, dx, dy)
        p2 = movement_point_on_line_side(box[MOVEMENT_BOXLEFT],
                                         box[MOVEMENT_BOXBOTTOM], v1, dx, dy)
    return p1 if p1 == p2 else -1


def movement_intercept_vector(tx: int, ty: int, tdx: int, tdy: int,
                              lx: int, ly: int, ldx: int, ldy: int) -> int:
    """P_InterceptVector(v2=trace, v1=line): upstream's own expression.

    Upstream is
    ``den = FixedMul(v1->dy>>8, v2->dx) - FixedMul(v1->dx>>8, v2->dy)`` and
    ``num = FixedMul((v1->x - v2->x)>>8, v1->dy)
           + FixedMul((v2->y - v1->y)>>8, v1->dx)``,
    so every direction factor in the numerator is the *line's*. The trace is
    ``tx..tdy`` (v2) and the line is ``lx..ldy`` (v1).
    """
    den = fixed_mul(ldy >> 8, tdx) - fixed_mul(ldx >> 8, tdy)
    if den == 0:
        return 0
    num = fixed_mul((lx - tx) >> 8, ldy) + fixed_mul((ty - ly) >> 8, ldx)
    return fixed_div(num, den)


def movement_line_opening(level: "SpecLevel", index: int) -> dict:
    line = level.bsp.linedefs[index]
    if line["side1"] < 0:
        return {"top": 0, "bottom": 0, "range": 0, "lowfloor": 0}
    front = _front_sector(level, line)
    back = _back_sector(level, line)
    if front is None or back is None:
        return {"top": 0, "bottom": 0, "range": 0, "lowfloor": 0}
    front_floor = level.sectors[front]["floor"]
    front_ceiling = level.sectors[front]["ceiling"]
    back_floor = level.sectors[back]["floor"]
    back_ceiling = level.sectors[back]["ceiling"]
    top = min(front_ceiling, back_ceiling)
    if front_floor > back_floor:
        bottom, lowfloor = front_floor, back_floor
    else:
        bottom, lowfloor = back_floor, front_floor
    return {"top": top, "bottom": bottom, "range": top - bottom,
            "lowfloor": lowfloor}


def movement_check_line(level: "SpecLevel", mo: dict, box: list[int],
                        out: dict, index: int) -> bool:
    line = level.bsp.linedefs[index]
    v1, v2, dx, dy = movement_line_geometry(level.bsp, line)
    lbox = [max(v1["y"], v2["y"]), min(v1["y"], v2["y"]),
            min(v1["x"], v2["x"]), max(v1["x"], v2["x"])]
    if (box[MOVEMENT_BOXRIGHT] <= lbox[MOVEMENT_BOXLEFT] or
            box[MOVEMENT_BOXLEFT] >= lbox[MOVEMENT_BOXRIGHT] or
            box[MOVEMENT_BOXTOP] <= lbox[MOVEMENT_BOXBOTTOM] or
            box[MOVEMENT_BOXBOTTOM] >= lbox[MOVEMENT_BOXTOP]):
        return True
    if movement_box_on_line_side(box, v1, dx, dy) != -1:
        return True

    # `if (!ld->backsector) return false;`: the port's back sector exists only
    # for a line that carries ML_TWOSIDED and has side 1.
    if not (line["flags"] & MOVEMENT_ML_TWOSIDED) or line["side1"] < 0:
        return False

    if not (mo["flags"] & MF_MISSILE):
        if line["flags"] & MOVEMENT_ML_BLOCKING:
            return False
        if not mo["player"] and line["flags"] & MOVEMENT_ML_BLOCKMONSTERS:
            return False

    opening = movement_line_opening(level, index)
    if opening["top"] < out["ceilingz"]:
        out["ceilingz"] = opening["top"]
        out["ceilingLine"] = index
    if opening["bottom"] > out["floorz"]:
        out["floorz"] = opening["bottom"]
    if opening["lowfloor"] < out["dropoffz"]:
        out["dropoffz"] = opening["lowfloor"]

    if level.line_special[index] != 0:
        if len(out["lines"]) < MOVEMENT_MAXSPECIALCROSS:
            out["lines"].append(index)
        else:
            out["overflow"] = True
    return True


def movement_check_thing(level: "SpecLevel", mo: dict, other: dict,
                         x: int, y: int) -> bool:
    # A thing already marked for removal is not offered again: upstream's
    # P_RemoveMobj unlinks it from the blockmap, and both walks here read a list
    # where the record survives until the next tick.
    if other.get("removed"):
        return True
    if not (other["flags"] & (MF_SOLID | MF_SPECIAL | MF_SHOOTABLE)):
        return True
    blockdist = other["radius"] + mo["radius"]
    if abs(other["x"] - x) >= blockdist or abs(other["y"] - y) >= blockdist:
        return True
    if other is mo:
        return True

    if mo["flags"] & MF_SKULLFLY:
        # The damage half is P_DamageMobj's (D4.2d); the stop is this layer's.
        mo["flags"] &= ~MF_SKULLFLY
        mo["momx"] = mo["momy"] = mo["momz"] = 0
        return False

    if mo["flags"] & MF_MISSILE:
        if mo["z"] > other["z"] + other["height"]:
            return True
        if mo["z"] + mo["height"] < other["z"]:
            return True
        target = mo.get("target")
        if target is not None and (
                target["type"] == other["type"] or
                (target["type"] == MT_KNIGHT and other["type"] == MT_BRUISER) or
                (target["type"] == MT_BRUISER and other["type"] == MT_KNIGHT)):
            if other is mo.get("target"):
                return True
            if other["type"] != MT_PLAYER:
                return False
        if not (other["flags"] & MF_SHOOTABLE):
            return not (other["flags"] & MF_SOLID)
        # `damage = ((P_Random()%8)+1)*tmthing->info->damage;` then
        # `P_DamageMobj (thing, tmthing, tmthing->target, damage);` -- the draw
        # is part of the sequence and happens before the call.
        damage = MOVEMENT_MISSILE_DAMAGE.get(mo["type"], 0)
        damage_damage(level, other, mo, mo.get("target"),
                      (level.rng.next() % 8 + 1) * damage)
        return False

    if other["flags"] & MF_SPECIAL:
        # `P_TouchSpecialThing (thing, tmthing)` -- only when this run carries the
        # interaction layer (--damage); the movement and player gates keep their
        # original "walked over" answer.
        if getattr(level, "pickups", False):
            damage_touch_special(level, other, mo)
        return not (other["flags"] & MF_SOLID)

    return not (other["flags"] & MF_SOLID)


def movement_check_position(level: "SpecLevel", mo: dict, x: int,
                            y: int) -> tuple[bool, dict]:
    level.move_stats["checks"] += 1
    out = {"floorz": 0, "ceilingz": 0, "dropoffz": 0, "subsector": 0,
           "sector": 0, "lines": [], "ceilingLine": None, "overflow": False}

    box = [y + mo["radius"], y - mo["radius"],
           x - mo["radius"], x + mo["radius"]]

    subsector = point_in_subsector(level.bsp, x, y)
    sector = sector_of_subsector(level.bsp, subsector)
    floor = level.sectors[sector]["floor"]
    ceiling = level.sectors[sector]["ceiling"]
    out["subsector"] = subsector
    out["sector"] = sector
    out["floorz"] = floor
    out["dropoffz"] = floor
    out["ceilingz"] = ceiling

    if mo["flags"] & MF_NOCLIP:
        return True, out

    for other in level.mobjs:
        if not movement_check_thing(level, mo, other, x, y):
            return False, out

    for index in range(len(level.bsp.linedefs)):
        if not movement_check_line(level, mo, box, out, index):
            return False, out
    return True, out


def movement_try_move(level: "SpecLevel", mo: dict, x: int, y: int) -> bool:
    ok, result = movement_check_position(level, mo, x, y)
    if not ok:
        level.move_stats["blocked"] += 1
        return False

    if not (mo["flags"] & MF_NOCLIP):
        if result["ceilingz"] - result["floorz"] < mo["height"]:
            level.move_stats["blocked"] += 1
            return False
        if not (mo["flags"] & MF_TELEPORT) and \
                result["ceilingz"] - mo["z"] < mo["height"]:
            level.move_stats["blocked"] += 1
            return False
        if not (mo["flags"] & MF_TELEPORT) and \
                result["floorz"] - mo["z"] > MOVEMENT_STEPUP:
            level.move_stats["blocked"] += 1
            return False
        if not (mo["flags"] & (MF_DROPOFF | MF_FLOAT)) and \
                result["floorz"] - result["dropoffz"] > MOVEMENT_STEPUP:
            level.move_stats["blocked"] += 1
            return False

    oldx = mo["x"]
    oldy = mo["y"]
    mo["floorz"] = result["floorz"]
    mo["ceilingz"] = result["ceilingz"]
    mo["subsector"] = result["subsector"]
    mo["sector"] = result["sector"]
    mo["x"] = x
    mo["y"] = y

    if not (mo["flags"] & (MF_TELEPORT | MF_NOCLIP)):
        for index in reversed(result["lines"]):
            if level.line_special[index] == 0:
                continue
            line = level.bsp.linedefs[index]
            v1, _v2, dx, dy = movement_line_geometry(level.bsp, line)
            side = movement_point_on_line_side(mo["x"], mo["y"], v1, dx, dy)
            oldside = movement_point_on_line_side(oldx, oldy, v1, dx, dy)
            if side != oldside:
                trigger_special_line(level, index, "cross", mo, oldside)
                level.move_stats["crossed"] += 1
    return True


def movement_trace_line(level: "SpecLevel", x1: int, y1: int, dx: int, dy: int,
                        index: int) -> int | None:
    line = level.bsp.linedefs[index]
    v1, v2, ldx, ldy = movement_line_geometry(level.bsp, line)

    if dx > 16 * FRACUNIT or dy > 16 * FRACUNIT or \
            dx < -16 * FRACUNIT or dy < -16 * FRACUNIT:
        s1 = movement_point_on_divline_side(v1["x"], v1["y"], x1, y1, dx, dy)
        s2 = movement_point_on_divline_side(v2["x"], v2["y"], x1, y1, dx, dy)
    else:
        s1 = movement_point_on_line_side(x1, y1, v1, ldx, ldy)
        s2 = movement_point_on_line_side(x1 + dx, y1 + dy, v1, ldx, ldy)
    if s1 == s2:
        return None

    frac = movement_intercept_vector(x1, y1, dx, dy, v1["x"], v1["y"], ldx, ldy)
    if frac < 0 or frac > FRACUNIT:
        return None
    return frac


def movement_slide_line_blocks(level: "SpecLevel", mo: dict, index: int) -> bool:
    line = level.bsp.linedefs[index]
    v1, _v2, dx, dy = movement_line_geometry(level.bsp, line)

    if not (line["flags"] & MOVEMENT_ML_TWOSIDED):
        if movement_point_on_line_side(mo["x"], mo["y"], v1, dx, dy):
            return False  # don't hit the back side
        return True

    opening = movement_line_opening(level, index)
    if opening["range"] < mo["height"]:
        return True
    if opening["top"] - mo["z"] < mo["height"]:
        return True
    if opening["bottom"] - mo["z"] > MOVEMENT_STEPUP:
        return True
    return False


def movement_slide_trace_ray(level: "SpecLevel", mo: dict, best: list[int | None],
                             x1: int, y1: int, dx: int, dy: int) -> None:
    for index in range(len(level.bsp.linedefs)):
        frac = movement_trace_line(level, x1, y1, dx, dy, index)
        if frac is None:
            continue
        if movement_slide_line_blocks(level, mo, index):
            if best[0] is None or frac < best[0]:
                best[0] = frac
                best[1] = index


def movement_hit_slide_line(level: "SpecLevel", mo: dict, index: int,
                            tmxmove: int, tmymove: int) -> tuple[int, int]:
    line = level.bsp.linedefs[index]
    v1, _v2, dx, dy = movement_line_geometry(level.bsp, line)
    slope = movement_slope_type(dx, dy)

    if slope == "horizontal":
        return tmxmove, 0
    if slope == "vertical":
        return 0, tmymove

    side = movement_point_on_line_side(mo["x"], mo["y"], v1, dx, dy)
    lineangle = point_to_angle(0, 0, dx, dy)
    if side == 1:
        lineangle = u32(lineangle + ANG180)

    moveangle = point_to_angle(0, 0, tmxmove, tmymove)
    deltaangle = u32(moveangle - lineangle)
    if deltaangle > ANG180:
        deltaangle = u32(deltaangle + ANG180)

    lineindex = lineangle >> ANGLETOFINESHIFT
    deltaindex = deltaangle >> ANGLETOFINESHIFT
    movelen = movement_aprox_distance(tmxmove, tmymove)
    newlen = fixed_mul(movelen, FINE_SINE[deltaindex + FINEANGLES // 4])

    return (fixed_mul(newlen, FINE_SINE[lineindex + FINEANGLES // 4]),
            fixed_mul(newlen, FINE_SINE[lineindex]))


def movement_stairstep(level: "SpecLevel", mo: dict) -> None:
    if not movement_try_move(level, mo, mo["x"], mo["y"] + mo["momy"]):
        movement_try_move(level, mo, mo["x"] + mo["momx"], mo["y"])


def movement_slide_move(level: "SpecLevel", mo: dict) -> None:
    level.move_stats["slides"] += 1
    hitcount = 0

    while True:
        hitcount += 1
        if hitcount == 3:
            movement_stairstep(level, mo)
            return

        if mo["momx"] > 0:
            leadx, trailx = mo["x"] + mo["radius"], mo["x"] - mo["radius"]
        else:
            leadx, trailx = mo["x"] - mo["radius"], mo["x"] + mo["radius"]
        if mo["momy"] > 0:
            leady, traily = mo["y"] + mo["radius"], mo["y"] - mo["radius"]
        else:
            leady, traily = mo["y"] - mo["radius"], mo["y"] + mo["radius"]

        best: list[int | None] = [None, None]
        movement_slide_trace_ray(level, mo, best, leadx, leady,
                                 mo["momx"], mo["momy"])
        movement_slide_trace_ray(level, mo, best, trailx, leady,
                                 mo["momx"], mo["momy"])
        movement_slide_trace_ray(level, mo, best, leadx, traily,
                                 mo["momx"], mo["momy"])

        if best[0] is None:
            movement_stairstep(level, mo)
            return

        frac = best[0] - 0x800
        if frac > 0:
            newx = fixed_mul(mo["momx"], frac)
            newy = fixed_mul(mo["momy"], frac)
            if not movement_try_move(level, mo, mo["x"] + newx, mo["y"] + newy):
                movement_stairstep(level, mo)
                return

        frac = FRACUNIT - (frac + 0x800)
        if frac > FRACUNIT:
            frac = FRACUNIT
        if frac <= 0:
            return

        tmxmove = fixed_mul(mo["momx"], frac)
        tmymove = fixed_mul(mo["momy"], frac)
        tmxmove, tmymove = movement_hit_slide_line(level, mo, best[1],
                                                   tmxmove, tmymove)
        mo["momx"] = tmxmove
        mo["momy"] = tmymove

        if movement_try_move(level, mo, mo["x"] + tmxmove, mo["y"] + tmymove):
            return


def movement_explode_missile(level: "SpecLevel", mo: dict) -> None:
    """P_ExplodeMissile(): stop, enter the death state, drop MF_MISSILE."""
    mo["momx"] = mo["momy"] = mo["momz"] = 0
    damage_set_state(level, mo, damage_mobj(mo["type"])["deathstate"])
    mo["tics"] -= level.rng.next() & 3
    if mo["tics"] < 1:
        mo["tics"] = 1
    mo["flags"] &= ~MF_MISSILE


def movement_xy_movement(level: "SpecLevel", mo: dict) -> None:
    if mo["momx"] == 0 and mo["momy"] == 0:
        if mo["flags"] & MF_SKULLFLY:
            mo["flags"] &= ~MF_SKULLFLY
            mo["momx"] = mo["momy"] = mo["momz"] = 0
        return

    if mo["momx"] > MOVEMENT_MAXMOVE:
        mo["momx"] = MOVEMENT_MAXMOVE
    elif mo["momx"] < -MOVEMENT_MAXMOVE:
        mo["momx"] = -MOVEMENT_MAXMOVE
    if mo["momy"] > MOVEMENT_MAXMOVE:
        mo["momy"] = MOVEMENT_MAXMOVE
    elif mo["momy"] < -MOVEMENT_MAXMOVE:
        mo["momy"] = -MOVEMENT_MAXMOVE

    xmove = mo["momx"]
    ymove = mo["momy"]
    while True:
        if xmove > MOVEMENT_MAXMOVE // 2 or ymove > MOVEMENT_MAXMOVE // 2:
            # C: `ptryx = mo->x + xmove/2;` truncates toward zero, while
            # `xmove >>= 1` below floors. For a negative odd move the two
            # differ by one, so the position half must use trunc_div.
            ptryx = mo["x"] + trunc_div(xmove, 2)
            ptryy = mo["y"] + trunc_div(ymove, 2)
            xmove >>= 1
            ymove >>= 1
        else:
            ptryx = mo["x"] + xmove
            ptryy = mo["y"] + ymove
            xmove = 0
            ymove = 0

        if not movement_try_move(level, mo, ptryx, ptryy):
            if mo["player"]:
                movement_slide_move(level, mo)
            elif mo["flags"] & MF_MISSILE:
                movement_explode_missile(level, mo)
            else:
                mo["momx"] = 0
                mo["momy"] = 0
        if xmove == 0 and ymove == 0:
            break

    if mo["flags"] & (MF_MISSILE | MF_SKULLFLY):
        return
    if mo["z"] > mo["floorz"]:
        return

    if mo["flags"] & MF_CORPSE:
        if (abs(mo["momx"]) > FRACUNIT // 4 or
                abs(mo["momy"]) > FRACUNIT // 4):
            if mo["floorz"] != level.sectors[mo["sector"]]["floor"]:
                return

    if (-MOVEMENT_STOPSPEED < mo["momx"] < MOVEMENT_STOPSPEED and
            -MOVEMENT_STOPSPEED < mo["momy"] < MOVEMENT_STOPSPEED):
        mo["momx"] = 0
        mo["momy"] = 0
    else:
        mo["momx"] = fixed_mul(mo["momx"], MOVEMENT_FRICTION)
        mo["momy"] = fixed_mul(mo["momy"], MOVEMENT_FRICTION)


def movement_z_movement(level: "SpecLevel", mo: dict) -> None:
    mo["z"] = i32(mo["z"] + mo["momz"])

    if mo["flags"] & MF_FLOAT and mo.get("target") is not None:
        if not (mo["flags"] & (MF_SKULLFLY | MF_INFLOAT)):
            target = mo["target"]
            dist = movement_aprox_distance(mo["x"] - target["x"],
                                           mo["y"] - target["y"])
            delta = (target["z"] + (mo["height"] >> 1)) - mo["z"]
            if delta < 0 and dist < -(delta * 3):
                mo["z"] -= MOVEMENT_FLOATSPEED
            elif delta > 0 and dist < delta * 3:
                mo["z"] += MOVEMENT_FLOATSPEED

    if mo["z"] <= mo["floorz"]:
        if mo["flags"] & MF_SKULLFLY:
            mo["momz"] = -mo["momz"]
        if mo["momz"] < 0:
            mo["momz"] = 0
        mo["z"] = mo["floorz"]
        if mo["flags"] & MF_MISSILE and not (mo["flags"] & MF_NOCLIP):
            movement_explode_missile(level, mo)
            return
    elif not (mo["flags"] & MF_NOGRAVITY):
        if mo["momz"] == 0:
            mo["momz"] = -MOVEMENT_GRAVITY * 2
        else:
            mo["momz"] -= MOVEMENT_GRAVITY

    if mo["z"] + mo["height"] > mo["ceilingz"]:
        if mo["momz"] > 0:
            mo["momz"] = 0
        mo["z"] = mo["ceilingz"] - mo["height"]
        if mo["flags"] & MF_SKULLFLY:
            mo["momz"] = -mo["momz"]
        if mo["flags"] & MF_MISSILE and not (mo["flags"] & MF_NOCLIP):
            movement_explode_missile(level, mo)
            return


def movement_thrust(level: "SpecLevel", mo: dict, angle: int, move: int) -> None:
    index = angle >> ANGLETOFINESHIFT
    mo["momx"] = i32(mo["momx"] + fixed_mul(move, FINE_SINE[index + FINEANGLES // 4]))
    mo["momy"] = i32(mo["momy"] + fixed_mul(move, FINE_SINE[index]))


def movement_phase(tic: int) -> tuple[int | None, int]:
    """(angle, thrust) for one scripted tic; angle is None when it is unchanged."""
    if tic < 32:
        return 0, 0x19 * 2048
    if tic < 64:
        return ANG90, 0x19 * 2048
    if tic < 96:
        return ANG45, 0x32 * 2048
    if tic < 128:
        return None, 0
    if tic < 160:
        return ANG180, 0x19 * 2048
    return None, 0


def movement_player(level: "SpecLevel", use_start_angle: bool = False) -> dict:
    """P_SpawnPlayer()'s mobj for the first type-1 THING, as p_level_init leaves it.

    The level load already consumed the RNG for the whole THINGS pass, which is
    why the caller seeds RandomState from run_things(). Only the collision fields
    the movement rules read are carried. ``--movement`` overrides the angle to
    zero; ``--player`` keeps the THING record's angle, which is what
    P_SpawnPlayer's `angle = ANG45 * (mthing->angle/45)` produces.
    """
    record = None
    for candidate in level.bsp.things:
        if candidate["type"] == 1:
            record = candidate
            break
    if record is None:
        raise InputError(f"{level.bsp.name}: no single-player start")
    x = _fixed16(record["x"])
    y = _fixed16(record["y"])
    subsector = point_in_subsector(level.bsp, x, y)
    sector = sector_of_subsector(level.bsp, subsector)
    floor = level.sectors[sector]["floor"]
    ceiling = level.sectors[sector]["ceiling"]
    angle = 0
    if use_start_angle:
        angle = u32(ANG45 * (record["angle"] // 45))
    return {
        "x": x, "y": y, "z": floor,
        "momx": 0, "momy": 0, "momz": 0,
        "angle": angle,
        "radius": MOVEMENT_PLAYER_RADIUS,
        "height": MOVEMENT_PLAYER_HEIGHT,
        "floorz": floor, "ceilingz": ceiling,
        "subsector": subsector, "sector": sector,
        "flags": PLAYER_INFO[0],
        "health": PLAYER_INFO[2],
        "state": PLAYER_INFO[3],
        "tics": -1,
        "player": True,
        "type": 0,
        "target": None,
        "reactiontime": PLAYER_INFO[4],
    }


def report_movement(lumps: list[Lump], map_name: str, tics: int) -> None:
    """The `--movement` protocol: the 160-tic walk over p_map.c's rules."""
    if tics < 0:
        raise FormatError("--tics cannot be negative")

    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)          # the RNG position the level load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.mobjs = [movement_player(level)]
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}

    mo = level.mobjs[0]

    print(f"MOVEMENT {map_name} tics={tics} script={MOVEMENT_SCRIPT}")

    for tic in range(tics):
        angle, move = movement_phase(tic)
        if angle is not None:
            mo["angle"] = angle
        if move:
            movement_thrust(level, mo, mo["angle"], move)

        blocked_before = level.move_stats["blocked"]
        crossed_before = level.move_stats["crossed"]

        if mo["momx"] or mo["momy"] or (mo["flags"] & MF_SKULLFLY):
            movement_xy_movement(level, mo)
        if mo["z"] != mo["floorz"] or mo["momz"]:
            movement_z_movement(level, mo)

        print(f"TIC {tic} x={mo['x']} y={mo['y']} z={mo['z']} "
              f"momx={mo['momx']} momy={mo['momy']} floorz={mo['floorz']} "
              f"ceilingz={mo['ceilingz']} sector={mo['sector']} "
              f"subsector={mo['subsector']} "
              f"blocked={1 if level.move_stats['blocked'] != blocked_before else 0} "
              f"cross={level.move_stats['crossed'] - crossed_before}")

    print(f"SUMMARY tics={tics} finalx={mo['x']} finaly={mo['y']} "
          f"finalz={mo['z']} blocked={level.move_stats['blocked']} "
          f"cross={level.move_stats['crossed']} "
          f"checks={level.move_stats['checks']}")


# --- The player (D4.2c) -----------------------------------------------------


def movement_use_lines(level: "SpecLevel", mo: dict) -> None:
    """P_UseLines(): the nearest special line within USERANGE.

    p_map.c's repeated-minimum intercept walk, line for line: ascending
    intercept fraction, skip a fraction already visited (so only the first line
    of a tie is examined), a non-special line with no opening stops the trace
    ("can't use through a wall"), and the first special line is triggered once.
    """
    angle = mo["angle"] >> ANGLETOFINESHIFT
    x1, y1 = mo["x"], mo["y"]
    # `x2 = x1 + (USERANGE>>FRACBITS)*finecosine[angle]` -- 64 map units, not a
    # FixedMul.
    x2 = x1 + (MOVEMENT_USERANGE >> FRACBITS) * FINE_SINE[angle + FINEANGLES // 4]
    y2 = y1 + (MOVEMENT_USERANGE >> FRACBITS) * FINE_SINE[angle]
    dx = i32(x2 - x1)
    dy = i32(y2 - y1)

    last_frac = -1
    while True:
        best_frac = FRACUNIT + 1
        best_line = None
        for index in range(len(level.bsp.linedefs)):
            frac = movement_trace_line(level, x1, y1, dx, dy, index)
            if frac is None or frac <= last_frac:
                continue
            if frac < best_frac:
                best_frac = frac
                best_line = index
        if best_line is None:
            return
        last_frac = best_frac

        if level.line_special[best_line] == 0:
            opening = movement_line_opening(level, best_line)
            if opening["range"] <= 0:
                return
            continue

        # `if (P_PointOnLineSide (usething->x, usething->y, in->d.line) == 1)
        # side = 1;` -- P_UseSpecialLine refuses a back-side press.
        use_line = level.bsp.linedefs[best_line]
        use_v1, _use_v2, use_dx, use_dy = movement_line_geometry(level.bsp, use_line)
        use_side = movement_point_on_line_side(mo["x"], mo["y"], use_v1, use_dx,
                                               use_dy)
        trigger_special_line(level, best_line, "use", mo, use_side)
        return


def player_record(mo: dict) -> dict:
    """G_PlayerReborn()'s live record: everything zero, then the spawn fields."""
    return {
        "mo": mo,
        "playerstate": PST_LIVE,
        "cmd": {"forwardmove": 0, "sidemove": 0, "angleturn": 0, "buttons": 0},
        "viewz": 0, "viewheight": 0, "deltaviewheight": 0, "bob": 0,
        "health": PLAYER_MAXHEALTH, "armorpoints": 0, "armortype": 0,
        "attackdown": 1, "usedown": 1, "refire": 0,
        "killcount": 0, "itemcount": 0, "secretcount": 0,
        "damagecount": 0, "bonuscount": 0, "extralight": 0,
        "fixedcolormap": 0, "attacker": None, "message": None,
        "didsecret": False, "onground": 0, "useTriggered": 0,
        "useTriggers": 0, "unsupportedAttack": 0,
        "unsupportedWeaponChange": 0, "unsupportedDeath": 0,
    }


def player_move_player(level: "SpecLevel", player: dict) -> None:
    """P_MovePlayer(): turn, then forward and side thrust on the ground."""
    mo = player["mo"]
    cmd = player["cmd"]

    # `player->mo->angle += (cmd->angleturn<<16);`
    mo["angle"] = u32(mo["angle"] + (cmd["angleturn"] << 16))
    # `onground = (player->mo->z <= player->mo->floorz);`
    player["onground"] = 1 if mo["z"] <= mo["floorz"] else 0

    if cmd["forwardmove"] and player["onground"]:
        movement_thrust(level, mo, mo["angle"], cmd["forwardmove"] * 2048)
    if cmd["sidemove"] and player["onground"]:
        # `P_Thrust (player, player->mo->angle-ANG90, cmd->sidemove*2048);`
        movement_thrust(level, mo, u32(mo["angle"] - ANG90),
                        cmd["sidemove"] * 2048)

    # `P_SetMobjState (player->mo, S_PLAY_RUN1)` changes only the sprite and
    # the state tic counter, neither of which this protocol prints; the oracle
    # models no state machine.


def player_calc_height(level: "SpecLevel", player: dict) -> None:
    """P_CalcHeight(): the bounded bob and the view height walk."""
    mo = player["mo"]

    player["bob"] = i32(
        (fixed_mul(mo["momx"], mo["momx"]) +
         fixed_mul(mo["momy"], mo["momy"])) >> 2)
    if player["bob"] > PLAYER_MAXBOB:
        player["bob"] = PLAYER_MAXBOB

    # `(player->cheats & CF_NOMOMENTUM)` is absent: there are no cheats (D5).
    if not player["onground"]:
        player["viewz"] = i32(mo["z"] + PLAYER_VIEWHEIGHT)
        if player["viewz"] > mo["ceilingz"] - 4 * FRACUNIT:
            player["viewz"] = mo["ceilingz"] - 4 * FRACUNIT
        # Upstream's next statement overwrites the clamp; kept verbatim.
        player["viewz"] = i32(mo["z"] + player["viewheight"])
        return

    angle = (PLAYER_BOB_FINE_STEP * level.leveltime) & FINEMASK
    bob = fixed_mul(player["bob"] // 2, FINE_SINE[angle])

    if player["playerstate"] == PST_LIVE:
        player["viewheight"] = i32(
            player["viewheight"] + player["deltaviewheight"])

        if player["viewheight"] > PLAYER_VIEWHEIGHT:
            player["viewheight"] = PLAYER_VIEWHEIGHT
            player["deltaviewheight"] = 0

        if player["viewheight"] < PLAYER_VIEWHEIGHT // 2:
            player["viewheight"] = PLAYER_VIEWHEIGHT // 2
            if player["deltaviewheight"] <= 0:
                player["deltaviewheight"] = 1

        if player["deltaviewheight"]:
            player["deltaviewheight"] = i32(
                player["deltaviewheight"] + FRACUNIT // 4)
            if player["deltaviewheight"] == 0:
                player["deltaviewheight"] = 1

    player["viewz"] = i32(mo["z"] + player["viewheight"] + bob)
    if player["viewz"] > mo["ceilingz"] - 4 * FRACUNIT:
        player["viewz"] = mo["ceilingz"] - 4 * FRACUNIT


def player_think(level: "SpecLevel", player: dict, cmd: dict) -> None:
    """P_PlayerThink(): upstream's body, minus the D4.2d branches."""
    player["cmd"] = dict(cmd)
    command = player["cmd"]
    player["useTriggered"] = 0

    if player["playerstate"] != PST_LIVE:
        # `P_DeathThink (player); return;` is D4.2d's: counted, not faked.
        player["unsupportedDeath"] += 1
        return

    mo = player["mo"]
    if mo["reactiontime"]:
        mo["reactiontime"] -= 1
    else:
        player_move_player(level, player)

    player_calc_height(level, player)

    # `if (cmd->buttons & BT_SPECIAL) cmd->buttons = 0;`
    if command["buttons"] & BT_SPECIAL:
        command["buttons"] = 0

    if command["buttons"] & BT_CHANGE:
        player["unsupportedWeaponChange"] += 1

    if command["buttons"] & BT_USE:
        if not player["usedown"]:
            before = level.stats["triggers"]
            movement_use_lines(level, mo)
            if level.stats["triggers"] != before:
                player["useTriggered"] = 1
                player["useTriggers"] += 1
            player["usedown"] = 1
    else:
        player["usedown"] = 0

    if command["buttons"] & BT_ATTACK:
        player["unsupportedAttack"] += 1

    if player["damagecount"]:
        player["damagecount"] -= 1
    if player["bonuscount"]:
        player["bonuscount"] -= 1


def player_command(tic: int) -> dict:
    """The PLAYER script's command for one tic (documented in movement_check.c)."""
    cmd = {"forwardmove": 0, "sidemove": 0, "angleturn": 0, "buttons": 0}
    if tic < 32:
        cmd["forwardmove"] = 0x19
    elif tic < 64:
        cmd["forwardmove"] = 0x32
    elif tic < 96:
        cmd["forwardmove"] = 0x19
        cmd["angleturn"] = -640
    elif tic < 128:
        cmd["sidemove"] = 0x19
    elif tic < 160:
        cmd["forwardmove"] = -0x19
    elif tic < 168:
        cmd["buttons"] = BT_USE
    return cmd


def report_player(lumps: list[Lump], map_name: str, tics: int) -> None:
    """The `--player` protocol: a ticcmd drives the avatar through P_PlayerThink."""
    if tics < 0:
        raise FormatError("--tics cannot be negative")

    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)          # the RNG position the level load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.mobjs = [movement_player(level, use_start_angle=True)]
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}

    mo = level.mobjs[0]
    player = player_record(mo)

    print(f"PLAYER {map_name} tics={tics} script={PLAYER_SCRIPT}")

    for tic in range(tics):
        player_think(level, player, player_command(tic))

        # P_RunThinkers: the avatar is the first mobj; run_tic walks the sector
        # thinkers the use press may just have appended.
        if mo["momx"] or mo["momy"] or (mo["flags"] & MF_SKULLFLY):
            movement_xy_movement(level, mo)
        if mo["z"] != mo["floorz"] or mo["momz"]:
            movement_z_movement(level, mo)
        run_tic(level)
        level.leveltime += 1

        print(f"TIC {tic} x={mo['x']} y={mo['y']} z={mo['z']} "
              f"angle={mo['angle'] & 0xFFFFFFFF} momx={mo['momx']} "
              f"momy={mo['momy']} floorz={mo['floorz']} sector={mo['sector']} "
              f"viewz={player['viewz']} viewheight={player['viewheight']} "
              f"bob={player['bob']} use={player['useTriggered']}")

    print(f"SUMMARY tics={tics} finalx={mo['x']} finaly={mo['y']} "
          f"finalangle={mo['angle'] & 0xFFFFFFFF} "
          f"finalviewz={player['viewz']} used={player['useTriggers']}")


# --- The door (T26) ---------------------------------------------------------
#
# The same scripted scenario tools/door_check.c runs, from the same frozen
# viewpoint: the nearest closed manual door to the player start, the player one
# radius in front of it, then the block/use/open/walk sequence. This side has no
# renderer aimed at an arbitrary viewpoint, so the C harness's FRAME lines are
# not part of the protocol; they carry an invariant (the doorway is not the
# 0xFF prefill) rather than an equality.

DOOR_SCRIPT = "closed-block-use-open-walk-through"
DOOR_DEFAULT_TICS = 120
DOOR_THRESHOLD_GAP = 16 * FRAC


def door_command(tic: int) -> dict:
    """The door script's command for one tic (documented in door_check.c)."""
    cmd = {"forwardmove": 0, "sidemove": 0, "angleturn": 0, "buttons": 0}
    if tic in (1, 2):
        cmd["forwardmove"] = 0x19
    elif 6 <= tic <= 12:
        cmd["buttons"] = BT_USE
    elif tic >= 45:
        cmd["forwardmove"] = 0x19
    return cmd


def _c_div2(value: int) -> int:
    """C's `value / 2`: truncation toward zero, not Python's floor."""
    return value // 2 if value >= 0 else -((-value) // 2)


def door_fixture(level: "SpecLevel", start: dict) -> dict:
    """The nearest closed manual door: special 1, closed back, flat threshold."""
    best = None
    best_distance = 0
    for index, line in enumerate(level.bsp.linedefs):
        if level.line_special[index] != 1:
            continue
        front = _front_sector(level, line)
        back = _back_sector(level, line)
        if front is None or back is None:
            continue
        if level.sectors[back]["ceiling"] > level.sectors[back]["floor"]:
            continue
        if level.sectors[front]["floor"] != level.sectors[back]["floor"]:
            continue
        v1 = level.bsp.vertexes[line["v1"]]
        v2 = level.bsp.vertexes[line["v2"]]
        dx = i32(v2["x"] - v1["x"])
        dy = i32(v2["y"] - v1["y"])
        mx = i32(v1["x"] + _c_div2(dx))
        my = i32(v1["y"] + _c_div2(dy))
        distance = (mx - start["x"]) ** 2 + (my - start["y"]) ** 2
        if best is not None and distance >= best_distance:
            continue

        nx = -dy
        ny = dx
        length = math.hypot(nx, ny)
        if length <= 0.0:
            continue
        step_x = int(nx / length * DOOR_THRESHOLD_GAP)
        step_y = int(ny / length * DOOR_THRESHOLD_GAP)
        for sign in (1, -1):
            px = i32(mx + sign * step_x)
            py = i32(my + sign * step_y)
            sector = sector_of_subsector(level.bsp,
                                         point_in_subsector(level.bsp, px, py))
            if sector == front:
                best = {"line": index, "front": front, "back": back,
                        "x": px, "y": py,
                        "angle": point_to_angle(px, py, mx, my)}
                best_distance = distance
                break
    if best is None:
        raise FormatError(f"{level.bsp.name}: no closed manual door in front of the start")
    return best


def report_door(lumps: list[Lump], map_name: str,
                tics: int = DOOR_DEFAULT_TICS) -> None:
    """The `--door` protocol: block, use, open and walk through one door."""
    if tics < 0:
        raise FormatError("--tics cannot be negative")

    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)         # the RNG position the level load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.mobjs = [movement_player(level)]
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}

    mo = level.mobjs[0]
    door = door_fixture(level, mo)
    mo["x"] = door["x"]
    mo["y"] = door["y"]
    mo["angle"] = door["angle"]
    mo["momx"] = mo["momy"] = 0
    ok, position = movement_check_position(level, mo, door["x"], door["y"])
    if not ok:
        raise FormatError(f"{level.bsp.name}: the threshold is not standable")
    mo["z"] = position["floorz"]
    mo["floorz"] = position["floorz"]
    mo["ceilingz"] = position["ceilingz"]
    mo["subsector"] = position["subsector"]
    mo["sector"] = position["sector"]

    player = player_record(mo)

    print(f"DOOR {map_name} line={door['line']} sector={door['back']} "
          f"front={door['front']} threshold={door['x'] // FRAC},{door['y'] // FRAC} "
          f"angle={door['angle']} tics={tics} script={DOOR_SCRIPT}")

    opens_at = -1
    crossed = False
    for tic in range(tics):
        blocked_before = level.move_stats["blocked"]
        player_think(level, player, door_command(tic))

        if mo["momx"] or mo["momy"] or (mo["flags"] & MF_SKULLFLY):
            movement_xy_movement(level, mo)
        if mo["z"] != mo["floorz"] or mo["momz"]:
            movement_z_movement(level, mo)
        run_tic(level)
        level.leveltime += 1

        opening = movement_line_opening(level, door["line"])
        passable = (opening["range"] >= mo["height"] and
                    opening["top"] - mo["z"] >= mo["height"])
        if opens_at < 0 and passable:
            opens_at = tic

        specialdata = level.sectors[door["back"]]["specialdata"]
        active = specialdata is not None and specialdata["kind"] == "door"

        if not crossed and mo["sector"] not in (door["back"], door["front"]):
            crossed = True

        print(f"TIC {tic} x={mo['x']} y={mo['y']} z={mo['z']} "
              f"viewz={player['viewz']} sector={mo['sector']} "
              f"door={1 if active else 0} use={player['useTriggered']} "
              f"blocked={1 if level.move_stats['blocked'] != blocked_before else 0} "
              f"ceiling={level.sectors[door['back']]['ceiling']} "
              f"passable={1 if passable else 0}")

    print(f"SUMMARY used={player['useTriggers']} triggered={level.stats['triggers']} "
          f"opens={opens_at} finalceiling={level.sectors[door['back']]['ceiling']} "
          f"finalx={mo['x']} finaly={mo['y']} sector={mo['sector']} "
          f"crossed={1 if crossed else 0}")


# --- Damage, death and pickups (D4.2d1) -------------------------------------
#
# A second implementation of linuxdoom-1.10 p_inter.c (P_GiveAmmo, P_GiveWeapon,
# P_GiveBody, P_GiveArmor, P_GiveCard, P_GivePower, P_TouchSpecialThing,
# P_KillMobj, P_DamageMobj), of p_mobj.c's P_SpawnPuff/P_SpawnBlood, and of
# p_map.c's PIT_ChangeSector. It reuses this file's movement core (P_CheckPosition
# and P_XYMovement/P_ZMovement are the same rules) and never calls the C.
#
# WHAT IS TRANSCRIBED, AND FROM WHERE
#
# DAMAGE_MOBJINFO is a *second* reading of upstream info.c's mobjinfo rows for
# the eight types this script uses: flags, radius, height, mass, spawnhealth,
# painchance and the six state numbers. DAMAGE_STATES is a second reading of
# info.h/info.c's state rows for the states the script reaches. Both were read
# from upstream, not from lib/doom/info_tables.h, so a disagreement between the
# two is a real defect in one of them. The oracle switches on the sprite *name*
# rather than its number, because a name is what upstream's P_TouchSpecialThing
# switch reads.
#
# WHERE THE PLAYER RECORD COMES FROM
#
# Upstream reads `mobj->player`. This port's `player` flag marks the avatar and
# the record is the level's, so the oracle keeps one `level.damage_player` and
# every rule that needs it tests the flag first, exactly as the C does.

DAMAGE_SCRIPT = "damage-death-crush-pickup"
DAMAGE_CRUSH_STEPS = 32
DAMAGE_CRUSH_STEP = 8 * FRACUNIT
DAMAGE_TICKS_AFTER_DEATH = 24
DAMAGE_STEPS = 84

DAMAGE_MG0_DX = 64
DAMAGE_MG1_DY = 64
DAMAGE_MG2_DX = -64
DAMAGE_MB0_DX = -96
DAMAGE_REACH = 20 * FRACUNIT
DAMAGE_STEP_FORWARD = 8 * FRACUNIT
DAMAGE_DROP_HEIGHT = 96 * FRACUNIT
DAMAGE_SPRAY_Z = 28 * FRACUNIT

DAMAGE_BONUSADD = 6
DAMAGE_BASETHRESHOLD = 100
DAMAGE_MAXHEALTH = 100
DAMAGE_MAXAMMO = (200, 50, 300, 50)
DAMAGE_CLIPAMMO = (10, 4, 20, 1)
# d_items.c's weaponinfo[].ammo, by weapon slot: fist, pistol, shotgun, chaingun,
# missile, plasma, bfg, chainsaw, super shotgun. None is a pool.
DAMAGE_NOAMMO = -1
DAMAGE_WEAPON_AMMO = (DAMAGE_NOAMMO, 0, 1, 0, 3, 2, 2, DAMAGE_NOAMMO, 1)
DAMAGE_INVULNTICS = 30 * 35
DAMAGE_INVISTICS = 60 * 35
DAMAGE_INFRATICS = 120 * 35
DAMAGE_IRONTICS = 60 * 35

# The constants the C build can be perturbed with: the oracle holds the shipped
# values, so a build that changes one and still agrees would mean the gate is not
# comparing what it claims to.
DAMAGE_GREEN_DIVISOR = 3
DAMAGE_BLUE_DIVISOR = 2
DAMAGE_CRUSH_DAMAGE = 10
DAMAGE_BLOOD_Z_SHIFT = 10

MF_JUSTHIT = 0x00000040
MF_JUSTATTACKED = 0x00000080
MF_PICKUP = 0x00000800
MF_DROPPED = 0x00020000
MF_SHADOW = 0x00040000
MF_NOBLOOD = 0x00080000

MT_BLOOD = 38
MT_PUFF = 37
MT_MISC0 = 43
MT_MISC2 = 45
MT_MISC3 = 46
MT_CLIP = 63
# D4.2d2's additions: the thing the weapon script shoots, and the three player
# missiles p_pspr.c's A_FireMissile/A_FirePlasma/A_FireBFG name.
MT_PLAYER = 0
MT_POSSESSED = 1
MT_ROCKET = 33
MT_PLASMA = 34
MT_BFG = 35

S_GIBS = 895

# mobjtype: (flags, radius, height, mass, spawnhealth, painchance,
#            spawnstate, painstate, deathstate, xdeathstate, seestate)
DAMAGE_MOBJINFO = {
    0: (0x02000C06, 16 * FRACUNIT, 56 * FRACUNIT, 100, 100, 255,
        149, 156, 158, 165, 150),          # MT_PLAYER
    1: (0x00400006, 20 * FRACUNIT, 56 * FRACUNIT, 100, 20, 200,
        174, 187, 189, 194, 176),          # MT_POSSESSED
    # T15b's imp. meleestate and missilestate are the same state (S_TROO_ATK1):
    # A_TroopAttack decides between the claw and the fireball when it runs.
    11: (0x00400006, 20 * FRACUNIT, 56 * FRACUNIT, 100, 60, 200,
         442, 455, 457, 462, 444),         # MT_TROOP
    # The imp's fireball, spawned by P_SpawnMissile rather than placed by the map.
    31: (0x00010610, 6 * FRACUNIT, 8 * FRACUNIT, 100, 1000, 0,
         97, 0, 99, 0, 0),                 # MT_TROOPSHOT
    30: (0x00080006, 10 * FRACUNIT, 42 * FRACUNIT, 100, 20, 0,
         806, 0, 808, 0, 0),               # MT_BARREL
    37: (0x00000210, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         93, 0, 0, 0, 0),                  # MT_PUFF
    38: (0x00000010, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         90, 0, 0, 0, 0),                  # MT_BLOOD
    43: (0x00000001, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         802, 0, 0, 0, 0),                 # MT_MISC0, the green armor
    45: (0x00800001, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         816, 0, 0, 0, 0),                 # MT_MISC2, the health bonus
    46: (0x00800001, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         822, 0, 0, 0, 0),                 # MT_MISC3, the armor bonus
    63: (0x00000001, 20 * FRACUNIT, 16 * FRACUNIT, 100, 1000, 0,
         870, 0, 0, 0, 0),                 # MT_CLIP
    # D4.2d2 adds the three player missiles, which P_SpawnPlayerMissile names.
    # The weapon script cannot fire any of them (shareware has no launcher), so
    # these rows exist for the code path and not for a compared line.
    33: (0x00010610, 11 * FRACUNIT, 8 * FRACUNIT, 100, 1000, 0,
         114, 0, 127, 0, 0),               # MT_ROCKET
    34: (0x00010610, 13 * FRACUNIT, 8 * FRACUNIT, 100, 1000, 0,
         107, 0, 109, 0, 0),               # MT_PLASMA
    35: (0x00010610, 13 * FRACUNIT, 8 * FRACUNIT, 100, 1000, 0,
         115, 0, 117, 0, 0),               # MT_BFG
}

# The deathsounds the action model needs. A_Scream chooses its sound with one
# P_Random() draw for the zombie families -- `sfx_podth1 + P_Random()%3`
# (59..61) and `sfx_bgdth1 + P_Random()%2` (62..63) -- and draws nothing on the
# default arm. The two monster types these scenes simulate both carry a drawing
# sound; the player's deathsound belongs to A_PlayerScream, which draws none.
# (linuxdoom-1.10 sounds.h: sfx_pldeth=57, sfx_podth1=59, sfx_bgdth1=62.)
DAMAGE_DEATHSOUND = {
    0: 57,   # MT_PLAYER     sfx_pldeth
    1: 59,   # MT_POSSESSED  sfx_podth1
    11: 62,  # MT_TROOP      sfx_bgdth1
    31: 0,   # MT_TROOPSHOT  no deathsound
    30: 0,   # MT_BARREL     no deathsound
}

# statenum: (tics, nextstate, action, sprite name). Only the states this script
# can reach are carried; an unknown state raises, which is how a rule that walks
# somewhere unexpected is caught instead of silently doing nothing.
DAMAGE_STATES = {
    90: (8, 91, "NULL", "BLUD"),        # S_BLOOD1
    91: (8, 92, "NULL", "BLUD"),        # S_BLOOD2
    92: (8, 0, "NULL", "BLUD"),         # S_BLOOD3
    93: (4, 94, "NULL", "PUFF"),        # S_PUFF1
    94: (4, 95, "NULL", "PUFF"),        # S_PUFF2
    95: (4, 96, "NULL", "PUFF"),        # S_PUFF3
    96: (4, 0, "NULL", "PUFF"),         # S_PUFF4
    # T15b: the imp's fireball. S_TBALL1/S_TBALL2 are the flight frames and
    # S_TBALLX1..X3 the burst; the burst rows take no action, because the damage
    # is the direct missile hit in PIT_CheckThing, not A_Explode.
    97: (4, 98, "NULL", "BAL1"),        # S_TBALL1
    98: (4, 97, "NULL", "BAL1"),        # S_TBALL2
    99: (6, 100, "NULL", "BAL1"),       # S_TBALLX1
    100: (6, 101, "NULL", "BAL1"),      # S_TBALLX2
    101: (6, 0, "NULL", "BAL1"),        # S_TBALLX3
    149: (-1, 0, "NULL", "PLAY"),       # S_PLAY
    156: (4, 157, "NULL", "PLAY"),      # S_PLAY_PAIN
    157: (4, 149, "A_Pain", "PLAY"),    # S_PLAY_PAIN2
    158: (10, 159, "NULL", "PLAY"),     # S_PLAY_DIE1
    159: (10, 160, "A_PlayerScream", "PLAY"),
    160: (10, 161, "A_Fall", "PLAY"),
    161: (10, 162, "NULL", "PLAY"),
    162: (10, 163, "NULL", "PLAY"),
    163: (10, 164, "NULL", "PLAY"),
    164: (-1, 0, "NULL", "PLAY"),       # S_PLAY_DIE7
    165: (5, 166, "NULL", "PLAY"),      # S_PLAY_XDIE1
    166: (5, 167, "A_XScream", "PLAY"),
    174: (10, 175, "A_Look", "POSS"),   # S_POSS_STND
    175: (10, 174, "A_Look", "POSS"),
    176: (4, 177, "A_Chase", "POSS"),   # S_POSS_RUN1
    177: (4, 178, "A_Chase", "POSS"),
    178: (4, 179, "A_Chase", "POSS"),
    179: (4, 180, "A_Chase", "POSS"),
    180: (4, 181, "A_Chase", "POSS"),   # S_POSS_RUN5
    181: (4, 182, "A_Chase", "POSS"),
    182: (4, 183, "A_Chase", "POSS"),
    183: (4, 176, "A_Chase", "POSS"),   # S_POSS_RUN8
    184: (10, 185, "A_FaceTarget", "POSS"),  # S_POSS_ATK1
    185: (8, 186, "A_PosAttack", "POSS"),    # S_POSS_ATK2
    186: (8, 176, "NULL", "POSS"),      # S_POSS_ATK3
    187: (3, 188, "NULL", "POSS"),      # S_POSS_PAIN
    188: (3, 176, "A_Pain", "POSS"),
    189: (5, 190, "NULL", "POSS"),      # S_POSS_DIE1
    190: (5, 191, "A_Scream", "POSS"),
    191: (5, 192, "A_Fall", "POSS"),
    192: (5, 193, "NULL", "POSS"),
    193: (-1, 0, "NULL", "POSS"),       # S_POSS_DIE5
    194: (5, 195, "NULL", "POSS"),      # S_POSS_XDIE1
    195: (5, 196, "A_XScream", "POSS"),
    196: (5, 197, "A_Fall", "POSS"),
    197: (5, 198, "NULL", "POSS"),
    198: (5, 199, "NULL", "POSS"),
    199: (5, 200, "NULL", "POSS"),
    200: (5, 201, "NULL", "POSS"),
    201: (5, 202, "NULL", "POSS"),
    202: (-1, 0, "NULL", "POSS"),       # S_POSS_XDIE9
    # T15b: the imp's own chain. Its run frames are three tics, so A_Chase is
    # reached every third tic rather than every fourth.
    442: (10, 443, "A_Look", "TROO"),    # S_TROO_STND
    443: (10, 442, "A_Look", "TROO"),
    444: (3, 445, "A_Chase", "TROO"),    # S_TROO_RUN1
    445: (3, 446, "A_Chase", "TROO"),
    446: (3, 447, "A_Chase", "TROO"),
    447: (3, 448, "A_Chase", "TROO"),
    448: (3, 449, "A_Chase", "TROO"),
    449: (3, 450, "A_Chase", "TROO"),
    450: (3, 451, "A_Chase", "TROO"),
    451: (3, 444, "A_Chase", "TROO"),
    452: (8, 453, "A_FaceTarget", "TROO"),   # S_TROO_ATK1
    453: (8, 454, "A_FaceTarget", "TROO"),
    454: (6, 444, "A_TroopAttack", "TROO"),
    455: (2, 456, "NULL", "TROO"),       # S_TROO_PAIN
    456: (2, 444, "A_Pain", "TROO"),
    457: (8, 458, "NULL", "TROO"),       # S_TROO_DIE1
    458: (8, 459, "A_Scream", "TROO"),
    459: (6, 460, "NULL", "TROO"),
    460: (6, 461, "A_Fall", "TROO"),
    461: (-1, 0, "NULL", "TROO"),        # S_TROO_DIE5
    462: (5, 463, "NULL", "TROO"),       # S_TROO_XDIE1
    463: (5, 464, "A_XScream", "TROO"),
    464: (5, 465, "NULL", "TROO"),
    465: (5, 466, "A_Fall", "TROO"),
    466: (5, 467, "NULL", "TROO"),
    467: (5, 468, "NULL", "TROO"),
    468: (5, 469, "NULL", "TROO"),
    469: (5, 470, "NULL", "TROO"),
    470: (-1, 0, "NULL", "TROO"),        # S_TROO_XDIE8
    802: (6, 803, "NULL", "ARM1"),      # S_ARM1
    803: (7, 802, "NULL", "ARM1"),      # S_ARM1A
    806: (6, 807, "NULL", "BAR1"),      # S_BAR1
    807: (6, 806, "NULL", "BAR1"),
    808: (5, 809, "NULL", "BEXP"),      # S_BEXP
    809: (5, 810, "A_Scream", "BEXP"),
    810: (5, 811, "NULL", "BEXP"),
    811: (10, 812, "A_Explode", "BEXP"),
    812: (10, 0, "NULL", "BEXP"),
    816: (6, 817, "NULL", "BON1"),      # S_BON1
    817: (6, 818, "NULL", "BON1"),
    822: (6, 823, "NULL", "BON2"),      # S_BON2
    823: (6, 824, "NULL", "BON2"),
    870: (-1, 0, "NULL", "CLIP"),       # S_CLIP
    895: (-1, 0, "NULL", "POL5"),       # S_GIBS
}

# The drop table P_KillMobj switches on: a zombie leaves a clip, a shotgun guy a
# shotgun, a chaingun guy a chaingun. The other two arms are unreachable here
# because the script spawns no such monster, so they are named and not carried.
DAMAGE_DROPS = {
    1: MT_CLIP,     # MT_POSSESSED -> MT_CLIP
    23: 63,         # MT_WOLFSS -> MT_CLIP (same arm as MT_POSSESSED)
}

# P_TouchSpecialThing's switch, derived from upstream's arms rather than copied as
# a statement list: sprite name -> (kind, payload).
#   armor        payload is the armortype
#   health_bonus one point, up to 200
#   armor_bonus  one point, up to 200, armortype 1 if none
#   health       payload is the amount, clamped at 200
#   mega         refused outside commercial DOOM
#   card         payload is the card index
#   body         payload is the amount, through P_GiveBody
#   power        payload is the power index
#   ammo         (pool, loads); a dropped clip halves the load
#   backpack     doubles every ceiling, then one load of each pool
#   weapon       (weapon slot, honours MF_DROPPED)
DAMAGE_PICKUPS = {
    "ARM1": ("armor", 1),
    "ARM2": ("armor", 2),
    "BON1": ("health_bonus", 1),
    "BON2": ("armor_bonus", 1),
    "SOUL": ("health", 100),
    "MEGA": ("mega", 0),
    "BKEY": ("card", 0),
    "YKEY": ("card", 1),
    "RKEY": ("card", 2),
    "BSKU": ("card", 3),
    "YSKU": ("card", 4),
    "RSKU": ("card", 5),
    "STIM": ("body", 10),
    "MEDI": ("body", 25),
    "PINV": ("power", 0),
    "PSTR": ("power", 1),
    "PINS": ("power", 2),
    "SUIT": ("power", 3),
    "PMAP": ("power", 4),
    "PVIS": ("power", 5),
    "CLIP": ("ammo", 0, 1),
    "AMMO": ("ammo", 0, 5),
    "ROCK": ("ammo", 3, 1),
    "BROK": ("ammo", 3, 5),
    "CELL": ("ammo", 2, 1),
    "CELP": ("ammo", 2, 5),
    "SHEL": ("ammo", 1, 1),
    "SBOX": ("ammo", 1, 5),
    "BPAK": ("backpack", 0),
    "BFUG": ("weapon", 6),
    "MGUN": ("weapon", 3),
    "CSAW": ("weapon", 7),
    "LAUN": ("weapon", 4),
    "PLAS": ("weapon", 5),
    "SHOT": ("weapon", 2),
    "SGN2": ("weapon", 8),
}


def damage_mobj(type_: int) -> dict:
    flags, radius, height, mass, health, painchance, spawnstate, pain, death, \
        xdeath, see = DAMAGE_MOBJINFO[type_]
    return {"type": type_, "flags": flags, "radius": radius, "height": height,
            "mass": mass, "spawnhealth": health, "painchance": painchance,
            "spawnstate": spawnstate, "painstate": pain,
            "deathstate": death, "xdeathstate": xdeath, "seestate": see}


def damage_spawn(level: "SpecLevel", type_: int, x: int, y: int, z) -> dict:
    """P_SpawnMobj(): one lastlook draw, the states' spawn row, and z."""
    info = damage_mobj(type_)
    lastlook = level.rng.next() % MAXPLAYERS           # `lastlook`
    subsector = point_in_subsector(level.bsp, x, y)
    sector = sector_of_subsector(level.bsp, subsector)
    floor = level.sectors[sector]["floor"]
    ceiling = level.sectors[sector]["ceiling"]
    if z == "floor":
        z = floor
    elif z == "ceiling":
        z = ceiling - info["height"]
    tics, _next, _action, sprite = level_mobj_states(level)[info["spawnstate"]]

    mo = {
        "type": type_, "x": x, "y": y, "z": i32(z),
        "momx": 0, "momy": 0, "momz": 0, "angle": 0,
        "radius": info["radius"], "height": info["height"],
        "flags": info["flags"], "health": info["spawnhealth"],
        "state": info["spawnstate"], "tics": tics, "sprite": sprite,
        "floorz": floor, "ceilingz": ceiling,
        "subsector": subsector, "sector": sector,
        "player": False, "target": None, "reactiontime": 0,
        "threshold": 0, "removed": False, "lastlook": lastlook,
        "movedir": 0, "movecount": 0,
    }
    level.mobjs.append(mo)
    return mo


def damage_remove(level: "SpecLevel", mo: dict) -> None:
    """P_RemoveMobj(): the mark. The next walk drops the record."""
    mo["removed"] = True


def level_mobj_states(level: "SpecLevel") -> dict:
    """The mobj state table this protocol compares against.

    ``DAMAGE_STATES`` is the table ``--damage`` transcribes; ``--attack`` installs a
    superset because the weapon script also drives the avatar's attack states. A
    protocol that installs nothing gets the damage table, exactly as before.
    """
    table = getattr(level, "mobj_states", None)
    return table if table is not None else DAMAGE_STATES


def damage_set_state(level: "SpecLevel", mo: dict, state: int) -> bool:
    """P_SetMobjState(): follow zero-tic states, run A_Fall, stop at S_NULL."""
    table = level_mobj_states(level)
    while True:
        if state == 0:
            mo["state"] = 0
            damage_remove(level, mo)
            return False
        tics, nxt, action, sprite = table[state]
        mo["state"] = state
        mo["tics"] = tics
        mo["sprite"] = sprite
        # A_Fall is the action here that changes the mobj's flags outright. A_Look
        # (T15a) wakes and enters the see state; A_Chase and the four attack
        # actions (T15b) move, face, shoot and burst. A_Scream picks its sound
        # with one draw for the zombie families; A_XScream, A_PlayerScream and
        # A_Pain are sound only and draw nothing.
        if action == "A_Fall":
            mo["flags"] &= ~MF_SOLID
        elif action == "A_Look":
            enemy_look(level, mo)
        elif action == "A_Chase":
            enemy_chase(level, mo)
        elif action == "A_FaceTarget":
            enemy_face_target(level, mo)
        elif action == "A_PosAttack":
            enemy_pos_attack(level, mo)
        elif action == "A_TroopAttack":
            enemy_troop_attack(level, mo)
        elif action == "A_Explode":
            enemy_explode(level, mo)
        elif action == "A_Scream":
            # `sfx_podth1 + P_Random()%3` or `sfx_bgdth1 + P_Random()%2`: the
            # sound choice is one draw, and it sits in the sequence a demo
            # replays even though this port has no audio layer.
            if 59 <= DAMAGE_DEATHSOUND.get(mo["type"], 0) <= 63:
                level.rng.next()
        state = nxt
        if tics != 0:
            break
    return True


def damage_player_of(level: "SpecLevel", mo: dict):
    return level.damage_player if mo.get("player") else None


def damage_give_ammo(level: "SpecLevel", player: dict, ammo: int, num: int) -> bool:
    if ammo == DAMAGE_NOAMMO:
        return False
    if player["ammo"][ammo] == player["maxammo"][ammo]:
        return False
    if num:
        num *= DAMAGE_CLIPAMMO[ammo]
    else:
        num = DAMAGE_CLIPAMMO[ammo] // 2
    if level.damage_skill in (0, 4):          # sk_baby / sk_nightmare
        num <<= 1
    old = player["ammo"][ammo]
    player["ammo"][ammo] = min(player["ammo"][ammo] + num,
                               player["maxammo"][ammo])
    if old:
        return True
    # D4.2d2's auto-select tail: "We were down to zero, so select a new weapon.
    # Preferences are not user selectable." It reads `readyweapon`, which D4.2d1
    # could not.
    ready = player["readyweapon"]
    owned = player["weaponowned"]
    if ammo == 0:
        if ready == 0:
            player["pendingweapon"] = 3 if owned[3] else 1
    elif ammo == 1:
        if ready in (0, 1) and owned[2]:
            player["pendingweapon"] = 2
    elif ammo == 2:
        if ready in (0, 1) and owned[5]:
            player["pendingweapon"] = 5
    elif ammo == 3:
        if ready == 0 and owned[4]:
            player["pendingweapon"] = 4
    return True


def damage_give_weapon(level: "SpecLevel", player: dict, weapon: int,
                       dropped: bool) -> bool:
    ammo = DAMAGE_WEAPON_AMMO[weapon]
    gaveammo = False
    if ammo != DAMAGE_NOAMMO:
        gaveammo = damage_give_ammo(level, player, ammo, 1 if dropped else 2)
    if player["weaponowned"][weapon]:
        gaveweapon = False
    else:
        gaveweapon = True
        player["weaponowned"][weapon] = 1
        player["pendingweapon"] = weapon
    return gaveweapon or gaveammo


def damage_give_body(player: dict, num: int, mo: dict) -> bool:
    if player["health"] >= DAMAGE_MAXHEALTH:
        return False
    player["health"] = min(player["health"] + num, DAMAGE_MAXHEALTH)
    mo["health"] = player["health"]
    return True


def damage_give_armor(player: dict, armortype: int) -> bool:
    hits = armortype * 100
    if player["armorpoints"] >= hits:
        return False
    player["armortype"] = armortype
    player["armorpoints"] = hits
    return True


def damage_give_card(player: dict, card: int) -> None:
    if player["cards"][card]:
        return
    player["bonuscount"] = DAMAGE_BONUSADD
    player["cards"][card] = 1


def damage_give_power(player: dict, mo: dict, power: int) -> bool:
    if power == 0:
        player["powers"][power] = DAMAGE_INVULNTICS
        return True
    if power == 2:
        player["powers"][power] = DAMAGE_INVISTICS
        mo["flags"] |= MF_SHADOW
        return True
    if power == 5:
        player["powers"][power] = DAMAGE_INFRATICS
        return True
    if power == 3:
        player["powers"][power] = DAMAGE_IRONTICS
        return True
    if power == 1:
        damage_give_body(player, 100, mo)
        player["powers"][power] = 1
        return True
    if player["powers"][power]:
        return False
    player["powers"][power] = 1
    return True


def damage_touch_special(level: "SpecLevel", special: dict, toucher: dict) -> None:
    """P_TouchSpecialThing(), upstream's order: reach, dead, the sprite's arm."""
    player = damage_player_of(level, toucher)
    if player is None:
        return
    if special["z"] - toucher["z"] > toucher["height"]:
        return
    if special["z"] - toucher["z"] < -8 * FRACUNIT:
        return
    if toucher["health"] <= 0:
        return

    sprite = special["sprite"]
    if sprite not in DAMAGE_PICKUPS:
        return                                        # I_Error upstream
    arm = DAMAGE_PICKUPS[sprite]
    kind = arm[0]
    dropped = bool(special["flags"] & MF_DROPPED)

    if kind == "armor":
        if not damage_give_armor(player, arm[1]):
            return
    elif kind == "health_bonus":
        player["health"] = min(player["health"] + 1, 200)
        toucher["health"] = player["health"]
    elif kind == "armor_bonus":
        player["armorpoints"] = min(player["armorpoints"] + 1, 200)
        if not player["armortype"]:
            player["armortype"] = 1
    elif kind == "health":
        player["health"] = min(player["health"] + arm[1], 200)
        toucher["health"] = player["health"]
    elif kind == "mega":
        return                                        # gamemode != commercial
    elif kind == "card":
        damage_give_card(player, arm[1])
    elif kind == "body":
        if not damage_give_body(player, arm[1], toucher):
            return
    elif kind == "power":
        if not damage_give_power(player, toucher, arm[1]):
            return
        # `if (player->readyweapon != wp_fist) player->pendingweapon = wp_fist;`
        # -- D4.2d2 could finally compare the weapon in hand.
        if arm[1] == 1 and player["readyweapon"] != 0:
            player["pendingweapon"] = 0
    elif kind == "ammo":
        pool, loads = arm[1], arm[2]
        if not damage_give_ammo(level, player, pool, 0 if dropped else loads):
            return
    elif kind == "backpack":
        if not player["backpack"]:
            for i in range(4):
                player["maxammo"][i] *= 2
            player["backpack"] = 1
        for i in range(4):
            damage_give_ammo(level, player, i, 1)
    elif kind == "weapon":
        if not damage_give_weapon(level, player, arm[1], dropped):
            return

    if special["flags"] & MF_COUNTITEM:
        player["itemcount"] += 1
    damage_remove(level, special)
    player["bonuscount"] += DAMAGE_BONUSADD


def damage_kill(level: "SpecLevel", source, target: dict) -> None:
    """P_KillMobj(): the flags, the totals, the state chain and the drop."""
    info = damage_mobj(target["type"])
    target["flags"] &= ~(MF_SHOOTABLE | MF_FLOAT | MF_SKULLFLY)
    if target["type"] != 18:                          # MT_SKULL
        target["flags"] &= ~MF_NOGRAVITY
    target["flags"] |= MF_CORPSE | MF_DROPOFF
    target["height"] >>= 2

    source_player = damage_player_of(level, source) if source is not None else None
    if source_player is not None:
        if target["flags"] & MF_COUNTKILL:
            source_player["killcount"] += 1
        # frags: deathmatch, D5
    elif not level.damage_netgame and target["flags"] & MF_COUNTKILL:
        level.damage_player["killcount"] += 1

    victim = damage_player_of(level, target)
    if victim is not None:
        target["flags"] &= ~MF_SOLID
        victim["playerstate"] = PST_DEAD

    if target["health"] < -info["spawnhealth"] and info["xdeathstate"]:
        damage_set_state(level, target, info["xdeathstate"])
    else:
        damage_set_state(level, target, info["deathstate"])
    target["tics"] -= level.rng.next() & 3
    if target["tics"] < 1:
        target["tics"] = 1

    item = DAMAGE_DROPS.get(target["type"])
    if item is None:
        return
    mo = damage_spawn(level, item, target["x"], target["y"], "floor")
    mo["flags"] |= MF_DROPPED


def damage_damage(level: "SpecLevel", target: dict, inflictor, source,
                  damage: int) -> None:
    """P_DamageMobj(): thrust, armor, the skull stop, pain and the target."""
    if not (target["flags"] & MF_SHOOTABLE):
        return
    if target["health"] <= 0:
        return
    info = damage_mobj(target["type"])

    if target["flags"] & MF_SKULLFLY:
        target["momx"] = target["momy"] = target["momz"] = 0

    player = damage_player_of(level, target)
    if player is not None and level.damage_skill == 0:      # sk_baby
        damage >>= 1

    if inflictor is not None and not (target["flags"] & MF_NOCLIP):
        ang = point_to_angle(0, 0, target["x"] - inflictor["x"],
                             target["y"] - inflictor["y"])
        thrust = (damage * (FRACUNIT >> 3) * 100) // info["mass"]
        if (damage < 40 and damage > target["health"]
                and target["z"] - inflictor["z"] > 64 * FRACUNIT
                and level.rng.next() & 1):
            ang = u32(ang + ANG180)
            thrust *= 4
        ang >>= ANGLETOFINESHIFT
        target["momx"] = i32(target["momx"] +
                             fixed_mul(thrust, FINE_SINE[ang + FINEANGLES // 4]))
        target["momy"] = i32(target["momy"] +
                             fixed_mul(thrust, FINE_SINE[ang]))

    if player is not None:
        if level.sectors[target["sector"]]["special"] == 11 and \
                damage >= target["health"]:
            damage = target["health"] - 1
        if damage < 1000 and player["powers"][0]:
            return
        if player["armortype"]:
            if player["armortype"] == 1:
                saved = damage // DAMAGE_GREEN_DIVISOR
            else:
                saved = damage // DAMAGE_BLUE_DIVISOR
            if player["armorpoints"] <= saved:
                saved = player["armorpoints"]
                player["armortype"] = 0
            player["armorpoints"] -= saved
            damage -= saved
        player["health"] = max(player["health"] - damage, 0)
        player["attacker"] = source
        player["damagecount"] = min(player["damagecount"] + damage, 100)

    target["health"] -= damage
    if target["health"] <= 0:
        damage_kill(level, source, target)
        return

    if level.rng.next() < info["painchance"] and not (target["flags"] & MF_SKULLFLY):
        target["flags"] |= MF_JUSTHIT
        damage_set_state(level, target, info["painstate"])

    target["reactiontime"] = 0
    if (not target["threshold"] or target["type"] == 3) and \
            source is not None and source is not target and source["type"] != 3:
        target["target"] = source
        target["threshold"] = DAMAGE_BASETHRESHOLD
        if target["state"] == info["spawnstate"] and info["seestate"]:
            damage_set_state(level, target, info["seestate"])


def damage_spray_z(level: "SpecLevel", z: int) -> int:
    jitter = (level.rng.next() - level.rng.next()) << DAMAGE_BLOOD_Z_SHIFT
    return i32(z + jitter)


def damage_spawn_puff(level: "SpecLevel", x: int, y: int, z: int,
                      attackrange: int) -> dict:
    """P_SpawnPuff(): two draws for z, one for the tic jitter, then the state.

    ``attackrange`` is the *shot's* reach: a melee spark starts at S_PUFF3 so the
    flare reads on the tic the fist lands.
    """
    th = damage_spawn(level, MT_PUFF, x, y, damage_spray_z(level, z))
    th["momz"] = FRACUNIT
    th["tics"] -= level.rng.next() & 3
    if th["tics"] < 1:
        th["tics"] = 1
    if attackrange == ATTACK_MELEERANGE:
        damage_set_state(level, th, 95)          # S_PUFF3
    return th


def damage_spawn_blood(level: "SpecLevel", x: int, y: int, z: int,
                       damage: int) -> dict:
    """P_SpawnBlood(): the same draws, then the damage band's state."""
    th = damage_spawn(level, MT_BLOOD, x, y, damage_spray_z(level, z))
    th["momz"] = FRACUNIT * 2
    th["tics"] -= level.rng.next() & 3
    if th["tics"] < 1:
        th["tics"] = 1
    if 9 <= damage <= 12:
        damage_set_state(level, th, 91)
    elif damage < 9:
        damage_set_state(level, th, 92)
    return th


def damage_height_clip(level: "SpecLevel", mo: dict) -> bool:
    """P_ThingHeightClip(): follow the floor or ceiling, answer "still fits"."""
    on_floor = mo["z"] == mo["floorz"]
    _ok, result = movement_check_position(level, mo, mo["x"], mo["y"])
    mo["floorz"] = result["floorz"]
    mo["ceilingz"] = result["ceilingz"]
    if on_floor:
        mo["z"] = mo["floorz"]
    elif mo["z"] + mo["height"] > mo["ceilingz"]:
        mo["z"] = mo["ceilingz"] - mo["height"]
    return mo["ceilingz"] - mo["floorz"] >= mo["height"]


def damage_sector_bounds(level: "SpecLevel", sector_index: int):
    """P_GroupLines' sector box, widened by MAXRADIUS: P_ChangeSector's walk."""
    top = -(2 ** 31)
    bottom = 2 ** 31 - 1
    left = 2 ** 31 - 1
    right = -(2 ** 31)
    found = False
    for line in level.bsp.linedefs:
        front = _front_sector(level, line)
        back = _back_sector(level, line)
        if front != sector_index and back != sector_index:
            continue
        v1 = level.bsp.vertexes[line["v1"]]
        v2 = level.bsp.vertexes[line["v2"]]
        top = max(top, v1["y"], v2["y"])
        bottom = min(bottom, v1["y"], v2["y"])
        left = min(left, v1["x"], v2["x"])
        right = max(right, v1["x"], v2["x"])
        found = True
    if not found:
        return None
    return (top + MOVEMENT_MAXRADIUS, bottom - MOVEMENT_MAXRADIUS,
            left - MOVEMENT_MAXRADIUS, right + MOVEMENT_MAXRADIUS)


def damage_change_sector(level: "SpecLevel", sector_index: int,
                         crush: bool) -> bool:
    """P_ChangeSector(): the height clip, giblets, dropped items and the crush."""
    box = damage_sector_bounds(level, sector_index)
    nofit = False
    for mo in level.mobjs:
        if mo.get("removed"):
            continue
        if box is not None:
            top, bottom, left, right = box
            if mo["x"] < left or mo["x"] > right or \
                    mo["y"] < bottom or mo["y"] > top:
                continue
        if damage_height_clip(level, mo):
            continue
        if mo["health"] <= 0:
            damage_set_state(level, mo, S_GIBS)
            mo["flags"] &= ~MF_SOLID
            mo["height"] = 0
            mo["radius"] = 0
            continue
        if mo["flags"] & MF_DROPPED:
            damage_remove(level, mo)
            continue
        if not (mo["flags"] & MF_SHOOTABLE):
            continue
        nofit = True
        if crush and not (level.leveltime & 3):
            damage_damage(level, mo, None, None, DAMAGE_CRUSH_DAMAGE)
            level.crushed += 1
            blood = damage_spawn(level, MT_BLOOD, mo["x"], mo["y"],
                                 mo["z"] + mo["height"] // 2)
            blood["momx"] = i32((level.rng.next() - level.rng.next()) << 12)
            blood["momy"] = i32((level.rng.next() - level.rng.next()) << 12)
    return nofit


def damage_think(level: "SpecLevel", mo: dict) -> None:
    """P_MobjThinker(): movement, then the state tic."""
    if mo["momx"] or mo["momy"] or (mo["flags"] & MF_SKULLFLY):
        movement_xy_movement(level, mo)
        if mo.get("removed"):
            return
    if mo["z"] != mo["floorz"] or mo["momz"]:
        movement_z_movement(level, mo)
        if mo.get("removed"):
            return
    if mo["tics"] != -1:
        mo["tics"] -= 1
        if mo["tics"] == 0:
            damage_set_state(level, mo, level_mobj_states(level)[mo["state"]][1])


def damage_run_thinkers(level: "SpecLevel") -> None:
    """P_RunThinkers(): every node once, in insertion order.

    The C's walk captures `next` before calling a node and appends new nodes at
    the tail, so a mobj spawned during the walk is visited on that tic only when
    the node that spawned it was not the tail when it ran. Both cases happen:
    a monster that spawns a missile while another mobj is still ahead is
    followed by that missile in the same tic, while the last mobj in the list
    spawning a puff leaves the puff for the next tic. `was_tail` reproduces it.
    """
    index = 0
    while index < len(level.mobjs):
        was_tail = index == len(level.mobjs) - 1
        mo = level.mobjs[index]
        index += 1
        if not mo.get("removed"):
            damage_think(level, mo)
        if was_tail:
            break
    level.mobjs = [mo for mo in level.mobjs if not mo.get("removed")]


def damage_slot_of(level: "SpecLevel", mo) -> int:
    if mo is None:
        return -1
    for index, each in enumerate(level.mobjs):
        if each is mo:
            return index
    return -1


def damage_print_mobj(level: "SpecLevel", slot: int, mo: dict) -> None:
    print(f"MOBJ {slot} type={mo['type']} x={mo['x']} y={mo['y']} z={mo['z']} "
          f"health={mo['health']} flags=0x{mo['flags'] & 0xFFFFFFFF:08x} "
          f"state={mo['state']} tics={mo['tics']} "
          f"tgt={damage_slot_of(level, mo.get('target'))} "
          f"momx={mo['momx']} momy={mo['momy']}")


def damage_print_player(level: "SpecLevel") -> None:
    player = level.damage_player
    weapons = 0
    for index, owned in enumerate(player["weaponowned"]):
        if owned:
            weapons |= 1 << index
    cards = 0
    for index, held in enumerate(player["cards"]):
        if held:
            cards |= 1 << index
    print(f"PLAYER hp={player['health']} ar={player['armorpoints']} "
          f"at={player['armortype']} item={player['itemcount']} "
          f"kill={player['killcount']} "
          f"att={damage_slot_of(level, player['attacker'])} "
          f"ps={player['playerstate']} dmg={player['damagecount']} "
          f"bonus={player['bonuscount']} "
          f"ammo={','.join(str(a) for a in player['ammo'])} "
          f"maxammo={','.join(str(a) for a in player['maxammo'])} "
          f"w={weapons} cards={cards} pack={player['backpack']} "
          f"pend={player['pendingweapon']} "
          f"pow={','.join(str(p) for p in player['powers'])}")


def damage_emit(level: "SpecLevel", step: list[int], op: str) -> None:
    print(f"STEP {step[0]} op={op} rng={level.rng.index} crushed={level.crushed}")
    for slot, mo in enumerate(level.mobjs):
        damage_print_mobj(level, slot, mo)
    damage_print_player(level)
    step[0] += 1


def report_damage(lumps: list[Lump], map_name: str) -> None:
    """The `--damage` protocol: P_DamageMobj, P_KillMobj, pickups and the crush."""
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)                 # the RNG position the load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.leveltime = 0
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}
    level.pickups = True
    level.damage_skill = 2                      # sk_medium, p_spawn_rules_default
    level.damage_netgame = False
    level.crushed = 0

    mo = movement_player(level)
    level.mobjs = [mo]
    # The avatar's record lacks the two fields only the interaction rules read:
    # the chase threshold and the state's sprite name.
    mo["threshold"] = 0
    mo["sprite"] = "PLAY"
    player = player_record(mo)
    # G_PlayerReborn's item state: 50 bullets, the fists and the pistol, the
    # ammo ceilings, and the pistol as the pending weapon (p_player_wire).
    player["ammo"] = [50, 0, 0, 0]
    player["maxammo"] = list(DAMAGE_MAXAMMO)
    player["weaponowned"] = [1, 1, 0, 0, 0, 0, 0, 0, 0]
    player["cards"] = [0] * 6
    player["powers"] = [0] * 6
    player["pendingweapon"] = 1
    player["backpack"] = 0
    level.damage_player = player

    sector = mo["sector"]
    print(f"DAMAGE {map_name} script={DAMAGE_SCRIPT} steps={DAMAGE_STEPS}")
    print(f"SETUP rng={level.rng.index} mobjs={len(level.mobjs)} "
          f"sector={sector} floor={level.sectors[sector]['floor']} "
          f"ceiling={level.sectors[sector]['ceiling']} "
          f"player={damage_slot_of(level, mo)}")

    mg0 = damage_spawn(level, 1, mo["x"] + DAMAGE_MG0_DX * FRACUNIT, mo["y"],
                       "floor")
    mg1 = damage_spawn(level, 1, mo["x"], mo["y"] + DAMAGE_MG1_DY * FRACUNIT,
                       "floor")
    mb0 = damage_spawn(level, 30, mo["x"] + DAMAGE_MB0_DX * FRACUNIT, mo["y"],
                       "floor")
    mg2 = damage_spawn(level, 1, mo["x"] + DAMAGE_MG2_DX * FRACUNIT, mo["y"],
                       mo["z"] + DAMAGE_DROP_HEIGHT)
    mh0 = damage_spawn(level, MT_PUFF, mg2["x"], mg2["y"], mo["z"])

    step = [0]
    damage_damage(level, mg2, mh0, mo, 25)
    damage_emit(level, step, "damage-mg2-fallforward")

    for type_, op in ((MT_MISC2, "pickup-health"), (MT_MISC3, "pickup-armor"),
                      (MT_CLIP, "pickup-ammo")):
        damage_spawn(level, type_, mo["x"], mo["y"] + DAMAGE_REACH, "floor")
        movement_try_move(level, mo, mo["x"], mo["y"] + DAMAGE_STEP_FORWARD)
        damage_emit(level, step, op)

    damage_run_thinkers(level)
    level.leveltime += 1
    damage_emit(level, step, "tick-release-items")

    damage_give_armor(player, 1)
    damage_emit(level, step, "give-armor-green")
    damage_damage(level, mo, None, None, 40)
    damage_emit(level, step, "damage-player-40")
    damage_damage(level, mo, mg0, mg0, 30)
    damage_emit(level, step, "damage-player-30-from-mg0")
    damage_damage(level, mo, None, None, 1)
    damage_emit(level, step, "damage-player-1")
    damage_give_armor(player, 2)
    damage_emit(level, step, "give-armor-blue")
    damage_damage(level, mo, None, None, 60)
    damage_emit(level, step, "damage-player-60")
    damage_damage(level, mo, None, None, 5)
    damage_emit(level, step, "damage-player-5")

    armour = damage_spawn(level, MT_MISC0, mo["x"], mo["y"] + DAMAGE_REACH,
                          "floor")
    damage_touch_special(level, armour, mo)
    damage_emit(level, step, "touch-armor-refused")

    damage_damage(level, mg0, mo, mo, 15)
    damage_emit(level, step, "damage-mg0-pain")
    damage_run_thinkers(level)
    level.leveltime += 1
    damage_emit(level, step, "tick-mg0-pain")
    damage_damage(level, mg0, mo, mo, 25)
    damage_emit(level, step, "damage-mg0-kill")
    damage_run_thinkers(level)
    level.leveltime += 1
    damage_emit(level, step, "tick-mg0-death")
    damage_damage(level, mb0, mo, mo, 60)
    damage_emit(level, step, "damage-barrel")
    damage_run_thinkers(level)
    level.leveltime += 1
    damage_emit(level, step, "tick-barrel-death")
    damage_damage(level, mg1, mo, mo, 60)
    damage_emit(level, step, "damage-mg1-gib")
    damage_run_thinkers(level)
    level.leveltime += 1
    damage_emit(level, step, "tick-mg1-gib")

    x = mo["x"] + 16 * FRACUNIT
    z = mo["z"] + DAMAGE_SPRAY_Z
    damage_spawn_blood(level, x, mo["y"], z, 15)
    damage_emit(level, step, "spawn-blood-15")
    damage_spawn_blood(level, x, mo["y"], z, 10)
    damage_emit(level, step, "spawn-blood-10")
    damage_spawn_blood(level, x, mo["y"], z, 5)
    damage_emit(level, step, "spawn-blood-5")
    damage_spawn_puff(level, x, mo["y"], z, ATTACK_MISSILERANGE)
    damage_emit(level, step, "spawn-puff")
    for _ in range(3):
        damage_run_thinkers(level)
        level.leveltime += 1
        damage_emit(level, step, "tick-sprays")

    for index in range(DAMAGE_CRUSH_STEPS):
        level.leveltime = index
        level.sectors[sector]["floor"] += DAMAGE_CRUSH_STEP
        damage_change_sector(level, sector, True)
        damage_emit(level, step, "crush")

    for _ in range(DAMAGE_TICKS_AFTER_DEATH):
        damage_run_thinkers(level)
        level.leveltime += 1
        damage_emit(level, step, "tick-death")

    print(f"SUMMARY steps={step[0]} rng={level.rng.index} "
          f"crushed={level.crushed} kill={player['killcount']} "
          f"item={player['itemcount']} health={player['health']} "
          f"spawns={len(level.mobjs)}")


# --- P_PlayerInSpecialSector ------------------------------------------------
#
# A second implementation of linuxdoom-1.10's P_PlayerInSpecialSector -- the
# per-tic question P_PlayerThink asks about the sector the avatar stands in --
# written from p_spec.c's switch and its two guards, and using this file's own
# P_DamageMobj (``damage_damage``) for the damage it deals. It never calls the C.
#
# WHAT THE RULE IS
#
#   `if (player->mo->z != sector->floorheight) return;` -- a thing still falling
#   through the sector is not on its floor yet and takes nothing.
#
#   `!(leveltime&0x1f)` is the cadence: the effect fires on tics 0, 32, 64, ...
#
#   5   HELLSLIME DAMAGE        10 points; nothing while `powers[pw_ironfeet]`
#   7   NUKAGE DAMAGE            5 points; nothing while `powers[pw_ironfeet]`
#   4   STROBE HURT             20 points
#   16  SUPER HELLSLIME DAMAGE  20 points, both guarded by
#                               `!powers[pw_ironfeet] || (P_Random()<5)` -- so the
#                               suit still *draws* the number every tic and only
#                               makes the hit rare
#   9   SECRET SECTOR           `player->secretcount++` and `sector->special = 0`
#   11  EXIT SUPER DAMAGE      20 points, never shielded (it clears god mode,
#                               which this port has not got), and *after* the
#                               damage `if (player->health <= 10) G_ExitLevel()`.
#                               P_DamageMobj's own "end of game hell" clamp keeps
#                               it from killing: `special == 11 && damage >=
#                               health` becomes `damage = health - 1`.
#
# G_ExitLevel() is `secretexit = false; gameaction = ga_completed`, which this
# file's level state carries as ``exit_request`` -- the same field the line
# specials write.
#
# THE GATE'S FIXTURES
#
# The C harness (tools/specials_check.c) runs one fixture per (map, special,
# suit, starting health) from a fresh load of the map, prints the state after
# each tic and this file reproduces it. The fixture list, the two synthetic
# cases (no episode-1 map carries sector 4 or 5) and the reporting order are
# documented in the harness; both files must agree on them or the gate fails.
#
# `--perturb` on the gate moves one of the three constants below in *this*
# implementation only, which is what makes the comparison able to fail:
#
#   damage     every damage number +1
#   threshold  the exit threshold 10 -> 9
#   ironfeet   the suit ignored, so 5 and 7 damage a suited player

PLAYER_SPECIALS = (4, 5, 7, 9, 11, 16)
SECTOR_DAMAGE_HELLSLIME = 10
SECTOR_DAMAGE_NUKAGE = 5
SECTOR_DAMAGE_STRONG = 20
SECTOR_EXIT_THRESHOLD = 10
SECTOR_DAMAGE_SUIT_ROLL = 5
SECTOR_DAMAGE_MASK = 0x1F
SECTOR_IRONFEET = 3

# Filled in by the command line: the gate's `--perturb` names one of these.
SECTOR_PERTURB = {"damage": 0, "threshold": 0, "ironfeet": 0}

#: T34's two perturbations, applied in the oracle only so the stair and teleport
#: comparisons are shown to be able to fail:
#:
#:   stairs    one extra map unit per step (build8 9, turbo16 17)
#:   teleport  the destination fog 20 -> 21 map units along the teleportman's
#:             angle, and the actor's angle left unchanged
STAIR_PERTURB = {"stairs": 0, "teleport": 0}


def sector_damage_amount(amount: int) -> int:
    """The shipped number, or +1 when the gate perturbs every amount."""
    return amount + SECTOR_PERTURB["damage"]


def player_in_special_sector(level: SpecLevel, player: dict) -> None:
    """P_PlayerInSpecialSector(): the player's tic in a special sector."""
    mo = player["mo"]
    sector_index = mo["sector"]
    sector = level.sectors[sector_index]

    # "Falling, not all the way down yet?"
    if mo["z"] != sector["floor"]:
        return

    special = sector["special"]
    suit = player["powers"][SECTOR_IRONFEET] != 0
    if SECTOR_PERTURB["ironfeet"]:
        suit = False

    if special == 5:
        if not suit and not (level.leveltime & SECTOR_DAMAGE_MASK):
            damage_damage(level, mo, None, None,
                          sector_damage_amount(SECTOR_DAMAGE_HELLSLIME))
    elif special == 7:
        if not suit and not (level.leveltime & SECTOR_DAMAGE_MASK):
            damage_damage(level, mo, None, None,
                          sector_damage_amount(SECTOR_DAMAGE_NUKAGE))
    elif special in (4, 16):
        # `!powers[pw_ironfeet] || (P_Random() < 5)`: short-circuit, so the draw
        # happens only for a suited player -- and then on every tic.
        if not suit or level.rng.next() < SECTOR_DAMAGE_SUIT_ROLL:
            if not (level.leveltime & SECTOR_DAMAGE_MASK):
                damage_damage(level, mo, None, None,
                              sector_damage_amount(SECTOR_DAMAGE_STRONG))
    elif special == 9:
        player["secretcount"] += 1
        sector["special"] = 0
    elif special == 11:
        if not (level.leveltime & SECTOR_DAMAGE_MASK):
            damage_damage(level, mo, None, None,
                          sector_damage_amount(SECTOR_DAMAGE_STRONG))
        threshold = SECTOR_EXIT_THRESHOLD - (1 if SECTOR_PERTURB["threshold"] else 0)
        if player["health"] <= threshold:
            level.exit_request = "normal"
    # Upstream's `I_Error("unknown special")` for every other number: nothing to
    # do, because after P_SpawnSpecials the field can only hold one of the six.


def sector_special_at(level: SpecLevel, special: int) -> int:
    for index, sector in enumerate(level.sectors):
        if sector["special"] == special:
            return index
    return -1


def sector_damage_scenarios(level: SpecLevel) -> list[dict]:
    """The fixture list, in the order the harness prints it."""
    scenarios: list[dict] = []

    def add(special: int, sector: int, synthetic: int, health: int, suit: int,
            tics: int) -> None:
        scenarios.append({"special": special, "sector": sector,
                          "synthetic": synthetic, "health": health,
                          "suit": suit, "tics": tics})

    for special in PLAYER_SPECIALS:
        sector = sector_special_at(level, special)
        synthetic = 0
        if sector < 0 and special in (4, 5):
            sector = sector_special_at(level, 7)
            synthetic = 1
        if sector < 0:
            continue
        add(special, sector, synthetic, 100, 0, 96)
        if special in (4, 16):
            add(special, sector, synthetic, 100, 1, 96)
        if special in (5, 7):
            add(special, sector, synthetic, 100, 1, 64)
        if special == 11:
            add(special, sector, synthetic, 30, 0, 1)
            add(special, sector, synthetic, 45, 1, 33)
            add(special, sector, synthetic, 100, 0, 129)
    return scenarios


def sector_damage_player(level: SpecLevel, sector_index: int, health: int,
                         suit: int) -> dict:
    """A G_PlayerReborn record standing on @p sector_index's floor.

    The C harness moves the avatar with P_CheckPosition and writes the sector's
    own floor into `z`; the rule reads nothing else about the position, so this
    side needs only the same three fields.
    """
    sector = level.sectors[sector_index]
    mo = movement_player(level)
    mo["sector"] = sector_index
    mo["z"] = sector["floor"]
    mo["floorz"] = sector["floor"]
    mo["ceilingz"] = sector["ceiling"]
    mo["health"] = health
    mo["threshold"] = 0
    mo["sprite"] = "PLAY"

    level.mobjs = [mo]
    player = player_record(mo)
    player["health"] = health
    player["powers"] = [0] * 6
    player["ammo"] = [50, 0, 0, 0]
    player["maxammo"] = list(DAMAGE_MAXAMMO)
    player["weaponowned"] = [1, 1, 0, 0, 0, 0, 0, 0, 0]
    player["cards"] = [0] * 6
    player["pendingweapon"] = 1
    player["backpack"] = 0
    if suit:
        player["powers"][SECTOR_IRONFEET] = 35 * 60

    level.damage_player = player
    level.damage_skill = 2          # sk_medium, p_spawn_rules_default
    level.damage_netgame = False
    level.crushed = 0
    return player


def report_sector_damage(lumps: list[Lump], map_name: str) -> None:
    """The `--sector-damage` protocol: the fixtures the C harness runs."""
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)     # the RNG position the level load leaves

    # The fixtures come from a pristine post-spawn load: the runs below move the
    # clock and, for sector 9, clear the number they are about.
    probe = SpecLevel(bsp, RandomState(things.rng))
    spawn_specials(probe)
    scenarios = sector_damage_scenarios(probe)

    for scenario in scenarios:
        level = SpecLevel(bsp, RandomState(things.rng))
        level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}
        spawn_specials(level)
        if scenario["synthetic"]:
            level.sectors[scenario["sector"]]["special"] = scenario["special"]
        player = sector_damage_player(level, scenario["sector"],
                                      scenario["health"], scenario["suit"])

        print(f"DAMAGE map={map_name} sector={scenario['sector']} "
              f"special={scenario['special']} health0={scenario['health']} "
              f"suit={scenario['suit']} tics={scenario['tics']} "
              f"synthetic={scenario['synthetic']}")
        for tic in range(scenario["tics"] + 1):
            print(f"HEALTH {tic} health={player['health']} "
                  f"exit={level.exit_request} secret={player['secretcount']} "
                  f"special={level.sectors[scenario['sector']]['special']}")
            if tic == scenario["tics"]:
                break
            player_in_special_sector(level, player)
            level.leveltime += 1


# --- Weapons and the attack path (D4.2d2) -----------------------------------
#
# A second implementation of linuxdoom-1.10 p_pspr.c (P_SetPsprite,
# P_SetupPsprites, P_MovePsprites, P_CalcSwing, P_BringUpWeapon, P_CheckAmmo,
# P_FireWeapon, P_DropWeapon, P_BulletSlope, P_GunShot and every A_ function in
# that file), of d_items.c's weaponinfo[], and of p_map.c's attack half
# (P_AimLineAttack, P_LineAttack, P_ShootSpecialLine and the two traversers). It
# reuses this file's movement core for the geometry and its damage core for the
# consequences, and never calls the C.
#
# WHAT IS TRANSCRIBED, AND FROM WHERE
#
# ATTACK_WEAPONINFO is a second reading of d_items.c: one row per weapon with its
# ammo pool and the five psprite states. ATTACK_STATES is a second reading of
# info.c's rows for the weapon states, keyed by the statenum_t value the info.h
# enum gives them (S_NULL is 0 and S_LIGHTDONE is 1, then the weapons in
# PUNCH/PISTOL/SGUN/CHAIN/MISSILE/SAW/PLASMA/BFG order); DAMAGE_STATES covers the
# mobj states the script reaches and is extended with the avatar's two attack
# states. Nothing above is read out of lib/doom/info_tables.h, so a disagreement
# between the two is a real defect in one of them.
#
# WHERE THE PLAYER RECORD COMES FROM
#
# Upstream reads `mobj->player`. This port's `player` flag marks the avatar and
# the record is the level's, so the oracle keeps one `level.damage_player`, as the
# damage protocol does, and the psprite layers live on it.

ATTACK_SCRIPT = "weapons-wall-and-target"

# p_local.h's MELEERANGE and MISSILERANGE, and p_map.c's slope band.
ATTACK_MELEERANGE = 64 * FRACUNIT
ATTACK_MISSILERANGE = 32 * 64 * FRACUNIT
ATTACK_AIM_TOP_SLOPE = 100 * FRACUNIT // 160

# p_pspr.c's LOWERSPEED, RAISESPEED, WEAPONBOTTOM, WEAPONTOP and BFGCELLS.
ATTACK_LOWERSPEED = FRACUNIT * 6
ATTACK_RAISESPEED = FRACUNIT * 6
ATTACK_WEAPONBOTTOM = 128 * FRACUNIT
ATTACK_WEAPONTOP = 32 * FRACUNIT
ATTACK_BFGCELLS = 40

# The shipped values of the constants tools/attack_check.py can perturb. The oracle
# always holds these, so a build that changes one and still agrees would mean the
# gate is not comparing what it claims to.
ATTACK_GUNSHOT_DAMAGE = 5
ATTACK_SPREAD_SHIFT = 18

ATTACK_NUMPSPRITES = 2
PS_WEAPON = 0
PS_FLASH = 1

ATTACK_AM_NOAMMO = -1
ATTACK_NOCHANGE = 9

# d_items.c weaponinfo[], by weapon slot: (ammo, upstate, downstate, readystate,
# attackstate, flashstate).
ATTACK_WEAPONINFO = (
    (ATTACK_AM_NOAMMO, 4, 3, 2, 5, 0),        # fist:      PUNCHUP/DOWN/PUNCH/PUNCH1
    (0, 12, 11, 10, 13, 17),                  # pistol
    (1, 20, 19, 18, 21, 30),                  # shotgun
    (0, 51, 50, 49, 52, 55),                  # chaingun
    (3, 59, 58, 57, 60, 63),                  # missile launcher
    (2, 76, 75, 74, 77, 79),                  # plasma rifle
    (2, 83, 82, 81, 84, 88),                  # BFG 9000
    (ATTACK_AM_NOAMMO, 70, 69, 67, 71, 0),    # chainsaw
    (1, 32, 33, 34, 35, 46),                  # super shotgun
)

# statenum: (tics, nextstate, action, sprite name). A second reading of info.c's
# weapon rows; DAMAGE_STATES supplies the mobj rows and this only adds the avatar's
# two attack states.
ATTACK_WEAPON_STATES = {
    1: (0, 0, "A_Light0", "SHTG"),             # S_LIGHTDONE
    2: (1, 2, "A_WeaponReady", "PUNG"),        # S_PUNCH
    3: (1, 3, "A_Lower", "PUNG"),              # S_PUNCHDOWN
    4: (1, 4, "A_Raise", "PUNG"),              # S_PUNCHUP
    5: (4, 6, "NULL", "PUNG"),                 # S_PUNCH1
    6: (4, 7, "A_Punch", "PUNG"),              # S_PUNCH2
    7: (5, 8, "NULL", "PUNG"),                 # S_PUNCH3
    8: (4, 9, "NULL", "PUNG"),                 # S_PUNCH4
    9: (5, 2, "A_ReFire", "PUNG"),             # S_PUNCH5
    10: (1, 10, "A_WeaponReady", "PISG"),      # S_PISTOL
    11: (1, 11, "A_Lower", "PISG"),            # S_PISTOLDOWN
    12: (1, 12, "A_Raise", "PISG"),            # S_PISTOLUP
    13: (4, 14, "NULL", "PISG"),               # S_PISTOL1
    14: (6, 15, "A_FirePistol", "PISG"),       # S_PISTOL2
    15: (4, 16, "NULL", "PISG"),               # S_PISTOL3
    16: (5, 10, "A_ReFire", "PISG"),           # S_PISTOL4
    17: (7, 1, "A_Light1", "PISF"),            # S_PISTOLFLASH
    18: (1, 18, "A_WeaponReady", "SHTG"),      # S_SGUN
    19: (1, 19, "A_Lower", "SHTG"),            # S_SGUNDOWN
    20: (1, 20, "A_Raise", "SHTG"),            # S_SGUNUP
    21: (3, 22, "NULL", "SHTG"),               # S_SGUN1
    22: (7, 23, "A_FireShotgun", "SHTG"),      # S_SGUN2
    23: (5, 24, "NULL", "SHTG"),               # S_SGUN3
    24: (5, 25, "NULL", "SHTG"),               # S_SGUN4
    25: (4, 26, "NULL", "SHTG"),               # S_SGUN5
    26: (5, 27, "NULL", "SHTG"),               # S_SGUN6
    27: (5, 28, "NULL", "SHTG"),               # S_SGUN7
    28: (3, 29, "NULL", "SHTG"),               # S_SGUN8
    29: (7, 18, "A_ReFire", "SHTG"),           # S_SGUN9
    30: (4, 31, "A_Light1", "SHTF"),           # S_SGUNFLASH1
    31: (3, 1, "A_Light2", "SHTF"),            # S_SGUNFLASH2
    49: (1, 49, "A_WeaponReady", "CHGG"),      # S_CHAIN
    50: (1, 50, "A_Lower", "CHGG"),            # S_CHAINDOWN
    51: (1, 51, "A_Raise", "CHGG"),            # S_CHAINUP
    52: (4, 53, "A_FireCGun", "CHGG"),         # S_CHAIN1
    53: (4, 54, "A_FireCGun", "CHGG"),         # S_CHAIN2
    54: (0, 49, "A_ReFire", "CHGG"),           # S_CHAIN3
    55: (5, 1, "A_Light1", "CHGF"),            # S_CHAINFLASH1
    56: (5, 1, "A_Light2", "CHGF"),            # S_CHAINFLASH2
    57: (1, 57, "A_WeaponReady", "MISG"),      # S_MISSILE
    58: (1, 58, "A_Lower", "MISG"),            # S_MISSILEDOWN
    59: (1, 59, "A_Raise", "MISG"),            # S_MISSILEUP
    60: (8, 61, "A_GunFlash", "MISG"),         # S_MISSILE1
    61: (12, 62, "A_FireMissile", "MISG"),     # S_MISSILE2
    62: (0, 57, "A_ReFire", "MISG"),           # S_MISSILE3
    63: (3, 64, "A_Light1", "MISF"),           # S_MISSILEFLASH1
    64: (4, 65, "NULL", "MISF"),               # S_MISSILEFLASH2
    65: (4, 66, "A_Light2", "MISF"),           # S_MISSILEFLASH3
    66: (4, 1, "A_Light2", "MISF"),            # S_MISSILEFLASH4
    67: (4, 68, "A_WeaponReady", "SAWG"),      # S_SAW
    68: (4, 67, "A_WeaponReady", "SAWG"),      # S_SAWB
    69: (1, 69, "A_Lower", "SAWG"),            # S_SAWDOWN
    70: (1, 70, "A_Raise", "SAWG"),            # S_SAWUP
    71: (4, 72, "A_Saw", "SAWG"),              # S_SAW1
    72: (4, 73, "A_Saw", "SAWG"),              # S_SAW2
    73: (0, 67, "A_ReFire", "SAWG"),           # S_SAW3
    74: (1, 74, "A_WeaponReady", "PLSG"),      # S_PLASMA
    75: (1, 75, "A_Lower", "PLSG"),            # S_PLASMADOWN
    76: (1, 76, "A_Raise", "PLSG"),            # S_PLASMAUP
    77: (3, 78, "A_FirePlasma", "PLSG"),       # S_PLASMA1
    78: (20, 74, "A_ReFire", "PLSG"),          # S_PLASMA2
    79: (4, 1, "A_Light1", "PLSF"),            # S_PLASMAFLASH1
    80: (4, 1, "A_Light1", "PLSF"),            # S_PLASMAFLASH2
    81: (1, 81, "A_WeaponReady", "BFGG"),      # S_BFG
    82: (1, 82, "A_Lower", "BFGG"),            # S_BFGDOWN
    83: (1, 83, "A_Raise", "BFGG"),            # S_BFGUP
    84: (20, 85, "A_BFGsound", "BFGG"),        # S_BFG1
    85: (10, 86, "A_GunFlash", "BFGG"),        # S_BFG2
    86: (10, 87, "A_FireBFG", "BFGG"),         # S_BFG3
    87: (20, 81, "A_ReFire", "BFGG"),          # S_BFG4
    88: (11, 89, "A_Light1", "BFGF"),          # S_BFGFLASH1
    89: (6, 1, "A_Light2", "BFGF"),            # S_BFGFLASH2
    154: (12, 149, "NULL", "PLAY"),            # S_PLAY_ATK1
    155: (6, 154, "NULL", "PLAY"),             # S_PLAY_ATK2
}

ATTACK_STATES = {**DAMAGE_STATES, **ATTACK_WEAPON_STATES}

# The weapon actions this port implements; anything else on a psprite state is
# counted as not implemented by the C and has no observable effect, so the oracle
# only needs to know that it changes nothing.
ATTACK_PHYS_ACTIONS = frozenset({
    "A_WeaponReady", "A_ReFire", "A_CheckReload", "A_Lower", "A_Raise",
    "A_GunFlash", "A_FirePistol", "A_FireShotgun", "A_FireCGun", "A_FireMissile",
    "A_FirePlasma", "A_FireBFG", "A_Punch", "A_Light0", "A_Light1", "A_Light2",
})

# P_CheckAmmo's `gamemode` arms: DOOM 1 shareware is the only game this port loads
# (L2 Q3), so the plasma, super-shotgun and BFG preferences can never be chosen.
ATTACK_SHAREWARE = True

# The sky flat's packed name, "F_SKY1": r_defs.h packs a name's eight bytes
# most-significant first, and the p_map.c sky hack compares the same constant.
ATTACK_SKY_FLAT = int.from_bytes(b"F_SKY1\0\0", "big")

# p_mobj.c's P_SpawnPlayerMissile needs the missile's speed; these are the three
# types its callers name. The script cannot reach them (shareware has no rocket
# launcher, plasma rifle or BFG), so nothing below is compared.
ATTACK_MISSILE_SPEED = {
    33: 20 * FRACUNIT,                                  # MT_ROCKET
    34: 25 * FRACUNIT,                                  # MT_PLASMA
    35: 25 * FRACUNIT,                                  # MT_BFG
}



def attack_weapon(weapon: int) -> tuple:
    """d_items.c's weaponinfo[weapon] row; a bad slot answers the fist's."""
    if 0 <= weapon < len(ATTACK_WEAPONINFO):
        return ATTACK_WEAPONINFO[weapon]
    return ATTACK_WEAPONINFO[0]


def attack_new_psprite() -> dict:
    """One pspdef_t at its G_PlayerReborn value: inactive."""
    return {"state": 0, "tics": 0, "sx": 0, "sy": 0}


def attack_level_time(level: "SpecLevel") -> int:
    return level.leveltime


def attack_psprite_action(level: "SpecLevel", player: dict, psp: dict,
                          action: str) -> None:
    """The psprite dispatch: the twelve weapon actions, plus the three lights."""
    if action == "A_WeaponReady":
        attack_weapon_ready(level, player, psp)
    elif action == "A_ReFire":
        attack_refire(level, player)
    elif action == "A_CheckReload":
        attack_check_ammo(level, player)
    elif action == "A_Lower":
        attack_lower(level, player, psp)
    elif action == "A_Raise":
        attack_raise(level, player, psp)
    elif action == "A_GunFlash":
        attack_gun_flash(level, player)
    elif action == "A_FirePistol":
        attack_fire_pistol(level, player)
    elif action == "A_FireShotgun":
        attack_fire_shotgun(level, player)
    elif action == "A_FireCGun":
        attack_fire_cgun(level, player, psp)
    elif action == "A_FireMissile":
        level.player["ammo"][attack_weapon(player["readyweapon"])[0]] -= 1
        if player["mo"] is not None:
            attack_spawn_player_missile(level, player["mo"], MT_ROCKET)
    elif action == "A_FirePlasma":
        attack_fire_plasma(level, player)
    elif action == "A_FireBFG":
        level.player["ammo"][attack_weapon(player["readyweapon"])[0]] -= \
            ATTACK_BFGCELLS
        if player["mo"] is not None:
            attack_spawn_player_missile(level, player["mo"], MT_BFG)
    elif action == "A_Punch":
        attack_punch(level, player)
    elif action == "A_Light0":
        player["extralight"] = 0
    elif action == "A_Light1":
        player["extralight"] = 1
    elif action == "A_Light2":
        player["extralight"] = 2
    # A_Saw, A_FireShotgun2, A_BFGSpray and A_BFGsound are not implemented in the
    # C: it counts them and changes nothing, which is what this branch does.


def attack_set_psprite(level: "SpecLevel", player: dict, position: int,
                       stnum: int) -> None:
    """P_SetPsprite(): enter a state, run its action, follow the zero-tic chain."""
    psp = player["psprites"][position]
    guard = 0
    while True:
        if stnum == 0:
            psp["state"] = 0
            return
        tics, nxt, action, _sprite = ATTACK_STATES[stnum]
        psp["state"] = stnum
        psp["tics"] = tics
        # `if (state->misc1) { ... }`: no shipped row has a non-zero misc1.
        if action != "NULL":
            attack_psprite_action(level, player, psp, action)
            if psp["state"] == 0:
                return
        # `stnum = psp->state->nextstate;` -- the state after the action.
        stnum = ATTACK_STATES[psp["state"]][1]
        if psp["tics"] != 0:
            return
        guard += 1
        if guard > 256:
            return


def attack_setup_psprites(level: "SpecLevel", player: dict) -> None:
    """P_SetupPsprites(): clear both layers, then raise the ready weapon."""
    for position in range(ATTACK_NUMPSPRITES):
        player["psprites"][position] = attack_new_psprite()
    player["pendingweapon"] = player["readyweapon"]
    attack_bring_up_weapon(level, player)
    player["pspritesStarted"] = 1


def attack_move_psprites(level: "SpecLevel", player: dict) -> None:
    """P_MovePsprites(): one tic per layer, then the flash follows the gun."""
    for position in range(ATTACK_NUMPSPRITES):
        psp = player["psprites"][position]
        if psp["state"] == 0:
            continue
        if psp["tics"] != -1:
            psp["tics"] -= 1
            if psp["tics"] == 0:
                attack_set_psprite(level, player, position,
                                   ATTACK_STATES[psp["state"]][1])
    player["psprites"][PS_FLASH]["sx"] = player["psprites"][PS_WEAPON]["sx"]
    player["psprites"][PS_FLASH]["sy"] = player["psprites"][PS_WEAPON]["sy"]


def attack_bring_up_weapon(level: "SpecLevel", player: dict) -> None:
    """P_BringUpWeapon(): start raising the pending weapon from the bottom."""
    if player["pendingweapon"] == ATTACK_NOCHANGE:
        player["pendingweapon"] = player["readyweapon"]
    if player["pendingweapon"] == 7:                       # wp_chainsaw
        player["silentSounds"] += 1                        # sfx_sawup
    newstate = attack_weapon(player["pendingweapon"])[1]
    player["pendingweapon"] = ATTACK_NOCHANGE
    player["psprites"][PS_WEAPON]["sy"] = ATTACK_WEAPONBOTTOM
    attack_set_psprite(level, player, PS_WEAPON, newstate)


def attack_check_ammo(level: "SpecLevel", player: dict) -> bool:
    """P_CheckAmmo(): enough ammo to fire, or pick another weapon."""
    ammo = attack_weapon(player["readyweapon"])[0]
    if player["readyweapon"] == 6:                          # wp_bfg
        count = ATTACK_BFGCELLS
    elif player["readyweapon"] == 8:                        # wp_supershotgun
        count = 2
    else:
        count = 1

    if ammo == ATTACK_AM_NOAMMO or player["ammo"][ammo] >= count:
        return True

    # Upstream's preference order. The plasma, super-shotgun and BFG arms are its
    # `gamemode` tests and are false in shareware, so they are absent here too.
    if player["weaponowned"][3] and player["ammo"][0]:
        player["pendingweapon"] = 3
    elif player["weaponowned"][2] and player["ammo"][1]:
        player["pendingweapon"] = 2
    elif player["ammo"][0]:
        player["pendingweapon"] = 1
    elif player["weaponowned"][7]:
        player["pendingweapon"] = 7
    elif player["weaponowned"][4] and player["ammo"][3]:
        player["pendingweapon"] = 4
    else:
        player["pendingweapon"] = 0

    attack_set_psprite(level, player, PS_WEAPON,
                       attack_weapon(player["readyweapon"])[2])
    return False


def attack_fire_weapon(level: "SpecLevel", player: dict) -> None:
    """P_FireWeapon(): the ammo check, the avatar's attack state, the gun's."""
    if not attack_check_ammo(level, player):
        return
    if player["mo"] is not None:
        damage_set_state(level, player["mo"], 154)          # S_PLAY_ATK1
    attack_set_psprite(level, player, PS_WEAPON,
                       attack_weapon(player["readyweapon"])[4])
    player["noiseAlerts"] += 1
    enemy_noise_alert(level, player["mo"], player["mo"])


def attack_drop_weapon(level: "SpecLevel", player: dict) -> None:
    attack_set_psprite(level, player, PS_WEAPON,
                       attack_weapon(player["readyweapon"])[2])


def attack_weapon_ready(level: "SpecLevel", player: dict, psp: dict) -> None:
    """A_WeaponReady(): leave the attack state, honour a change, fire, or bob."""
    mo = player["mo"]

    if mo is not None and mo["state"] in (154, 155):
        damage_set_state(level, mo, 149)                    # S_PLAY

    if player["readyweapon"] == 7 and psp["state"] == 67:
        player["silentSounds"] += 1                         # sfx_sawidl

    if player["pendingweapon"] != ATTACK_NOCHANGE or not player["health"]:
        attack_set_psprite(level, player, PS_WEAPON,
                           attack_weapon(player["readyweapon"])[2])
        return

    if player["cmd"]["buttons"] & BT_ATTACK:
        if not player["attackdown"] or player["readyweapon"] not in (4, 6):
            player["attackdown"] = 1
            attack_fire_weapon(level, player)
            return
    else:
        player["attackdown"] = 0

    angle = (128 * attack_level_time(level)) & FINEMASK
    psp["sx"] = i32(FRACUNIT + fixed_mul(
        player["bob"], FINE_SINE[angle + FINEANGLES // 4]))
    angle &= FINEANGLES // 2 - 1
    psp["sy"] = i32(ATTACK_WEAPONTOP + fixed_mul(player["bob"], FINE_SINE[angle]))


def attack_refire(level: "SpecLevel", player: dict) -> None:
    """A_ReFire(): fire again without lowering, or stop and re-check."""
    if (player["cmd"]["buttons"] & BT_ATTACK and
            player["pendingweapon"] == ATTACK_NOCHANGE and player["health"]):
        player["refire"] += 1
        attack_fire_weapon(level, player)
    else:
        player["refire"] = 0
        attack_check_ammo(level, player)


def attack_lower(level: "SpecLevel", player: dict, psp: dict) -> None:
    """A_Lower(): walk the gun off the bottom, then swap and raise."""
    psp["sy"] = i32(psp["sy"] + ATTACK_LOWERSPEED)
    if psp["sy"] < ATTACK_WEAPONBOTTOM:
        return
    if player["playerstate"] == PST_DEAD:
        psp["sy"] = ATTACK_WEAPONBOTTOM
        return
    if not player["health"]:
        attack_set_psprite(level, player, PS_WEAPON, 0)
        return
    player["readyweapon"] = player["pendingweapon"]
    attack_bring_up_weapon(level, player)


def attack_raise(level: "SpecLevel", player: dict, psp: dict) -> None:
    """A_Raise(): walk the gun up, then enter its ready state."""
    psp["sy"] = i32(psp["sy"] - ATTACK_RAISESPEED)
    if psp["sy"] > ATTACK_WEAPONTOP:
        return
    psp["sy"] = ATTACK_WEAPONTOP
    attack_set_psprite(level, player, PS_WEAPON,
                       attack_weapon(player["readyweapon"])[3])


def attack_gun_flash(level: "SpecLevel", player: dict) -> None:
    """A_GunFlash(): the flash on its own state row."""
    if player["mo"] is not None:
        damage_set_state(level, player["mo"], 155)          # S_PLAY_ATK2
    attack_set_psprite(level, player, PS_FLASH,
                       attack_weapon(player["readyweapon"])[5])


def attack_fire_pistol(level: "SpecLevel", player: dict) -> None:
    """A_FirePistol(): one bullet, accurate only on the first shot."""
    row = attack_weapon(player["readyweapon"])
    player["silentSounds"] += 1                             # sfx_pistol
    if player["mo"] is not None:
        damage_set_state(level, player["mo"], 155)          # S_PLAY_ATK2
    player["ammo"][row[0]] -= 1
    attack_set_psprite(level, player, PS_FLASH, row[5])
    if player["mo"] is None:
        return
    slope = attack_bullet_slope(level, player["mo"])
    attack_gun_shot(level, player["mo"], slope, not player["refire"])


def attack_fire_shotgun(level: "SpecLevel", player: dict) -> None:
    """A_FireShotgun(): seven inaccurate pellets."""
    row = attack_weapon(player["readyweapon"])
    player["silentSounds"] += 1                             # sfx_shotgn
    if player["mo"] is not None:
        damage_set_state(level, player["mo"], 155)
    player["ammo"][row[0]] -= 1
    attack_set_psprite(level, player, PS_FLASH, row[5])
    if player["mo"] is None:
        return
    slope = attack_bullet_slope(level, player["mo"])
    for _ in range(7):
        attack_gun_shot(level, player["mo"], slope, False)


def attack_fire_cgun(level: "SpecLevel", player: dict, psp: dict) -> None:
    """A_FireCGun(): the sound always, the bullet only with ammo left."""
    row = attack_weapon(player["readyweapon"])
    player["silentSounds"] += 1                             # sfx_pistol
    if not player["ammo"][row[0]]:
        return
    if player["mo"] is not None:
        damage_set_state(level, player["mo"], 155)
    player["ammo"][row[0]] -= 1
    attack_set_psprite(level, player, PS_FLASH,
                       row[5] + psp["state"] - 52)          # flashstate + ps - S_CHAIN1
    if player["mo"] is None:
        return
    slope = attack_bullet_slope(level, player["mo"])
    attack_gun_shot(level, player["mo"], slope, not player["refire"])


def attack_fire_plasma(level: "SpecLevel", player: dict) -> None:
    """A_FirePlasma(): the flash choice is a draw, taken before the missile."""
    row = attack_weapon(player["readyweapon"])
    player["ammo"][row[0]] -= 1
    attack_set_psprite(level, player, PS_FLASH,
                       row[5] + (level.rng.next() & 1))
    if player["mo"] is not None:
        attack_spawn_player_missile(level, player["mo"], MT_PLASMA)


def attack_bullet_slope(level: "SpecLevel", mo: dict) -> int:
    """P_BulletSlope(): the player's angle, then a quarter turn either side."""
    an = mo["angle"]
    slope, found = attack_aim_line_attack(level, mo, an, 16 * 64 * FRACUNIT)
    if found is None:
        an = u32(an + (1 << 26))
        slope, found = attack_aim_line_attack(level, mo, an, 16 * 64 * FRACUNIT)
        if found is None:
            an = u32(an - (2 << 26))
            slope, found = attack_aim_line_attack(level, mo, an,
                                                  16 * 64 * FRACUNIT)
    return slope


def attack_gun_shot(level: "SpecLevel", mo: dict, slope: int,
                    accurate: bool) -> None:
    """P_GunShot(): one bullet. The damage draw is first, the drift's two next."""
    damage = ATTACK_GUNSHOT_DAMAGE * (level.rng.next() % 3 + 1)
    angle = mo["angle"]
    if not accurate:
        angle = u32(angle + (((level.rng.next() - level.rng.next())
                              << ATTACK_SPREAD_SHIFT) & 0xFFFFFFFF))
    attack_line_attack(level, mo, angle, ATTACK_MISSILERANGE, slope, damage)


def attack_punch(level: "SpecLevel", player: dict) -> None:
    """A_Punch(): the fist's damage, its drift, and the turn to face the hit."""
    mo = player["mo"]
    if mo is None:
        return
    damage = (level.rng.next() % 10 + 1) << 1
    if player["powers"][1]:                                 # pw_strength
        damage *= 10
    angle = mo["angle"]
    angle = u32(angle + (((level.rng.next() - level.rng.next()) << 18)
                         & 0xFFFFFFFF))
    slope, target = attack_aim_line_attack(level, mo, angle, ATTACK_MELEERANGE)
    attack_line_attack(level, mo, angle, ATTACK_MELEERANGE, slope, i32(damage))
    if target is not None:
        player["silentSounds"] += 1                         # sfx_punch
        mo["angle"] = point_to_angle(mo["x"], mo["y"], target["x"], target["y"])


def attack_spawn_player_missile(level: "SpecLevel", source: dict,
                                type_: int) -> dict:
    """P_SpawnPlayerMissile(): three aim probes, then the missile."""
    an = source["angle"]
    slope, found = attack_aim_line_attack(level, source, an, 16 * 64 * FRACUNIT)
    if found is None:
        an = u32(an + (1 << 26))
        slope, found = attack_aim_line_attack(level, source, an,
                                              16 * 64 * FRACUNIT)
        if found is None:
            an = u32(an - (2 << 26))
            slope, found = attack_aim_line_attack(level, source, an,
                                                  16 * 64 * FRACUNIT)
    if found is None:
        an = source["angle"]
        slope = 0

    th = damage_spawn(level, type_, source["x"], source["y"],
                      i32(source["z"] + 8 * FRACUNIT))
    row = DAMAGE_MOBJINFO.get(type_)
    if row is not None:
        level.player["silentSounds"] += 1                   # seesound

    th["target"] = source
    th["angle"] = an
    speed = ATTACK_MISSILE_SPEED[type_]
    th["momx"] = i32(fixed_mul(speed,
                               FINE_SINE[(an >> ANGLETOFINESHIFT) + FINEANGLES // 4]))
    th["momy"] = i32(fixed_mul(speed, FINE_SINE[an >> ANGLETOFINESHIFT]))
    th["momz"] = i32(fixed_mul(speed, slope))
    attack_check_missile_spawn(level, th)
    return th


def attack_check_missile_spawn(level: "SpecLevel", th: dict) -> bool:
    """P_CheckMissileSpawn(): jitter, half-step forward, explode if solid."""
    th["tics"] -= level.rng.next() & 3
    if th["tics"] < 1:
        th["tics"] = 1
    th["x"] = i32(th["x"] + (th["momx"] >> 1))
    th["y"] = i32(th["y"] + (th["momy"] >> 1))
    th["z"] = i32(th["z"] + (th["momz"] >> 1))
    if not movement_try_move(level, th, th["x"], th["y"]):
        movement_explode_missile(level, th)
        return False
    return True


def attack_trace_line(level: "SpecLevel", x1: int, y1: int, dx: int, dy: int,
                      index: int) -> int | None:
    """PIT_AddLineIntercepts, over one linedef."""
    line = level.bsp.linedefs[index]
    v1, v2, ldx, ldy = movement_line_geometry(level.bsp, line)

    if dx > 16 * FRACUNIT or dy > 16 * FRACUNIT or \
            dx < -16 * FRACUNIT or dy < -16 * FRACUNIT:
        s1 = movement_point_on_divline_side(v1["x"], v1["y"], x1, y1, dx, dy)
        s2 = movement_point_on_divline_side(v2["x"], v2["y"], x1, y1, dx, dy)
    else:
        s1 = movement_point_on_line_side(x1, y1, v1, ldx, ldy)
        s2 = movement_point_on_line_side(x1 + dx, y1 + dy, v1, ldx, ldy)
    if s1 == s2:
        return None

    frac = movement_intercept_vector(x1, y1, dx, dy, v1["x"], v1["y"], ldx, ldy)
    if frac < 0 or frac > FRACUNIT:
        return None
    return frac


def attack_collect(level: "SpecLevel", x1: int, y1: int, dx: int, dy: int):
    """PIT_AddLineIntercepts and PIT_AddThingIntercepts, over the whole map."""
    entries: list[tuple[int, object, object]] = []          # (frac, thing, line)
    for index in range(len(level.bsp.linedefs)):
        frac = attack_trace_line(level, x1, y1, dx, dy, index)
        if frac is None:
            continue
        entries.append((frac, None, index))

    trace_positive = i32(dx ^ dy) > 0
    for mo in level.mobjs:
        if mo.get("removed"):
            continue
        if trace_positive:
            cx1 = mo["x"] - mo["radius"]
            cy1 = mo["y"] + mo["radius"]
            cx2 = mo["x"] + mo["radius"]
            cy2 = mo["y"] - mo["radius"]
        else:
            cx1 = mo["x"] - mo["radius"]
            cy1 = mo["y"] - mo["radius"]
            cx2 = mo["x"] + mo["radius"]
            cy2 = mo["y"] + mo["radius"]
        s1 = movement_point_on_divline_side(cx1, cy1, x1, y1, dx, dy)
        s2 = movement_point_on_divline_side(cx2, cy2, x1, y1, dx, dy)
        if s1 == s2:
            continue
        frac = movement_intercept_vector(x1, y1, dx, dy, cx1, cy1,
                                         cx2 - cx1, cy2 - cy1)
        if frac < 0 or frac > FRACUNIT:
            continue
        entries.append((frac, mo, None))
    return entries


def attack_run_traverse(level: "SpecLevel", ctx: dict, entries: list,
                        shooting: bool) -> None:
    """P_TraverseIntercepts: the nearest unvisited crossing, over and over."""
    work = [list(entry) for entry in entries]
    for _ in range(len(work)):
        best = INT32_MAX
        chosen = None
        for entry in work:
            if entry[0] < best:
                best = entry[0]
                chosen = entry
        if chosen is None or best > FRACUNIT:
            return
        if chosen[1] is not None:
            keep = (attack_shoot_thing(level, ctx, chosen[0], chosen[1])
                    if shooting else
                    attack_aim_thing(level, ctx, chosen[0], chosen[1]))
        else:
            keep = (attack_shoot_line(level, ctx, chosen[0], chosen[2])
                    if shooting else
                    attack_aim_line(level, ctx, chosen[0], chosen[2]))
        chosen[0] = INT32_MAX
        if not keep:
            return


def attack_side_heights(level: "SpecLevel", line: dict):
    """(front floor, front ceiling, back floor, back ceiling) or None."""
    front = _front_sector(level, line)
    back = _back_sector(level, line)
    if front is None or back is None:
        return None
    return (level.sectors[front]["floor"], level.sectors[front]["ceiling"],
            level.sectors[back]["floor"], level.sectors[back]["ceiling"])


def attack_divide(ctx: dict, numerator: int, dist: int):
    """FixedDiv, with the zero-distance case skipped instead of dividing by 0."""
    if dist == 0:
        return None
    return fixed_div(numerator, dist)


def attack_aim_line(level: "SpecLevel", ctx: dict, frac: int, index: int) -> bool:
    """PTR_AimTraverse's line arm: narrow the aim band."""
    line = level.bsp.linedefs[index]
    if not (line["flags"] & 4):
        return False                                        # stop
    opening = movement_line_opening(level, index)
    if opening["bottom"] >= opening["top"]:
        return False
    heights = attack_side_heights(level, line)
    if heights is None:
        return False
    front_floor, front_ceiling, back_floor, back_ceiling = heights
    dist = fixed_mul(ctx["range"], frac)
    if front_floor != back_floor:
        slope = attack_divide(ctx, opening["bottom"] - ctx["shootz"], dist)
        if slope is not None and slope > ctx["bottomSlope"]:
            ctx["bottomSlope"] = slope
    if front_ceiling != back_ceiling:
        slope = attack_divide(ctx, opening["top"] - ctx["shootz"], dist)
        if slope is not None and slope < ctx["topSlope"]:
            ctx["topSlope"] = slope
    if ctx["topSlope"] <= ctx["bottomSlope"]:
        return False
    return True


def attack_aim_thing(level: "SpecLevel", ctx: dict, frac: int,
                     th: dict) -> bool:
    """PTR_AimTraverse's thing arm: the midpoint of the visible band."""
    if th is ctx["shooter"]:
        return True
    if not (th["flags"] & MF_SHOOTABLE):
        return True
    dist = fixed_mul(ctx["range"], frac)
    top = attack_divide(ctx, th["z"] + th["height"] - ctx["shootz"], dist)
    if top is None or top < ctx["bottomSlope"]:
        return True
    bottom = attack_divide(ctx, th["z"] - ctx["shootz"], dist)
    if bottom is None or bottom > ctx["topSlope"]:
        return True
    if top > ctx["topSlope"]:
        top = ctx["topSlope"]
    if bottom < ctx["bottomSlope"]:
        bottom = ctx["bottomSlope"]
    ctx["aimSlope"] = trunc_div(top + bottom, 2)
    ctx["lineTarget"] = th
    return False


def attack_shoot_line(level: "SpecLevel", ctx: dict, frac_in: int,
                      index: int) -> bool:
    """PTR_ShootTraverse's line arm: trigger a special, then hit or pass."""
    line = level.bsp.linedefs[index]
    if level.line_special[index]:
        attack_shoot_special_line(level, ctx["shooter"], index)

    if not (line["flags"] & 4):
        return attack_hit_line(level, ctx, frac_in, index)

    opening = movement_line_opening(level, index)
    heights = attack_side_heights(level, line)
    if heights is None:
        return attack_hit_line(level, ctx, frac_in, index)
    front_floor, front_ceiling, back_floor, back_ceiling = heights
    dist = fixed_mul(ctx["range"], frac_in)
    if front_floor != back_floor:
        slope = attack_divide(ctx, opening["bottom"] - ctx["shootz"], dist)
        if slope is not None and slope > ctx["aimSlope"]:
            return attack_hit_line(level, ctx, frac_in, index)
    if front_ceiling != back_ceiling:
        slope = attack_divide(ctx, opening["top"] - ctx["shootz"], dist)
        if slope is not None and slope < ctx["aimSlope"]:
            return attack_hit_line(level, ctx, frac_in, index)
    return True


def attack_hit_line(level: "SpecLevel", ctx: dict, frac_in: int,
                    index: int) -> bool:
    """PTR_ShootTraverse's `hitline`: the sky hack, then a puff."""
    frac = frac_in - fixed_div(4 * FRACUNIT, ctx["range"])
    x = i32(ctx["x1"] + fixed_mul(ctx["dx"], frac))
    y = i32(ctx["y1"] + fixed_mul(ctx["dy"], frac))
    z = i32(ctx["shootz"] + fixed_mul(ctx["aimSlope"],
                                      fixed_mul(frac, ctx["range"])))
    line = level.bsp.linedefs[index]
    front = _front_sector(level, line)
    if front is not None and \
            level.sectors[front]["ceilingpic"] == ATTACK_SKY_FLAT:
        if z > level.sectors[front]["ceiling"]:
            return False
        back = _back_sector(level, line)
        if back is not None and \
                level.sectors[back]["ceilingpic"] == ATTACK_SKY_FLAT:
            return False
    damage_spawn_puff(level, x, y, z, ctx["range"])
    return False


def attack_shoot_thing(level: "SpecLevel", ctx: dict, frac_in: int,
                       th: dict) -> bool:
    """PTR_ShootTraverse's thing arm: a puff or blood, then the damage."""
    if th is ctx["shooter"]:
        return True
    if not (th["flags"] & MF_SHOOTABLE):
        return True
    dist = fixed_mul(ctx["range"], frac_in)
    top = attack_divide(ctx, th["z"] + th["height"] - ctx["shootz"], dist)
    if top is None or top < ctx["aimSlope"]:
        return True
    bottom = attack_divide(ctx, th["z"] - ctx["shootz"], dist)
    if bottom is None or bottom > ctx["aimSlope"]:
        return True

    frac = frac_in - fixed_div(10 * FRACUNIT, ctx["range"])
    x = i32(ctx["x1"] + fixed_mul(ctx["dx"], frac))
    y = i32(ctx["y1"] + fixed_mul(ctx["dy"], frac))
    z = i32(ctx["shootz"] + fixed_mul(ctx["aimSlope"],
                                      fixed_mul(frac, ctx["range"])))
    if th["flags"] & MF_NOBLOOD:
        damage_spawn_puff(level, x, y, z, ctx["range"])
    else:
        damage_spawn_blood(level, x, y, z, ctx["damage"])
    if ctx["damage"]:
        damage_damage(level, th, ctx["shooter"], ctx["shooter"], ctx["damage"])
    return False


def attack_aim_line_attack(level: "SpecLevel", shooter: dict, angle: int,
                           distance: int):
    """P_AimLineAttack(): (slope, linetarget)."""
    ctx = {
        "shooter": shooter,
        "shootz": i32(shooter["z"] + (shooter["height"] >> 1) + 8 * FRACUNIT),
        "range": distance,
        "aimSlope": 0,
        "topSlope": ATTACK_AIM_TOP_SLOPE,
        "bottomSlope": -ATTACK_AIM_TOP_SLOPE,
        "lineTarget": None,
        "damage": 0,
    }
    angle >>= ANGLETOFINESHIFT
    x1 = shooter["x"]
    y1 = shooter["y"]
    x2 = i32(x1 + (distance >> FRACBITS) * FINE_SINE[angle + FINEANGLES // 4])
    y2 = i32(y1 + (distance >> FRACBITS) * FINE_SINE[angle])
    ctx["x1"], ctx["y1"] = x1, y1
    ctx["dx"], ctx["dy"] = i32(x2 - x1), i32(y2 - y1)
    entries = attack_collect(level, x1, y1, ctx["dx"], ctx["dy"])
    attack_run_traverse(level, ctx, entries, False)
    if ctx["lineTarget"] is not None:
        return ctx["aimSlope"], ctx["lineTarget"]
    # This port has no pitch: a trace that found nothing aims dead level.
    return 0, None


def attack_line_attack(level: "SpecLevel", shooter: dict, angle: int,
                       distance: int, slope: int, damage: int) -> None:
    """P_LineAttack(): trace a shot and resolve what it hits."""
    ctx = {
        "shooter": shooter,
        "shootz": i32(shooter["z"] + (shooter["height"] >> 1) + 8 * FRACUNIT),
        "range": distance,
        "aimSlope": slope,
        "topSlope": 0,
        "bottomSlope": 0,
        "lineTarget": None,
        "damage": damage,
    }
    angle >>= ANGLETOFINESHIFT
    x1 = shooter["x"]
    y1 = shooter["y"]
    x2 = i32(x1 + (distance >> FRACBITS) * FINE_SINE[angle + FINEANGLES // 4])
    y2 = i32(y1 + (distance >> FRACBITS) * FINE_SINE[angle])
    ctx["x1"], ctx["y1"] = x1, y1
    ctx["dx"], ctx["dy"] = i32(x2 - x1), i32(y2 - y1)
    entries = attack_collect(level, x1, y1, ctx["dx"], ctx["dy"])
    attack_run_traverse(level, ctx, entries, True)


def attack_shoot_special_line(level: "SpecLevel", shooter: dict,
                              index: int) -> None:
    """P_ShootSpecialLine(): p_spec.c owns the trigger rules."""
    if not shooter.get("player"):
        return
    trigger_special_line(level, index, "shoot", shooter)


def attack_player_think(level: "SpecLevel", player: dict, cmd: dict) -> None:
    """P_PlayerThink(), with D4.2d2's weapon half."""
    player["cmd"] = dict(cmd)
    command = player["cmd"]
    player["useTriggered"] = 0

    if not player.get("pspritesStarted"):
        attack_setup_psprites(level, player)

    if player["playerstate"] != PST_LIVE:
        player["unsupportedDeath"] += 1
        return

    mo = player["mo"]
    if mo["reactiontime"]:
        mo["reactiontime"] -= 1
    else:
        player_move_player(level, player)

    player_calc_height(level, player)

    if command["buttons"] & BT_SPECIAL:
        command["buttons"] = 0

    if command["buttons"] & BT_CHANGE:
        attack_weapon_change(player, command)

    if command["buttons"] & BT_USE:
        if not player["usedown"]:
            before = level.stats["triggers"]
            movement_use_lines(level, mo)
            if level.stats["triggers"] != before:
                player["useTriggered"] = 1
                player["useTriggers"] += 1
            player["usedown"] = 1
    else:
        player["usedown"] = 0

    attack_move_psprites(level, player)

    if player["powers"][1]:                                 # pw_strength
        player["powers"][1] += 1
    if player["powers"][0]:                                 # pw_invulnerability
        player["powers"][0] -= 1
    if player["powers"][2]:                                 # pw_invisibility
        player["powers"][2] -= 1
        if player["powers"][2] == 0:
            mo["flags"] &= ~MF_SHADOW
    if player["powers"][5]:                                 # pw_infrared
        player["powers"][5] -= 1
    if player["powers"][3]:                                 # pw_ironfeet
        player["powers"][3] -= 1

    if player["damagecount"]:
        player["damagecount"] -= 1
    if player["bonuscount"]:
        player["bonuscount"] -= 1

    player["fixedcolormap"] = 0


def attack_weapon_change(player: dict, cmd: dict) -> None:
    """P_PlayerThink's BT_CHANGE block."""
    new = (cmd["buttons"] & BT_WEAPONMASK) >> BT_WEAPONSHIFT
    if new == 0 and player["weaponowned"][7] and \
            not (player["readyweapon"] == 7 and player["powers"][1]):
        new = 7
    # The super-shotgun swap is `gamemode == commercial`: absent in shareware.
    if 0 <= new < len(player["weaponowned"]) and player["weaponowned"][new] and \
            new != player["readyweapon"]:
        if new not in (5, 6):                               # plasma, BFG
            player["pendingweapon"] = new



def attack_print_player(level: "SpecLevel") -> None:
    """The player line: ammo, both weapon slots, the fire latch and the counters."""
    player = level.damage_player
    weapons = 0
    for index, owned in enumerate(player["weaponowned"]):
        if owned:
            weapons |= 1 << index
    print(f"PLAYER hp={player['health']} "
          f"ammo={','.join(str(a) for a in player['ammo'])} w={weapons} "
          f"pend={player['pendingweapon']} ready={player['readyweapon']} "
          f"refire={player['refire']} atk={player['attackdown']} "
          f"lite={player['extralight']} snd={player['silentSounds']} "
          f"noise={player['noiseAlerts']}")


def attack_print_psprites(player: dict) -> None:
    for position, psp in enumerate(player["psprites"]):
        print(f"PSP {position} state={psp['state']} tics={psp['tics']} "
              f"sx={psp['sx']} sy={psp['sy']}")


def attack_emit(level: "SpecLevel", step: list[int], op: str) -> None:
    print(f"STEP {step[0]} op={op} rng={level.rng.index}")
    attack_print_psprites(level.damage_player)
    for slot, mo in enumerate(level.mobjs):
        damage_print_mobj(level, slot, mo)
    attack_print_player(level)
    step[0] += 1


def report_attack(lumps: list[Lump], map_name: str) -> None:
    """The `--attack` protocol: the four shareware weapons against a real map."""
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)                 # the RNG position the load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.leveltime = 0
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}
    level.pickups = True
    level.damage_skill = 2                      # sk_medium, p_spawn_rules_default
    level.damage_netgame = False
    level.crushed = 0
    level.mobj_states = ATTACK_STATES
    level.stats["triggers"] = 0

    mo = movement_player(level, use_start_angle=True)
    level.mobjs = [mo]
    mo["threshold"] = 0
    mo["sprite"] = "PLAY"
    # T14's weapon script needs a stationary damage sink, and T15b makes A_Chase
    # real: its first test is `!(actor->target->flags & MF_SHOOTABLE)`. An avatar
    # without the bit leaves the monster re-acquiring an invalid target on every
    # run state, so it never steps out from under a shot aimed at where it stood
    # and never fires back. The avatar is never damaged in this script;
    # enemy_check is the gate whose monster does chase.
    mo["flags"] &= ~MF_SHOOTABLE
    player = player_record(mo)
    # G_PlayerReborn's item state (p_player_wire): 50 bullets, the fists and the
    # pistol, the ammo ceilings, and the pistol in both weapon slots.
    player["ammo"] = [50, 0, 0, 0]
    player["maxammo"] = list(DAMAGE_MAXAMMO)
    player["weaponowned"] = [1, 1, 0, 0, 0, 0, 0, 0, 0]
    player["cards"] = [0] * 6
    player["powers"] = [0] * 6
    player["readyweapon"] = 1                   # wp_pistol, as G_PlayerReborn leaves
    player["pendingweapon"] = 1
    player["backpack"] = 0
    player["psprites"] = [attack_new_psprite(), attack_new_psprite()]
    player["pspritesStarted"] = 0
    player["silentSounds"] = 0
    player["noiseAlerts"] = 0
    level.damage_player = player

    # The two weapons P_GiveWeapon hands over, in the C's order: each newly owned
    # one also sets `pendingweapon`, which is why the last one given wins until
    # P_SetupPsprites replaces it with the weapon in hand.
    damage_give_weapon(level, player, 2, False)             # shotgun
    damage_give_weapon(level, player, 3, False)             # chaingun
    player["ammo"] = [50, 20, 0, 0]

    sector = mo["sector"]
    print(f"ATTACK {map_name} script={ATTACK_SCRIPT}")
    print(f"SETUP rng={level.rng.index} mobjs={len(level.mobjs)} "
          f"sector={sector} floor={level.sectors[sector]['floor']} "
          f"ceiling={level.sectors[sector]['ceiling']} "
          f"player={damage_slot_of(level, mo)}")

    # The target's spot: 40 map units along the avatar's spawn facing, computed
    # once so the second target lands where the first stood.
    fine = mo["angle"] >> ANGLETOFINESHIFT
    target_x = i32(mo["x"] + 40 * FINE_SINE[fine + FINEANGLES // 4])
    target_y = i32(mo["y"] + 40 * FINE_SINE[fine])
    target = damage_spawn(level, MT_POSSESSED, target_x, target_y, "floor")
    # Aim at it exactly, so the shot's direction does not depend on the THING
    # record's angle.
    mo["angle"] = point_to_angle(mo["x"], mo["y"], target["x"], target["y"])

    step = [0]

    def tic(op: str, buttons: int = 0) -> None:
        cmd = {"forwardmove": 0, "sidemove": 0, "angleturn": 0,
               "buttons": buttons}
        attack_player_think(level, player, cmd)
        damage_run_thinkers(level)
        run_tic(level)
        level.leveltime += 1
        attack_emit(level, step, op)

    attack_emit(level, step, "setup-weapons")
    attack_setup_psprites(level, player)
    attack_emit(level, step, "setup-psprites")

    for _ in range(15):
        tic("raise-pistol")
    tic("fire-pistol", BT_ATTACK)
    for _ in range(14):
        tic("pistol-hold", BT_ATTACK)
    tic("pistol-release")

    tic("change-shotgun", BT_CHANGE | (2 << BT_WEAPONSHIFT))
    for _ in range(15):
        tic("lower-for-shotgun")
    for _ in range(16):
        tic("raise-shotgun")
    tic("fire-shotgun", BT_ATTACK)
    for _ in range(20):
        tic("shotgun-hold", BT_ATTACK)
    tic("shotgun-release")

    tic("change-chaingun", BT_CHANGE | (3 << BT_WEAPONSHIFT))
    for _ in range(15):
        tic("lower-for-chaingun")
    for _ in range(16):
        tic("raise-chaingun")
    tic("fire-chaingun", BT_ATTACK)
    for _ in range(10):
        tic("chaingun-hold", BT_ATTACK)

    # One clip left, then the trigger keeps going: the gun clicks empty, A_ReFire
    # asks P_CheckAmmo, and the preference order picks the shotgun.
    player["ammo"][0] = 1
    attack_emit(level, step, "clips-one")
    for _ in range(40):
        tic("chaingun-dry", BT_ATTACK)
    tic("dry-release")

    tic("change-fist", BT_CHANGE | (0 << BT_WEAPONSHIFT))
    for _ in range(15):
        tic("lower-for-fist")
    for _ in range(16):
        tic("raise-fist")

    # A fresh target where the first stood: the fist's trace passes the corpse
    # without a blood spray, so the punch needs something that is still alive.
    target2 = damage_spawn(level, MT_POSSESSED, target_x, target_y, "floor")
    mo["angle"] = point_to_angle(mo["x"], mo["y"], target2["x"], target2["y"])
    attack_emit(level, step, "target-two")
    tic("fire-fist", BT_ATTACK)
    for _ in range(6):
        tic("fist-hold", BT_ATTACK)
    tic("fist-release")

    puffs = sum(1 for each in level.mobjs if each["type"] == MT_PUFF)
    bloods = sum(1 for each in level.mobjs if each["type"] == MT_BLOOD)
    print(f"SUMMARY steps={step[0]} rng={level.rng.index} puffs={puffs} "
          f"bloods={bloods} targetHp={target['health']} "
          f"target2Hp={target2['health']} mobjs={len(level.mobjs)} "
          f"ready={player['readyweapon']} snd={player['silentSounds']} "
          f"noise={player['noiseAlerts']} shots={level.stats['triggers']}")


# --- Sight, sound and waking (T15a) ----------------------------------------


ENEMY_SCRIPT = "chase-and-attack"
ENEMY_TICS = 40
ENEMY_FIRE_TIC = 20
ENEMY_PAIR_MOD = 13
ENEMY_MONSTER = 1                                     # MT_POSSESSED

# The seesound numbers A_Look's switch draws for: sfx_posit1..3 (36..38) and
# sfx_bgsit1..2 (39..40), from linuxdoom-1.10 sounds.h. The types that carry one
# of them are the zombie and imp families; every other see sound takes the
# default arm and consumes no random number.
ENEMY_SEE_SOUND = {1: 36, 2: 37, 10: 37, 11: 39}


def sight_divline_side(x: int, y: int, node: dict) -> int:
    """P_DivlineSide(): 0 front, 1 back, 2 on. Keeps upstream's x==node->y typo."""
    if node["dx"] == 0:
        if x == node["x"]:
            return 2
        if x <= node["x"]:
            return 1 if node["dy"] > 0 else 0
        return 1 if node["dy"] < 0 else 0
    if node["dy"] == 0:
        if x == node["y"]:
            return 2
        if y <= node["y"]:
            return 1 if node["dx"] < 0 else 0
        return 1 if node["dx"] > 0 else 0

    dx = i32(x - node["x"])
    dy = i32(y - node["y"])
    left = i32((node["dy"] >> FRACBITS) * (dx >> FRACBITS))
    right = i32((dy >> FRACBITS) * (node["dx"] >> FRACBITS))

    if right < left:
        return 0
    if left == right:
        return 2
    return 1


def sight_intercept(trace: dict, line: dict) -> int:
    """P_InterceptVector2(): the fractional intercept along the trace.

    Upstream's signature is `P_InterceptVector2 (divline_t* v2, divline_t* v1)`
    and it is called as `P_InterceptVector2 (&strace, &divl)`, so `v2` is the
    trace and `v1` the line. Every factor in the numerator is the line's.
    """
    den = i32(fixed_mul(line["dy"] >> 8, trace["dx"]) -
              fixed_mul(line["dx"] >> 8, trace["dy"]))
    if den == 0:
        return 0
    num = i32(fixed_mul(i32(line["x"] - trace["x"]) >> 8, line["dy"]) +
              fixed_mul(i32(trace["y"] - line["y"]) >> 8, line["dx"]))
    return fixed_div(num, den)


def sight_check(level: "SpecLevel", t1: dict, t2: dict) -> bool:
    """P_CheckSight(): the REJECT pre-test, then the BSP walk."""
    bsp = level.bsp
    sectors = level.sectors
    s1, s2 = t1["sector"], t2["sector"]
    pnum = s1 * len(sectors) + s2
    bytenum, bitnum = pnum >> 3, 1 << (pnum & 7)
    if bytenum < len(bsp.reject) and (bsp.reject[bytenum] & bitnum):
        return False

    height1 = t1["height"]
    sightzstart = i32(t1["z"] + height1 - (height1 >> 2))
    topslope = i32(i32(t2["z"] + t2["height"]) - sightzstart)
    bottomslope = i32(t2["z"] - sightzstart)

    strace = {"x": t1["x"], "y": t1["y"],
              "dx": i32(t2["x"] - t1["x"]), "dy": i32(t2["y"] - t1["y"])}
    t2x, t2y = t2["x"], t2["y"]

    def cross_subsector(num: int) -> bool:
        nonlocal topslope, bottomslope
        subsector = bsp.ssectors[num]
        for seg in bsp.segs[subsector["firstseg"]:
                           subsector["firstseg"] + subsector["numsegs"]]:
            line = bsp.linedefs[seg["linedef"]]
            v1 = bsp.vertexes[line["v1"]]
            v2 = bsp.vertexes[line["v2"]]
            if sight_divline_side(v1["x"], v1["y"], strace) == \
                    sight_divline_side(v2["x"], v2["y"], strace):
                continue
            divl = {"x": v1["x"], "y": v1["y"],
                    "dx": i32(v2["x"] - v1["x"]),
                    "dy": i32(v2["y"] - v1["y"])}
            if sight_divline_side(strace["x"], strace["y"], divl) == \
                    sight_divline_side(t2x, t2y, divl):
                continue
            if not (line["flags"] & 4):
                return False
            if seg["side"] == 0:
                front = bsp.sidedefs[line["side0"]]["sector"]
                back = bsp.sidedefs[line["side1"]]["sector"]
            else:
                front = bsp.sidedefs[line["side1"]]["sector"]
                back = bsp.sidedefs[line["side0"]]["sector"]
            front_floor = sectors[front]["floor"]
            front_ceiling = sectors[front]["ceiling"]
            back_floor = sectors[back]["floor"]
            back_ceiling = sectors[back]["ceiling"]
            if front_floor == back_floor and front_ceiling == back_ceiling:
                continue
            opentop = min(front_ceiling, back_ceiling)
            openbottom = max(front_floor, back_floor)
            if openbottom >= opentop:
                return False
            frac = sight_intercept(strace, divl)
            if front_floor != back_floor:
                slope = fixed_div(i32(openbottom - sightzstart), frac)
                if slope > bottomslope:
                    bottomslope = slope
            if front_ceiling != back_ceiling:
                slope = fixed_div(i32(opentop - sightzstart), frac)
                if slope < topslope:
                    topslope = slope
            if topslope <= bottomslope:
                return False
        return True

    def cross_node(num: int) -> bool:
        if num & 0x8000:
            return cross_subsector(0 if num == 0xFFFF else (num & 0x7FFF))
        node = bsp.nodes[num]
        side = sight_divline_side(strace["x"], strace["y"], node)
        if side == 2:
            side = 0
        if not cross_node(node["children"][side]):
            return False
        if side == sight_divline_side(t2x, t2y, node):
            return True
        return cross_node(node["children"][side ^ 1])

    if not bsp.nodes:
        return cross_subsector(0)
    return cross_node(len(bsp.nodes) - 1)


def enemy_sound_recursive(level: "SpecLevel", sector: int, soundblocks: int,
                          target) -> None:
    """P_RecursiveSound(): flood one sector and its open neighbours."""
    state = level.sound[sector]
    if state["valid"] == level.sound_validcount and \
            state["traversed"] <= soundblocks + 1:
        return
    state["valid"] = level.sound_validcount
    state["traversed"] = soundblocks + 1
    state["target"] = target

    for index, line in enumerate(level.bsp.linedefs):
        if not (line["flags"] & 4):
            continue
        front = _front_sector(level, line)
        back = _back_sector(level, line)
        if front is None or back is None:
            continue
        if front != sector and back != sector:
            continue
        opening = movement_line_opening(level, index)
        if opening["range"] <= 0:
            continue                                    # closed door
        other = back if front == sector else front
        if line["flags"] & ML_SOUNDBLOCK:
            if not soundblocks:
                enemy_sound_recursive(level, other, 1, target)
        else:
            enemy_sound_recursive(level, other, soundblocks, target)


def enemy_noise_alert(level: "SpecLevel", target, emitter) -> None:
    """P_NoiseAlert(): `soundtarget = target; validcount++; flood(emitter)`."""
    level.sound_validcount = (level.sound_validcount + 1) & 0xFFFFFFFF
    enemy_sound_recursive(level, emitter["sector"], 0, target)


def enemy_look_for_players(level: "SpecLevel", mo: dict, allaround: bool) -> bool:
    """P_LookForPlayers(): upstream's four-slot scan over this port's one player."""
    player = level.damage_player
    if player is None:
        return False

    c = 0
    stop = (mo["lastlook"] - 1) & 3
    while True:
        slot = mo["lastlook"]
        if level.player_in_game[slot]:
            if c == 2 or slot == stop:
                return False
            c += 1
            if slot == 0:
                if player["health"] > 0:
                    pmo = player["mo"]
                    if sight_check(level, mo, pmo):
                        if not allaround:
                            an = u32(point_to_angle(mo["x"], mo["y"],
                                                    pmo["x"], pmo["y"]) -
                                     mo["angle"])
                            if ANG90 < an < ANG270:
                                dist = movement_aprox_distance(
                                    pmo["x"] - mo["x"], pmo["y"] - mo["y"])
                                if dist > ATTACK_MELEERANGE:
                                    mo["lastlook"] = (slot + 1) & 3
                                    continue
                        mo["target"] = pmo
                        return True
        mo["lastlook"] = (slot + 1) & 3


def enemy_seeyou(level: "SpecLevel", mo: dict) -> None:
    """A_Look's `seeyou:` label: the discarded sound draw, then the see state."""
    if ENEMY_SEE_SOUND.get(mo["type"], 0):
        level.rng.next()                                # sound discarded, draw kept
    damage_set_state(level, mo, damage_mobj(mo["type"])["seestate"])


def enemy_look(level: "SpecLevel", mo: dict) -> None:
    """A_Look(): the sector's sound target first, then the sight scan."""
    mo["threshold"] = 0
    targ = level.sound[mo["sector"]]["target"]
    if targ is not None and (targ["flags"] & MF_SHOOTABLE):
        mo["target"] = targ
        if mo["flags"] & MF_AMBUSH:
            if sight_check(level, mo, targ):
                enemy_seeyou(level, mo)
            return
        enemy_seeyou(level, mo)
        return
    if not enemy_look_for_players(level, mo, False):
        return
    enemy_seeyou(level, mo)


# --- Chase and attack (T15b) -------------------------------------------------
#
# A_Chase, P_Move/P_TryWalk/P_NewChaseDir, P_CheckMeleeRange/P_CheckMissileRange,
# A_FaceTarget, A_PosAttack, A_TroopAttack, P_SpawnMissile/P_CheckMissileSpawn,
# A_Explode and P_RadiusAttack. A second reading of linuxdoom-1.10's p_enemy.c,
# p_mobj.c and p_map.c, written from upstream rather than from the C.
#
# THE WALK SPEED IS AN INTEGER
#
# A monster's mobjinfo `speed` is 8 map units, not 8*FRACUNIT: upstream's P_Move
# multiplies it by the 16.16 xspeed[] table with a plain integer product. A
# missile's speed is 16.16 and P_SpawnMissile uses FixedMul. The two shapes are
# upstream's and both are kept.

ENEMY_CHASE_TICS = 60
ENEMY_COMBAT_TICS = 96
# The zombieman's first shot needs its reactiontime to run out and a chase window
# to land on movecount == 0; from 224 units that is not guaranteed inside 96
# tics, so this one scene runs longer.
ENEMY_HITSCAN_TICS = 160
ENEMY_KILL_TIC = 20
ENEMY_IMP = 11
ENEMY_TROOPSHOT = 31

# A_Chase's one-step turn: upstream subtracts or adds `ANG90/2` (45 degrees)
# until the facing lands on the 45-degree grid `angle &= 7<<29` keeps.
ENEMY_TURN = 0x20000000

# dirtype_t, upstream's order.
ENEMY_EAST = 0
ENEMY_NORTHEAST = 1
ENEMY_NORTH = 2
ENEMY_NORTHWEST = 3
ENEMY_WEST = 4
ENEMY_SOUTHWEST = 5
ENEMY_SOUTH = 6
ENEMY_SOUTHEAST = 7
ENEMY_NODIR = 8

ENEMY_OPPOSITE = (ENEMY_WEST, ENEMY_SOUTHWEST, ENEMY_SOUTH, ENEMY_SOUTHEAST,
                  ENEMY_EAST, ENEMY_NORTHEAST, ENEMY_NORTH, ENEMY_NORTHWEST,
                  ENEMY_NODIR)
ENEMY_DIAGS = (ENEMY_NORTHWEST, ENEMY_NORTHEAST, ENEMY_SOUTHWEST,
               ENEMY_SOUTHEAST)
ENEMY_XSPEED = (FRACUNIT, 47000, 0, -47000, -FRACUNIT, -47000, 0, 47000)
ENEMY_YSPEED = (0, 47000, FRACUNIT, 47000, 0, -47000, -FRACUNIT, -47000)

# mobjtype: (speed, reactiontime, meleestate, missilestate, activesound,
#            damage). MT_POSSESSED and MT_TROOP walk at 8; the fireball flies at
#            10*FRACUNIT. The two attack states are the same number for the imp.
ENEMY_MOBJINFO = {
    1: (8, 8, 0, 184, 75, 0),                   # MT_POSSESSED
    11: (8, 8, 452, 452, 76, 0),                # MT_TROOP
    31: (10 * FRACUNIT, 8, 0, 0, 0, 3),         # MT_TROOPSHOT
}


def enemy_check_melee_range(level: "SpecLevel", actor: dict) -> bool:
    """P_CheckMeleeRange(): 64 units less 20, widened by the target's radius."""
    if actor.get("target") is None:
        return False
    target = actor["target"]
    dist = movement_aprox_distance(target["x"] - actor["x"],
                                   target["y"] - actor["y"])
    if dist >= ATTACK_MELEERANGE - 20 * FRACUNIT + target["radius"]:
        return False
    return sight_check(level, actor, target)


def enemy_check_missile_range(level: "SpecLevel", actor: dict) -> bool:
    """P_CheckMissileRange(): sight, then the walk distance's random gate.

    The four type-specific arms upstream carries (MT_VILE's 14*64 cap,
    MT_UNDEAD's 196 floor and halving, the cyborg/spider/skull halving and the
    cyborg's 160 cap) name types this port does not simulate; the two E1M1
    enemies take only the generic path.
    """
    if actor.get("target") is None:
        return False
    if not sight_check(level, actor, actor["target"]):
        return False
    if actor["flags"] & MF_JUSTHIT:
        actor["flags"] &= ~MF_JUSTHIT
        return True
    if actor["reactiontime"]:
        return False
    dist = movement_aprox_distance(actor["x"] - actor["target"]["x"],
                                   actor["y"] - actor["target"]["y"])
    dist -= 64 * FRACUNIT
    if not ENEMY_MOBJINFO[actor["type"]][2]:
        dist -= 128 * FRACUNIT
    dist >>= 16
    if dist > 200:
        dist = 200
    if level.rng.next() < dist:
        return False
    return True


def enemy_move(level: "SpecLevel", actor: dict) -> bool:
    """P_Move(): one eight-unit step in `movedir` through P_TryMove."""
    if actor["movedir"] == ENEMY_NODIR:
        return False
    if actor["movedir"] < 0 or actor["movedir"] >= 8:
        actor["movedir"] = ENEMY_NODIR
        return False
    speed = ENEMY_MOBJINFO[actor["type"]][0]
    tryx = i32(actor["x"] + speed * ENEMY_XSPEED[actor["movedir"]])
    tryy = i32(actor["y"] + speed * ENEMY_YSPEED[actor["movedir"]])
    if not movement_try_move(level, actor, tryx, tryy):
        # Upstream opens a door it bumps and returns true; this port has no
        # spechit list exposed by P_TryMove, so a blocked monster stays blocked.
        return False
    if not (actor["flags"] & MF_FLOAT):
        actor["z"] = actor["floorz"]
    return True


def enemy_try_walk(level: "SpecLevel", actor: dict) -> bool:
    """P_TryWalk(): move, then re-arm the zig-zag counter."""
    if not enemy_move(level, actor):
        return False
    actor["movecount"] = level.rng.next() & 15
    return True


def enemy_new_chase_dir(level: "SpecLevel", actor: dict) -> None:
    """P_NewChaseDir(): the direct diagonal, the two axes, then a hunt."""
    if actor.get("target") is None:
        return
    olddir = actor["movedir"]
    turnaround = ENEMY_OPPOSITE[olddir]
    deltax = i32(actor["target"]["x"] - actor["x"])
    deltay = i32(actor["target"]["y"] - actor["y"])

    d = [ENEMY_NODIR, ENEMY_NODIR, ENEMY_NODIR]
    if deltax > 10 * FRACUNIT:
        d[1] = ENEMY_EAST
    elif deltax < -10 * FRACUNIT:
        d[1] = ENEMY_WEST
    else:
        d[1] = ENEMY_NODIR
    if deltay < -10 * FRACUNIT:
        d[2] = ENEMY_SOUTH
    elif deltay > 10 * FRACUNIT:
        d[2] = ENEMY_NORTH
    else:
        d[2] = ENEMY_NODIR

    if d[1] != ENEMY_NODIR and d[2] != ENEMY_NODIR:
        actor["movedir"] = ENEMY_DIAGS[((1 if deltay < 0 else 0) << 1) +
                                        (1 if deltax > 0 else 0)]
        if actor["movedir"] != turnaround and enemy_try_walk(level, actor):
            return

    if level.rng.next() > 200 or abs(deltay) > abs(deltax):
        d[1], d[2] = d[2], d[1]
    if d[1] == turnaround:
        d[1] = ENEMY_NODIR
    if d[2] == turnaround:
        d[2] = ENEMY_NODIR
    if d[1] != ENEMY_NODIR:
        actor["movedir"] = d[1]
        if enemy_try_walk(level, actor):
            return
    if d[2] != ENEMY_NODIR:
        actor["movedir"] = d[2]
        if enemy_try_walk(level, actor):
            return
    if olddir != ENEMY_NODIR:
        actor["movedir"] = olddir
        if enemy_try_walk(level, actor):
            return

    if level.rng.next() & 1:
        for tdir in range(ENEMY_EAST, ENEMY_SOUTHEAST + 1):
            if tdir != turnaround:
                actor["movedir"] = tdir
                if enemy_try_walk(level, actor):
                    return
    else:
        for tdir in range(ENEMY_SOUTHEAST, ENEMY_EAST - 1, -1):
            if tdir != turnaround:
                actor["movedir"] = tdir
                if enemy_try_walk(level, actor):
                    return

    if turnaround != ENEMY_NODIR:
        actor["movedir"] = turnaround
        if enemy_try_walk(level, actor):
            return
    actor["movedir"] = ENEMY_NODIR


def enemy_face_target(level: "SpecLevel", actor: dict) -> None:
    """A_FaceTarget(): exact bearing, and the fuzzy-target jitter."""
    if actor.get("target") is None:
        return
    actor["flags"] &= ~MF_AMBUSH
    actor["angle"] = point_to_angle(actor["x"], actor["y"],
                                    actor["target"]["x"],
                                   actor["target"]["y"])
    if actor["target"]["flags"] & MF_SHADOW:
        actor["angle"] = u32(actor["angle"] +
                             ((level.rng.next() - level.rng.next()) << 21))


def enemy_chase(level: "SpecLevel", actor: dict) -> None:
    """A_Chase(): reaction time, the 45-degree turn, melee, missile, walk."""
    info = ENEMY_MOBJINFO[actor["type"]]
    if actor["reactiontime"]:
        actor["reactiontime"] -= 1
    if actor["threshold"]:
        if actor.get("target") is None or actor["target"]["health"] <= 0:
            actor["threshold"] = 0
        else:
            actor["threshold"] -= 1

    if actor["movedir"] < 8:
        actor["angle"] &= 7 << 29
        delta = i32(actor["angle"] - (actor["movedir"] << 29))
        if delta > 0:
            actor["angle"] = u32(actor["angle"] - ENEMY_TURN)
        elif delta < 0:
            actor["angle"] = u32(actor["angle"] + ENEMY_TURN)

    if actor.get("target") is None or \
            not (actor["target"]["flags"] & MF_SHOOTABLE):
        if enemy_look_for_players(level, actor, True):
            return
        damage_set_state(level, actor, damage_mobj(actor["type"])["spawnstate"])
        return

    if actor["flags"] & MF_JUSTATTACKED:
        actor["flags"] &= ~MF_JUSTATTACKED
        enemy_new_chase_dir(level, actor)
        return

    if info[2] and enemy_check_melee_range(level, actor):
        damage_set_state(level, actor, info[2])
        return

    if info[3]:
        if not actor["movecount"] and enemy_check_missile_range(level, actor):
            damage_set_state(level, actor, info[3])
            actor["flags"] |= MF_JUSTATTACKED
            return

    if level.damage_netgame and not actor["threshold"] and \
            not sight_check(level, actor, actor["target"]):
        if enemy_look_for_players(level, actor, True):
            return

    actor["movecount"] -= 1
    if actor["movecount"] < 0 or not enemy_move(level, actor):
        enemy_new_chase_dir(level, actor)

    # The active sound's draw: both E1M1 enemies name one, so every A_Chase
    # consumes a number even though no audio exists (S2).
    if info[4]:
        level.rng.next()


def enemy_pos_attack(level: "SpecLevel", actor: dict) -> None:
    """A_PosAttack(): the zombieman's pistol -- aim, drift, 3..15 points."""
    if actor.get("target") is None:
        return
    enemy_face_target(level, actor)
    angle = actor["angle"]
    slope, _target = attack_aim_line_attack(level, actor, angle,
                                            ATTACK_MISSILERANGE)
    angle = u32(angle + ((level.rng.next() - level.rng.next()) << 20))
    damage = ((level.rng.next() % 5) + 1) * 3
    attack_line_attack(level, actor, angle, ATTACK_MISSILERANGE, slope, damage)


def enemy_troop_attack(level: "SpecLevel", actor: dict) -> None:
    """A_TroopAttack(): the claw within range, else the fireball."""
    if actor.get("target") is None:
        return
    enemy_face_target(level, actor)
    if enemy_check_melee_range(level, actor):
        damage = (level.rng.next() % 8 + 1) * 3
        damage_damage(level, actor["target"], actor, actor, damage)
        return
    enemy_spawn_missile(level, actor, actor["target"], ENEMY_TROOPSHOT)


def enemy_check_missile_spawn(level: "SpecLevel", th: dict) -> bool:
    """P_CheckMissileSpawn(): jitter, half a step forward, maybe explode."""
    th["tics"] -= level.rng.next() & 3
    if th["tics"] < 1:
        th["tics"] = 1
    th["x"] = i32(th["x"] + (th["momx"] >> 1))
    th["y"] = i32(th["y"] + (th["momy"] >> 1))
    th["z"] = i32(th["z"] + (th["momz"] >> 1))
    if not movement_try_move(level, th, th["x"], th["y"]):
        movement_explode_missile(level, th)
        return False
    return True


def enemy_spawn_missile(level: "SpecLevel", source: dict, dest: dict,
                        type_: int) -> dict:
    """P_SpawnMissile(): spawn 32 units up, aim at the target, check the spawn."""
    info = ENEMY_MOBJINFO[type_]
    th = damage_spawn(level, type_, source["x"], source["y"],
                      source["z"] + 32 * FRACUNIT)
    th["target"] = source
    an = point_to_angle(source["x"], source["y"], dest["x"], dest["y"])
    if dest["flags"] & MF_SHADOW:
        an = u32(an + ((level.rng.next() - level.rng.next()) << 20))
    th["angle"] = an
    an >>= ANGLETOFINESHIFT
    th["momx"] = fixed_mul(info[0], FINE_SINE[an + FINEANGLES // 4])
    th["momy"] = fixed_mul(info[0], FINE_SINE[an])
    dist = trunc_div(movement_aprox_distance(dest["x"] - source["x"],
                                             dest["y"] - source["y"]), info[0])
    if dist < 1:
        dist = 1
    th["momz"] = trunc_div(dest["z"] - source["z"], dist)
    enemy_check_missile_spawn(level, th)
    return th


def enemy_radius_attack(level: "SpecLevel", spot: dict, source, damage: int) -> None:
    """P_RadiusAttack(): the box test, then sight, then damage minus distance."""
    for thing in list(level.mobjs):
        if not (thing["flags"] & MF_SHOOTABLE):
            continue
        if thing["type"] in (7, 16):                    # MT_SPIDER, MT_CYBORG
            continue
        dx = abs(thing["x"] - spot["x"])
        dy = abs(thing["y"] - spot["y"])
        dist = dx if dx > dy else dy
        dist = (dist - thing["radius"]) >> FRACBITS
        if dist < 0:
            dist = 0
        if dist >= damage:
            continue
        if sight_check(level, thing, spot):
            damage_damage(level, thing, spot, source, damage - dist)


def enemy_explode(level: "SpecLevel", thingy: dict) -> None:
    """A_Explode(): `P_RadiusAttack (thingy, thingy->target, 128)`."""
    enemy_radius_attack(level, thingy, thingy.get("target"), 128)


# --- The --enemy fixtures ---------------------------------------------------


def enemy_sector_at(bsp: BspMap, x: int, y: int) -> int:
    return sector_of_subsector(bsp, point_in_subsector(bsp, x, y))


def enemy_segments_cross(ax: int, ay: int, bx: int, by: int,
                         cx: int, cy: int, dx: int, dy: int) -> bool:
    """The fixture selector's strict straddle test, not the sight rule."""
    def cross(ox, oy, px, py, qx, qy):
        return (px - ox) * (qy - oy) - (py - oy) * (qx - ox)

    d1 = cross(ax, ay, bx, by, cx, cy)
    d2 = cross(ax, ay, bx, by, dx, dy)
    d3 = cross(cx, cy, dx, dy, ax, ay)
    d4 = cross(cx, cy, dx, dy, bx, by)
    return ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0))


def enemy_crosses_wall(bsp: BspMap, ax: int, ay: int, bx: int, by: int) -> bool:
    for line in bsp.linedefs:
        if line["flags"] & 4:
            continue
        v1 = bsp.vertexes[line["v1"]]
        v2 = bsp.vertexes[line["v2"]]
        if enemy_segments_cross(ax, ay, bx, by, v1["x"], v1["y"],
                                v2["x"], v2["y"]):
            return True
    return False


def enemy_sound_reachable(bsp: BspMap, sectors, start: int, target: int) -> bool:
    """The fixture selector's optimistic reachability (ignores ML_SOUNDBLOCK)."""
    seen = {start}
    stack = [start]
    while stack:
        sector = stack.pop()
        if sector == target:
            return True
        for line in bsp.linedefs:
            if not (line["flags"] & 4):
                continue
            front = bsp.sidedefs[line["side0"]]["sector"] if line["side0"] >= 0 else None
            if not (line["flags"] & 4) or line["side1"] < 0:
                continue
            back = bsp.sidedefs[line["side1"]]["sector"]
            if front is None or (front != sector and back != sector):
                continue
            other = back if front == sector else front
            if other in seen:
                continue
            top = min(sectors[front]["ceiling"], sectors[back]["ceiling"])
            bottom = max(sectors[front]["floor"], sectors[back]["floor"])
            if top <= bottom:
                continue
            seen.add(other)
            stack.append(other)
    return False


def enemy_find_door(bsp: BspMap, sectors, start: dict):
    """door_check.c's nearest closed manual door, plus the sound-reach checks."""
    best = None
    for index, line in enumerate(bsp.linedefs):
        if line["special"] != 1:
            continue
        front = bsp.sidedefs[line["side0"]]["sector"] if line["side0"] >= 0 else None
        if not (line["flags"] & 4) or line["side1"] < 0:
            continue
        back = bsp.sidedefs[line["side1"]]["sector"]
        if front is None:
            continue
        if sectors[back]["ceiling"] > sectors[back]["floor"] or \
                sectors[front]["floor"] != sectors[back]["floor"]:
            continue
        if not enemy_sound_reachable(bsp, sectors, start["sector"], front) or \
                enemy_sound_reachable(bsp, sectors, start["sector"], back):
            continue
        v1 = bsp.vertexes[line["v1"]]
        v2 = bsp.vertexes[line["v2"]]
        dx = v2["x"] - v1["x"]
        dy = v2["y"] - v1["y"]
        mx = v1["x"] + dx // 2
        my = v1["y"] + dy // 2
        distance = (mx - start["x"]) ** 2 + (my - start["y"]) ** 2
        if best is not None and distance >= best[0]:
            continue
        nx, ny = -dy, dx
        length = math.sqrt(nx * nx + ny * ny)
        if length <= 0.0:
            continue
        point = None
        for sign in (1, -1):
            stepx = int(nx / length * 8.0 * FRACUNIT)
            stepy = int(ny / length * 8.0 * FRACUNIT)
            px = i32(mx + sign * stepx)
            py = i32(my + sign * stepy)
            if enemy_sector_at(bsp, px, py) == back:
                point = (px, py)
                break
        if point is None:
            continue
        best = (distance, index, front, back, point[0], point[1])
    return best


def enemy_print_mobj(level: "SpecLevel", slot: int, mo: dict) -> None:
    """One MOBJ line, with the live sector and facing T15b's assertions read."""
    line = (f"MOBJ {slot} type={mo['type']} x={mo['x']} y={mo['y']} "
            f"z={mo['z']} health={mo['health']} "
            f"flags=0x{mo['flags'] & 0xFFFFFFFF:08x} "
            f"state={mo['state']} tics={mo['tics']} "
            f"tgt={damage_slot_of(level, mo.get('target'))} "
            f"momx={mo['momx']} momy={mo['momy']} "
            f"sec={mo['sector']} sub={mo['subsector']} ang={u32(mo['angle'])}")
    if mo["type"] in (ENEMY_MONSTER, ENEMY_IMP):
        player = level.damage_player["mo"]
        line += f" see={1 if sight_check(level, mo, player) else 0}"
    print(line)


def enemy_emit(level: "SpecLevel", step: list[int], op: str) -> None:
    print(f"STEP {step[0]} op={op} rng={level.rng.index}")
    attack_print_psprites(level.damage_player)
    for slot, mo in enumerate(level.mobjs):
        enemy_print_mobj(level, slot, mo)
    attack_print_player(level)
    step[0] += 1


def enemy_scene(bsp: BspMap, name: str, x: int, y: int, fire: bool = False,
                monster_type: int = ENEMY_MONSTER, tics: int = ENEMY_TICS,
                fire_tic: int = ENEMY_FIRE_TIC, kill_tic: int = -1) -> None:
    """One scene: a fresh level, the avatar and one monster, `tics` tics."""
    things = run_things(bsp, 2)                 # the RNG position the load leaves
    level = SpecLevel(bsp, RandomState(things.rng))
    level.leveltime = 0
    level.move_stats = {"checks": 0, "blocked": 0, "crossed": 0, "slides": 0}
    level.pickups = True
    level.damage_skill = 2
    level.damage_netgame = False
    level.crushed = 0
    level.mobj_states = ATTACK_STATES
    level.stats["triggers"] = 0

    mo = movement_player(level, use_start_angle=True)
    level.mobjs = [mo]
    mo["threshold"] = 0
    mo["sprite"] = "PLAY"
    mo["lastlook"] = 0
    player = player_record(mo)
    player["ammo"] = [50, 0, 0, 0]
    player["maxammo"] = list(DAMAGE_MAXAMMO)
    player["weaponowned"] = [1, 1, 0, 0, 0, 0, 0, 0, 0]
    player["cards"] = [0] * 6
    player["powers"] = [0] * 6
    player["readyweapon"] = 1
    player["pendingweapon"] = 1
    player["backpack"] = 0
    player["psprites"] = [attack_new_psprite(), attack_new_psprite()]
    player["pspritesStarted"] = 0
    player["silentSounds"] = 0
    player["noiseAlerts"] = 0
    level.damage_player = player

    monster = damage_spawn(level, monster_type, x, y, "floor")
    # P_SpawnMobj's reactiontime: `mobjinfo[type].reactiontime` unless the skill
    # is nightmare. This scene's level is medium, so the row's value stands.
    monster["reactiontime"] = ENEMY_MOBJINFO[monster_type][1]
    monster["angle"] = point_to_angle(monster["x"], monster["y"],
                                      mo["x"], mo["y"])
    spawn_state = damage_mobj(monster_type)["spawnstate"]
    alt_state = level_mobj_states(level)[spawn_state][1]

    step = [0]
    wake = -1
    target = -1
    stayed = 1
    wall_cross = 0
    previous = (monster["x"], monster["y"])
    print(f"SCENE-BEGIN name={name}")
    enemy_emit(level, step, "setup")
    for tic in range(tics):
        if tic == kill_tic:
            damage_damage(level, monster, None, None, 30)
        buttons = BT_ATTACK if (fire and tic == fire_tic) else 0
        cmd = {"forwardmove": 0, "sidemove": 0, "angleturn": 0,
               "buttons": buttons}
        attack_player_think(level, player, cmd)
        damage_run_thinkers(level)
        run_tic(level)
        level.leveltime += 1
        if not monster.get("removed"):
            position = (monster["x"], monster["y"])
            if enemy_crosses_wall(bsp, previous[0], previous[1],
                                  position[0], position[1]):
                wall_cross += 1
            previous = position
        if wake < 0 and monster.get("target") is not None:
            wake = tic
            target = damage_slot_of(level, monster["target"])
        if monster.get("target") is not None or \
                monster["state"] not in (spawn_state, alt_state):
            stayed = 0
        if tic == kill_tic:
            op = "kill"
        elif buttons:
            op = "fire"
        else:
            op = "tick"
        enemy_emit(level, step, op)

    if monster.get("target") is None:
        target = -1
    sound = level.sound[monster["sector"]]["target"]
    print(f"SCENE-END name={name} tics={tics} woketic={wake} "
          f"target={target} state={monster['state']} stayed={stayed} "
          f"sndtarget={damage_slot_of(level, sound)} rng={level.rng.index} "
          f"wallcross={wall_cross} hp={player['health']}")


def report_enemy(lumps: list[Lump], map_name: str) -> None:
    """The `--enemy` protocol: four waking scenes and the sight sweep."""
    bsp = read_bsp_map(lumps, map_name)
    things = run_things(bsp, 2)
    player = things.mobjs[things.player_index]
    player["height"] = 56 * FRACUNIT

    print(f"ENEMY {map_name} script={ENEMY_SCRIPT}")

    fine = player["angle"] >> ANGLETOFINESHIFT
    sees_x = i32(player["x"] + 40 * FINE_SINE[fine + FINEANGLES // 4])
    sees_y = i32(player["y"] + 40 * FINE_SINE[fine])
    sees_sector = enemy_sector_at(bsp, sees_x, sees_y)
    print(f"FIXTURE sees x={sees_x} y={sees_y} sector={sees_sector}")

    wall = None
    for index, candidate in enumerate(things.mobjs):
        if index == things.player_index or candidate["sector"] == player["sector"]:
            continue
        if not enemy_crosses_wall(bsp, candidate["x"], candidate["y"],
                                  player["x"], player["y"]):
            continue
        if not enemy_sound_reachable(bsp, bsp.sectors, player["sector"],
                                     candidate["sector"]):
            continue
        wall = (index, candidate)
        break
    if wall is None:
        raise InputError(f"{map_name}: no wall-separated, sound-reachable spot")
    wall_index, wall_mobj = wall
    print(f"FIXTURE wall x={wall_mobj['x']} y={wall_mobj['y']} "
          f"sector={wall_mobj['sector']} from={wall_index}")

    count = len(things.mobjs)
    pairs = [(i, j) for i in range(count) for j in range(count)
             if i != j and (i * 17 + j * 31) % ENEMY_PAIR_MOD == 0]
    print(f"SWEEP {map_name} mobjs={count} pairs={len(pairs)}")

    # The sweep's endpoints are the THINGS mobjs; P_CheckSight only reads each
    # one's sector, z and height, and the oracle's records have all three.
    sweep_level = SpecLevel(bsp, RandomState(things.rng))
    sweep_level.sectors = [dict(sector) for sector in bsp.sectors]
    for i, j in pairs:
        visible = sight_check(sweep_level, things.mobjs[i], things.mobjs[j])
        print(f"SIGHT {i} {j} v={1 if visible else 0} "
              f"s1={things.mobjs[i]['sector']} s2={things.mobjs[j]['sector']}")

    door = enemy_find_door(bsp, bsp.sectors, player)
    if door is None:
        raise InputError(f"{map_name}: no closed manual door fixture")
    _, door_line, door_front, door_back, door_x, door_y = door
    print(f"FIXTURE door x={door_x} y={door_y} sector={door_back} "
          f"line={door_line} front={door_front} back={door_back}")

    # The corridor fixture: the farthest point along the avatar's spawn facing,
    # at or below 240 map units, that is in the same sector and whose straight
    # segment crosses no one-sided linedef. On E1M1 that is 224 units, in the
    # start room; the two 8-unit steps beyond it fall into the next sectors.
    corridor_x = 0
    corridor_y = 0
    corridor_sector = -1
    distance = 240
    while distance >= 40:
        candidate_x = i32(player["x"] +
                          distance * FINE_SINE[fine + FINEANGLES // 4])
        candidate_y = i32(player["y"] + distance * FINE_SINE[fine])
        sector = enemy_sector_at(bsp, candidate_x, candidate_y)
        if sector == player["sector"] and \
                not enemy_crosses_wall(bsp, player["x"], player["y"],
                                       candidate_x, candidate_y):
            corridor_x, corridor_y, corridor_sector = (candidate_x, candidate_y,
                                                       sector)
            break
        distance -= 8
    if corridor_sector < 0:
        raise InputError(f"{map_name}: no clear corridor spot along the facing")
    print(f"FIXTURE corridor x={corridor_x} y={corridor_y} "
          f"sector={corridor_sector}")

    enemy_scene(bsp, "sees", sees_x, sees_y, False)
    enemy_scene(bsp, "wall", wall_mobj["x"], wall_mobj["y"], False)
    enemy_scene(bsp, "noise", wall_mobj["x"], wall_mobj["y"], True)
    enemy_scene(bsp, "door", door_x, door_y, True)
    # T15b: it turns and walks, stops to maul, shoots at range, throws the
    # baked fireball, and stops dead when killed.
    enemy_scene(bsp, "chase", corridor_x, corridor_y,
                tics=ENEMY_CHASE_TICS)
    enemy_scene(bsp, "melee", sees_x, sees_y, monster_type=ENEMY_IMP,
                tics=ENEMY_CHASE_TICS)
    enemy_scene(bsp, "hitscan", corridor_x, corridor_y,
                tics=ENEMY_HITSCAN_TICS)
    enemy_scene(bsp, "wallshot", wall_mobj["x"], wall_mobj["y"], True,
                tics=ENEMY_COMBAT_TICS, fire_tic=ENEMY_FIRE_TIC)
    enemy_scene(bsp, "fireball", corridor_x, corridor_y,
                monster_type=ENEMY_IMP, tics=ENEMY_COMBAT_TICS)
    enemy_scene(bsp, "death", sees_x, sees_y, tics=ENEMY_CHASE_TICS,
                kill_tic=ENEMY_KILL_TIC)



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
CENTER_Y = VIEW_HEIGHT // 2
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
SKY_TEXTUREMID = 100 << FRACBITS  # R_InitSkyMap's literal, not the horizon
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
    for i in range(VIEW_HEIGHT):
        dy = abs(((i - VIEW_HEIGHT // 2) << FRACBITS) + FRACUNIT // 2)
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
        self.sprite_patches: dict[str, Patch] = {}

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

    def sprite_patch(self, name: str) -> Patch:
        """A sprite lump, parsed with the same validator patches use (D3.4)."""
        key = name.upper()
        cached = self.sprite_patches.get(key)
        if cached is not None:
            return cached
        index = first_index(self.lumps, key)
        if index < 0:
            raise FormatError(f"sprite lump {key!r} is not in this WAD")
        patch = parse_patch(key, self.lumps[index].data)
        self.sprite_patches[key] = patch
        return patch


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
    records: list[dict] = []
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
        masked = ds["has_back"] and has_texture(side["mid"])
        masked_cols = ([0] * (ds["x2"] - ds["x1"] + 1)) if masked else None

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
            if wall_mid or wall_top or wall_bottom or masked:
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

                if masked_cols is not None:
                    masked_cols[x - ds["x1"]] = texture_column

            scale += scalestep
            top_frac += top_step
            bottom_frac += bottom_step

        # R_StoreWallRange's tail, as the clip capture and the masked record
        # (D3.4). "const" is the shared all-viewheight/all--1 array; None means
        # the drawseg has no such block (upstream's NULL pointer).
        silhouette = ds["silhouette"]
        top_sil = ds["top"]
        bottom_sil = ds["bottom"]
        clip_top = None
        clip_bottom = None
        if ds["has_back"]:
            if ds["clip_top_solid"]:
                clip_top = "const"
            elif masked or (silhouette & 2):
                clip_top = tuple(ceilingclip[ds["x1"]:ds["x2"] + 1])
            if ds["clip_bottom_solid"]:
                clip_bottom = "const"
            elif masked or (silhouette & 1):
                clip_bottom = tuple(floorclip[ds["x1"]:ds["x2"] + 1])
        else:
            clip_top = "const"
            clip_bottom = "const"
        if masked:
            if not (silhouette & 2):
                silhouette |= 2
                top_sil = INT32_MIN
            if not (silhouette & 1):
                silhouette |= 1
                bottom_sil = INT32_MAX
        records.append({
            "masked": masked_cols,
            "top": clip_top,
            "bottom": clip_bottom,
            "silhouette": silhouette,
            "top_sil": top_sil,
            "bottom_sil": bottom_sil,
        })

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
        "segs": records,
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
        if y < 0 or y >= VIEW_HEIGHT or x1 < 0 or x2 >= SCREEN_WIDTH or x1 > x2:
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
                if yh > VIEW_HEIGHT - 1:
                    yh = VIEW_HEIGHT - 1
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


# --- D3.4: sprites and the masked mid textures ------------------------------
#
# Written from r_things.c's R_ProjectSprite / R_SortVisSprites / R_DrawSprite /
# R_DrawVisSprite / R_DrawMaskedColumn, r_segs.c's R_RenderMaskedSegRange and
# r_light.h's sprite row. It reads the wall pass's recorded clip arrays, never
# r_things.c's C text.

SPRITE_MINZ = 4 << FRACBITS
SPRITE_CLIP_UNSET = -2
MASKED_COLUMN_USED = 32767
POST_MASK = 127


def sprite_list(bsp: BspMap, params: dict,
                tables: dict[str, dict[int, e1m1_sprites.FrameTable]]) -> list[dict]:
    """The fixture's sprite list for one viewpoint.

    Every THINGS record the info.c transcription covers becomes one sprite: its
    position, the sector floor it stands on (P_SpawnMapThing's ONFLOORZ), the
    angle P_SpawnMapThing derives, the lump and flip R_ProjectSprite's rotation
    rule selects, and the sector's light level.
    """
    sprites: list[dict] = []
    for thing in bsp.things:
        rule = e1m1_sprites.rule_for(thing["type"])
        if rule is None:
            continue
        x = _fixed16(thing["x"])
        y = _fixed16(thing["y"])
        subsector = bsp.ssectors[point_in_subsector(bsp, x, y)]
        seg = bsp.segs[subsector["firstseg"]]
        line = bsp.linedefs[seg["linedef"]]
        side = bsp.sidedefs[line["side0"] if seg["side"] == 0 else line["side1"]]
        sector = bsp.sectors[side["sector"]]
        angle = e1m1_sprites.thing_angle(thing["angle"])
        rotation = e1m1_sprites.rotation_for(params["x"], params["y"], x, y, angle)
        lump, flip = e1m1_sprites.select_sprite(tables, rule.sprite, rule.frame,
                                                rotation)
        sprites.append({
            "x": x, "y": y, "z": sector["floor"], "angle": angle,
            "lump": lump, "flip": flip, "fullbright": rule.fullbright,
            "light": sector["light"], "noclip": False,
        })
    return sprites


def project_sprites(bsp: BspMap, params: dict, sprites: list[dict],
                    textures: TextureLibrary) -> tuple[list[dict], dict]:
    """R_ProjectSprite over a caller list; returns (accepted, counters)."""
    viewx = params["x"]
    viewy = params["y"]
    viewz = params["z"]
    viewangle = params["angle"]
    extralight = params["extralight"]
    viewsin = FINE_SINE[viewangle >> ANGLETOFINESHIFT]
    viewcos = FINE_SINE[(viewangle >> ANGLETOFINESHIFT) + FINEANGLES // 4]
    centerxfrac = (SCREEN_WIDTH // 2) << FRACBITS
    stats = {"submitted": len(sprites), "backface": 0, "frustum": 0,
             "zerospan": 0}
    accepted: list[dict] = []

    for sprite in sprites:
        patch = textures.sprite_patch(sprite["lump"])
        trx = i32(sprite["x"] - viewx)
        try_y = i32(sprite["y"] - viewy)
        gxt = fixed_mul(trx, viewcos)
        gyt = -fixed_mul(try_y, viewsin)
        tz = i32(gxt - gyt)
        if tz < SPRITE_MINZ:
            stats["backface"] += 1
            continue
        xscale = fixed_div(centerxfrac, tz)

        gxt = -fixed_mul(trx, viewsin)
        gyt = fixed_mul(try_y, viewcos)
        tx = i32(-(gyt + gxt))
        if abs(tx) > i32(tz << 2):
            stats["frustum"] += 1
            continue

        tx = i32(tx - (patch.left_offset << FRACBITS))
        x1 = i32(centerxfrac + fixed_mul(tx, xscale)) >> FRACBITS
        if x1 > SCREEN_WIDTH:
            stats["frustum"] += 1
            continue
        tx = i32(tx + (patch.width << FRACBITS))
        x2 = (i32(centerxfrac + fixed_mul(tx, xscale)) >> FRACBITS) - 1
        if x2 < 0:
            stats["frustum"] += 1
            continue

        gzt = i32(sprite["z"] + (patch.top_offset << FRACBITS))
        iscale = fixed_div(FRACUNIT, xscale)
        if sprite["flip"]:
            startfrac = (patch.width - 1) << FRACBITS
            xiscale = -iscale
        else:
            startfrac = 0
            xiscale = iscale
        stored_x1 = 0 if x1 < 0 else x1
        stored_x2 = SCREEN_WIDTH - 1 if x2 >= SCREEN_WIDTH else x2
        if stored_x1 > x1:
            startfrac = i32(startfrac + xiscale * (stored_x1 - x1))
        if stored_x1 > stored_x2:
            stats["zerospan"] += 1
            continue

        lightnum = light_row((sprite["light"] >> LIGHTSEGSHIFT) + extralight)
        accepted.append({
            "sprite": sprite, "patch": patch, "scale": xscale,
            "xiscale": xiscale, "startfrac": startfrac,
            "texturemid": i32(gzt - viewz), "gx": sprite["x"],
            "gy": sprite["y"], "gz": sprite["z"], "gzt": gzt,
            "x1": stored_x1, "x2": stored_x2, "lightnum": lightnum,
        })

    stats["drawn"] = len(accepted)
    return accepted, stats


def sort_vissprites(accepted: list[dict]) -> None:
    """R_SortVisSprites(): ascending scale, stable for equal scales."""
    for i in range(len(accepted)):
        best = i
        best_scale = accepted[i]["scale"]
        for j in range(i + 1, len(accepted)):
            if accepted[j]["scale"] < best_scale:
                best_scale = accepted[j]["scale"]
                best = j
        if best != i:
            chosen = accepted.pop(best)
            accepted.insert(i, chosen)


def point_on_seg_side(bsp: BspMap, seg_index: int, x: int, y: int) -> int:
    """R_PointOnSegSide(), the sign-bit shortcut included."""
    seg = bsp.segs[seg_index]
    v1 = bsp.vertexes[seg["v1"]]
    v2 = bsp.vertexes[seg["v2"]]
    lx, ly = v1["x"], v1["y"]
    ldx = i32(v2["x"] - lx)
    ldy = i32(v2["y"] - ly)
    if ldx == 0:
        if x <= lx:
            return 1 if ldy > 0 else 0
        return 1 if ldy < 0 else 0
    if ldy == 0:
        if y <= ly:
            return 1 if ldx < 0 else 0
        return 1 if ldx > 0 else 0
    dx = i32(x - lx)
    dy = i32(y - ly)
    if ((ldy ^ ldx ^ dx ^ dy) & 0x80000000):
        return 1 if ((ldy ^ dx) & 0x80000000) else 0
    left = fixed_mul(ldy >> FRACBITS, dx)
    right = fixed_mul(dy, ldx >> FRACBITS)
    return 0 if right < left else 1


def emit_masked(bsp: BspMap, drawsegs: list[dict], wall: dict, plane: dict,
                params: dict, textures: TextureLibrary, colormaps: bytes,
                sprites: list[dict],
                tables: dict[str, dict[int, "e1m1_sprites.FrameTable"]]) -> dict:
    """R_DrawMasked(): sprites back to front, masked mids interleaved, then the
    untouched mid columns far to near, on top of the plane pass's frame."""
    pixels = bytearray(plane["pixels"])
    covered = bytearray(plane["covered"])
    mask = bytearray(SCREEN_WIDTH * SCREEN_HEIGHT)
    records = wall["segs"]
    viewz = params["z"]
    counters = {"sprite_pixels": 0}

    def write_column(x: int, yl: int, yh: int, texture_mid: int, iscale: int,
                     column: bytes, height_mask: int, colormap: int) -> None:
        if yl > yh:
            return
        if yl < 0:
            yl = 0
        if yh > VIEW_HEIGHT - 1:
            yh = VIEW_HEIGHT - 1
        if yl > yh:
            return
        frac = i32(texture_mid + (yl - CENTER_Y) * iscale)
        for y in range(yl, yh + 1):
            spot = colormaps[colormap + column[(frac >> FRACBITS) & height_mask]]
            at = y * SCREEN_WIDTH + x
            pixels[at] = spot
            mask[at] = 1
            covered[x] = 1
            counters["sprite_pixels"] += 1
            frac = i32(frac + iscale)

    def render_masked(ds_index: int, x1: int, x2: int) -> None:
        record = records[ds_index]
        if record["masked"] is None:
            return
        ds = drawsegs[ds_index]
        texture = textures.get(name_of(ds["side"]["mid"]))
        if ds["flags"] & ML_DONTPEGBOTTOM:
            floor = max(ds["bottom16"], ds["low16"])
            texture_mid = i32(floor + (texture.height << FRACBITS))
        else:
            texture_mid = min(ds["top16"], ds["high16"])
        texture_mid = i32(texture_mid + ds["side"]["rowoffset"])

        scale = i32(ds["scale1"] + (x1 - ds["x1"]) * ds["scalestep"])
        for x in range(x1, x2 + 1):
            column = record["masked"][x - ds["x1"]]
            if column != MASKED_COLUMN_USED:
                if record["top"] is None or record["top"] == "const":
                    yl = (VIEW_HEIGHT if record["top"] == "const" else -1) + 1
                else:
                    yl = record["top"][x - ds["x1"]] + 1
                if record["bottom"] is None or record["bottom"] == "const":
                    yh = (-1 if record["bottom"] == "const" else VIEW_HEIGHT) - 1
                else:
                    yh = record["bottom"][x - ds["x1"]] - 1
                colormap = LIGHT_LEVEL[light_row(ds["lightnum"])][light_column(scale)] * 256
                iscale = 0xFFFFFFFF // (scale & 0xFFFFFFFF) if (scale & 0xFFFFFFFF) else INT32_MAX
                write_column(x, yl, yh, texture_mid, iscale, texture.pixels,
                             texture.column_height - 1, colormap)
                record["masked"][x - ds["x1"]] = MASKED_COLUMN_USED
            scale = i32(scale + ds["scalestep"])

    def draw_sprite(vis: dict) -> None:
        sprite = vis["sprite"]
        patch = vis["patch"]
        clipbot = [SPRITE_CLIP_UNSET] * SCREEN_WIDTH
        cliptop = [SPRITE_CLIP_UNSET] * SCREEN_WIDTH
        if not sprite["noclip"]:
            for ds_index in range(len(drawsegs) - 1, -1, -1):
                ds = drawsegs[ds_index]
                record = records[ds_index]
                if ds["x1"] > vis["x2"] or ds["x2"] < vis["x1"]:
                    continue
                if record["silhouette"] == 0 and record["masked"] is None:
                    continue
                r1 = max(ds["x1"], vis["x1"])
                r2 = min(ds["x2"], vis["x2"])
                if ds["scale1"] > ds["scale2"]:
                    lowscale, scale = ds["scale2"], ds["scale1"]
                else:
                    lowscale, scale = ds["scale1"], ds["scale2"]
                if (scale < vis["scale"]
                        or (lowscale < vis["scale"]
                            and not point_on_seg_side(bsp, ds["seg"], vis["gx"],
                                                      vis["gy"]))):
                    if record["masked"] is not None:
                        render_masked(ds_index, r1, r2)
                    continue
                silhouette = record["silhouette"]
                if vis["gz"] >= record["bottom_sil"]:
                    silhouette &= ~1
                if vis["gzt"] <= record["top_sil"]:
                    silhouette &= ~2
                if silhouette in (1, 2, 3):
                    for x in range(r1, r2 + 1):
                        if silhouette & 1 and clipbot[x] == SPRITE_CLIP_UNSET:
                            clipbot[x] = (-1 if record["bottom"] == "const"
                                          else record["bottom"][x - ds["x1"]])
                        if silhouette & 2 and cliptop[x] == SPRITE_CLIP_UNSET:
                            cliptop[x] = (VIEW_HEIGHT if record["top"] == "const"
                                          else record["top"][x - ds["x1"]])
        for x in range(vis["x1"], vis["x2"] + 1):
            if clipbot[x] == SPRITE_CLIP_UNSET:
                clipbot[x] = VIEW_HEIGHT
            if cliptop[x] == SPRITE_CLIP_UNSET:
                cliptop[x] = -1

        if sprite["fullbright"]:
            colormap = 0
        else:
            colormap = LIGHT_LEVEL[vis["lightnum"]][light_column(vis["scale"])] * 256
        iscale = abs(vis["xiscale"])
        spryscale = vis["scale"]
        sprtopscreen = i32(CENTER_Y_FRAC - fixed_mul(vis["texturemid"], spryscale))
        frac = vis["startfrac"]
        for x in range(vis["x1"], vis["x2"] + 1):
            texture_column = frac >> FRACBITS
            if 0 <= texture_column < patch.width:
                stream = patch.columns[texture_column]
                at = 0
                while stream[at] != POST_END:
                    topdelta = stream[at]
                    length = stream[at + 1]
                    run = stream[at + 3:at + 3 + length]
                    topscreen = i32(sprtopscreen + spryscale * topdelta)
                    bottomscreen = i32(topscreen + spryscale * length)
                    yl = (topscreen + FRACUNIT - 1) >> FRACBITS
                    yh = (bottomscreen - 1) >> FRACBITS
                    if yh >= clipbot[x]:
                        yh = clipbot[x] - 1
                    if yl <= cliptop[x]:
                        yl = cliptop[x] + 1
                    if yl <= yh:
                        post_mid = i32(vis["texturemid"] - (topdelta << FRACBITS))
                        if yl < 0:
                            yl = 0
                        if yh > VIEW_HEIGHT - 1:
                            yh = VIEW_HEIGHT - 1
                        pfrac = i32(post_mid + (yl - CENTER_Y) * iscale)
                        for y in range(yl, yh + 1):
                            at_pixel = y * SCREEN_WIDTH + x
                            pixels[at_pixel] = colormaps[
                                colormap + run[(pfrac >> FRACBITS) & POST_MASK]]
                            mask[at_pixel] = 1
                            covered[x] = 1
                            counters["sprite_pixels"] += 1
                            pfrac = i32(pfrac + iscale)
                    at += length + 4
            frac = i32(frac + vis["xiscale"])

    accepted, stats = project_sprites(bsp, params, sprites, textures)
    sort_vissprites(accepted)
    for vis in accepted:
        draw_sprite(vis)

    for ds_index in range(len(drawsegs) - 1, -1, -1):
        if records[ds_index]["masked"] is not None:
            render_masked(ds_index, drawsegs[ds_index]["x1"], drawsegs[ds_index]["x2"])

    clip_blocks = 2  # R_DRAWSEG_CLIP_TOP and R_DRAWSEG_CLIP_BOTTOM
    opening_blocks = 0
    for record in records:
        if isinstance(record["top"], tuple):
            clip_blocks += 1
        if isinstance(record["bottom"], tuple):
            clip_blocks += 1
        if record["masked"] is not None:
            opening_blocks += 1
    stats["clip_blocks"] = clip_blocks
    stats["opening_blocks"] = opening_blocks
    return {
        "pixels": bytes(pixels),
        "covered": bytes(covered),
        "mask": bytes(mask),
        "sprhash": hashlib.sha256(bytes(mask)).hexdigest(),
        "framehash": hashlib.sha256(bytes(pixels) + bytes(covered)).hexdigest(),
        "sprite_pixels": counters["sprite_pixels"],
        "stats": stats,
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


def render_frame_pixels(lumps: list[Lump], map_name: str, view_index: int = 0,
                        drawsegs: int = 256, planes: int = 128,
                        view: tuple[int, int, int, int] | None = None) -> tuple[bytes, bytes]:
    """The finished index surface of one viewpoint, and its column coverage.

    The same four passes report_segs() runs (walk, walls, planes, sprites):
    SCREEN_WIDTH x SCREEN_HEIGHT pixel bytes (0xFF where nothing drew) and the
    SCREEN_WIDTH `covered` bytes. The board's frame hash is SHA-256 over the
    pixels followed by the coverage, so a gate rebuilding it needs both. D5.1's
    HUD gate overlays the status bar on these pixels to rebuild the HUD frame-00
    hash. `view` is (x, y, z, angle); omitted, the harness's view `view_index`.
    """
    bsp = read_bsp_map(lumps, map_name)
    textures = TextureLibrary(lumps)
    flats = FlatLibrary(lumps)
    colormaps = lumps[first_index(lumps, "COLORMAP")].data
    params = frame_views(bsp)[view_index]
    if view is not None:
        params = {"x": view[0], "y": view[1], "z": view[2],
                  "angle": u32(view[3] & 0xFFFFFFFF), "extralight": 0,
                  "sky": b"F_SKY1\x00\x00"}
    tables = e1m1_sprites.build_sprite_tables(lump.name for lump in lumps)
    state = WalkState(bsp, drawsegs, planes)
    state.render(params)
    wall = emit_walls(bsp, state.drawsegs, params, textures, colormaps)
    plane = emit_planes(state.planes, params, textures, flats, colormaps,
                        wall["pixels"], wall["covered"])
    sprites = sprite_list(bsp, params, tables)
    masked = emit_masked(bsp, state.drawsegs, wall, plane, params, textures,
                         colormaps, sprites, tables)
    return bytes(masked["pixels"]), bytes(masked["covered"])


def report_segs(lumps: list[Lump], map_name: str, drawsegs: int, planes: int,
                detail: int | None = None,
                column: tuple[int, int] | None = None,
                scroll: tuple[int, int] | None = None,
                view: tuple[int, int, int, int] | None = None) -> None:
    bsp = read_bsp_map(lumps, map_name)
    if scroll is not None:
        # P_UpdateSpecials' scroll, applied where upstream applies it: to the
        # sidedef's textureoffset, before R_StoreWallRange reads it. The copy
        # keeps the rest of the map (and every other frame) untouched.
        side_index, delta = scroll
        if side_index < 0 or side_index >= len(bsp.sidedefs):
            raise FormatError(f"--segs-scroll: side {side_index} is outside the map")
        sidedefs = list(bsp.sidedefs)
        sidedefs[side_index] = dict(sidedefs[side_index])
        sidedefs[side_index]["textureoffset"] = i32(
            sidedefs[side_index]["textureoffset"] + delta)
        bsp = replace(bsp, sidedefs=tuple(sidedefs))
    textures = TextureLibrary(lumps)
    flats = FlatLibrary(lumps)
    colormap_index = first_index(lumps, "COLORMAP")
    if colormap_index < 0:
        raise FormatError("COLORMAP is not in this WAD")
    colormaps = lumps[colormap_index].data
    if len(colormaps) != 34 * 256:
        raise FormatError(f"COLORMAP is {len(colormaps)} B, not 34 * 256")

    views = frame_views(bsp)
    if view is not None:
        # One viewpoint, given by the gate: the same four numbers the C harness
        # is given, so the scroll proof compares two renders of one camera.
        views = [{"x": view[0], "y": view[1], "z": view[2],
                  "angle": u32(view[3] & 0xFFFFFFFF), "extralight": 0,
                  "sky": b"F_SKY1\x00\x00"}]
    tables = e1m1_sprites.build_sprite_tables(lump.name for lump in lumps)
    print(f"SEGS {map_name} frames={len(views)}")
    frames: list[dict] = []
    for index, params in enumerate(views):
        state = WalkState(bsp, drawsegs, planes)
        state.render(params)
        wall = emit_walls(bsp, state.drawsegs, params, textures, colormaps)
        plane = emit_planes(state.planes, params, textures, flats, colormaps,
                            wall["pixels"], wall["covered"])
        sprites = sprite_list(bsp, params, tables)
        masked = emit_masked(bsp, state.drawsegs, wall, plane, params, textures,
                             colormaps, sprites, tables)
        frame = {
            "wall": wall,
            "plane": plane,
            "masked": masked,
        }
        frames.append(frame)
        wall_hash = wall["digest"]
        pixel_digest = hashlib.sha256(wall["pixels"]).hexdigest()
        cover_digest = hashlib.sha256(wall["covered"]).hexdigest()
        stats = masked["stats"]
        print(f"frame {index:02d}: {wall_hash}")
        print(f"FRAMESTAT {index:02d} drawsegs={wall['drawsegs']} "
              f"columns={wall['covered_columns']} pixels={wall['pixels_written']} "
              f"mid={wall['mid']} top={wall['top']} bottom={wall['bottom']} "
              f"pixelhash={pixel_digest} coverhash={cover_digest}")
        print(f"PLANESTAT {index:02d} planes={plane['planes']} spans={plane['spans']} "
              f"plane_pixels={plane['plane_pixels']} wallhash={wall_hash} "
              f"planehash={plane['planehash']} framehash={plane['framehash']}")
        print(f"SPRSTAT {index:02d} sprites={stats['submitted']} "
              f"drawn={stats['drawn']} backface={stats['backface']} "
              f"frustum={stats['frustum']} zerospan={stats['zerospan']} "
              f"sprite_pixels={masked['sprite_pixels']} "
              f"clip_blocks={stats['clip_blocks']} "
              f"opening_blocks={stats['opening_blocks']} "
              f"sprhash={masked['sprhash']} framehash={masked['framehash']}")

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
    print(f"SPR-SUMMARY frames={len(frames)} "
          f"sprites={sum(f['masked']['stats']['submitted'] for f in frames)} "
          f"drawn={sum(f['masked']['stats']['drawn'] for f in frames)} "
          f"backface={sum(f['masked']['stats']['backface'] for f in frames)} "
          f"frustum={sum(f['masked']['stats']['frustum'] for f in frames)} "
          f"zerospan={sum(f['masked']['stats']['zerospan'] for f in frames)} "
          f"sprite_pixels={sum(f['masked']['sprite_pixels'] for f in frames)} "
          f"clip_blocks_peak={max(f['masked']['stats']['clip_blocks'] for f in frames)} "
          f"opening_blocks_peak={max(f['masked']['stats']['opening_blocks'] for f in frames)}")

    if detail is not None:
        if detail < 0 or detail >= len(frames):
            raise InputError(f"--segs-detail {detail} is not a frame of this run")
        frame = frames[detail]
        for x in range(SCREEN_WIDTH):
            wall_hash = hashlib.sha256(
                frame["wall"]["pixels"][x::SCREEN_WIDTH]
                + frame["wall"]["covered"][x:x + 1]).hexdigest()
            frame_hash = hashlib.sha256(
                frame["masked"]["pixels"][x::SCREEN_WIDTH]
                + frame["masked"]["covered"][x:x + 1]).hexdigest()
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
                  f"{frame['masked']['pixels'][at]:02x} "
                  f"{frame['wall']['covered'][x]:02x} "
                  f"{frame['masked']['mask'][at]:02x}")


# --- Command line -----------------------------------------------------------

# --- The automap (D5.3) -------------------------------------------------------
#
# linuxdoom-1.10's am_map.c, reimplemented here from the same source: the
# window and its scale, AM_findMinMaxBoundaries, the outcode clipper, the
# Bresenham rasteriser, the flat-aligned grid, AM_drawWalls' colour ladder, the
# single-player arrow and the crosshair. It shares the fixed-point helpers and
# the fine-sine table above and nothing else, so what this rules out is a
# transcription slip in the C -- not a misreading of upstream's layout that both
# sides would repeat.
#
# It draws into a caller-supplied surface exactly as the C does: rows 0..167,
# stride bytes between rows, and nothing outside them.

AM_WINDOW_WIDTH = SCREEN_WIDTH
AM_WINDOW_HEIGHT = SCREEN_HEIGHT - 32

AM_RED = 256 - 5 * 16
AM_REDRANGE = 16
AM_GREENS = 7 * 16
AM_GREENRANGE = 16
AM_GRAYS = 6 * 16
AM_GRAYSRANGE = 16
AM_BROWNS = 4 * 16
AM_BROWNRANGE = 16
AM_YELLOWS = 256 - 32 + 7
AM_YELLOWRANGE = 1
AM_BLACK = 0
AM_WHITE = 256 - 47

AM_BACKGROUND = AM_BLACK
AM_YOURCOLORS = AM_WHITE
AM_WALLCOLORS = AM_RED
AM_WALLRANGE = AM_REDRANGE
AM_TSWALLCOLORS = AM_GRAYS
AM_TSWALLRANGE = AM_GRAYSRANGE
AM_FDWALLCOLORS = AM_BROWNS
AM_FDWALLRANGE = AM_BROWNRANGE
AM_CDWALLCOLORS = AM_YELLOWS
AM_CDWALLRANGE = AM_YELLOWRANGE
AM_SECRETWALLCOLORS = AM_WALLCOLORS
AM_SECRETWALLRANGE = AM_WALLRANGE
AM_GRIDCOLORS = AM_GRAYS + AM_GRAYSRANGE // 2
AM_XHAIRCOLORS = AM_GRAYS

# Upstream's double expressions, written as the integers they truncate to:
# (int)(0.2*FRACUNIT), (int)(1.02*FRACUNIT), (int)(FRACUNIT/1.02) and
# (int)(0.7*FRACUNIT).
AM_INITSCALEMTOF = FRACUNIT * 2 // 10
AM_M_ZOOMIN = FRACUNIT * 102 // 100
AM_M_ZOOMOUT = FRACUNIT * 100 // 102
AM_ENTRYSCALE = FRACUNIT * 7 // 10
AM_PAN_STEP = 4

MAPBLOCKUNITS = 128
PLAYERRADIUS = 16 * FRACUNIT
ML_SECRET = 32
ML_DONTDRAW = 128
ML_MAPPED = 256

AM_PAN_UP = 0x01
AM_PAN_DOWN = 0x02
AM_PAN_LEFT = 0x04
AM_PAN_RIGHT = 0x08

AM_ZOOM_RELEASE = 0
AM_ZOOM_IN = 1
AM_ZOOM_OUT = -1

_AM_R = (8 * PLAYERRADIUS) // 7
PLAYER_ARROW = (
    ((-_AM_R + _AM_R // 8, 0), (_AM_R, 0)),
    ((_AM_R, 0), (_AM_R - _AM_R // 2, _AM_R // 4)),
    ((_AM_R, 0), (_AM_R - _AM_R // 2, -_AM_R // 4)),
    ((-_AM_R + _AM_R // 8, 0), (-_AM_R - _AM_R // 8, _AM_R // 4)),
    ((-_AM_R + _AM_R // 8, 0), (-_AM_R - _AM_R // 8, -_AM_R // 4)),
    ((-_AM_R + 3 * _AM_R // 8, 0), (-_AM_R + _AM_R // 8, _AM_R // 4)),
    ((-_AM_R + 3 * _AM_R // 8, 0), (-_AM_R + _AM_R // 8, -_AM_R // 4)),
)


def c_mod(value: int, modulus: int) -> int:
    """C's %: the sign follows the dividend, unlike Python's floor modulo."""
    return value - trunc_div(value, modulus) * modulus


def blockmap_origin(lumps: list[Lump], map_name: str) -> tuple[int, int]:
    """P_LoadBlockMap's bmaporgx/bmaporgy: the first two int16 of BLOCKMAP."""
    marker = first_index(lumps, map_name)
    if marker < 0:
        raise InputError(f"{map_name}: map marker not found")
    data = lumps[marker + len(BSP_MAP_LUMPS)].data
    if len(data) < 4:
        return (0, 0)
    return (_fixed16(_i16(data, 0)), _fixed16(_i16(data, 2)))


class Automap:
    """AM_LevelInit / AM_Start / AM_Ticker / AM_Drawer over a BspMap."""

    def __init__(self, bsp: BspMap, origin: tuple[int, int] = (0, 0)) -> None:
        self.bsp = bsp
        self.mapped = set()
        self.bmaporgx, self.bmaporgy = origin
        # The gate's --perturb hooks: two constants a caller may move, and
        # nothing else in this implementation is parameterised.
        self.entry_scale = AM_ENTRYSCALE
        self.floor_color = AM_FDWALLCOLORS
        self.f_x = 0
        self.f_y = 0
        self.f_w = AM_WINDOW_WIDTH
        self.f_h = AM_WINDOW_HEIGHT
        self.automapactive = False
        self.stopped = True
        self.grid = False
        self.lightlev = 0
        self.amclock = 0
        self.m_paninc = [0, 0]
        self.mtof_zoommul = FRACUNIT
        self.ftom_zoommul = FRACUNIT
        self.m_x = self.m_y = 0
        self.m_x2 = self.m_y2 = 0
        self.m_w = self.m_h = 0
        self.min_x = self.min_y = 0
        self.max_x = self.max_y = 0
        self.max_w = self.max_h = 0
        self.min_w = self.min_h = 0
        self.min_scale_mtof = 0
        self.max_scale_mtof = 0
        self.scale_mtof = AM_INITSCALEMTOF
        self.scale_ftom = FRACUNIT
        self.old_m_w = self.old_m_h = 0
        self.old_m_x = self.old_m_y = 0
        self.f_oldloc = [INT32_MAX, INT32_MAX]
        self.followplayer = True
        self.player = [0, 0]
        self.player_angle = 0
        self.allmap = False
        self.light_nexttic = 0
        self.light_count = 0
        self.drawn = 0
        self.out_of_range = 0
        self.fb = None
        self.stride = AM_WINDOW_WIDTH

    # --- AM_LevelInit ---------------------------------------------------

    def find_min_max_boundaries(self) -> None:
        min_x = min_y = INT32_MAX
        max_x = max_y = -INT32_MAX
        for vertex in self.bsp.vertexes:
            if vertex["x"] < min_x:
                min_x = vertex["x"]
            elif vertex["x"] > max_x:
                max_x = vertex["x"]
            if vertex["y"] < min_y:
                min_y = vertex["y"]
            elif vertex["y"] > max_y:
                max_y = vertex["y"]
        self.min_x, self.min_y, self.max_x, self.max_y = min_x, min_y, max_x, max_y
        self.max_w = max_x - min_x
        self.max_h = max_y - min_y
        self.min_w = 2 * PLAYERRADIUS
        self.min_h = 2 * PLAYERRADIUS
        a = fixed_div(self.f_w << FRACBITS, self.max_w)
        b = fixed_div(self.f_h << FRACBITS, self.max_h)
        self.min_scale_mtof = a if a < b else b
        self.max_scale_mtof = fixed_div(self.f_h << FRACBITS, 2 * PLAYERRADIUS)

    def level_init(self) -> None:
        self.f_x = 0
        self.f_y = 0
        self.f_w = AM_WINDOW_WIDTH
        self.f_h = AM_WINDOW_HEIGHT
        self.find_min_max_boundaries()
        self.scale_mtof = fixed_div(self.min_scale_mtof, self.entry_scale)
        if self.scale_mtof > self.max_scale_mtof:
            self.scale_mtof = self.min_scale_mtof
        self.scale_ftom = fixed_div(FRACUNIT, self.scale_mtof)

    # --- the two coordinate transforms ----------------------------------

    def ftom(self, value: int) -> int:
        return fixed_mul(value << FRACBITS, self.scale_ftom)

    def mtof(self, value: int) -> int:
        return fixed_mul(value, self.scale_mtof) >> FRACBITS

    def cxtof(self, x: int) -> int:
        return self.f_x + self.mtof(x - self.m_x)

    def cytof(self, y: int) -> int:
        return self.f_y + (self.f_h - self.mtof(y - self.m_y))

    # --- AM_Start / AM_Stop / the window --------------------------------

    def start(self, px: int, py: int) -> None:
        self.automapactive = True
        self.stopped = False
        self.f_oldloc = [INT32_MAX, INT32_MAX]
        self.amclock = 0
        self.lightlev = 0
        self.m_paninc = [0, 0]
        self.ftom_zoommul = FRACUNIT
        self.mtof_zoommul = FRACUNIT
        self.m_w = self.ftom(self.f_w)
        self.m_h = self.ftom(self.f_h)
        self.m_x = px - self.m_w // 2
        self.m_y = py - self.m_h // 2
        self.change_window_loc()
        self.old_m_x = self.m_x
        self.old_m_y = self.m_y
        self.old_m_w = self.m_w
        self.old_m_h = self.m_h

    def stop(self) -> None:
        self.automapactive = False
        self.stopped = True

    def activate_new_scale(self) -> None:
        self.m_x += self.m_w // 2
        self.m_y += self.m_h // 2
        self.m_w = self.ftom(self.f_w)
        self.m_h = self.ftom(self.f_h)
        self.m_x -= self.m_w // 2
        self.m_y -= self.m_h // 2
        self.m_x2 = self.m_x + self.m_w
        self.m_y2 = self.m_y + self.m_h

    def min_out_window_scale(self) -> None:
        self.scale_mtof = self.min_scale_mtof
        self.scale_ftom = fixed_div(FRACUNIT, self.scale_mtof)
        self.activate_new_scale()

    def max_out_window_scale(self) -> None:
        self.scale_mtof = self.max_scale_mtof
        self.scale_ftom = fixed_div(FRACUNIT, self.scale_mtof)
        self.activate_new_scale()

    def change_window_scale(self) -> None:
        self.scale_mtof = fixed_mul(self.scale_mtof, self.mtof_zoommul)
        self.scale_ftom = fixed_div(FRACUNIT, self.scale_mtof)
        if self.scale_mtof < self.min_scale_mtof:
            self.min_out_window_scale()
        elif self.scale_mtof > self.max_scale_mtof:
            self.max_out_window_scale()
        else:
            self.activate_new_scale()

    def change_window_loc(self) -> None:
        if self.m_paninc[0] or self.m_paninc[1]:
            self.followplayer = False
            self.f_oldloc[0] = INT32_MAX
        self.m_x += self.m_paninc[0]
        self.m_y += self.m_paninc[1]
        if self.m_x + self.m_w // 2 > self.max_x:
            self.m_x = self.max_x - self.m_w // 2
        elif self.m_x + self.m_w // 2 < self.min_x:
            self.m_x = self.min_x - self.m_w // 2
        if self.m_y + self.m_h // 2 > self.max_y:
            self.m_y = self.max_y - self.m_h // 2
        elif self.m_y + self.m_h // 2 < self.min_y:
            self.m_y = self.min_y - self.m_h // 2
        self.m_x2 = self.m_x + self.m_w
        self.m_y2 = self.m_y + self.m_h

    def do_follow_player(self) -> None:
        if self.f_oldloc[0] != self.player[0] or self.f_oldloc[1] != self.player[1]:
            self.m_x = self.ftom(self.mtof(self.player[0])) - self.m_w // 2
            self.m_y = self.ftom(self.mtof(self.player[1])) - self.m_h // 2
            self.m_x2 = self.m_x + self.m_w
            self.m_y2 = self.m_y + self.m_h
            self.f_oldloc = [self.player[0], self.player[1]]

    # --- AM_Ticker -------------------------------------------------------

    def ticker(self, px: int, py: int, angle: int) -> None:
        self.player = [px, py]
        self.player_angle = u32(angle)
        if not self.automapactive:
            return
        self.amclock += 1
        if self.followplayer:
            self.do_follow_player()
        if self.ftom_zoommul != FRACUNIT:
            self.change_window_scale()
        if self.m_paninc[0] or self.m_paninc[1]:
            self.change_window_loc()

    def update_light_lev(self) -> None:
        litelevels = (0, 4, 7, 10, 12, 14, 15, 15)
        if self.amclock > self.light_nexttic:
            self.lightlev = litelevels[self.light_count]
            self.light_count += 1
            if self.light_count == len(litelevels):
                self.light_count = 0
            self.light_nexttic = self.amclock + 6 - c_mod(self.amclock, 6)

    # --- the buttons ------------------------------------------------------

    def pan(self, bits: int) -> bool:
        if not self.automapactive:
            return False
        if bits:
            self.followplayer = False
        self.m_paninc = [0, 0]
        step = self.ftom(AM_PAN_STEP)
        if bits & AM_PAN_RIGHT:
            self.m_paninc[0] = step
        if bits & AM_PAN_LEFT:
            self.m_paninc[0] = -step
        if bits & AM_PAN_UP:
            self.m_paninc[1] = step
        if bits & AM_PAN_DOWN:
            self.m_paninc[1] = -step
        return True

    def zoom(self, direction: int) -> bool:
        if not self.automapactive:
            return False
        if direction == AM_ZOOM_IN:
            self.mtof_zoommul = AM_M_ZOOMIN
            self.ftom_zoommul = AM_M_ZOOMOUT
        elif direction == AM_ZOOM_OUT:
            self.mtof_zoommul = AM_M_ZOOMOUT
            self.ftom_zoommul = AM_M_ZOOMIN
        else:
            self.mtof_zoommul = FRACUNIT
            self.ftom_zoommul = FRACUNIT
        return True

    # --- drawing ----------------------------------------------------------

    def clip_mline(self, ml) -> tuple[tuple[int, int], tuple[int, int]] | None:
        left, right, bottom, top = 1, 2, 4, 8
        outcode1 = 0
        outcode2 = 0

        if ml[0][1] > self.m_y2:
            outcode1 = top
        elif ml[0][1] < self.m_y:
            outcode1 = bottom
        if ml[1][1] > self.m_y2:
            outcode2 = top
        elif ml[1][1] < self.m_y:
            outcode2 = bottom
        if outcode1 & outcode2:
            return None
        if ml[0][0] < self.m_x:
            outcode1 |= left
        elif ml[0][0] > self.m_x2:
            outcode1 |= right
        if ml[1][0] < self.m_x:
            outcode2 |= left
        elif ml[1][0] > self.m_x2:
            outcode2 |= right
        if outcode1 & outcode2:
            return None

        ax, ay = self.cxtof(ml[0][0]), self.cytof(ml[0][1])
        bx, by = self.cxtof(ml[1][0]), self.cytof(ml[1][1])

        def outcode(x: int, y: int) -> int:
            code = 0
            if y < 0:
                code |= top
            elif y >= self.f_h:
                code |= bottom
            if x < 0:
                code |= left
            elif x >= self.f_w:
                code |= right
            return code

        outcode1 = outcode(ax, ay)
        outcode2 = outcode(bx, by)
        if outcode1 & outcode2:
            return None

        while outcode1 | outcode2:
            outside = outcode1 if outcode1 else outcode2
            tmp_x = 0
            tmp_y = 0
            if outside & top:
                dy = ay - by
                dx = bx - ax
                tmp_x = ax + trunc_div(dx * ay, dy)
                tmp_y = 0
            elif outside & bottom:
                dy = ay - by
                dx = bx - ax
                tmp_x = ax + trunc_div(dx * (ay - self.f_h), dy)
                tmp_y = self.f_h - 1
            elif outside & right:
                dy = by - ay
                dx = bx - ax
                tmp_y = ay + trunc_div(dy * (self.f_w - 1 - ax), dx)
                tmp_x = self.f_w - 1
            elif outside & left:
                dy = by - ay
                dx = bx - ax
                tmp_y = ay + trunc_div(dy * (-ax), dx)
                tmp_x = 0

            if outside == outcode1:
                ax, ay = tmp_x, tmp_y
                outcode1 = outcode(ax, ay)
            else:
                bx, by = tmp_x, tmp_y
                outcode2 = outcode(bx, by)
            if outcode1 & outcode2:
                return None
        return ((ax, ay), (bx, by))

    def draw_fline(self, fl, color: int) -> None:
        (ax, ay), (bx, by) = fl
        if (ax < 0 or ax >= self.f_w or ay < 0 or ay >= self.f_h
                or bx < 0 or bx >= self.f_w or by < 0 or by >= self.f_h):
            self.out_of_range += 1
            return
        dx = bx - ax
        ax2 = 2 * (dx if dx >= 0 else -dx)
        sx = -1 if dx < 0 else 1
        dy = by - ay
        ay2 = 2 * (dy if dy >= 0 else -dy)
        sy = -1 if dy < 0 else 1
        x = ax
        y = ay
        fb = self.fb
        stride = self.stride
        if ax2 > ay2:
            d = ay2 - ax2 // 2
            while True:
                fb[y * stride + x] = color
                if x == bx:
                    return
                if d >= 0:
                    y += sy
                    d -= ax2
                x += sx
                d += ay2
        else:
            d = ax2 - ay2 // 2
            while True:
                fb[y * stride + x] = color
                if y == by:
                    return
                if d >= 0:
                    x += sx
                    d -= ay2
                y += sy
                d += ax2

    def draw_mline(self, ml, color: int) -> bool:
        clipped = self.clip_mline(ml)
        if clipped is None:
            return False
        self.draw_fline(clipped, color)
        return True

    def draw_grid(self, color: int) -> None:
        block = MAPBLOCKUNITS << FRACBITS
        start = self.m_x
        remainder = c_mod(start - self.bmaporgx, block)
        if remainder:
            start += block - remainder
        end = self.m_x + self.m_w
        ml = [[0, self.m_y], [0, self.m_y + self.m_h]]
        x = start
        while x < end:
            ml[0][0] = x
            ml[1][0] = x
            self.draw_mline(ml, color)
            x += block

        start = self.m_y
        remainder = c_mod(start - self.bmaporgy, block)
        if remainder:
            start += block - remainder
        end = self.m_y + self.m_h
        ml = [[self.m_x, 0], [self.m_x + self.m_w, 0]]
        y = start
        while y < end:
            ml[0][1] = y
            ml[1][1] = y
            self.draw_mline(ml, color)
            y += block

    def _sector_of_side(self, side: int) -> dict:
        return self.bsp.sectors[self.bsp.sidedefs[side]["sector"]]

    def draw_walls(self) -> None:
        for index, line in enumerate(self.bsp.linedefs):
            v1 = self.bsp.vertexes[line["v1"]]
            v2 = self.bsp.vertexes[line["v2"]]
            ml = [[v1["x"], v1["y"]], [v2["x"], v2["y"]]]
            if index in self.mapped:
                if line["flags"] & ML_DONTDRAW:
                    continue
                if line["side1"] < 0:
                    color = AM_WALLCOLORS + self.lightlev
                else:
                    if line["special"] == 39:
                        color = AM_WALLCOLORS + AM_WALLRANGE // 2
                    elif line["flags"] & ML_SECRET:
                        color = AM_WALLCOLORS + self.lightlev
                    elif self._sector_of_side(line["side1"])["floor"] != \
                            self._sector_of_side(line["side0"])["floor"]:
                        color = self.floor_color + self.lightlev
                    elif self._sector_of_side(line["side1"])["ceiling"] != \
                            self._sector_of_side(line["side0"])["ceiling"]:
                        color = AM_CDWALLCOLORS + self.lightlev
                    else:
                        continue
                if self.draw_mline(ml, color):
                    self.drawn += 1
            elif self.allmap:
                if not (line["flags"] & ML_DONTDRAW):
                    if self.draw_mline(ml, AM_GRAYS + 3):
                        self.drawn += 1

    def rotate(self, x: int, y: int, angle: int) -> tuple[int, int]:
        index = u32(angle) >> ANGLETOFINESHIFT
        cosine = FINE_SINE[index + FINEANGLES // 4]
        sine = FINE_SINE[index]
        tmpx = fixed_mul(x, cosine) - fixed_mul(y, sine)
        newy = fixed_mul(x, sine) + fixed_mul(y, cosine)
        return (tmpx, newy)

    def draw_line_character(self, lineguy, scale: int, angle: int, color: int,
                            x: int, y: int) -> None:
        for (lax, lay), (lbx, lby) in lineguy:
            ax, ay = lax, lay
            if scale:
                ax = fixed_mul(scale, ax)
                ay = fixed_mul(scale, ay)
            if angle:
                ax, ay = self.rotate(ax, ay, angle)
            ax += x
            ay += y
            bx, by = lbx, lby
            if scale:
                bx = fixed_mul(scale, bx)
                by = fixed_mul(scale, by)
            if angle:
                bx, by = self.rotate(bx, by, angle)
            bx += x
            by += y
            self.draw_mline([[ax, ay], [bx, by]], color)

    def draw_players(self) -> None:
        self.draw_line_character(PLAYER_ARROW, 0, u32(self.player_angle), AM_WHITE,
                                 self.player[0], self.player[1])

    def draw_crosshair(self, color: int) -> None:
        self.fb[((self.f_h + 1) // 2) * self.stride + self.f_w // 2] = color

    def draw(self, surface: bytearray, stride: int = AM_WINDOW_WIDTH) -> tuple[bytes, int]:
        """AM_Drawer(); returns (the whole surface, the number of lines drawn)."""
        if not self.automapactive:
            return (bytes(surface), 0)
        self.fb = surface
        self.stride = stride
        self.out_of_range = 0
        self.drawn = 0
        for row in range(self.f_h):
            surface[row * stride:row * stride + self.f_w] = bytes([AM_BACKGROUND]) * self.f_w
        if self.grid:
            self.draw_grid(AM_GRIDCOLORS)
        self.draw_walls()
        self.draw_players()
        self.draw_crosshair(AM_XHAIRCOLORS)
        return (bytes(surface), self.drawn)


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
    parser.add_argument("--things", metavar="NAME",
                        help="run the map's THINGS lump through P_SpawnMapThing()'s "
                             "filters and print every mobj it produces (D4.1a)")
    parser.add_argument("--specials", metavar="NAME",
                        help="run P_SpawnSpecials, trigger every special line at "
                             "tic 0 and dump the sectors and thinkers every tic "
                             "(D4.1b)")
    parser.add_argument("--sector-damage", metavar="NAME",
                        help="run the P_PlayerInSpecialSector fixtures (every "
                             "sector special the map carries, both suits and the "
                             "10% exit threshold) and print health per tic")
    parser.add_argument("--sector-perturb",
                        choices=("none", "damage", "threshold", "ironfeet"),
                        default="none",
                        help="with --sector-damage: move one constant in this "
                             "implementation only, so the gate can prove it fails")
    parser.add_argument("--stairs", metavar="MAP",
                        help="run one stair/teleport fixture on MAP: one line "
                             "triggered once, then the sector and thinker dumps "
                             "(T34)")
    parser.add_argument("--stairs-fixture", metavar="LINE,SPECIAL,SIDE,TICS",
                        help="with --stairs: the fixture, e.g. 787,97,0,2")
    parser.add_argument("--stairs-perturb", choices=("none", "stairs", "teleport"),
                        default="none",
                        help="with --stairs: move one constant in this "
                             "implementation only, so the gate can prove it fails")
    parser.add_argument("--scrolls", metavar="MAP",
                        help="run P_UpdateSpecials on MAP: the scroll offsets of "
                             "every line special 48 and the animated pictures, "
                             "every tic (T35)")
    parser.add_argument("--scrolls-perturb",
                        choices=("none", "step", "speed", "phase"), default="none",
                        help="with --scrolls: move one constant in this "
                             "implementation only, so the gate can prove it fails")
    parser.add_argument("--movement", metavar="NAME",
                        help="run the 160-tic movement script (thrust, wall, "
                             "slide, corner, friction, return) and print the mobj "
                             "every tic (D4.2b)")
    parser.add_argument("--player", metavar="NAME",
                        help="run the 200-tic player script through P_PlayerThink "
                             "(walk, run, turn, strafe, backpedal, use, friction) "
                             "and print the avatar and view every tic (D4.2c)")
    parser.add_argument("--door", metavar="NAME",
                        help="run the door script (block, BT_USE, open, walk "
                             "through) and print the avatar and the door every tic "
                             "(T26)")
    parser.add_argument("--damage", metavar="NAME",
                        help="run the damage/death/pickup/crush script against the "
                             "damage rules and print every mobj and the player "
                             "record each step (D4.2d1)")
    parser.add_argument("--attack", metavar="NAME",
                        help="run the weapon script (fists, pistol, shotgun, "
                             "chaingun; raise, fire, refire, change, dry and "
                             "switch) against the psprite and attack rules and "
                             "print both psprite layers, every mobj and the player "
                             "record each step (D4.2d2)")
    parser.add_argument("--enemy", metavar="NAME",
                        help="run the sight-and-waking protocol: four scenes "
                             "(sees, wall, noise, door) over 40 tics each plus "
                             "the P_CheckSight pair sweep (T15a)")
    parser.add_argument("--tics", type=int, default=420, metavar="N",
                        help="with --specials, --movement or --player: tics to run "
                             "(default: 420 for --specials; the movement gate "
                             "passes 160 or 200)")
    parser.add_argument("--skill", default="medium", metavar="NAME",
                        help="with --things: baby, easy, medium, hard or nightmare "
                             "(default: medium)")
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
    parser.add_argument("--segs-scroll", metavar="SIDE,DX",
                        help="with --segs: add DX (16.16) to side SIDE's "
                             "textureoffset before rendering, which is what "
                             "P_UpdateSpecials' line special 48 does (T35)")
    parser.add_argument("--segs-view", metavar="X,Y,Z,ANGLE",
                        help="with --segs: render only this viewpoint instead of "
                             "the 20 sampled ones, so the gate can point the "
                             "camera at a scrolling wall (T35)")
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
            or arguments.stats or arguments.bsp or arguments.segs
            or arguments.things or arguments.specials or arguments.sector_damage
            or arguments.movement or arguments.stairs
            or arguments.player or arguments.door or arguments.damage
            or arguments.attack or arguments.enemy or arguments.scrolls):
        parser.error("nothing to do: pass --patches, --textures, --flats, --all, "
                     "--map, --stats, --bsp, --segs, --things, --specials, "
                     "--sector-damage, --movement, --player, --door, --damage, "
                     "--stairs, --scrolls, --attack or --enemy")
    if arguments.limit is not None and arguments.limit < 0:
        parser.error("--limit cannot be negative")
    if arguments.tics < 0:
        parser.error("--tics cannot be negative")
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

    segs_scroll = None
    if arguments.segs_scroll is not None:
        if not arguments.segs:
            parser.error("--segs-scroll needs --segs")
        parts = arguments.segs_scroll.split(",")
        if len(parts) != 2:
            parser.error("--segs-scroll wants SIDE,DX")
        try:
            segs_scroll = (int(parts[0]), int(parts[1]))
        except ValueError:
            parser.error("--segs-scroll wants two integers")

    segs_view = None
    if arguments.segs_view is not None:
        if not arguments.segs:
            parser.error("--segs-view needs --segs")
        parts = arguments.segs_view.split(",")
        if len(parts) != 4:
            parser.error("--segs-view wants X,Y,Z,ANGLE")
        try:
            segs_view = tuple(int(part) for part in parts)
        except ValueError:
            parser.error("--segs-view wants four integers")

    if arguments.sector_perturb != "none":
        SECTOR_PERTURB[arguments.sector_perturb] = 1
    if arguments.stairs_perturb != "none":
        STAIR_PERTURB[arguments.stairs_perturb] = 1

    stairs_fixture = None
    if arguments.stairs is not None or arguments.stairs_fixture is not None:
        if arguments.stairs is None or arguments.stairs_fixture is None:
            parser.error("--stairs needs --stairs-fixture")
        parts = arguments.stairs_fixture.split(",")
        if len(parts) != 4:
            parser.error("--stairs-fixture wants LINE,SPECIAL,SIDE,TICS")
        try:
            stairs_fixture = (int(parts[0]), int(parts[1]), int(parts[2]),
                              int(parts[3]))
        except ValueError:
            parser.error("--stairs-fixture's four fields are integers")

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
                        arguments.planes, arguments.segs_detail, segs_column,
                        segs_scroll, segs_view)
        if arguments.things:
            report_things(lumps, arguments.things.upper(), arguments.skill)
        if arguments.scrolls:
            report_scrolls(lumps, arguments.scrolls.upper(), arguments.tics,
                           arguments.scrolls_perturb)
        if arguments.specials:
            report_specials(lumps, arguments.specials.upper(), arguments.tics)
        if arguments.sector_damage:
            report_sector_damage(lumps, arguments.sector_damage.upper())
        if arguments.stairs:
            report_stairs(lumps, arguments.stairs.upper(), stairs_fixture[0],
                          stairs_fixture[1], stairs_fixture[2],
                          stairs_fixture[3])
        if arguments.movement:
            report_movement(lumps, arguments.movement.upper(), arguments.tics)
        if arguments.player:
            report_player(lumps, arguments.player.upper(), arguments.tics)
        if arguments.door:
            report_door(lumps, arguments.door.upper(), arguments.tics)
        if arguments.damage:
            report_damage(lumps, arguments.damage.upper())
        if arguments.attack:
            report_attack(lumps, arguments.attack.upper())
        if arguments.enemy:
            report_enemy(lumps, arguments.enemy.upper())
    except FormatError as error:
        print(f"doom_ref: {error}", file=sys.stderr)
        return EXIT_INPUT
    except WalkError as error:
        print(f"doom_ref: {arguments.bsp or arguments.segs}: {error}", file=sys.stderr)
        return EXIT_INPUT

    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
