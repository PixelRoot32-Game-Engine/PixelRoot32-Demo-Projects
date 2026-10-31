#!/usr/bin/env python3
"""Writes tools/e1m1_sprites.inc from one WAD, and checks the committed copy.

WHAT IT DOES

  1. reads the WAD's THINGS lump for E1M1 and collects the DoomEd numbers;
  2. refuses to run when a number is neither a player/deathmatch start (1-4,
     11: not mobjs, so not sprites) nor a row of the transcribed mobjinfo table
     in tools/e1m1_sprites.py;
  3. adds the fixture's runtime set -- the mobj types the simulation creates
     after the level is loaded, which E1M1's THINGS can never place -- and
     refuses a runtime key that is also a DoomEd number this map carries,
     because both namespaces share one array and one first-match scan;
  4. walks lib/doom/info_tables.h's mobjinfo[] and states[] rows from every
     state a placed type can enter -- spawnstate, seestate, painstate,
     meleestate, missilestate, deathstate, xdeathstate and raisestate -- and
     emits one row per reachable state, so a monster's walk, attack, pain,
     death and corpse frames are all named. tools/e1m1_sprites.inc is also the
     container's bake manifest (whd_encode.py --sprite-list), and a frame the
     manifest does not name is a frame the live renderer cannot ask for;
  5. cross-checks the fixture against that table: a map spawn row must name the
     type's spawnstate with the same sprite, frame and fullbright flag, and the
     runtime rows must cover exactly their type's chain. A disagreement between
     the two transcriptions is refused, not baked;
  6. builds R_InitSpriteDefs()'s sprite table from the S_START..S_END lumps and
     proves every selected frame has each rotation the projection can ask for;
  7. emits the C table, or (with --check) compares it with the file on disk and
     exits 1 when they differ.

Why info_tables.h is read here: that file is the engine's own generated
transcription of upstream info.c, so the bake cannot disagree with the frames
the simulation will actually ask for. The fixture's spawn rows stay a second,
independent transcription (tools/e1m1_sprites.py) and step 5 makes the two
prove each other.

The emitted file holds DoomEd numbers, sprite names, frame indices and rotation
lump names -- fixture data. It holds no map coordinate, no texture byte and no
sprite pixel, and lib/doom/ never includes it.

USAGE

    python tools/gen_e1m1_sprites.py --wad path/to/DOOM1.WAD
    python tools/gen_e1m1_sprites.py --wad path/to/DOOM1.WAD --check

Standard library only.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

import doom_ref
import e1m1_sprites

PLAYER_START_TYPES = frozenset({1, 2, 3, 4, 11})
THING_RECORD_SIZE = 10

#: lib/doom/info_tables.h, generated from the pinned upstream info.c. Read-only.
INFO_TABLES = (Path(__file__).resolve().parent.parent
               / "lib" / "doom" / "info_tables.h")

#: mobjinfo[]'s fields, in the order the generated table stores them.
MOBJ_FIELDS = (
    "doomednum", "spawnstate", "spawnhealth", "seestate", "seesound",
    "reactiontime", "attacksound", "painstate", "painchance", "painsound",
    "meleestate", "missilestate", "deathstate", "xdeathstate", "deathsound",
    "speed", "radius", "height", "mass", "damage", "activesound", "flags",
    "raisestate",
)

#: The mobjinfo[] fields a state machine can enter a state through. spawnstate
#: is the first; the rest are what A_Look, A_Chase, P_DamageMobj, P_KillMobj and
#: an arch-vile's resurrection reach. Every state below them is reached through
#: states[].nextstate.
CHAIN_FIELDS = ("spawnstate", "seestate", "painstate", "meleestate",
                "missilestate", "deathstate", "xdeathstate", "raisestate")

FF_FULLBRIGHT = 0x8000
FF_FRAMEMASK = 0x7FFF


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


@dataclass(frozen=True)
class InfoTables:
    """The engine's generated mobjinfo[]/states[] rows, as read here."""

    #: mobjtype_t name -> its number.
    mobj_names: dict[str, int]
    #: statenum_t number -> its name.
    state_names: dict[int, str]
    #: states[] rows: (frame, sprite, tics, nextstate).
    states: tuple[tuple[int, int, int, int], ...]
    #: mobjinfo[] rows, each one dict keyed by MOBJ_FIELDS.
    mobj: tuple[dict[str, int], ...]


def _enum(text: str, tag: str) -> dict[str, int]:
    """One explicit C enum (`NAME = number`) as a name -> number dict."""
    try:
        start = text.index(f"typedef enum {tag} {{")
        end = text.index(f"}} {tag}_t;", start)
    except ValueError as error:
        raise doom_ref.FormatError(
            f"info_tables.h has no {tag} enum") from error
    names = re.findall(r"(?m)^\s*(\w+)\s*=\s*(-?\d+)\s*,", text[start:end])
    if not names:
        raise doom_ref.FormatError(f"info_tables.h's {tag} enum is empty")
    return {name: int(number) for name, number in names}


def _states(text: str) -> tuple[tuple[int, int, int, int], ...]:
    """states[]' (frame, sprite, tics, nextstate) rows, in enum order."""
    try:
        start = text.index("const doom_state_t states[NUM_STATES] = {")
        end = text.index("};", start)
    except ValueError as error:
        raise doom_ref.FormatError(
            "info_tables.h has no states[] table") from error
    rows = [
        (int(frame), int(sprite), int(tics), int(nextstate))
        for frame, sprite, tics, nextstate in re.findall(
            r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,"
            r"\s*(?:A_\w+|ACT_NULL)\s*\}", text[start:end])
    ]
    expected = re.search(r"#define NUM_STATES (\d+)", text)
    if expected is None or len(rows) != int(expected.group(1)):
        raise doom_ref.FormatError(
            "info_tables.h's states[] does not hold NUM_STATES rows")
    return tuple(rows)


def _mobj_value(token: str) -> int:
    """One mobjinfo[] value: a decimal, a `0x..u` flag, or `N * FRACUNIT`."""
    if token.lower().startswith("0x"):
        return int(token.rstrip("uU"), 16)
    if "FRACUNIT" in token:
        return int(token.replace(" * FRACUNIT", "").replace("*FRACUNIT", ""))
    return int(token)


def _mobj(text: str) -> tuple[dict[str, int], ...]:
    """mobjinfo[]'s rows, each keyed by MOBJ_FIELDS."""
    try:
        start = text.index("const doom_mobjinfo_t mobjinfo[NUM_MOBJ_TYPES] = {")
        end = text.index("};", start)
    except ValueError as error:
        raise doom_ref.FormatError(
            "info_tables.h has no mobjinfo[] table") from error
    rows: list[dict[str, int]] = []
    for line in text[start:end].splitlines():
        line = line.strip()
        if not line.startswith("{"):
            continue
        values = [_mobj_value(token.strip())
                  for token in line[line.index("{") + 1:line.rindex("}")].split(",")]
        if len(values) != len(MOBJ_FIELDS):
            raise doom_ref.FormatError(
                f"info_tables.h's mobjinfo row has {len(values)} fields, "
                f"not {len(MOBJ_FIELDS)}")
        rows.append(dict(zip(MOBJ_FIELDS, values)))
    expected = re.search(r"#define NUM_MOBJ_TYPES (\d+)", text)
    if expected is None or len(rows) != int(expected.group(1)):
        raise doom_ref.FormatError(
            "info_tables.h's mobjinfo[] does not hold NUM_MOBJ_TYPES rows")
    return tuple(rows)


def read_info_tables(path: Path = INFO_TABLES) -> InfoTables:
    """Reads lib/doom/info_tables.h's enums and tables. Raises FormatError."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise doom_ref.FormatError(f"{path}: {error.strerror}") from error
    mobj_names = _enum(text, "mobjtype")
    state_name_by_number = _enum(text, "statenum")
    return InfoTables(
        mobj_names=mobj_names,
        state_names={number: name for name, number in state_name_by_number.items()},
        states=_states(text),
        mobj=_mobj(text),
    )


def thing_types(lumps: list[doom_ref.Lump], map_name: str) -> list[int]:
    """The DoomEd number of every record in the map's THINGS lump, in order."""
    try:
        marker = next(index for index, lump in enumerate(lumps)
                      if lump.name == map_name)
    except StopIteration as error:
        raise doom_ref.InputError(f"this WAD has no {map_name}") from error
    if marker + 1 >= len(lumps) or lumps[marker + 1].name != "THINGS":
        raise doom_ref.InputError(f"{map_name} is not followed by THINGS")
    data = lumps[marker + 1].data
    if len(data) % THING_RECORD_SIZE != 0:
        raise doom_ref.FormatError(f"THINGS is {len(data)} B, not a multiple of 10")
    types: list[int] = []
    for at in range(0, len(data), THING_RECORD_SIZE):
        _x, _y, _angle, kind, _options = struct.unpack_from("<hhHHH", data, at)
        types.append(kind)
    return types


def check_types(types: list[int]) -> dict[int, int]:
    """Counts the types and rejects one the fixture table does not cover."""
    counts: dict[int, int] = {}
    for kind in types:
        counts[kind] = counts.get(kind, 0) + 1
    missing = sorted(kind for kind in counts
                     if kind not in PLAYER_START_TYPES
                     and e1m1_sprites.rule_for(kind) is None)
    if missing:
        raise doom_ref.FormatError(
            "E1M1 contains a thing type the fixture table does not cover: "
            + ", ".join(str(kind) for kind in missing))
    return counts


def check_runtime_keys(map_types: list[int]) -> None:
    """Rejects a runtime row whose key is also a DoomEd number this map carries.

    The .inc holds both namespaces in one array and the runtime looks a row up
    with a first-match scan (`spriteRule()`), so a shared number would answer a
    THINGS lookup with a runtime row, or the other way round. DoomEd numbers are
    what `thing_types()` returned; a runtime row's key is a `mobjtype_t`.
    """
    collisions = sorted({row.key for row in e1m1_sprites.RUNTIME_SPRITES}
                        & set(map_types))
    if collisions:
        raise doom_ref.FormatError(
            "a runtime sprite key is also a DoomEd number in this map: "
            + ", ".join(str(key) for key in collisions))


def _mobj_name(info: InfoTables, index: int) -> str:
    """The mobjtype_t name of one mobjinfo[] row."""
    for name, number in info.mobj_names.items():
        if number == index:
            return name
    raise doom_ref.FormatError(f"mobjinfo[{index}] has no mobjtype_t name")


def _state_index(info: InfoTables, name: str) -> int:
    """The statenum of @p name, or FormatError."""
    for number, state_name in info.state_names.items():
        if state_name == name:
            return number
    raise doom_ref.FormatError(f"info_tables.h has no state {name}")


def _check_row_against_state(info: InfoTables, row: e1m1_sprites.ThingSprite,
                             state: int, where: str) -> None:
    """The fixture row's sprite/frame/fullbright must be the state's own."""
    frame, sprite, _tics, _next = info.states[state]
    name = info.state_names[state]
    if not 0 <= sprite < len(e1m1_sprites.SPRITE_NAMES):
        raise doom_ref.FormatError(f"{where}: {name} names sprite {sprite}, "
                                   "outside sprnames[]")
    want = e1m1_sprites.ThingSprite(
        key=row.key, mobjtype=row.mobjtype, state=name,
        sprite=e1m1_sprites.SPRITE_NAMES[sprite], frame=frame & FF_FRAMEMASK,
        fullbright=bool(frame & FF_FULLBRIGHT), runtime=row.runtime)
    if (row.sprite, row.frame, row.fullbright) != (want.sprite, want.frame,
                                                   want.fullbright):
        raise doom_ref.FormatError(
            f"{where}: the fixture says {row.sprite} frame {row.frame} "
            f"(fullbright {1 if row.fullbright else 0}), but info_tables.h's "
            f"{name} is {want.sprite} frame {want.frame} "
            f"(fullbright {1 if want.fullbright else 0})")


def state_chain(info: InfoTables, mobj_index: int) -> list[int]:
    """Every statenum one mobj type can enter, in statenum order.

    The entries are mobjinfo[]'s CHAIN_FIELDS -- the states A_Look, A_Chase,
    P_DamageMobj, P_KillMobj and a resurrection set -- and everything below them
    follows states[].nextstate. nextstate 0 is S_NULL (the end of a death
    animation), never a state.
    """
    if not 0 <= mobj_index < len(info.mobj):
        raise doom_ref.FormatError(f"mobjinfo index {mobj_index} is out of range")
    pending = [info.mobj[mobj_index][field] for field in CHAIN_FIELDS]
    seen: set[int] = set()
    while pending:
        state = pending.pop()
        if state == 0:
            continue
        if not 0 < state < len(info.states):
            raise doom_ref.FormatError(
                f"mobjinfo[{mobj_index}] reaches statenum {state}, but states[] "
                f"has {len(info.states)} rows")
        if state in seen:
            continue
        seen.add(state)
        pending.append(info.states[state][3])
    if not seen:
        raise doom_ref.FormatError(f"mobjinfo[{mobj_index}] reaches no state")
    return sorted(seen)


def _state_sprite(info: InfoTables, mobj_index: int, state: int, key: int,
                  runtime: bool) -> e1m1_sprites.ThingSprite:
    """One fixture row for @p state, named after its own states[] record."""
    frame, sprite, _tics, _next = info.states[state]
    if not 0 <= sprite < len(e1m1_sprites.SPRITE_NAMES):
        raise doom_ref.FormatError(
            f"{info.state_names[state]} names sprite {sprite}, outside sprnames[]")
    return e1m1_sprites.ThingSprite(
        key=key, mobjtype=_mobj_name(info, mobj_index),
        state=info.state_names[state],
        sprite=e1m1_sprites.SPRITE_NAMES[sprite],
        frame=frame & FF_FRAMEMASK,
        fullbright=bool(frame & FF_FULLBRIGHT),
        runtime=runtime)


def placed_rows(info: InfoTables, types: list[int]) -> list[e1m1_sprites.ThingSprite]:
    """The fixture rows for every type the map places: spawn row, then its chain.

    The spawn row comes first for each DoomEd number because `spriteRule()` is a
    first-match scan and the static sprite list answers a THINGS record with the
    first row of its type -- that row has to stay the spawnstate, or the frame
    the map places at load would move. The remaining rows are every other state
    the type can enter, in statenum order, each naming its own frame and the
    rotations the WAD defines for it.
    """
    rows: list[e1m1_sprites.ThingSprite] = []
    for kind in sorted(set(types)):
        spawn = e1m1_sprites.rule_for(kind)
        if spawn is None:
            continue  # a player/deathmatch start: not a mobj
        mobj_index = info.mobj_names.get(spawn.mobjtype)
        if mobj_index is None:
            raise doom_ref.FormatError(
                f"DoomEd {kind}: {spawn.mobjtype} is not a mobjtype_t name")
        if info.mobj[mobj_index]["doomednum"] != kind:
            raise doom_ref.FormatError(
                f"DoomEd {kind}: mobjinfo[{mobj_index}] "
                f"({spawn.mobjtype}) has doomednum "
                f"{info.mobj[mobj_index]['doomednum']}")
        spawn_state = info.mobj[mobj_index]["spawnstate"]
        if info.state_names.get(spawn_state) != spawn.state:
            raise doom_ref.FormatError(
                f"DoomEd {kind}: the fixture names {spawn.state} as the spawn "
                f"state, but mobjinfo[{mobj_index}]'s spawnstate is "
                f"{info.state_names.get(spawn_state, spawn_state)}")
        _check_row_against_state(info, spawn, spawn_state, f"DoomEd {kind}")
        rows.append(spawn)
        for state in state_chain(info, mobj_index):
            if state != spawn_state:
                rows.append(_state_sprite(info, mobj_index, state, kind, False))
    return rows


def check_runtime_rows(info: InfoTables) -> None:
    """The runtime rows must cover exactly their type's chain, spawn row first.

    They are the other half of the manifest and are transcribed by hand
    (RUNTIME_SPRITES), so this is where that transcription is held against the
    engine's own table: a missing frame would be a live mobj the bake cannot
    draw, and an extra one would bake a lump nothing can ask for.
    """
    by_type: dict[int, list[e1m1_sprites.ThingSprite]] = {}
    for row in e1m1_sprites.RUNTIME_SPRITES:
        by_type.setdefault(row.key, []).append(row)
    for key, rows in sorted(by_type.items()):
        if not 0 <= key < len(info.mobj):
            raise doom_ref.FormatError(
                f"runtime key {key} is not a mobjinfo[] index")
        name = _mobj_name(info, key)
        if rows[0].mobjtype != name:
            raise doom_ref.FormatError(
                f"runtime key {key}: the first row says {rows[0].mobjtype}, "
                f"but mobjinfo[{key}] is {name}")
        chain = state_chain(info, key)
        chain_names = [info.state_names[state] for state in chain]
        if rows[0].state != chain_names[0]:
            raise doom_ref.FormatError(
                f"{name}: the first runtime row must be the spawnstate "
                f"{chain_names[0]}, not {rows[0].state}")
        named = {row.state for row in rows}
        missing = [state for state in chain_names if state not in named]
        if missing:
            raise doom_ref.FormatError(
                f"{name}: the runtime rows do not name "
                + ", ".join(missing))
        extra = sorted(named - set(chain_names))
        if extra:
            raise doom_ref.FormatError(
                f"{name}: the runtime rows name states outside the chain: "
                + ", ".join(extra))
        for row in rows:
            _check_row_against_state(info, row, _state_index(info, row.state),
                                     f"{name} {row.state}")


def check_rotations(tables: dict[str, dict[int, e1m1_sprites.FrameTable]],
                    rows: list[e1m1_sprites.ThingSprite]) -> None:
    """Every rotation the projection can produce must have a lump."""
    for row in rows:
        for rotation in range(8):
            try:
                lump, _flip = e1m1_sprites.select_sprite(tables, row.sprite,
                                                         row.frame, rotation)
            except ValueError as error:
                raise doom_ref.FormatError(str(error)) from error
            if len(lump) not in (6, 8):
                raise doom_ref.FormatError(
                    f"{row.sprite} frame {row.frame}: lump {lump!r} is not 6 or 8 "
                    "characters")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="gen_e1m1_sprites.py",
        description="Emit tools/e1m1_sprites.inc from a WAD's E1M1.",
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD to read; keep it outside the repository")
    parser.add_argument("--map", default="E1M1", metavar="NAME",
                        help="the level whose THINGS are transcribed (default: E1M1)")
    parser.add_argument("--out", type=Path, metavar="PATH",
                        help="write here instead of tools/e1m1_sprites.inc")
    parser.add_argument("--check", action="store_true",
                        help="compare with the file on disk; write nothing")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"gen_e1m1_sprites: {arguments.wad} is not a file", file=sys.stderr)
        return 2

    try:
        _identification, lumps = doom_ref.read_wad(arguments.wad)
        types = thing_types(lumps, arguments.map)
        check_types(types)
        check_runtime_keys(types)
        info = read_info_tables()
        tables = e1m1_sprites.build_sprite_tables(lump.name for lump in lumps)
        map_rows = placed_rows(info, types)
        check_runtime_rows(info)
        rows = map_rows + list(e1m1_sprites.RUNTIME_SPRITES)
        check_rotations(tables, rows)
    except (doom_ref.InputError, doom_ref.FormatError, ValueError) as error:
        print(f"gen_e1m1_sprites: {error}", file=sys.stderr)
        return 2

    text = e1m1_sprites.emit_inc(tables, rows)
    target = arguments.out if arguments.out is not None else (
        project_root() / "tools" / "e1m1_sprites.inc")

    if arguments.check:
        try:
            current = target.read_text(encoding="ascii")
        except OSError as error:
            print(f"gen_e1m1_sprites: {error}", file=sys.stderr)
            return 2
        if current != text:
            print(f"gen_e1m1_sprites: {target} is out of date", file=sys.stderr)
            return 1
        print(f"gen_e1m1_sprites: {target} matches the WAD")
        return 0

    with open(target, "w", encoding="ascii", newline="\n") as handle:
        handle.write(text)
    print(f"gen_e1m1_sprites: wrote {target.name}: "
          f"{len(map_rows)} map frames + "
          f"{len(e1m1_sprites.RUNTIME_SPRITES)} runtime frames = "
          f"{len(rows)} rules, "
          f"{sum(1 for row in rows if row.fullbright)} full bright")
    return 0


if __name__ == "__main__":
    sys.exit(main())
