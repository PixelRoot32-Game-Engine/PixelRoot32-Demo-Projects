#!/usr/bin/env python3
"""Prove every baked status-bar patch is present, raw, and the source patch.

``tools/gen_hud_lumps.py`` lists the pictures the status bar draws. This gate
opens a finished container and, for each name, requires:

* a lump of that name in the container's name table;
* that lump stored RAW (method 2) and 4-byte aligned: a baked picture is read
  in place from the mapped partition, so a deflated one cannot be drawn without
  a zone allocation (``whd_encode.py --raw-list``);
* a payload that equals the source WAD's lump byte for byte, and that decodes
  as a patch (``bake_graphics.parse_patch``, every rule ``r_patch_validate``
  enforces) to the same columns the source decodes to.

The pictures are not manifest records: 86 more would overflow DRAM (see
``gen_hud_lumps.py``). The gate also requires the manifest NOT to have grown past
``kBakedCapacity`` and to carry no HUD name. A gate that cannot fail proves only that it runs, so ``--perturb`` flips one
payload byte in memory and the run must then exit non-zero.

    python tools/hud_check.py --container tools/DOOM1.whd --wad tools/DOOM1.WAD
    python tools/hud_check.py --container ... --wad ... --perturb   # must FAIL

Exit 0 when every name passed, 1 when at least one failed, 2 on unreadable input.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import bake_graphics as bake  # noqa: E402
import gen_hud_lumps  # noqa: E402
import whd_encode  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def capacity() -> int:
    text = (ROOT / "src" / "doomc" / "BakedGraphics.h").read_text(encoding="utf-8")
    match = re.search(r"kBakedCapacity\s*=\s*(\d+)", text)
    if match is None:
        raise SystemExit("hud_check: kBakedCapacity not found in BakedGraphics.h")
    return int(match.group(1))


def lump_table(container: bytes) -> tuple[dict[str, int], list[tuple[int, int, int, int]]]:
    """Name -> lump index, and (payload start, stored, uncompressed, method) per lump."""
    num_lumps, name_count, directory_offset, name_table_offset, data_offset, _ = \
        struct.unpack_from("<IIIIII", container, 8)
    names: dict[str, int] = {}
    for index in range(name_count):
        entry = name_table_offset + index * whd_encode.NAME_ENTRY_SIZE
        raw = container[entry:entry + whd_encode.NAME_SIZE].rstrip(b"\0")
        (lump_index,) = struct.unpack_from("<I", container, entry + 8)
        names.setdefault(raw.decode("latin-1").upper(), lump_index)
    rows = []
    for index in range(num_lumps):
        offset, stored, uncompressed, method = struct.unpack_from(
            "<IIIB", container, directory_offset + index * whd_encode.DIRECTORY_ENTRY_SIZE)
        rows.append((data_offset + offset, stored, uncompressed, method))
    return names, rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--container", type=Path, required=True)
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--perturb", action="store_true",
                        help="flip one payload byte in memory; the run must then fail")
    arguments = parser.parse_args(argv)

    try:
        container = bytearray(arguments.container.read_bytes())
        _, lumps, _ = whd_encode.read_wad(arguments.wad)
    except (OSError, whd_encode.InputError) as error:
        print(f"hud_check: {error}", file=sys.stderr)
        return 2

    source = {lump.name.upper(): lump.data for lump in lumps}
    names, rows = lump_table(bytes(container))
    manifest_index = names.get(bake.MANIFEST_NAME)
    if manifest_index is None:
        print("hud_check: the container has no ~PWBAKE~ manifest", file=sys.stderr)
        return 1
    start, stored, _, _ = rows[manifest_index]
    entries = bake.parse_manifest(bytes(container[start:start + stored]))

    failures: list[str] = []
    if len(entries) > capacity():
        failures.append(f"manifest holds {len(entries)} records, kBakedCapacity is {capacity()}")

    hud = gen_hud_lumps.hud_names()
    in_manifest = [entry.name for entry in entries if entry.name.upper() in set(hud)]
    if in_manifest:
        failures.append(f"HUD names registered in the manifest (DRAM slots): {in_manifest[:3]}")
    if arguments.perturb:
        first = hud[0]
        if first in names:
            container[rows[names[first]][0]] ^= 0xFF

    raw_bytes = 0
    for name in hud:
        index = names.get(name)
        if index is None:
            failures.append(f"{name}: not in the container")
            continue
        payload_start, stored_size, _, method = rows[index]
        if method != whd_encode.METHOD_RAW:
            failures.append(f"{name}: stored with method {method}, a baked picture is raw")
            continue
        if payload_start % whd_encode.RAW_ALIGNMENT != 0:
            failures.append(f"{name}: raw payload at {payload_start} is not 4-byte aligned")
            continue
        payload = bytes(container[payload_start:payload_start + stored_size])
        if payload != source[name]:
            failures.append(f"{name}: payload differs from the source WAD lump")
            continue
        try:
            baked_patch = bake.parse_patch(name, payload)
            source_patch = bake.parse_patch(name, source[name])
        except bake.BakeError as error:
            failures.append(f"{name}: does not decode as a patch: {error}")
            continue
        if baked_patch != source_patch:
            failures.append(f"{name}: decoded columns differ from the source patch")
            continue
        raw_bytes += stored_size

    for failure in failures:
        print(f"HUDCHECK,fail,{failure}")
    print(f"HUDCHECK,names={len(hud)},failures={len(failures)},raw_bytes={raw_bytes},"
          f"manifest={len(entries)}/{capacity()},perturb={int(arguments.perturb)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
