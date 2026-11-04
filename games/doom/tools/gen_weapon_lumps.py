#!/usr/bin/env python3
"""Generate tools/weapon_lumps.txt: the weapon pictures the container keeps raw.

WHY THIS EXISTS

The weapon widget draws the player's psprites by lump name, the way
``R_DrawPSprite`` does: ``sprnames[state->sprite]`` + the frame letter + ``0``.
Like the status-bar art (``gen_hud_lumps.py``), a patch the container only
*stores* is deflated and needs a zone to decode into; a patch kept RAW
(``whd_encode.py --raw-list``) is 4-byte aligned and read in place from the
mapped partition. They are NOT registered in the ``~PWBAKE~`` manifest: every
record costs about 50 B of DRAM and the board has ~2.4 KB spare.

The names are not typed in. They are derived from the tables the simulation
runs on:

* ``weaponinfo[]`` in ``lib/doom/p_pspr.c`` names the five entry states of each
  weapon (up, down, ready, attack, flash);
* the ``states[]`` rows of ``lib/doom/info_tables.h`` give each state's sprite,
  frame, tics and next state, so every state a psprite can reach is walked;
* a state with 0 tics is never visible (``P_SetPsprite`` runs straight through
  it), so its frame is not baked;
* the lump name is ``sprnames[sprite] + chr('A' + frame & 0x7FFF) + '0'``
  (``rotate`` is false for a weapon, so lump[0] is the only one asked for).

A derived name that is not in the WAD is an error, with named exceptions: SHT2
(super shotgun), PLSG/PLSF (plasma rifle) and BFGG/BFGF (BFG 9000). The shareware
IWAD carries no lump for them and no pickup in episode 1 gives the player those
weapons, so their states are unreachable.

    python tools/gen_weapon_lumps.py --wad tools/DOOM1.WAD           # rewrite
    python tools/gen_weapon_lumps.py --wad tools/DOOM1.WAD --check   # drift
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
NAMES_FILE = TOOLS / "weapon_lumps.txt"
FF_FRAMEMASK = 0x7FFF
FF_FULLBRIGHT = 0x8000
# The shareware IWAD ships no art for the super shotgun, the plasma rifle or the
# BFG 9000, and no pickup in episode 1 can give the player any of the three
# (PLSG/PLSF/BFGG/BFGF and SHT2 have no lump in DOOM1.WAD).
ABSENT_OK = {"SHT2", "PLSG", "PLSF", "BFGG", "BFGF"}

STATE_ROW = re.compile(
    r"\{(-?\d+), (-?\d+), (-?\d+), (-?\d+), (\w+)\},\s*/\* (S_\w+) \*/")
TABLE_ROW = re.compile(r"\{\(int8_t\)P_AM_\w+,\s*(S_\w+),\s*(S_\w+),\s*(S_\w+),\s*(S_\w+),\s*(S_\w+)\}")


class State:
    __slots__ = ("frame", "sprite", "tics", "nextstate", "action", "name")

    def __init__(self, row: tuple[str, ...]) -> None:
        self.frame = int(row[0])
        self.sprite = int(row[1])
        self.tics = int(row[2])
        self.nextstate = int(row[3])
        self.action = row[4]
        self.name = row[5]


def load_tables() -> tuple[list[State], list[str]]:
    text = (LIB / "info_tables.h").read_text(encoding="utf-8")
    states = [State(row) for row in STATE_ROW.findall(text)]
    block = text[text.index("const char* const sprnames[NUM_SPRITES]"):]
    block = block[:block.index("};")]
    sprnames = re.findall(r'"([A-Z0-9]{4})"', block)
    return states, sprnames


def weapon_entry_states() -> list[str]:
    """The state names weaponinfo[] hands the psprite machine, in table order."""
    text = (LIB / "p_pspr.c").read_text(encoding="utf-8")
    start = text.index("p_weapon_table[P_NUMWEAPONS]")
    rows = TABLE_ROW.findall(text[start:text.index("};", start)])
    names: list[str] = []
    for row in rows:
        names.extend(row)
    return names


def reachable_frames() -> list[tuple[str, int, int]]:
    """(sprite, frame letter index, flags) for every visible psprite state."""
    states, sprnames = load_tables()
    by_name = {state.name: index for index, state in enumerate(states)}
    entries = [by_name[name] for name in weapon_entry_states()]
    pending = list(entries)
    # A_FireCGun does not name its flash: it sets
    # `flashstate + (psp->state - &states[S_CHAIN1])`, i.e. the flash state at
    # the same offset as the attack state that fired. That state is reached by
    # index, not by nextstate, so it is added here, from the table rather than
    # from a typed name.
    for first in range(0, len(entries), 5):
        attack, flash = entries[first + 3], entries[first + 4]
        index, steps = attack, 0
        while steps < 16 and index != 0:
            if states[index].action == "A_FireCGun":
                pending.append(flash + (index - attack))
            index = states[index].nextstate
            steps += 1
            if index == attack:
                break
    seen: set[int] = set()
    found: dict[tuple[str, int], int] = {}
    while pending:
        index = pending.pop()
        if index == 0 or index in seen:
            continue
        seen.add(index)
        state = states[index]
        if state.tics != 0:
            key = (sprnames[state.sprite], state.frame & FF_FRAMEMASK)
            found[key] = found.get(key, 0) | (state.frame & FF_FULLBRIGHT)
        pending.append(state.nextstate)
    return sorted((sprite, frame, flags) for (sprite, frame), flags in found.items())


def lump_name(sprite: str, frame: int) -> str:
    return f"{sprite}{chr(ord('A') + frame)}0"


def weapon_names(present: set[str] | None = None) -> list[str]:
    """The lump names, in a stable order. With @p present, names the WAD lacks
    for an ABSENT_OK sprite are dropped and any other missing name is an error."""
    names: list[str] = []
    missing: list[str] = []
    for sprite, frame, _flags in reachable_frames():
        name = lump_name(sprite, frame)
        if present is not None and name not in present:
            if sprite in ABSENT_OK:
                continue
            missing.append(name)
            continue
        names.append(name)
    if missing:
        raise SystemExit(f"gen_weapon_lumps: not in the WAD: {', '.join(missing)}")
    return names


def render(names: list[str]) -> str:
    header = ("# GENERATED FILE -- do not edit. Regenerate with:\n"
              "#     python tools/gen_weapon_lumps.py --wad <path-to-DOOM1.WAD>\n"
              "# The weapon (psprite) patches the container keeps raw (--raw-list), one per line.\n")
    return header + "\n".join(names) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True, help="the IWAD to check the names against")
    parser.add_argument("--check", action="store_true", help="fail when weapon_lumps.txt differs")
    arguments = parser.parse_args(argv)

    _, lumps, _ = whd_encode.read_wad(arguments.wad)
    present = {lump.name.upper() for lump in lumps}
    names = weapon_names(present)
    text = render(names)
    if arguments.check:
        if not NAMES_FILE.exists() or NAMES_FILE.read_text(encoding="utf-8") != text:
            print(f"gen_weapon_lumps: {NAMES_FILE} is out of date", file=sys.stderr)
            return 1
        print(f"gen_weapon_lumps: {len(names)} names, {NAMES_FILE.name} is current")
        return 0
    NAMES_FILE.write_text(text, encoding="utf-8", newline="\n")
    print(f"gen_weapon_lumps: wrote {len(names)} names to {NAMES_FILE.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
