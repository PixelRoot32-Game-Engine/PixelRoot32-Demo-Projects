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
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path

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

# The application partition the container has to fit in.
FLASH_BUDGET = 2_097_152

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
    if lumps is not None and num_lumps != len(lumps):
        raise VerificationError(f"container holds {num_lumps} lumps, the WAD holds {len(lumps)}")

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
        if lumps is not None and decoded != lumps[index].data:
            raise VerificationError(f"lump {index} ({lumps[index].name}): bytes differ from the source WAD")

    for index in range(name_count):
        entry = name_table_offset + index * NAME_ENTRY_SIZE
        raw_name = container[entry:entry + NAME_SIZE]
        (lump_index,) = struct.unpack_from("<I", container, entry + 8)
        if lump_index >= num_lumps:
            raise VerificationError(f"name entry {index} points at lump {lump_index}")
        expected = (lumps[lump_index].name if lumps is not None
                    else "").encode("latin-1")[:NAME_SIZE]
        if lumps is not None and raw_name.rstrip(b"\0") != expected:
            raise VerificationError(
                f"name entry {index} is {raw_name!r}, expected {expected!r}")

    return raw_entries


# --- Reporting --------------------------------------------------------------


def _format_bytes(value: int) -> str:
    return f"{value:,} B"


def stats_report(container: bytes, encoded: list[EncodedLump], padding: list[int],
                 source_bytes: int, identification: bytes,
                 selection: RawSelection) -> str:
    """Renders the --stats block: sizes, budget, the raw split and nine levels."""
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
    spare = FLASH_BUDGET - output_bytes

    # What one more level would cost in flash: its ten lumps raw, plus the padding
    # that aligns each of them. Eight more levels on top of the one already here
    # is the nine-level question the next decision depends on.
    level_cost: int | None = None
    if selection.level_indices:
        level_cost = sum(padding[index] + encoded[index].stored_size
                         for index in sorted(selection.level_indices))
    nine_level_headroom = None
    if level_cost is not None:
        nine_level_headroom = FLASH_BUDGET - output_bytes - 8 * level_cost

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
        f"  2 MiB budget     {_format_bytes(FLASH_BUDGET)}  "
        + ("FITS" if spare >= 0 else "DOES NOT FIT")
        + f", {_format_bytes(abs(spare))} {'spare' if spare >= 0 else 'over'}"
        + f" ({output_bytes * 100.0 / FLASH_BUDGET:.1f}% used)",
        f"  lumps            {len(encoded):,} ({empty:,} empty, {deflated:,} deflated, "
        f"{stored:,} stored, {raw_count:,} raw)",
        f"  savings          {_format_bytes(savings)} of {_format_bytes(source_bytes)}",
    ]

    if level_cost is not None:
        lines.append(
            f"  nine levels      nine_level_headroom={nine_level_headroom:,} B  "
            f"({_format_bytes(level_cost)} per level, 8 more after "
            f"{selection.level_name} already in the container)")
    else:
        lines.append(
            "  nine levels      nine_level_headroom=n/a  (no --raw-level to size a level)")

    if ratios:
        lines.append(f"  ratio per lump   min {ratios[0]:.2f}x  median {median(ratios):.2f}x  "
                     f"max {ratios[-1]:.2f}x  (uncompressed / stored)")
    if worst:
        lines.append("  worst lumps      "
                     + ", ".join(f"{lump.name} {ratio:.2f}x" for lump, ratio in worst))

    return "\n".join(lines)


def container_stats_report(container: bytes) -> str:
    """Renders the --stats block for a container read back from disk.

    Every number is measured from the bytes, so this is what the image on the
    device actually holds. A level's cost cannot be inferred from a finished
    container (its directory does not say which lumps were selected as a level),
    so the nine-level answer is reported as n/a here and comes from the encode
    run, where the selection is known.
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
    spare = FLASH_BUDGET - total_size
    return "\n".join([
        f"  output           {_format_bytes(total_size)}  from {num_lumps:,} lumps",
        f"  metadata         {_format_bytes(metadata)}  header + directory + name table + padding",
        f"  compressed       {_format_bytes(compressed_total)}  "
        f"{deflated:,} deflated + {stored:,} stored payloads",
        f"  raw              {_format_bytes(raw_total)}  {raw_count:,} raw lumps, "
        f"{padding_total:,} B of alignment padding",
        f"  2 MiB budget     {_format_bytes(FLASH_BUDGET)}  "
        + ("FITS" if spare >= 0 else "DOES NOT FIT")
        + f", {_format_bytes(abs(spare))} {'spare' if spare >= 0 else 'over'}"
        + f" ({total_size * 100.0 / FLASH_BUDGET:.1f}% used)",
        "  nine levels      nine_level_headroom=n/a  (a finished container does not "
        "record its raw selection)",
    ])


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
  (default)        no raw lumps: every payload is deflate or stored, as before

--check:
  a WAD input is rebuilt and every lump is compared byte for byte, raw ones
  included; a container input is parsed and verified, and compared byte for
  byte against the WAD beside it (same stem, .WAD/.wad) when one exists.

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
                    "randomly accessible by lump index, sized for a 2 MiB partition.",
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
    parser.add_argument("--stats", action="store_true",
                        help="print input size, output size, the compressed/raw split, "
                             "whether the result fits 2,097,152 B and the headroom for "
                             "nine levels")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output file")
    parser.add_argument("--level", type=int, default=9, choices=range(1, 10), metavar="{1-9}",
                        help="DEFLATE level, 1 (fastest) to 9 (smallest, the default)")
    parser.add_argument("--raw", action="append", default=[], metavar="NAME[,NAME...]",
                        help="keep these lumps verbatim and 4-byte aligned, so the "
                             "device can read them in place; repeatable, comma separated")
    parser.add_argument("--raw-level", action="append", default=[], metavar="LEVEL",
                        help="keep a level's ten lumps raw; repeatable")
    return parser


def _split_names(values: list[str]) -> list[str]:
    names: list[str] = []
    for value in values:
        names.extend(part for part in value.split(",") if part.strip())
    return names


def _verify(container: bytes, lumps: list[Lump] | None, source_label: str | None) -> int:
    """Runs verify_container and prints the one-line result. Returns raw count."""
    raw_entries = verify_container(container, lumps)
    total = sum(
        struct.unpack_from("<I", container, HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE + 8)[0]
        for index in range(struct.unpack_from("<I", container, 8)[0]))
    compared = (f"byte for byte against {source_label}" if source_label is not None
                else "structurally (no source WAD beside the container)")
    print(f"  verified         {total:,} B in "
          f"{struct.unpack_from('<I', container, 8)[0]:,} lumps, {compared}")
    print(f"  raw alignment    {raw_entries:,} raw lumps, every payload on a "
          f"{RAW_ALIGNMENT}-byte boundary")
    return raw_entries


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    if arguments.check and arguments.output is not None:
        parser.error("--check writes nothing, so it cannot be combined with --output")
    if not arguments.check and arguments.output is None:
        parser.error("either --output is needed to write a container, or --check to verify one")

    raw_names = _split_names(arguments.raw)
    raw_levels = _split_names(arguments.raw_level)

    try:
        head = arguments.input.read_bytes()[:4]
    except OSError as error:
        print(f"whd_encode: {arguments.input}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT

    if head == MAGIC:
        return check_container(arguments, parser)
    if head not in (b"IWAD", b"PWAD"):
        print(f"whd_encode: {arguments.input}: neither a WAD nor a PWHD container",
              file=sys.stderr)
        return EXIT_INPUT

    try:
        identification, lumps, source = read_wad(arguments.input)
        selection = select_raw(lumps, raw_names, raw_levels)
    except InputError as error:
        print(f"whd_encode: {error}", file=sys.stderr)
        return EXIT_INPUT

    source_bytes = len(source)
    source_crc = zlib.crc32(source) & 0xFFFFFFFF
    container, encoded, padding = build_container(lumps, arguments.level, source_crc, selection)

    if arguments.stats or arguments.check:
        print(f"{arguments.input}: {len(lumps):,} lumps, {_format_bytes(source_bytes)}")
        print(stats_report(container, encoded, padding, source_bytes, identification, selection))

    if arguments.check:
        try:
            _verify(container, lumps, str(arguments.input))
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
    if arguments.raw or arguments.raw_level:
        parser.error("--raw and --raw-level select what to write; a container is checked as it is")

    try:
        container = arguments.input.read_bytes()
    except OSError as error:
        print(f"whd_encode: {arguments.input}: {error.strerror}", file=sys.stderr)
        return EXIT_INPUT

    lumps: list[Lump] | None = None
    label: str | None = None
    candidate = source_beside(arguments.input)
    if candidate is not None:
        try:
            _, lumps, _ = read_wad(candidate)
            label = str(candidate)
        except InputError as error:
            print(f"whd_encode: {error}", file=sys.stderr)
            return EXIT_INPUT

    try:
        raw_entries = verify_container(container, lumps)
    except VerificationError as error:
        print(f"whd_encode: verification failed: {error}", file=sys.stderr)
        return EXIT_VERIFY

    num_lumps = struct.unpack_from("<I", container, 8)[0]
    print(f"{arguments.input}: {num_lumps:,} lumps, {_format_bytes(len(container))}")
    if arguments.stats:
        print(container_stats_report(container))
    total = sum(struct.unpack_from("<I", container,
                                  HEADER_SIZE + index * DIRECTORY_ENTRY_SIZE + 8)[0]
                for index in range(num_lumps))
    compared = (f"byte for byte against {label}" if label is not None
                else "structurally (no source WAD beside the container)")
    print(f"  verified         {_format_bytes(total)} in {num_lumps:,} lumps, {compared}")
    print(f"  raw alignment    {raw_entries:,} raw lumps, every payload on a "
          f"{RAW_ALIGNMENT}-byte boundary")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
