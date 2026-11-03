#!/usr/bin/env python3
"""Generate tools/hud_lumps.txt: the status-bar pictures the container bakes.

WHY THIS EXISTS

The status bar draws patches by name, the way ``st_stuff.c`` loads them in
``ST_loadGraphics``. A patch the container only *stores* is deflated and has to
be decoded into the zone before it can be drawn; a patch the container keeps
RAW (``whd_encode.py --raw-list``) is 4-byte aligned and read in place from the
mapped partition, so ``whd_lump_view`` hands out a pointer and drawing it costs
no RAM.

These patches are deliberately NOT registered in the ``~PWBAKE~`` manifest
(``--sprite-list``): the adapter holds one ``entries_`` + ``values_`` slot of
about 50 B of DRAM per record, and the 86 status-bar records would overflow
``dram0_0_seg``. Raw-by-name costs
the same flash and no DRAM.

The names are not typed in: they are the patch names ``ST_loadGraphics`` builds
(its ``sprintf`` formats), expanded here and checked against the WAD, so a name
that does not exist is an error and the file is never hand-edited.

    python tools/gen_hud_lumps.py --wad tools/DOOM1.WAD           # rewrite the file
    python tools/gen_hud_lumps.py --wad tools/DOOM1.WAD --check   # fail on drift

The list, in ``ST_loadGraphics`` order::

    STBAR STARMS
    STTNUM0-9 STTMINUS STTPRCNT      big red digits, minus, percent
    STYSNUM0-9                       yellow arms digits (owned weapon)
    STGNUM0-9                        grey arms digits (not owned)
    STKEYS0-5                        keycards and skulls
    STFB0-3                          the face background per player
    STFST{0-4}{0-2}                  straight-ahead faces by pain level
    STFTR{0-4}0 STFTL{0-4}0          looking right / left
    STFOUCH{0-4} STFEVL{0-4} STFKILL{0-4}   ouch, evil grin, kill
    STFGOD0 STFDEAD0                 god mode and dead
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import whd_encode  # noqa: E402

NAMES_FILE = Path(__file__).resolve().parent / "hud_lumps.txt"
PAIN_LEVELS = 5


def hud_names() -> list[str]:
    """The patch names ST_loadGraphics() loads for the shareware HUD."""
    names = ["STBAR", "STARMS"]
    names += [f"STTNUM{digit}" for digit in range(10)]
    names += ["STTMINUS", "STTPRCNT"]
    names += [f"STYSNUM{digit}" for digit in range(10)]
    names += [f"STGNUM{digit}" for digit in range(10)]
    names += [f"STKEYS{key}" for key in range(6)]
    names += [f"STFB{player}" for player in range(4)]
    for pain in range(PAIN_LEVELS):
        names += [f"STFST{pain}{look}" for look in range(3)]
        names += [f"STFTR{pain}0", f"STFTL{pain}0"]
        names += [f"STFOUCH{pain}", f"STFEVL{pain}", f"STFKILL{pain}"]
    names += ["STFGOD0", "STFDEAD0"]
    return names


def render(names: list[str]) -> str:
    header = ("# GENERATED FILE -- do not edit. Regenerate with:\n"
              "#     python tools/gen_hud_lumps.py --wad <path-to-DOOM1.WAD>\n"
              "# The status-bar patches the container keeps raw (--raw-list), one per line.\n")
    return header + "\n".join(names) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True, help="the IWAD to check the names against")
    parser.add_argument("--check", action="store_true", help="fail when hud_lumps.txt differs")
    arguments = parser.parse_args(argv)

    _, lumps, _ = whd_encode.read_wad(arguments.wad)
    present = {lump.name.upper() for lump in lumps}
    names = hud_names()
    missing = [name for name in names if name not in present]
    if missing:
        print(f"gen_hud_lumps: not in {arguments.wad}: {', '.join(missing)}", file=sys.stderr)
        return 1

    text = render(names)
    if arguments.check:
        if not NAMES_FILE.exists() or NAMES_FILE.read_text(encoding="utf-8") != text:
            print(f"gen_hud_lumps: {NAMES_FILE} is out of date", file=sys.stderr)
            return 1
        print(f"gen_hud_lumps: {len(names)} names, {NAMES_FILE.name} is current")
        return 0
    NAMES_FILE.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_hud_lumps: wrote {len(names)} names to {NAMES_FILE.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
