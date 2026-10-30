#!/usr/bin/env python3
"""Independent host oracle for the DOOM picture decoders of this port (D2.4).

WHY THIS TOOL EXISTS

``lib/doom/r_data.{h,c}`` decodes the three picture formats a WAD stores: patch
columns, composite textures and flats. Unit tests pin those decoders against
synthetic lumps the test itself wrote, and that is all a CI machine can do -- it
has no WAD. This tool is the other half of the evidence: it reads the real
DOOM1.WAD, decodes the same items in Python from the raw WAD, and prints one
deterministic SHA-256 per item. ``tools/rdat_check.py`` compiles the C decoder,
runs both sides and compares the hashes.

INDEPENDENCE

Nothing here is derived from the C implementation. This file has its own WAD
reader, its own patch walker, its own texture-directory parser and its own
compositor; it does not import ``whd_encode.py`` (that tool writes a different
format and never decodes pictures), it does not run the C code, and it does not
read the C sources. The only shared thing between the two sides is the format
itself, as described in ``r_data.h`` and in DOOM's ``r_data.c``:

  * a patch is ``int16 width, height, leftoffset, topoffset`` then
    ``int32 columnofs[width]``; a column is a chain of
    ``topdelta, length, unused, pixels[length], unused`` posts ending in a
    0xFF topdelta, and post topdelta/length are bytes;
  * a texture is ``char name[8], int32 masked, int16 width, int16 height,
    int32 obsolete, int16 patchcount, patches[10 bytes each]`` inside
    TEXTURE1/TEXTURE2, and its patch indices address PNAMES;
  * a flat is 4096 raw bytes.

A disagreement between this file and the C decoder is therefore either a bug in
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
    parser.add_argument("--stats", action="store_true",
                        help="print STAT lines about the real data: composing "
                             "holes, negative patch origins, clipped posts and "
                             "the masked flag, the numbers r_data.h quotes")
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
            or arguments.stats):
        parser.error("nothing to do: pass --patches, --textures, --flats, --all, "
                     "--map or --stats")
    if arguments.limit is not None and arguments.limit < 0:
        parser.error("--limit cannot be negative")

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
    except FormatError as error:
        print(f"doom_ref: {error}", file=sys.stderr)
        return EXIT_INPUT

    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
