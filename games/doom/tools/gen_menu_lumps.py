#!/usr/bin/env python3
"""Generate tools/menu_lumps.txt and lib/doom/m_gamma_table.inc (T39, D5.2).

TWO GENERATED FILES, ONE DRIFT CHECK

1. ``tools/menu_lumps.txt`` -- the menu pictures the container keeps raw.
   Like the status-bar and weapon art (``gen_hud_lumps.py``, ``gen_weapon_lumps.py``)
   the menu draws patches by name from the mapped partition, so a deflated patch
   would need a zone to decode into, and a ``~PWBAKE~`` manifest slot costs about
   50 B of DRAM the board does not have (T36, L36). ``whd_encode.py --raw-list``
   keeps them 4-byte aligned and readable in place.

   The list is not an independent opinion: ``lib/doom/m_menu.c`` is the source of
   truth for what the menu asks for. Every ``"M_xxxx"`` string literal in it, and
   the font range its ``M_FONT_FIRST`` / ``M_FONT_LAST`` defines name (``STCFN%03d``,
   upstream's ``HU_FONTSTART``..``HU_FONTEND``), must be in the list and every
   listed name must be asked for. A name the WAD lacks is an error: the shareware
   IWAD has every menu picture this port draws.

2. ``lib/doom/m_gamma_table.inc`` -- linuxdoom's ``gammatable[5][256]``.
   Level 0 is the identity. Level ``g`` is ``round(255 * (i / 255) ** (1 - g / 8))``:
   exponents 0.875, 0.75, 0.625 and 0.5, which is what the table's entries imply
   (``gammatable[1][1] == 2``, ``[4][1] == 16``, ``[4][2] == 23``). No vanilla
   binary is available here, so the equality of this regeneration with id's
   printed table is checked by hand against the entries remembered, not by a
   diff; ``menu_check.py`` pins the structural properties (identity, monotonic,
   brighter with level, endpoints) so a wrong formula cannot pass silently.

    python tools/gen_menu_lumps.py --wad tools/DOOM1.WAD           # rewrite
    python tools/gen_menu_lumps.py --wad tools/DOOM1.WAD --check   # drift
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import whd_encode  # noqa: E402

TOOLS = Path(__file__).resolve().parent
LIB = TOOLS.parent / "lib" / "doom"
NAMES_FILE = TOOLS / "menu_lumps.txt"
GAMMA_FILE = LIB / "m_gamma_table.inc"
MENU_SOURCE = LIB / "m_menu.c"

GAMMA_LEVELS = 5


def gamma_table() -> list[list[int]]:
    """gammatable[level][value], level 0 the identity."""
    table: list[list[int]] = []
    for level in range(GAMMA_LEVELS):
        exponent = 1.0 - level / 8.0
        row = []
        for value in range(256):
            if level == 0:
                row.append(value)
            else:
                row.append(min(255, int(255.0 * (value / 255.0) ** exponent + 0.5)))
        table.append(row)
    return table


def render_gamma() -> str:
    lines = ["/* GENERATED FILE -- do not edit. Regenerate with:",
             " *     python tools/gen_menu_lumps.py --wad <path-to-DOOM1.WAD>",
             " * linuxdoom's gammatable[5][256]: level 0 identity, level g =",
             " * round(255 * (i / 255) ** (1 - g / 8)). */"]
    for row in gamma_table():
        lines.append("{")
        for start in range(0, 256, 16):
            lines.append("    " + ", ".join(f"{v}" for v in row[start:start + 16]) + ",")
        lines.append("},")
    return "\n".join(lines) + "\n"


def menu_source_names() -> tuple[list[str], int, int]:
    """The ``"M_xxxx"`` literals m_menu.c asks for and its font range."""
    text = MENU_SOURCE.read_text(encoding="utf-8")
    names = sorted(set(re.findall(r'"(M_[A-Z0-9]{1,6})"', text)))
    first = re.search(r"#define\s+M_FONT_FIRST\s+(\d+)", text)
    last = re.search(r"#define\s+M_FONT_LAST\s+(\d+)", text)
    if first is None or last is None:
        raise SystemExit("gen_menu_lumps: M_FONT_FIRST / M_FONT_LAST not found in m_menu.c")
    return names, int(first.group(1)), int(last.group(1))


def menu_names(present: set[str] | None = None) -> list[str]:
    names, first, last = menu_source_names()
    font = [f"STCFN{code:03d}" for code in range(first, last + 1)]
    wanted = names + font
    if present is not None:
        missing = [name for name in wanted if name not in present]
        if missing:
            raise SystemExit(f"gen_menu_lumps: not in the WAD: {', '.join(missing)}")
    return wanted


def render_names(names: list[str]) -> str:
    header = ("# GENERATED FILE -- do not edit. Regenerate with:\n"
              "#     python tools/gen_menu_lumps.py --wad <path-to-DOOM1.WAD>\n"
              "# The menu patches and the HU font the container keeps raw (--raw-list), one per line.\n")
    return header + "\n".join(names) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True, help="the IWAD to check the names against")
    parser.add_argument("--check", action="store_true", help="fail when a generated file differs")
    arguments = parser.parse_args(argv)

    _, lumps, _ = whd_encode.read_wad(arguments.wad)
    present = {lump.name.upper() for lump in lumps}
    names = menu_names(present)
    generated = ((NAMES_FILE, render_names(names)), (GAMMA_FILE, render_gamma()))

    if arguments.check:
        stale = [path.name for path, text in generated
                 if not path.exists() or path.read_text(encoding="utf-8") != text]
        if stale:
            print(f"gen_menu_lumps: out of date: {', '.join(stale)}", file=sys.stderr)
            return 1
        print(f"gen_menu_lumps: {len(names)} names, {NAMES_FILE.name} and {GAMMA_FILE.name} are current")
        return 0
    for path, text in generated:
        path.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_menu_lumps: wrote {len(names)} names and the {GAMMA_LEVELS}-level gamma table")
    return 0


if __name__ == "__main__":
    sys.exit(main())
