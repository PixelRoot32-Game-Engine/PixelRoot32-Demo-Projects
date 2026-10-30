#!/usr/bin/env python3
"""Pack a DOOM IWAD/PWAD into a PWHD container for the ESP32 port.

WHY THIS TOOL EXISTS

The shareware DOOM1.WAD is 4,196,020 bytes; the board has 4,194,304 bytes of
flash in total and the application partition needs about 2 MB. The WAD therefore
fits nowhere on the device, yet the renderer still needs random access to anyone
of its 1264 lumps. This tool writes the container described in
``lib/doom/whd.h``: a fixed 40-byte header, a fixed-size directory with each
lump's stored and uncompressed size, a name table, and the payloads back to back.

Each payload is one of three methods:

  0  "stored"   the lump itself
  1  "deflate"  one raw DEFLATE stream (RFC 1951, no zlib wrapper -- what
                ``zlib`` calls windowBits -15)
  2  "raw"      the lump itself, starting on a 4-byte boundary, and declared as
                a lump a reader may hand out as a pointer into the container

The encoder keeps whichever of stored/deflate is smaller, so a lump never grows.
A lump named on the command line (``--raw`` or ``--raw-level``) is kept raw
instead, whatever it would have compressed to: raw is not about size, it is
about the device reading the lump out of the mapping instead of decoding it into
DRAM. The alignment is bought with up to three zero padding bytes in front of a
raw payload, the format's single exception to "no gap and no padding".

DETERMINISM

The same input and the same Python/zlib build produce a byte-identical
container: no timestamps, no hash-order dependence, and every choice is made by
a rule written here. A different zlib build may pick different DEFLATE blocks
for the same bytes, which changes the container but never the bytes it decodes
to -- that is what ``--check`` proves.

Standard library only: argparse, struct, sys, zlib, pathlib.
"""

from __future__ import annotations

import argparse
import fnmatch
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

import bake_graphics as bake

# --- Container constants (keep in step with lib/doom/whd.h) ------------------

MAGIC = b"PWHD"
VERSION = 2
VERSION_MIN = 1
HEADER_SIZE = 40
DIRECTORY_ENTRY_SIZE = 16
NAME_ENTRY_SIZE = 12
NAME_SIZE = 8

METHOD_STORED = 0
METHOD_DEFLATE = 1
METHOD_RAW = 2

# The boundary a raw payload starts on, and the padding that buys it.
RAW_ALIGNMENT = 4
RAW_MAX_PADDING = RAW_ALIGNMENT - 1

# Raw DEFLATE: the zlib wrapper is what the device decoder does not have.
DEFLATE_WINDOW_BITS = -15
DEFLATE_MEMORY_LEVEL = 9

# The partition the shipped container is written to: partitions_wad.csv gives
# `wad` 0x100000..0x400000 (D3.10), 3 MiB. --budget overrides it for a probe of
# some other layout; every answer below is measured against whatever it is given.
FLASH_BUDGET = 3_145_728

WAD_HEADER_SIZE = 12
WAD_DIRECTORY_ENTRY_SIZE = 16

# The ten lumps a level marker is followed by, in WAD order (lib/doom/doom_map.h).
LEVEL_LUMPS = (
    "THINGS",
    "LINEDEFS",
    "SIDEDEFS",
    "VERTEXES",
    "SEGS",
    "SSECTORS",
    "NODES",
    "SECTORS",
    "REJECT",
    "BLOCKMAP",
)

EXIT_OK = 0
EXIT_INPUT = 1
EXIT_USAGE = 2
EXIT_OUTPUT_EXISTS = 3
EXIT_VERIFY = 4

# The ten map lumps in WAD order, and their 1993 record sizes
# (lib/doom/doom_map.h). REJECT and BLOCKMAP are byte blobs: doom_map_open()
# size-checks them and reports no record count for them.
MAP_LUMP_NAMES = (
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP",
)
MAP_RECORD_SIZES = (10, 14, 30, 4, 12, 4, 28, 26, 0, 0)
SHAREWARE_LEVELS = tuple(f"E1M{level}" for level in range(1, 10))


class InputError(Exception):
    """The source file is not a WAD this tool can read."""


class VerificationError(Exception):
    """The container this tool just built does not decode back to its input."""


@dataclass(frozen=True)
class Lump:
    """One WAD lump: its 8-character name and the bytes it stores."""

    name: str
    data: bytes


@dataclass(frozen=True)
class EncodedLump:
    """One directory entry, after the encoder decided how to store it."""

    name: str
    payload: bytes
    uncompressed_size: int
    method: int

    @property
    def stored_size(self) -> int:
        return len(self.payload)

    @property
    def ratio(self) -> float:
        """Bytes in per byte out: 1.0 means the lump did not compress."""
        if not self.uncompressed_size:
            return 1.0
        return self.stored_size / self.uncompressed_size


# --- Reading the source WAD -------------------------------------------------


def _read_le32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset:offset + 4], "little", signed=True)


def read_wad(path: Path) -> tuple[bytes, list[Lump], bytes]:
    """Reads an IWAD or PWAD, validating it the way w_wad.c does.

    A malformed source is rejected here rather than packed into a container that
    would look plausible and fail on the device.

    @return (identification, lumps, the whole file's bytes).
    """
    try:
        data = path.read_bytes()
    except OSError as error:
        raise InputError(f"{path}: {error.strerror}") from error

    if len(data) < WAD_HEADER_SIZE:
        raise InputError(f"{path}: {len(data)} bytes cannot hold a WAD header")

    identification = data[0:4]
    if identification not in (b"IWAD", b"PWAD"):
        raise InputError(f"{path}: not an IWAD or PWAD (first four bytes are {identification!r})")

    num_lumps = _read_le32(data, 4)
    info_table_offset = _read_le32(data, 8)
    if num_lumps < 0 or info_table_offset < WAD_HEADER_SIZE:
        raise InputError(f"{path}: numlumps={num_lumps} infotableofs={info_table_offset} is not a directory")
    if info_table_offset + num_lumps * WAD_DIRECTORY_ENTRY_SIZE > len(data):
        raise InputError(f"{path}: the directory runs past the end of the file")

    lumps: list[Lump] = []
    for index in range(num_lumps):
        entry = info_table_offset + index * WAD_DIRECTORY_ENTRY_SIZE
        file_position = _read_le32(data, entry)
        size = _read_le32(data, entry + 4)
        raw_name = data[entry + 8:entry + 16]
        if file_position < 0 or size < 0 or file_position + size > len(data):
            raise InputError(f"{path}: lump {index} ({raw_name!r}) points outside the file")
        name = raw_name.split(b"\0", 1)[0].decode("latin-1")
        lumps.append(Lump(name=name, data=data[file_position:file_position + size]))

    return identification, lumps, data


# --- Choosing the raw lumps -------------------------------------------------


@dataclass(frozen=True)
class RawSelection:
    """Which lumps stay raw, and which level's cost sizes the nine-level answer."""

    indices: frozenset[int]
    level_indices: frozenset[int]
    level_name: str | None


def select_raw(lumps: list[Lump], names: list[str], levels: list[str]) -> RawSelection:
    """Resolves ``--raw`` and ``--raw-level`` against the WAD's directory.

    A name matches case-insensitively on its first 8 characters, the way the
    container's own name table does. A level is its marker lump followed by the
    ten lumps LEVEL_LUMPS names: the same rule lib/doom/doom_map.h encodes.
    """
    by_name: dict[str, int] = {}
    for index, lump in enumerate(lumps):
        by_name.setdefault(lump.name.upper()[:NAME_SIZE], index)

    chosen: set[int] = set()
    level_chosen: set[int] = set()
    level_name: str | None = None

    for raw_name in names:
        key = raw_name.strip().upper()[:NAME_SIZE]
        if not key:
            raise InputError("--raw: an empty lump name")
        if key not in by_name:
            raise InputError(f"--raw: the WAD has no lump named {raw_name!r}")
        chosen.add(by_name[key])

    for level in levels:
        key = level.strip().upper()[:NAME_SIZE]
        if key not in by_name:
            raise InputError(f"--raw-level: the WAD has no level marker {level!r}")
        marker = by_name[key]
        following = [lump.name.upper() for lump in lumps[marker + 1:marker + 1 + len(LEVEL_LUMPS)]]
        if marker + len(LEVEL_LUMPS) >= len(lumps) or following != list(LEVEL_LUMPS):
            raise InputError(
                f"--raw-level: {level!r} is not followed by {', '.join(LEVEL_LUMPS)}")
        for offset in range(len(LEVEL_LUMPS)):
            chosen.add(marker + 1 + offset)
            level_chosen.add(marker + 1 + offset)
        if level_name is None:
            level_name = key

    return RawSelection(frozenset(chosen), frozenset(level_chosen), level_name)


def select_dropped(lumps: list[Lump], patterns: list[str], levels: list[str]) -> frozenset[int]:
    """Resolves --drop-lumps and --drop-level against the WAD's directory.

    A pattern is a case-insensitive shell glob over the lump name, so ``DS*``
    removes every PCM sound effect. A level is the same marker-plus-ten-lumps
    rule --raw-level uses; dropping it removes the marker too.
    """
    by_name: dict[str, int] = {}
    for index, lump in enumerate(lumps):
        by_name.setdefault(lump.name.upper()[:NAME_SIZE], index)

    dropped: set[int] = set()

    for pattern in patterns:
        key = pattern.strip().upper()
        if not key:
            raise InputError("--drop-lumps: an empty pattern")
        matched = [index for index, lump in enumerate(lumps)
                   if fnmatch.fnmatchcase(lump.name.upper(), key)]
        if not matched:
            raise InputError(f"--drop-lumps: no lump matches {pattern!r}")
        dropped.update(matched)

    for level in levels:
        key = level.strip().upper()[:NAME_SIZE]
        if key not in by_name:
            raise InputError(f"--drop-level: the WAD has no level marker {level!r}")
        marker = by_name[key]
        following = [lump.name.upper() for lump in lumps[marker + 1:marker + 1 + len(LEVEL_LUMPS)]]
        if marker + len(LEVEL_LUMPS) >= len(lumps) or following != list(LEVEL_LUMPS):
            raise InputError(
                f"--drop-level: {level!r} is not followed by {', '.join(LEVEL_LUMPS)}")
        dropped.add(marker)
        for offset in range(len(LEVEL_LUMPS)):
            dropped.add(marker + 1 + offset)

    return frozenset(dropped)


def select_unused_sprites(lumps: list[Lump], names: list[str]) -> frozenset[int]:
    """The sprite lump indices a bake does not name, from S_START to S_END.

    The shareware WAD carries 483 sprite lumps; one episode level's sprite table
    names 36 of them. The rest are animation frames this port cannot address yet
    (the fixture is frame 0), and at 493 KB compressed they are what makes the
    whole-episode container too large for the `wad` partition. Dropping them is
    explicit, never implicit: the names that stay are exactly the ones the bakery
    marked raw, so the renderer cannot ask for one that was removed.
    """
    start = next((index for index, lump in enumerate(lumps)
                  if lump.name.upper() == "S_START"), -1)
    if start < 0:
        raise InputError("--drop-unused-sprites: the WAD has no S_START")
    end = next((index for index in range(start + 1, len(lumps))
                if lumps[index].name.upper() == "S_END"), -1)
    if end < 0:
        raise InputError("--drop-unused-sprites: the WAD has no S_END after S_START")

    kept = {name.strip().upper()[:NAME_SIZE] for name in names}
    if not kept:
        raise InputError("--drop-unused-sprites: the sprite list is empty")
    return frozenset(index for index in range(start + 1, end)
                     if lumps[index].name.upper()[:NAME_SIZE] not in kept)


def build_baked_lumps(lumps: list[Lump], kept: list[Lump], levels: list[str], sky: str,
                      sprite_names: list[str]) -> tuple[list[Lump], list[tuple[str, int, int, int, int, int]],
                                                          bake.BakedSet, int, set[int]]:
    """Bakes the union of @p levels and appends the raw payloads and the manifest.

    One bake for every level given: the union is deduplicated by texture, flat
    and sprite identity, so a texture nine levels share is stored once.

    @return (the kept WAD lumps plus the new lumps, the manifest records with
    final indices, the baked set for --stats, how many lumps were appended, and
    the container indices that must be raw).
    """
    baked = bake.bake_union(lumps, levels, sky=sky, sprite_names=sprite_names)

    index_by_name: dict[str, int] = {}
    for index, lump in enumerate(kept):
        index_by_name.setdefault(lump.name.upper()[:NAME_SIZE], index)

    appended: list[Lump] = []
    records: list[tuple[str, int, int, int, int, int]] = []
    raw_indices: set[int] = set()

    for item in baked.items:
        if item.payload is not None:
            kind_letter = "S" if item.kind == bake.KIND_SKY else "W"
            lump_name = f"~B{kind_letter}{len(appended):05X}"
            appended.append(Lump(lump_name, item.payload))
            lump_index = len(kept) + len(appended) - 1
            raw_indices.add(lump_index)
        else:
            lump_index = index_by_name.get(item.name.upper()[:NAME_SIZE], -1)
            if lump_index < 0:
                raise InputError(f"--bake: {item.name!r} was dropped before it could be baked")
            raw_indices.add(lump_index)
        records.append((item.name, item.width, item.rows, item.flags, item.kind, lump_index))

    manifest = bake.build_manifest(records)
    appended.append(Lump(bake.MANIFEST_NAME, manifest))
    raw_indices.add(len(kept) + len(appended) - 1)

    return kept + appended, records, baked, len(appended), raw_indices


# --- Encoding ---------------------------------------------------------------


def deflate(blob: bytes, level: int) -> bytes:
    """Compresses @p blob into one raw DEFLATE stream."""
    compressor = zlib.compressobj(
        level,
        zlib.DEFLATED,
        DEFLATE_WINDOW_BITS,
        DEFLATE_MEMORY_LEVEL,
        zlib.Z_DEFAULT_STRATEGY,
    )
    return compressor.compress(blob) + compressor.flush()


def encode_lump(index: int, lump: Lump, level: int, selection: RawSelection) -> EncodedLump:
    """Stores @p lump with the method its role asks for.

    A lump the caller asked to keep raw is raw: the verbatim bytes, so the device
    can map them and skip the decode. Everything else keeps whichever of stored
    and deflate is smaller, and a stored lump wins ties: it is the cheaper of the
    two to read on the device, and it keeps the output independent of the
    compressor's block choices for data it cannot shrink.
    """
    if index in selection.indices:
        return EncodedLump(lump.name, lump.data, len(lump.data), METHOD_RAW)
    if not lump.data:
        return EncodedLump(lump.name, b"", 0, METHOD_STORED)

    compressed = deflate(lump.data, level)
    if len(compressed) < len(lump.data):
        return EncodedLump(lump.name, compressed, len(lump.data), METHOD_DEFLATE)
    return EncodedLump(lump.name, lump.data, len(lump.data), METHOD_STORED)


def build_container(lumps: list[Lump], level: int, source_crc: int,
                    selection: RawSelection) -> tuple[bytes, list[EncodedLump], list[int]]:
    """Assembles the container: header, directory, name table, payloads.

    A raw payload is moved forward by at most RAW_MAX_PADDING zero bytes so that
    its first byte is 4-aligned. The padding is written before the payload and
    belongs to no lump: the next entry's offset is the padded position plus the
    payload's own size, exactly as if the padding were not there.

    @return (the container, the entries, one padding byte count per entry).
    """
    encoded = [encode_lump(index, lump, level, selection)
               for index, lump in enumerate(lumps)]

    num_lumps = len(encoded)
    directory_offset = HEADER_SIZE
    name_table_offset = directory_offset + num_lumps * DIRECTORY_ENTRY_SIZE
    data_offset = name_table_offset + num_lumps * NAME_ENTRY_SIZE

    # The offsets first, because a raw payload's position decides its padding.
    offsets = [0] * num_lumps
    padding = [0] * num_lumps
    payload_cursor = 0
    for index, lump in enumerate(encoded):
        if lump.method == METHOD_RAW:
            pad = (-(data_offset + payload_cursor)) % RAW_ALIGNMENT
            padding[index] = pad
            payload_cursor += pad
        offsets[index] = payload_cursor
        payload_cursor += lump.stored_size

    container = bytearray()
    container += MAGIC
    container += struct.pack(
        "<HHIIIIII",
        VERSION,
        0,                              # flags
        num_lumps,
        num_lumps,                      # nameCount: one name per lump
        directory_offset,
        name_table_offset,
        data_offset,
        data_offset + payload_cursor,   # totalSize
    )
    container += struct.pack("<I", source_crc)
    container += struct.pack("<I", 0)    # reserved
    assert len(container) == HEADER_SIZE

    for index, lump in enumerate(encoded):
        container += struct.pack(
            "<IIIB3x",
            offsets[index],
            lump.stored_size,
            lump.uncompressed_size,
            lump.method,
        )
    assert len(container) == name_table_offset

    for index, lump in enumerate(encoded):
        name = lump.name.encode("latin-1")[:NAME_SIZE]
        container += name.ljust(NAME_SIZE, b"\0")
        container += struct.pack("<I", index)
    assert len(container) == data_offset

    for index, lump in enumerate(encoded):
        container += b"\0" * padding[index]
        container += lump.payload

    assert len(container) == data_offset + payload_cursor
    struct.pack_into("<I", container, 28, len(container))
    return bytes(container), encoded, padding


# --- Verification -----------------------------------------------------------


def verify_container(container: bytes, lumps: list[Lump] | None) -> int:
    """Decodes @p container and checks it, and compares it to @p lumps if given.

    This parses the finished bytes, not the structures that built them, and it
    applies the same rules as whd_open() in lib/doom/whd.c, so a container the
    device would reject cannot pass here. Every lump -- raw ones included -- is
    compared byte for byte against the source WAD when one is supplied; without
    one the container is verified structurally, which still proves every payload
    decodes to the size its own directory declares.

    @return how many raw entries the container holds.
    """
    if len(container) < HEADER_SIZE:
        raise VerificationError(f"container is {len(container)} bytes, shorter than the header")
    if container[0:4] != MAGIC:
        raise VerificationError(f"magic is {container[0:4]!r}, expected {MAGIC!r}")

    version, flags = struct.unpack_from("<HH", container, 4)
    if not VERSION_MIN <= version <= VERSION:
        raise VerificationError(f"version is {version}, expected {VERSION_MIN}..{VERSION}")
    if flags != 0:
        raise VerificationError(f"flags is {flags}, expected 0")

    num_lumps, name_count, directory_offset, name_table_offset, data_offset, total_size = \
        struct.unpack_from("<IIIIII", container, 8)
    (reserved,) = struct.unpack_from("<I", container, 36)

    if total_size != len(container):
        raise VerificationError(f"totalSize is {total_size}, the container is {len(container)} bytes")
    if reserved != 0:
        raise VerificationError(f"reserved is {reserved}, expected 0")
    if directory_offset != HEADER_SIZE:
        raise VerificationError(f"directoryOffset is {directory_offset}, expected {HEADER_SIZE}")
    if name_table_offset != directory_offset + num_lumps * DIRECTORY_ENTRY_SIZE:
        raise VerificationError("nameTableOffset does not follow the directory")
    if data_offset != name_table_offset + name_count * NAME_ENTRY_SIZE:
        raise VerificationError("dataOffset does not follow the name table")
    if data_offset > total_size:
        raise VerificationError("dataOffset is past the end of the container")
    if lumps is not None and num_lumps < len(lumps):
        raise VerificationError(f"container holds {num_lumps} lumps, the WAD holds {len(lumps)}")
    source_count = len(lumps) if lumps is not None else 0

    raw_entries = 0
    previous_end = 0
    for index in range(num_lumps):
        entry = directory_offset + index * DIRECTORY_ENTRY_SIZE
        payload_offset, stored_size, uncompressed_size, method = \
            struct.unpack_from("<IIIB", container, entry)
        payload_start = data_offset + payload_offset

        if method not in (METHOD_STORED, METHOD_DEFLATE, METHOD_RAW):
            raise VerificationError(f"lump {index}: unknown method {method}")
        if method == METHOD_RAW and version < 2:
            raise VerificationError(f"lump {index}: method 2 in a version {version} container")
        if payload_offset + stored_size > total_size - data_offset:
            raise VerificationError(f"lump {index}: payload runs past the data area")
        if payload_offset < previous_end:
            raise VerificationError(f"lump {index}: payload overlaps the previous entry")

        if method == METHOD_RAW:
            raw_entries += 1
            if stored_size != uncompressed_size:
                raise VerificationError(f"lump {index}: raw sizes disagree")
            if payload_start % RAW_ALIGNMENT != 0:
                raise VerificationError(
                    f"lump {index}: raw payload starts at {payload_start}, "
                    f"not on a {RAW_ALIGNMENT}-byte boundary")
            gap = payload_offset - previous_end
            if gap > RAW_MAX_PADDING:
                raise VerificationError(
                    f"lump {index}: {gap} bytes in front of a raw payload, "
                    f"at most {RAW_MAX_PADDING} are allowed")
            for at in range(data_offset + previous_end, payload_start):
                if container[at] != 0:
                    raise VerificationError(
                        f"lump {index}: the padding byte at {at} is {container[at]}, not 0")

        previous_end = payload_offset + stored_size

        payload = container[payload_start:payload_start + stored_size]
        if method in (METHOD_STORED, METHOD_RAW):
            if stored_size != uncompressed_size:
                raise VerificationError(f"lump {index}: stored sizes disagree")
            decoded = payload
        else:
            decoder = zlib.decompressobj(DEFLATE_WINDOW_BITS)
            try:
                decoded = decoder.decompress(payload) + decoder.flush()
            except zlib.error as error:
                raise VerificationError(f"lump {index}: {error}") from error
            if not decoder.eof:
                raise VerificationError(f"lump {index}: the DEFLATE stream does not end")

        if len(decoded) != uncompressed_size:
            raise VerificationError(
                f"lump {index}: decodes to {len(decoded)} bytes, directory says {uncompressed_size}")
        if index < source_count and decoded != lumps[index].data:
            raise VerificationError(f"lump {index} ({lumps[index].name}): bytes differ from the source WAD")

    for index in range(name_count):
        entry = name_table_offset + index * NAME_ENTRY_SIZE
        raw_name = container[entry:entry + NAME_SIZE]
        (lump_index,) = struct.unpack_from("<I", container, entry + 8)
        if lump_index >= num_lumps:
            raise VerificationError(f"name entry {index} points at lump {lump_index}")
        if lump_index < source_count:
            expected = lumps[lump_index].name.encode("latin-1")[:NAME_SIZE]
            if raw_name.rstrip(b"\0") != expected:
                raise VerificationError(
                    f"name entry {index} is {raw_name!r}, expected {expected!r}")

    return raw_entries


# --- Reporting --------------------------------------------------------------


def _format_bytes(value: int) -> str:
    return f"{value:,} B"


def stats_report(container: bytes, encoded: list[EncodedLump], padding: list[int],
                 source_bytes: int, identification: bytes,
                 selection: RawSelection,
                 baked_manifest: bytes | None = None,
                 baked_set: bake.BakedSet | None = None,
                 dropped_count: int = 0,
                 dropped_bytes: int = 0,
                 budget: int = FLASH_BUDGET) -> str:
    """Renders the --stats block: sizes, budget, the raw split and the bake."""
    output_bytes = len(container)
    uncompressed_total = sum(lump.uncompressed_size for lump in encoded)
    stored_total = sum(lump.stored_size for lump in encoded)
    raw_total = sum(lump.stored_size for lump in encoded if lump.method == METHOD_RAW)
    padding_total = sum(padding)
    compressed_total = stored_total - raw_total
    metadata = output_bytes - stored_total - padding_total

    empty = sum(1 for lump in encoded if lump.uncompressed_size == 0)
    raw_count = sum(1 for lump in encoded if lump.method == METHOD_RAW)
    deflated = sum(1 for lump in encoded if lump.method == METHOD_DEFLATE)
    stored = sum(1 for lump in encoded if lump.method == METHOD_STORED) - empty

    measured = [(lump, lump.uncompressed_size / lump.stored_size)
                for lump in encoded if lump.uncompressed_size > 0 and lump.stored_size > 0]
    ratios = sorted(ratio for _, ratio in measured)

    def median(values: list[float]) -> float:
        if not values:
            return 0.0
        middle = len(values) // 2
        if len(values) % 2 == 1:
            return values[middle]
        return (values[middle - 1] + values[middle]) / 2.0

    worst = sorted(measured, key=lambda item: (item[1], item[0].name))[:3]

    savings = source_bytes - output_bytes
    spare = budget - output_bytes

    # What the levels kept raw cost in the container: their ten lumps and the
    # padding that aligns each. This is the episode's map floor; the bake is the
    # other half of the flash bill.
    raw_level_bytes = None
    if selection.level_indices:
        raw_level_bytes = sum(padding[index] + encoded[index].stored_size
                              for index in sorted(selection.level_indices))

    lines = [
        f"  input            {_format_bytes(source_bytes)}  {identification.decode('ascii')}",
        f"  output           {_format_bytes(output_bytes)}  "
        f"({output_bytes * 100.0 / source_bytes:.1f}% of input, {source_bytes / output_bytes:.2f}x)",
        f"  metadata         {_format_bytes(metadata)}  header + directory + name table + padding",
        f"  compressed       {_format_bytes(compressed_total)}  "
        f"{deflated:,} deflated + {stored:,} stored payloads",
        f"  raw              {_format_bytes(raw_total)}  {raw_count:,} raw lumps, "
        f"{padding_total:,} B of alignment padding",
        f"  lump payloads    {_format_bytes(stored_total + padding_total)}  from "
        f"{_format_bytes(uncompressed_total)} of lump bytes",
        f"  wad budget       {_format_bytes(budget)}  "
        + ("FITS" if spare >= 0 else "DOES NOT FIT")
        + f", {_format_bytes(abs(spare))} {'spare' if spare >= 0 else 'over'}"
        + f" ({output_bytes * 100.0 / budget:.1f}% used)",
        f"  lumps            {len(encoded):,} ({empty:,} empty, {deflated:,} deflated, "
        f"{stored:,} stored, {raw_count:,} raw)",
        f"  savings          {_format_bytes(savings)} of {_format_bytes(source_bytes)}",
    ]

    if dropped_count:
        lines.append(
            f"  dropped          {dropped_count:,} lumps, {_format_bytes(dropped_bytes)} "
            f"removed by --drop-lumps/--drop-level/--drop-unused-sprites")
    if baked_manifest is not None and baked_set is not None:
        lines.append(bake.manifest_line(baked_manifest))
        entries = bake.parse_manifest(baked_manifest)

        def baked_bytes(kind: int) -> int:
            return sum(encoded[entry.lump_index].stored_size
                       for entry in entries if entry.kind == kind)

        lines.append(
            f"  baked payloads   {_format_bytes(baked_bytes(bake.KIND_WALL))} walls, "
            f"{_format_bytes(baked_bytes(bake.KIND_SKY))} sky, "
            f"{_format_bytes(baked_bytes(bake.KIND_FLAT))} flats, "
            f"{_format_bytes(baked_bytes(bake.KIND_SPRITE))} sprites")
        if baked_set.per_level:
            lines.append(
                f"  bake union       {baked_set.count(bake.KIND_WALL):,} textures, "
                f"{baked_set.count(bake.KIND_SKY):,} sky, "
                f"{baked_set.count(bake.KIND_FLAT):,} flats, "
                f"{baked_set.count(bake.KIND_SPRITE):,} sprite lumps from "
                f"{len(baked_set.per_level)} levels")

    if raw_level_bytes is not None:
        lines.append(
            f"  raw levels       {_format_bytes(raw_level_bytes)}  "
            f"{len(selection.level_indices):,} map lumps kept raw and mappable")
    else:
        lines.append("  raw levels       n/a  (no --raw-level)")

    # D3.10: one bake would still be a bake. If a level were packed alone, would
    # its own bake fit the partition? The union has to answer that per level,
    # because the largest is the one that decides the layout.
    if baked_set is not None and baked_set.per_level:
        for row in baked_set.per_level:
            alone = row.container_bytes()
            verdict = "FITS" if alone <= budget else "DOES NOT FIT"
            lines.append(
                f"  bake alone       {row.level} {_format_bytes(alone)}  "
                f"{verdict}, {_format_bytes(abs(budget - alone))} "
                f"{'spare' if alone <= budget else 'over'}  "
                f"({row.walls} walls + {row.skies} sky, {row.flats} flats, "
                f"{row.sprites} sprites)")

    if ratios:
        lines.append(f"  ratio per lump   min {ratios[0]:.2f}x  median {median(ratios):.2f}x  "
                     f"max {ratios[-1]:.2f}x  (uncompressed / stored)")
    if worst:
        lines.append("  worst lumps      "
                     + ", ".join(f"{lump.name} {ratio:.2f}x" for lump, ratio in worst))

    return "\n".join(lines)


def _raw_lump_by_name(container: bytes, name: str) -> bytes | None:
    """Returns a raw lump's bytes by name, or None when it is absent."""
    num_lumps, name_count, directory_offset, name_table_offset, data_offset, _total = \
        struct.unpack_from("<IIIIII", container, 8)
    key = name.upper().encode("latin-1")[:NAME_SIZE]
    for index in range(name_count):
        entry = name_table_offset + index * NAME_ENTRY_SIZE
        if container[entry:entry + NAME_SIZE].rstrip(b"\0") != key:
            continue
        (lump_index,) = struct.unpack_from("<I", container, entry + 8)
        if lump_index >= num_lumps:
            return None
        payload_offset, stored_size, _uncompressed, method = \
            struct.unpack_from("<IIIB", container, directory_offset + lump_index * DIRECTORY_ENTRY_SIZE)
        if method != METHOD_RAW:
            return None
        start = data_offset + payload_offset
        return container[start:start + stored_size]
    return None


def container_stats_report(container: bytes, budget: int = FLASH_BUDGET) -> str:
    """Renders the --stats block for a container read back from disk.

    Every number is measured from the bytes, so this is what the image on the
    device actually holds. A level's cost cannot be inferred from a finished
    container (its directory does not say which lumps were selected as a level),
    so the per-level bake answer comes from the encode run, where the selection
    and the bake are known.
    """
    num_lumps, name_count, directory_offset, name_table_offset, data_offset, total_size = \
        struct.unpack_from("<IIIIII", container, 8)

    raw_total = 0
    compressed_total = 0
    padding_total = 0
    raw_count = 0
    deflated = 0
    stored = 0
    previous_end = 0
    for index in range(num_lumps):
        entry = directory_offset + index * DIRECTORY_ENTRY_SIZE
        payload_offset, stored_size, _uncompressed, method = \
            struct.unpack_from("<IIIB", container, entry)
        if method == METHOD_RAW:
            raw_count += 1
            raw_total += stored_size
            padding_total += payload_offset - previous_end
        elif method == METHOD_DEFLATE:
            deflated += 1
            compressed_total += stored_size
        else:
            stored += 1
            compressed_total += stored_size
        previous_end = payload_offset + stored_size

    metadata = total_size - raw_total - compressed_total - padding_total
    spare = budget - total_size
    lines = [
        f"  output           {_format_bytes(total_size)}  from {num_lumps:,} lumps",
        f"  metadata         {_format_bytes(metadata)}  header + directory + name table + padding",
        f"  compressed       {_format_bytes(compressed_total)}  "
        f"{deflated:,} deflated + {stored:,} stored payloads",
        f"  raw              {_format_bytes(raw_total)}  {raw_count:,} raw lumps, "
        f"{padding_total:,} B of alignment padding",
        f"  wad budget       {_format_bytes(budget)}  "
        + ("FITS" if spare >= 0 else "DOES NOT FIT")
        + f", {_format_bytes(abs(spare))} {'spare' if spare >= 0 else 'over'}"
        + f" ({total_size * 100.0 / budget:.1f}% used)",
        "  bake alone       n/a  (a finished container does not record its bake "
        "selection; use the encode run)",
    ]
    manifest = _raw_lump_by_name(container, bake.MANIFEST_NAME)
    if manifest is not None:
        lines.append(bake.manifest_line(manifest))
    return "\n".join(lines)


def source_beside(container_path: Path) -> Path | None:
    """Finds the WAD a container was built from, if it sits beside it.

    The check is by stem, so ``tools/DOOM1.whd`` finds ``tools/DOOM1.WAD``. It
    only enables the byte-for-byte comparison: without it the check still runs,
    structurally, and says so.
    """
    for suffix in (".WAD", ".wad", ".Wad"):
        candidate = container_path.with_suffix(suffix)
        if candidate.is_file():
            return candidate
    return None


# --- Command line -----------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    epilog = """\
container layout (see lib/doom/whd.h for the full description):
  40-byte header   magic "PWHD", version 2, counts, three section offsets,
                   total size, CRC-32 of the source WAD
  16-byte entries  payload offset, stored size, uncompressed size, method
  12-byte names    8-byte name and the lump index it resolves to
  payloads         back to back: the lump itself, one raw DEFLATE stream, or a
                   raw lump (the lump itself, 4-byte aligned, preceded by up to
                   three zero padding bytes)

raw selection:
  --raw NAMES      keep those lumps verbatim and 4-byte aligned, so a reader can
                   use them in place instead of decoding them into RAM
  --raw-level LVL  keep that level's ten lumps raw: THINGS LINEDEFS SIDEDEFS
                   VERTEXES SEGS SSECTORS NODES SECTORS REJECT BLOCKMAP
                   (repeatable and comma separated: the shipped container names
                   all nine, so every map is mappable)
  (default)        no raw lumps: every payload is deflate or stored, as before

dropping and baking (both default off; the command records the choice):
  --drop-lumps GLOB   remove every lump whose name matches the glob, case
                      insensitively. ``--drop-lumps 'DS*'`` removes the 55 PCM
                      sound effects this port can never play (APU synthesis
                      only), which frees about 389 KB of the container.
  --drop-level LVL    remove a level marker and its ten data lumps.
  --drop-unused-sprites  with --bake, remove the S_START..S_END lumps the bake's
                      sprite list does not name. The shareware WAD's 447
                      unreferenced frames are 493 KB compressed and are the
                      difference between the episode fitting and not.
  --bake LVL[,LVL]    compose the union of those levels' wall textures and sky
                      into raw width*128 payloads, mark their flats and sprite
                      lumps raw, and append one ~PWBAKE~ manifest. One payload
                      per distinct texture, flat and sprite lump: the union is
                      deduplicated, not one bake per level. Requires TEXTURE1,
                      PNAMES and --sprite-list for the sprite names.
  --sprite-list PATH  the sprite lumps the bake keeps: the generated .inc form
                      or one name per line.
  --sky NAME          the sky texture to bake (default SKY1).

budget and proofs:
  --budget BYTES   the wad partition the container must fit (default 3,145,728 B,
                   the shipped 3 MiB partition). --stats reports total, spare and
                   what each level would cost if it were baked alone.

--check:
  a WAD input is rebuilt and every lump is compared byte for byte, raw ones
  included; a container input is parsed and verified, and compared byte for
  byte against the WAD beside it (same stem, .WAD/.wad) when one exists.

--check-levels:
  a container input only. Compiles test/unit/test_whd/tool/levels_check.c.inc
  against lib/doom/whd.c and lib/doom/doom_map.c, opens E1M1..E1M9 out of the
  container through doom_map_open(), prints each level's lump bytes and record
  counts, and compares both with the WAD beside it. A shifted or truncated set
  that still opens is a failure here, which is the D3.6 bug class.

exit codes:
  0  success
  1  the input is missing, unreadable, not a WAD or not a container
  2  the command line is wrong
  3  the output exists and --force was not given
  4  verification failed (--check only)
"""
    parser = argparse.ArgumentParser(
        prog="whd_encode.py",
        description="Pack a DOOM IWAD/PWAD into a PWHD container: compressed, "
                    "randomly accessible by lump index, sized for the wad partition.",
        epilog=epilog,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("input", type=Path, metavar="INPUT",
                        help="IWAD or PWAD to read, or a PWHD container with --check")
    parser.add_argument("-o", "--output", type=Path, metavar="PATH",
                        help="container to write; required unless --check is used")
    parser.add_argument("--check", action="store_true",
                        help="re-decode the input in memory and compare every lump "
                             "against the source; writes nothing and cannot be combined "
                             "with --output")
    parser.add_argument("--check-levels", action="store_true",
                        help="open all nine shareware levels out of a container through "
                             "lib/doom/doom_map.c, print each level's lump bytes and "
                             "record counts, and compare them with the WAD beside it")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler for --check-levels (default: gcc)")
    parser.add_argument("--stats", action="store_true",
                        help="print input size, output size, the compressed/raw split, "
                             "whether the result fits the wad budget and what each "
                             "level's bake would cost alone")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output file")
    parser.add_argument("--level", type=int, default=9, choices=range(1, 10), metavar="{1-9}",
                        help="DEFLATE level, 1 (fastest) to 9 (smallest, the default)")
    parser.add_argument("--raw", action="append", default=[], metavar="NAME[,NAME...]",
                        help="keep these lumps verbatim and 4-byte aligned, so the "
                             "device can read them in place; repeatable, comma separated")
    parser.add_argument("--raw-level", action="append", default=[], metavar="LEVEL",
                        help="keep a level's ten lumps raw; repeatable")
    parser.add_argument("--drop-lumps", action="append", default=[], metavar="GLOB",
                        help="remove every lump whose name matches the glob; repeatable, "
                             "comma separated")
    parser.add_argument("--drop-level", action="append", default=[], metavar="LEVEL",
                        help="remove a level marker and its ten data lumps; repeatable")
    parser.add_argument("--bake", action="append", default=[], metavar="LEVEL[,LEVEL...]",
                        help="bake the union of these levels' wall textures, sky, flats "
                             "and sprite lumps into raw payloads plus one manifest; "
                             "repeatable, comma separated")
    parser.add_argument("--drop-unused-sprites", action="store_true",
                        help="with --bake, drop every S_START..S_END lump the bake's "
                             "sprite list does not name (the shareware WAD's 447 "
                             "frame-0-unreachable animation frames)")
    parser.add_argument("--sky", default=None, metavar="NAME",
                        help="sky texture to bake (default SKY1)")
    parser.add_argument("--budget", type=int, default=FLASH_BUDGET, metavar="BYTES",
                        help=f"the wad partition the container must fit (default "
                             f"{FLASH_BUDGET:,} B, the shipped partitions_wad.csv)")
    parser.add_argument("--sprite-list", type=Path, metavar="PATH",
                        help="file naming the sprite lumps --bake keeps: the generated "
                             ".inc form or one name per line")
    return parser


def _split_names(values: list[str]) -> list[str]:
    names: list[str] = []
    for value in values:
        names.extend(part for part in value.split(",") if part.strip())
    return names


def _verify(container: bytes, lumps: list[Lump] | None, source_label: str | None) -> int:
    """Runs verify_container and prints the one-line result. Returns raw count."""
    raw_entries = verify_container(container, lumps)
    num_lumps = struct.unpack_from("<I", container, 8)[0]
    total = sum(
        struct.unpack_from("<I", container, HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE + 8)[0]
        for index in range(num_lumps))
    if source_label is not None and lumps is not None and num_lumps > len(lumps):
        compared = (f"byte for byte for the {len(lumps):,} WAD lumps; "
                    f"{num_lumps - len(lumps):,} appended baked lumps structurally")
    elif source_label is not None:
        compared = f"byte for byte against {source_label}"
    else:
        compared = "structurally (no source WAD beside the container)"
    print(f"  verified         {total:,} B in {num_lumps:,} lumps, {compared}")
    print(f"  raw alignment    {raw_entries:,} raw lumps, every payload on a "
          f"{RAW_ALIGNMENT}-byte boundary")
    return raw_entries


# --- The nine-level referee -------------------------------------------------


def compile_level_checker(workdir: Path, compiler: str) -> Path:
    """Builds test/unit/test_whd/tool/levels_check.c.inc against whd.c and doom_map.c."""
    root = Path(__file__).resolve().parent.parent
    executable = workdir / ("levels_check.exe" if sys.platform == "win32"
                            else "levels_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        "-x", "c",
        str(root / "test" / "unit" / "test_whd" / "tool" / "levels_check.c.inc"),
        str(root / "lib" / "doom" / "whd.c"),
        str(root / "lib" / "doom" / "doom_map.c"),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the nine-level harness did not compile:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the nine-level harness compiled with warnings, which this "
                           "project does not accept:\n" + result.stderr)
    return executable


def expected_level_sizes(wad_path: Path) -> dict[str, list[int]]:
    """The source WAD's own size for each of the ten lumps of every level."""
    _, lumps, _ = read_wad(wad_path)
    by_name: dict[str, int] = {}
    for index, lump in enumerate(lumps):
        by_name.setdefault(lump.name.upper()[:NAME_SIZE], index)

    expected: dict[str, list[int]] = {}
    for level in SHAREWARE_LEVELS:
        marker = by_name.get(level)
        if marker is None:
            raise InputError(f"--check-levels: the WAD has no {level}")
        following = [lump.name.upper() for lump in lumps[marker + 1:marker + 1 + len(MAP_LUMP_NAMES)]]
        if following != list(MAP_LUMP_NAMES):
            raise InputError(f"--check-levels: {level} is not followed by the ten map lumps")
        expected[level] = [len(lumps[marker + 1 + offset].data)
                           for offset in range(len(MAP_LUMP_NAMES))]
    return expected


def parse_level_line(line: str) -> tuple[str, str, dict[str, tuple[int, int]]] | None:
    """Parses one `LEVEL ...` line into (name, status, {lump: (bytes, count)})."""
    fields = line.split()
    if len(fields) < 3 or fields[0] != "LEVEL":
        return None
    status = fields[2].split("=", 1)[1] if "=" in fields[2] else ""
    measured: dict[str, tuple[int, int]] = {}
    for field in fields[3:]:
        if "=" not in field:
            continue
        key, value = field.split("=", 1)
        if key in MAP_LUMP_NAMES and "/" in value:
            size, count = value.split("/", 1)
            measured[key] = (int(size), int(count))
    return fields[1], status, measured


def check_levels(arguments: argparse.Namespace, parser: argparse.ArgumentParser) -> int:
    """``--check-levels``: opens all nine maps out of the container through C."""
    if (arguments.output is not None or arguments.check or arguments.raw
            or arguments.raw_level or arguments.drop_lumps or arguments.drop_level
            or arguments.bake or arguments.sprite_list or arguments.sky is not None
            or arguments.drop_unused_sprites):
        parser.error("--check-levels reads a container as it is; it writes nothing "
                     "and selects nothing")

    try:
        container = arguments.input.read_bytes()
    except OSError as error:
        print(f"whd_encode: {arguments.input}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT
    if container[0:4] != MAGIC:
        print(f"whd_encode: {arguments.input}: --check-levels needs a PWHD container",
              file=sys.stderr)
        return EXIT_INPUT

    try:
        raw_entries = verify_container(container, None)
    except VerificationError as error:
        print(f"whd_encode: verification failed: {error}", file=sys.stderr)
        return EXIT_VERIFY

    expected = None
    wad_path = source_beside(arguments.input)
    if wad_path is not None:
        try:
            expected = expected_level_sizes(wad_path)
        except InputError as error:
            print(f"whd_encode: {error}", file=sys.stderr)
            return EXIT_INPUT

    workdir = Path(tempfile.mkdtemp(prefix="levels_check_"))
    try:
        executable = compile_level_checker(workdir, arguments.cc)
        result = subprocess.run([str(executable), str(arguments.input)],
                                capture_output=True, text=True)
    except RuntimeError as error:
        print(f"whd_encode: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    if result.returncode not in (EXIT_OK, EXIT_INPUT):
        print(result.stderr or result.stdout, file=sys.stderr)
        print("whd_encode: the nine-level harness did not run cleanly", file=sys.stderr)
        return EXIT_USAGE

    print(f"{arguments.input}: {len(container):,} B, {raw_entries:,} raw lumps")
    failures = 0
    seen = 0
    for line in result.stdout.splitlines():
        parsed = parse_level_line(line)
        if parsed is None:
            continue
        level, status, measured = parsed
        seen += 1
        if status != "ok":
            print(f"  {level}  {status}")
            failures += 1
            continue
        if expected is not None:
            sizes = expected.get(level)
            if sizes is None:
                print(f"  {level}  not in the source WAD")
                failures += 1
                continue
            problems: list[str] = []
            for offset, name in enumerate(MAP_LUMP_NAMES):
                size, count = measured[name]
                stride = MAP_RECORD_SIZES[offset]
                if size != sizes[offset]:
                    problems.append(f"{name} {size} B != WAD {sizes[offset]} B")
                elif stride != 0 and count != size // stride:
                    problems.append(f"{name} {count} records != {size} B / {stride}")
            if problems:
                print(f"  {level}  FAIL  " + "; ".join(problems))
                failures += 1
                continue
        total = sum(size for size, _ in measured.values())
        print(f"  {level}  ok  {_format_bytes(total)}  "
              + "  ".join(f"{name} {measured[name][0]}/{measured[name][1]}"
                          for name in MAP_LUMP_NAMES))
    if expected is None:
        print("  counts   structural only (no WAD beside the container): "
              "sizes and counts were not compared with the source")
    else:
        print(f"  counts   every lump size and record count equals {wad_path.name}")

    if seen != len(SHAREWARE_LEVELS) or failures != 0:
        print(f"whd_encode: {seen - failures} of {len(SHAREWARE_LEVELS)} levels opened",
              file=sys.stderr)
        return EXIT_VERIFY
    print(f"  levels   {seen} of {len(SHAREWARE_LEVELS)} maps open, raw, and validate")
    return EXIT_OK


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    if arguments.check and arguments.output is not None:
        parser.error("--check writes nothing, so it cannot be combined with --output")
    if arguments.check and arguments.check_levels:
        parser.error("--check and --check-levels are two different proofs; pass one")
    if (not arguments.check and not arguments.check_levels
            and arguments.output is None):
        parser.error("either --output is needed to write a container, --check to verify "
                     "one, or --check-levels to open all nine maps out of one")

    raw_names = _split_names(arguments.raw)
    raw_levels = _split_names(arguments.raw_level)
    drop_patterns = _split_names(arguments.drop_lumps)
    drop_levels = _split_names(arguments.drop_level)
    bake_levels = _split_names(arguments.bake)

    try:
        head = arguments.input.read_bytes()[:4]
    except OSError as error:
        print(f"whd_encode: {arguments.input}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT

    if head == MAGIC:
        if arguments.check_levels:
            return check_levels(arguments, parser)
        return check_container(arguments, parser)
    if head not in (b"IWAD", b"PWAD"):
        print(f"whd_encode: {arguments.input}: neither a WAD nor a PWHD container",
              file=sys.stderr)
        return EXIT_INPUT
    if arguments.check_levels:
        print(f"whd_encode: {arguments.input}: --check-levels needs a PWHD container",
              file=sys.stderr)
        return EXIT_INPUT

    try:
        identification, lumps, source = read_wad(arguments.input)
        dropped = select_dropped(lumps, drop_patterns, drop_levels)

        sprite_names: list[str] = []
        if arguments.sprite_list is not None:
            sprite_names = bake.read_sprite_list(arguments.sprite_list)
        if arguments.drop_unused_sprites:
            if not bake_levels:
                raise InputError("--drop-unused-sprites is a bake decision: pass --bake")
            if not sprite_names:
                raise InputError("--drop-unused-sprites needs --sprite-list: it is the "
                                 "only list of the sprite lumps that stay")
            dropped = dropped | select_unused_sprites(lumps, sprite_names)

        kept = [lump for index, lump in enumerate(lumps) if index not in dropped]
        selection = select_raw(kept, raw_names, raw_levels)

        baked = None
        baked_manifest = None
        wad_kept = kept
        if bake_levels:
            dropped_keys = {level.strip().upper()[:NAME_SIZE] for level in drop_levels}
            for level in bake_levels:
                if level.strip().upper()[:NAME_SIZE] in dropped_keys:
                    raise InputError("--bake: a level cannot be both baked and dropped")
            kept, _records, baked, _appended, baked_raw = build_baked_lumps(
                lumps, kept, bake_levels, arguments.sky or "SKY1", sprite_names)
            selection = RawSelection(selection.indices | frozenset(baked_raw),
                                     selection.level_indices, selection.level_name)
            baked_manifest = next(lump.data for lump in kept if lump.name == bake.MANIFEST_NAME)
    except (InputError, bake.BakeError) as error:
        print(f"whd_encode: {error}", file=sys.stderr)
        return EXIT_INPUT

    source_bytes = len(source)
    source_crc = zlib.crc32(source) & 0xFFFFFFFF
    dropped_bytes = sum(len(lumps[index].data) for index in dropped)
    container, encoded, padding = build_container(kept, arguments.level, source_crc, selection)

    if arguments.stats or arguments.check:
        print(f"{arguments.input}: {len(lumps):,} lumps, {_format_bytes(source_bytes)}")
        print(stats_report(container, encoded, padding, source_bytes, identification, selection,
                           baked_manifest=baked_manifest, baked_set=baked,
                           dropped_count=len(dropped), dropped_bytes=dropped_bytes,
                           budget=arguments.budget))

    if arguments.check:
        try:
            _verify(container, wad_kept, str(arguments.input))
        except VerificationError as error:
            print(f"whd_encode: verification failed: {error}", file=sys.stderr)
            return EXIT_VERIFY
        return EXIT_OK

    output = arguments.output
    if output.exists() and not arguments.force:
        print(f"whd_encode: {output} exists; pass --force to overwrite", file=sys.stderr)
        return EXIT_OUTPUT_EXISTS

    try:
        output.write_bytes(container)
    except OSError as error:
        print(f"whd_encode: {output}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT

    if arguments.stats:
        print(f"  written          {output}")
    return EXIT_OK


def check_container(arguments: argparse.Namespace,
                    parser: argparse.ArgumentParser) -> int:
    """``--check`` on a container: verify it, and compare against its source WAD."""
    if (arguments.raw or arguments.raw_level or arguments.drop_lumps
            or arguments.drop_level or arguments.bake or arguments.sprite_list
            or arguments.sky is not None or arguments.drop_unused_sprites):
        parser.error("--raw, --raw-level, --drop-lumps, --drop-level, --bake, "
                     "--sprite-list and --drop-unused-sprites select what to write; "
                     "a container is checked as it is")

    try:
        container = arguments.input.read_bytes()
    except OSError as error:
        print(f"whd_encode: {arguments.input}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT

    num_lumps = struct.unpack_from("<I", container, 8)[0]
    lumps: list[Lump] | None = None
    label: str | None = None
    candidate = source_beside(arguments.input)
    baked = _raw_lump_by_name(container, bake.MANIFEST_NAME) is not None
    if candidate is not None:
        try:
            _, source_lumps, _ = read_wad(candidate)
            # Index alignment only exists when the container was not built with
            # a drop or a bake; a baked container is verified structurally and by
            # tools/bake_graphics.py's C check, not by a WAD-wide byte diff.
            if not baked and num_lumps == len(source_lumps):
                lumps = source_lumps
                label = str(candidate)
        except InputError as error:
            print(f"whd_encode: {error}", file=sys.stderr)
            return EXIT_INPUT

    try:
        raw_entries = verify_container(container, lumps)
    except VerificationError as error:
        print(f"whd_encode: verification failed: {error}", file=sys.stderr)
        return EXIT_VERIFY

    print(f"{arguments.input}: {num_lumps:,} lumps, {_format_bytes(len(container))}")
    if arguments.stats:
        print(container_stats_report(container, arguments.budget))
    total = sum(struct.unpack_from("<I", container,
                                  HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE + 8)[0]
                for index in range(num_lumps))
    compared = (f"byte for byte against {label}" if label is not None
                else "structurally (a baked/dropped container has no WAD index alignment)")
    print(f"  verified         {_format_bytes(total)} in {num_lumps:,} lumps, {compared}")
    print(f"  raw alignment    {raw_entries:,} raw lumps, every payload on a "
          f"{RAW_ALIGNMENT}-byte boundary")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
