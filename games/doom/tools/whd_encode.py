#!/usr/bin/env python3
"""Pack a DOOM IWAD/PWAD into a PWHD container for the ESP32 port.

WHY THIS TOOL EXISTS

The shareware DOOM1.WAD is 4,196,020 bytes; the board has 4,194,304 bytes of
flash in total and the application partition needs about 2 MB. The WAD therefore
fits nowhere on the device, yet the renderer still needs random access to anyone
of its 1264 lumps. This tool writes the container described in
``lib/doom/whd.h``: a fixed 40-byte header, a fixed-size directory with each
lump's stored and uncompressed size, a name table, and the payloads back to back.

Each payload is either the lump itself (method 0, "stored") or one raw DEFLATE
stream (method 1, RFC 1951 with no zlib wrapper, what ``zlib`` calls windowBits
-15). The encoder keeps whichever is smaller, so a lump never grows.

This is NOT the rp2040-doom "IWHD" container. That one stores lumps verbatim and
gets its size from a separate DOOM-specific re-encode pass; see the comment at
the top of ``lib/doom/whd.h`` for the full comparison.

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
VERSION = 1
HEADER_SIZE = 40
DIRECTORY_ENTRY_SIZE = 16
NAME_ENTRY_SIZE = 12
NAME_SIZE = 8

METHOD_STORED = 0
METHOD_DEFLATE = 1

# Raw DEFLATE: the zlib wrapper is what the device decoder does not have.
DEFLATE_WINDOW_BITS = -15
DEFLATE_MEMORY_LEVEL = 9

# The application partition the container has to fit in.
FLASH_BUDGET = 2_097_152

WAD_HEADER_SIZE = 12
WAD_DIRECTORY_ENTRY_SIZE = 16

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


def encode_lump(lump: Lump, level: int) -> EncodedLump:
    """Stores @p lump with whichever method makes it smaller.

    A stored lump wins ties: it is the cheaper of the two to read on the device,
    and it keeps the output independent of the compressor's block choices for
    data it cannot shrink.
    """
    if not lump.data:
        return EncodedLump(lump.name, b"", 0, METHOD_STORED)

    compressed = deflate(lump.data, level)
    if len(compressed) < len(lump.data):
        return EncodedLump(lump.name, compressed, len(lump.data), METHOD_DEFLATE)
    return EncodedLump(lump.name, lump.data, len(lump.data), METHOD_STORED)


def build_container(lumps: list[Lump], level: int, source_crc: int) -> tuple[bytes, list[EncodedLump]]:
    """Assembles the container: header, directory, name table, payloads."""
    encoded = [encode_lump(lump, level) for lump in lumps]

    num_lumps = len(encoded)
    directory_offset = HEADER_SIZE
    name_table_offset = directory_offset + num_lumps * DIRECTORY_ENTRY_SIZE
    data_offset = name_table_offset + num_lumps * NAME_ENTRY_SIZE

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
        0,                              # totalSize, filled in below
    )
    container += struct.pack("<I", source_crc)
    container += struct.pack("<I", 0)    # reserved
    assert len(container) == HEADER_SIZE

    payload_offset = 0
    for lump in encoded:
        container += struct.pack(
            "<IIIB3x",
            payload_offset,
            lump.stored_size,
            lump.uncompressed_size,
            lump.method,
        )
        payload_offset += lump.stored_size
    assert len(container) == name_table_offset

    for index, lump in enumerate(encoded):
        name = lump.name.encode("latin-1")[:NAME_SIZE]
        container += name.ljust(NAME_SIZE, b"\0")
        container += struct.pack("<I", index)
    assert len(container) == data_offset

    for lump in encoded:
        container += lump.payload

    total_size = len(container)
    struct.pack_into("<I", container, 28, total_size)
    return bytes(container), encoded


# --- Verification -----------------------------------------------------------


def verify_container(container: bytes, lumps: list[Lump]) -> None:
    """Decodes @p container and compares every lump against the source bytes.

    This parses the finished bytes, not the structures that built them, and it
    applies the same rules as whd_open() in lib/doom/whd.c, so a container the
    device would reject cannot pass here.
    """
    if len(container) < HEADER_SIZE:
        raise VerificationError(f"container is {len(container)} bytes, shorter than the header")
    if container[0:4] != MAGIC:
        raise VerificationError(f"magic is {container[0:4]!r}, expected {MAGIC!r}")

    version, flags = struct.unpack_from("<HH", container, 4)
    if version != VERSION:
        raise VerificationError(f"version is {version}, expected {VERSION}")
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
    if num_lumps != len(lumps):
        raise VerificationError(f"container holds {num_lumps} lumps, the WAD holds {len(lumps)}")

    previous_end = 0
    for index in range(num_lumps):
        entry = directory_offset + index * DIRECTORY_ENTRY_SIZE
        payload_offset, stored_size, uncompressed_size, method = struct.unpack_from("<IIIB", container, entry)

        if method not in (METHOD_STORED, METHOD_DEFLATE):
            raise VerificationError(f"lump {index}: unknown method {method}")
        if payload_offset + stored_size > total_size - data_offset:
            raise VerificationError(f"lump {index}: payload runs past the data area")
        if payload_offset < previous_end:
            raise VerificationError(f"lump {index}: payload overlaps the previous entry")
        previous_end = payload_offset + stored_size

        payload = container[data_offset + payload_offset:data_offset + payload_offset + stored_size]
        if method == METHOD_STORED:
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
        if decoded != lumps[index].data:
            raise VerificationError(f"lump {index} ({lumps[index].name}): bytes differ from the source WAD")

    for index in range(name_count):
        entry = name_table_offset + index * NAME_ENTRY_SIZE
        raw_name = container[entry:entry + NAME_SIZE]
        (lump_index,) = struct.unpack_from("<I", container, entry + 8)
        if lump_index >= num_lumps:
            raise VerificationError(f"name entry {index} points at lump {lump_index}")
        expected = lumps[lump_index].name.encode("latin-1")[:NAME_SIZE]
        if raw_name.rstrip(b"\0") != expected:
            raise VerificationError(
                f"name entry {index} is {raw_name!r}, expected {expected!r}")


# --- Reporting --------------------------------------------------------------


def _format_bytes(value: int) -> str:
    return f"{value:,} B"


def stats_report(container: bytes, encoded: list[EncodedLump], source_bytes: int,
                 identification: bytes) -> str:
    """Renders the --stats block: sizes, budget, and the per-lump ratios."""
    output_bytes = len(container)
    uncompressed_total = sum(lump.uncompressed_size for lump in encoded)
    stored_total = sum(lump.stored_size for lump in encoded)

    empty = sum(1 for lump in encoded if lump.uncompressed_size == 0)
    deflated = sum(1 for lump in encoded if lump.method == METHOD_DEFLATE)
    stored = sum(1 for lump in encoded if lump.method == METHOD_STORED) - empty
    metadata = output_bytes - stored_total

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
    lines = [
        f"  input            {_format_bytes(source_bytes)}  {identification.decode('ascii')}",
        f"  output           {_format_bytes(output_bytes)}  "
        f"({output_bytes * 100.0 / source_bytes:.1f}% of input, {source_bytes / output_bytes:.2f}x)",
        f"  metadata         {_format_bytes(metadata)}  header + directory + name table",
        f"  lump payloads    {_format_bytes(stored_total)}  from {_format_bytes(uncompressed_total)}"
        f" of lump bytes ({uncompressed_total / stored_total:.2f}x)",
        f"  2 MiB budget     {_format_bytes(FLASH_BUDGET)}  "
        + ("FITS" if spare >= 0 else "DOES NOT FIT")
        + f", {_format_bytes(abs(spare))} {'spare' if spare >= 0 else 'over'}"
        + f" ({output_bytes * 100.0 / FLASH_BUDGET:.1f}% used)",
        f"  lumps            {len(encoded):,} ({empty:,} empty, {deflated:,} deflated, {stored:,} stored)",
        f"  savings          {_format_bytes(savings)} of {_format_bytes(source_bytes)}",
    ]

    if ratios:
        lines.append(f"  ratio per lump   min {ratios[0]:.2f}x  median {median(ratios):.2f}x  "
                     f"max {ratios[-1]:.2f}x  (uncompressed / stored)")
    if worst:
        lines.append("  worst lumps      "
                     + ", ".join(f"{lump.name} {ratio:.2f}x" for lump, ratio in worst))

    return "\n".join(lines)


# --- Command line -----------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    epilog = """\
container layout (see lib/doom/whd.h for the full description):
  40-byte header   magic "PWHD", version, counts, three section offsets,
                   total size, CRC-32 of the source WAD
  16-byte entries  payload offset, stored size, uncompressed size, method
  12-byte names    8-byte name and the lump index it resolves to
  payloads         back to back: the lump itself, or one raw DEFLATE stream

exit codes:
  0  success
  1  the input is missing, unreadable or not a WAD
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
                        help="IWAD or PWAD to read")
    parser.add_argument("-o", "--output", type=Path, metavar="PATH",
                        help="container to write; required unless --check is used")
    parser.add_argument("--check", action="store_true",
                        help="re-decode the container in memory and compare every lump "
                             "against the source; writes nothing and cannot be combined "
                             "with --output")
    parser.add_argument("--stats", action="store_true",
                        help="print input size, output size, the per-lump ratio summary "
                             "and whether the result fits 2,097,152 B")
    parser.add_argument("--force", action="store_true",
                        help="overwrite an existing output file")
    parser.add_argument("--level", type=int, default=9, choices=range(1, 10), metavar="{1-9}",
                        help="DEFLATE level, 1 (fastest) to 9 (smallest, the default)")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    arguments = parser.parse_args(argv)

    if arguments.check and arguments.output is not None:
        parser.error("--check writes nothing, so it cannot be combined with --output")
    if not arguments.check and arguments.output is None:
        parser.error("either --output is needed to write a container, or --check to verify one")

    try:
        identification, lumps, source = read_wad(arguments.input)
    except InputError as error:
        print(f"whd_encode: {error}", file=sys.stderr)
        return EXIT_INPUT

    source_bytes = len(source)
    source_crc = zlib.crc32(source) & 0xFFFFFFFF
    container, encoded = build_container(lumps, arguments.level, source_crc)

    if arguments.stats or arguments.check:
        print(f"{arguments.input}: {len(lumps):,} lumps, {_format_bytes(source_bytes)}")
        print(stats_report(container, encoded, source_bytes, identification))

    if arguments.check:
        try:
            verify_container(container, lumps)
        except VerificationError as error:
            print(f"whd_encode: verification failed: {error}", file=sys.stderr)
            return EXIT_VERIFY
        total = sum(lump.uncompressed_size for lump in encoded)
        print(f"  verified         {len(encoded):,} lumps, {_format_bytes(total)} byte for byte")
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


if __name__ == "__main__":
    sys.exit(main())
