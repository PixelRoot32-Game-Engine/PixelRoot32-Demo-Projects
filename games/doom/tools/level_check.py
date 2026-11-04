#!/usr/bin/env python3
"""Prove the level transitions: the exit specials, the next map and the load.

WHAT THIS PROVES

The acceptance is "a scripted run reaches E1M9". ``tools/level_check.c``
opens the shareware container (the same ``PWHD`` image the board reads), loads a
level through ``lib/doom``, stands the player in front of that level's own exit
line, presses USE through ``p_level_tic()`` -- the same entry point the panel's
frame loop calls -- and follows the transition the game state machine computes.
This script runs that harness and checks every number against the WAD read
independently.

The chain, and why it is not nine normal transitions
----------------------------------------------------

Upstream's E1M8 has no exit line: its exit is sector special 11, the "exit super
damage" floor that ``P_PlayerInSpecialSector`` reads, and ``G_DoCompleted``
answers ``gamemap == 8`` with ``ga_victory`` before it looks at ``secretexit``.
So there is no E1M8 -> E1M9 transition in linuxdoom-1.10. The faithful route
visits all nine maps in eight transitions::

    E1M1 -11-> E1M2 -11-> E1M3 -51-> E1M9 -11-> E1M4 -11-> E1M5
         -11-> E1M6 -11-> E1M7 -11-> E1M8 (victory)

and the ninth level change is the separate secret exit E1M1 -> E1M9, through
``g_secret_exit_level()`` because E1M1 carries no 51/124 line. The gate says so
rather than inventing a ninth map exit.

E1M8's own ending is measured, not assumed: ``FINALE`` stands the player on the
sector the WAD marks 11, sets 30 health (so the first 20-point hit lands exactly
on the 10-point threshold), runs one tic through ``p_level_tic()`` and asserts
the damage, the ``normal`` exit request, a live player, ``G_DoCompleted``'s
victory, and that the victory loads no ninth map -- ``g_do_world_done`` refuses
it and the world is still E1M8's, over a fresh load's arena.

WHAT IS COMPARED

For every map, the C's fresh reference load (``LEVEL``) against the WAD: THINGS
record count, SECTORS count, LINEDEFS count and the type-1 start. For every
transition, the live world against that reference and against the WAD:

  * the map the world holds is the map the name says (counts and the type-1
    start, read from the WAD's own lumps);
  * the player is alive at the new level's start, at that start's coordinates;
  * ``leveltime`` is 0 and the exit request is cleared;
  * ``arena == ref``: a transition re-lays the arena instead of growing it.

The next-map decision is also implemented here, independently of ``g_game.c``'s
table, from linuxdoom-1.10's ``G_DoCompleted``.

PROVING THE GATE CAN FAIL

    python tools/level_check.py --wad ... --perturb next

replaces this script's next-map table with a wrong one, so the expected
transition disagrees with the C and the comparison must fail. A gate that cannot
fail is not a gate.

USAGE

    python tools/level_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed; point this tool
at a file outside the repository. The container the C harness reads is
``tools/DOOM1.whd`` (override with ``--container``); it is the same image the
board maps, and every map lump it binds must be a raw view. The harness is
compiled into a temporary directory that is removed before the script returns.

EXIT CODES

    0  every assertion held
    1  an assertion failed, or --perturb did not diverge
    2  the command line, the compiler or the input was unusable

Standard library only.
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

LEVEL_COUNT = 9
ARENA_BYTES = 1024 * 1024

MAP_LUMPS = (
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP",
)

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c", "g_game.c", "whd.c",
    "r_segs.c", "r_light.c", "r_bsp.c", "r_plane.c", "r_tables.c", "r_data.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)

# The exit special each scripted step presses, and the map the chain is on.
# The route's targets are computed below, never hard-coded twice.
ROUTE = (
    (1, "normal"), (2, "normal"), (3, "secret"), (9, "normal"),
    (4, "normal"), (5, "normal"), (6, "normal"), (7, "normal"),
)

PERTURBATIONS = ("none", "next")

EXIT_SPECIALS = (11, 51, 52, 124)


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/level_check.c against the real world."""
    root = project_root()
    executable = workdir / ("level_check.exe" if sys.platform == "win32"
                            else "level_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "level_check.c"),
    ]
    command.extend(str(root / "lib" / "doom" / name) for name in SOURCES)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project "
                           "does not accept:\n" + result.stderr)
    return executable


# --- The WAD, read independently ------------------------------------------


class Wad:
    def __init__(self, data: bytes) -> None:
        _, count, offset = struct.unpack_from("<4sii", data, 0)
        self.data = data
        self.lumps: list[tuple[str, int, int]] = []
        for index in range(count):
            position, size, name = struct.unpack_from(
                "<ii8s", data, offset + 16 * index)
            self.lumps.append(
                (name.rstrip(b"\0").decode("ascii", "replace"), position, size))

    def find(self, name: str) -> int:
        for index, lump in enumerate(self.lumps):
            if lump[0] == name:
                return index
        return -1

    def map_lumps(self, name: str) -> dict[str, tuple[int, int, int]]:
        marker = self.find(name)
        if marker < 0 or marker + 1 + len(MAP_LUMPS) > len(self.lumps):
            raise LookupError(name)
        return {lump: self.lumps[marker + 1 + index]
                for index, lump in enumerate(MAP_LUMPS)}

    def map_facts(self, name: str) -> dict[str, object]:
        lumps = self.map_lumps(name)
        things = lumps["THINGS"]
        linedefs = lumps["LINEDEFS"]
        sectors = lumps["SECTORS"]
        starts = []
        for index in range(things[2] // 10):
            record = struct.unpack_from("<hhhhh", self.data, things[1] + 10 * index)
            if record[3] == 1:
                starts.append((record[0], record[1]))
        exits = {}
        for index in range(linedefs[2] // 14):
            special = struct.unpack_from("<H", self.data,
                                         linedefs[1] + 14 * index + 6)[0]
            if special in EXIT_SPECIALS:
                exits[index] = special
        # The sectors whose special the *player* effect can see: the shareware
        # maps' raw specials, read here so the C's fixture is checked against the
        # WAD and not against itself.
        specials = {}
        for index in range(sectors[2] // 26):
            special = struct.unpack_from("<h", self.data,
                                         sectors[1] + 26 * index + 22)[0]
            if special:
                specials[index] = special
        return {
            "things": things[2] // 10,
            "lines": linedefs[2] // 14,
            "sectors": sectors[2] // 26,
            "starts": starts,
            "exits": exits,
            "specials": specials,
        }


def completed_next(episode: int, map_number: int, secret: bool,
                   perturb: bool) -> tuple[int, int] | None:
    """G_DoCompleted's non-commercial decision, from linuxdoom-1.10."""
    if perturb:
        # A deliberately wrong table: the secret exit and the map-9 return go to
        # a plausible but wrong map, so the secret step must disagree with the C.
        if map_number == 8:
            return None
        if secret:
            return episode, 4
        if map_number == 9:
            return episode, 5
        return episode, map_number + 1
    if map_number == 8:
        return None
    if secret:
        return episode, 9
    if map_number == 9:
        return episode, {1: 4, 2: 6, 3: 7, 4: 3}.get(episode, 0)
    return episode, map_number + 1


# --- The C's lines --------------------------------------------------------


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def lines_of(text: str, kind: str) -> list[dict[str, str]]:
    return [fields_of(line)[1] for line in text.splitlines()
            if line.startswith(kind + " ")]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="level_check.py",
        description="Prove the exit specials, G_DoCompleted's next map and the "
                    "load of the next level against the WAD (D4.5).",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read for the independent truth; "
                             "keep it outside the repository")
    parser.add_argument("--container", type=Path, default=None, metavar="PATH",
                        help="the PWHD container the C harness opens (default: "
                             "tools/DOOM1.whd beside this script)")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--perturb", choices=PERTURBATIONS, default="none",
                        help="replace this script's next-map table with a wrong one, "
                             "to prove the comparison can fail (default: none)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"level_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    if arguments.container is None:
        arguments.container = project_root() / "tools" / "DOOM1.whd"
    if not arguments.container.is_file():
        print(f"level_check: {arguments.container} is not a file",
              file=sys.stderr)
        return EXIT_USAGE

    wad = Wad(arguments.wad.read_bytes())
    facts = {}
    for map_number in range(1, LEVEL_COUNT + 1):
        name = f"E1M{map_number}"
        try:
            facts[name] = wad.map_facts(name)
        except LookupError:
            print(f"level_check: {name} is not in the WAD", file=sys.stderr)
            return EXIT_USAGE
        if len(facts[name]["starts"]) != 1:
            print(f"level_check: {name} has {len(facts[name]['starts'])} type-1 "
                  "starts; this gate asks for exactly one", file=sys.stderr)
            return EXIT_USAGE

    workdir = Path(tempfile.mkdtemp(prefix="level_check_"))
    try:
        executable = compile_harness(workdir, arguments.cc)
        result = subprocess.run([str(executable), str(arguments.container)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(result.stdout, file=sys.stdout)
            print(result.stderr, file=sys.stderr)
            print("level_check: the C harness reported a failure", file=sys.stderr)
            return EXIT_MISMATCH
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    text = result.stdout
    failures: list[str] = []

    # --- The reference loads, against the WAD ---
    levels = {}
    for record in lines_of(text, "LEVEL"):
        name = record.get("map", "")
        levels[name] = record
    for map_number in range(1, LEVEL_COUNT + 1):
        name = f"E1M{map_number}"
        record = levels.get(name)
        if record is None:
            failures.append(f"LEVEL: {name} is missing")
            continue
        fact = facts[name]
        if int(record.get("things", "-1")) != fact["things"]:
            failures.append(f"LEVEL {name}: things {record.get('things')} != WAD "
                            f"{fact['things']}")
        if int(record.get("sectors", "-1")) != fact["sectors"]:
            failures.append(f"LEVEL {name}: sectors {record.get('sectors')} != WAD "
                            f"{fact['sectors']}")
        if int(record.get("lines", "-1")) != fact["lines"]:
            failures.append(f"LEVEL {name}: lines {record.get('lines')} != WAD "
                            f"{fact['lines']}")
        start = fact["starts"][0]
        if record.get("playerstart") != f"{start[0]},{start[1]}":
            failures.append(f"LEVEL {name}: playerstart {record.get('playerstart')} "
                            f"!= WAD {start[0]},{start[1]}")
        if int(record.get("arenaSize", "-1")) != ARENA_BYTES:
            failures.append(f"LEVEL {name}: arenaSize {record.get('arenaSize')}")

    # --- The chain and the independent next-map table ---
    chain = []
    for from_map, kind in ROUTE:
        name = f"E1M{from_map}"
        secret = kind == "secret"
        target = completed_next(1, from_map, secret, arguments.perturb == "next")
        if target is None:
            failures.append(f"chain: {name} unexpectedly answered victory")
            break
        chain.append((name, f"E1M{target[1]}", 51 if secret else 11))

    transitions = lines_of(text, "TRANSITION")
    if len(transitions) != len(chain) + 1:
        failures.append(f"TRANSITION: {len(transitions)} lines, expected "
                        f"{len(chain) + 1} (the chain plus the secret exit)")
    for index, (from_name, to_name, special) in enumerate(chain):
        if index >= len(transitions):
            break
        record = transitions[index]
        fact = facts[from_name]
        target = facts[to_name]
        expected_exit = f"use:{special}"
        if record.get("from") != from_name or record.get("to") != to_name:
            failures.append(f"TRANSITION {index + 1}: {record.get('from')} -> "
                            f"{record.get('to')}, expected {from_name} -> {to_name}")
            continue
        if record.get("exit") != expected_exit:
            failures.append(f"TRANSITION {index + 1}: exit {record.get('exit')}, "
                            f"expected {expected_exit}")
        line_index = int(record.get("line", "-1"))
        if fact["exits"].get(line_index) != special:
            failures.append(f"TRANSITION {index + 1}: line {line_index} is not a "
                            f"special-{special} line of {from_name}")
        if record.get("consumed") != "1":
            failures.append(f"TRANSITION {index + 1}: the use line was not cleared")
        if record.get("ok") != "1":
            failures.append(f"TRANSITION {index + 1}: the C reported ok=0")
        if record.get("alive") != "1":
            failures.append(f"TRANSITION {index + 1}: the player is not alive")
        if record.get("leveltime") != "0":
            failures.append(f"TRANSITION {index + 1}: leveltime is not 0")
        start = target["starts"][0]
        if record.get("things") != str(target["things"]):
            failures.append(f"TRANSITION {index + 1}: things {record.get('things')} "
                            f"!= WAD {target['things']} of {to_name}")
        if record.get("mobjs") != record.get("spawned"):
            failures.append(f"TRANSITION {index + 1}: mobjs {record.get('mobjs')} "
                            f"!= the THINGS run's {record.get('spawned')}")
        if record.get("spawned") != levels.get(to_name, {}).get("spawned"):
            failures.append(f"TRANSITION {index + 1}: spawned "
                            f"{record.get('spawned')} != a fresh {to_name} load's")
        if record.get("sectors") != str(target["sectors"]):
            failures.append(f"TRANSITION {index + 1}: sectors {record.get('sectors')}"
                            f" != WAD {target['sectors']} of {to_name}")
        if record.get("lines") != str(target["lines"]):
            failures.append(f"TRANSITION {index + 1}: lines {record.get('lines')} != "
                            f"WAD {target['lines']} of {to_name}")
        if record.get("playerstart") != f"{start[0]},{start[1]}":
            failures.append(f"TRANSITION {index + 1}: playerstart "
                            f"{record.get('playerstart')} != WAD {start[0]},{start[1]}")
        if record.get("worldstart") != f"{start[0]},{start[1]}":
            failures.append(f"TRANSITION {index + 1}: the player is not at "
                            f"{to_name}'s start ({record.get('worldstart')})")
        if record.get("arena") != record.get("ref"):
            failures.append(f"TRANSITION {index + 1}: arena {record.get('arena')} "
                            f"!= a fresh load's {record.get('ref')}")
        reference = levels.get(to_name, {})
        if record.get("ref") != reference.get("arena"):
            failures.append(f"TRANSITION {index + 1}: ref {record.get('ref')} != "
                            f"LEVEL {to_name} arena {reference.get('arena')}")

    # --- The secret exit E1M1 -> E1M9, the ninth level change ---
    secrets = lines_of(text, "SECRET")
    if len(secrets) != 1:
        failures.append(f"SECRET: {len(secrets)} lines, expected 1")
    else:
        record = secrets[0]
        if (record.get("from") != "E1M1" or record.get("to") != "E1M9" or
                record.get("secret") != "1" or record.get("next") != "1"):
            failures.append(f"SECRET: {record}, expected E1M1 -> E1M9")
    if len(transitions) > len(chain):
        record = transitions[-1]
        start = facts["E1M9"]["starts"][0]
        if (record.get("from") != "E1M1" or record.get("to") != "E1M9" or
                record.get("exit") != "secret"):
            failures.append(f"SECRET TRANSITION: {record.get('from')} -> "
                            f"{record.get('to')} ({record.get('exit')})")
        if record.get("ok") != "1" or record.get("alive") != "1":
            failures.append("SECRET TRANSITION: the C reported a failed load")
        if record.get("things") != str(facts["E1M9"]["things"]):
            failures.append("SECRET TRANSITION: the world is not E1M9's")
        if record.get("mobjs") != record.get("spawned"):
            failures.append("SECRET TRANSITION: mobjs != the THINGS run's spawned")
        if record.get("worldstart") != f"{start[0]},{start[1]}":
            failures.append("SECRET TRANSITION: the player is not at E1M9's start")
        if record.get("arena") != record.get("ref"):
            failures.append("SECRET TRANSITION: the arena grew")

    # --- The walk-over fixtures and their guard ---
    cross = lines_of(text, "CROSS")
    if len(cross) != 3:
        failures.append(f"CROSS: {len(cross)} lines, expected 3")
    if len(cross) >= 1:
        record = cross[0]
        if (record.get("special") != "52" or record.get("result") != "1" or
                record.get("exit") != "normal" or record.get("consumed") != "0"):
            failures.append(f"CROSS 52: {record}")
    if len(cross) >= 2:
        record = cross[1]
        if (record.get("special") != "124" or record.get("result") != "1" or
                record.get("exit") != "secret" or record.get("consumed") != "0"):
            failures.append(f"CROSS 124: {record}")
    if len(cross) >= 3:
        record = cross[2]
        if (record.get("byplayer") != "0" or record.get("result") != "0" or
                record.get("refused") != "1"):
            failures.append(f"CROSS guard: {record}")

    # --- E1M8's finale and the summary ---
    finale = lines_of(text, "FINALE")
    if len(finale) != 1:
        failures.append(f"FINALE: {len(finale)} lines, expected 1")
    else:
        record = finale[0]
        if (record.get("map") != "E1M8" or record.get("exitlines") != "0" or
                record.get("victory") != "1" or record.get("next") != "0"):
            failures.append(f"FINALE: {record}")
        # P_PlayerInSpecialSector's case 11 is the boss map's only ending, so the
        # WAD's own sector table says which sector that has to be. E1M8 carries
        # exactly one 11, at the pad behind the exit door.
        eleven = sorted(index for index, special
                        in facts["E1M8"]["specials"].items() if special == 11)
        if len(eleven) != 1:
            failures.append(f"WAD: E1M8 carries {len(eleven)} sector-11 floors")
        elif record.get("sector") != str(eleven[0]):
            failures.append(f"FINALE: sector {record.get('sector')} != the WAD's "
                            f"{eleven[0]}")
        if record.get("special") != "11":
            failures.append(f"FINALE: the fixture sector's special is "
                            f"{record.get('special')}")
        # `if (!(leveltime&0x1f)) P_DamageMobj (player->mo, NULL, NULL, 20);` and
        # then `if (player->health <= 10) G_ExitLevel();`, from 30 health on the
        # first tic of the map.
        if (record.get("before") != "30" or record.get("damage") != "20" or
                record.get("health") != "10" or record.get("exit") != "normal"):
            failures.append(f"FINALE: the exit is not 30 - 20 = 10 with a normal "
                            f"request ({record})")
        if record.get("alive") != "1":
            failures.append("FINALE: E1M8's ending killed the player")
        if record.get("leveltime") != "1":
            failures.append(f"FINALE: leveltime {record.get('leveltime')}, "
                            "expected 1")
        if record.get("world") != "E1M8":
            failures.append(f"FINALE: the world is on {record.get('world')}")
        if record.get("arena") != record.get("ref"):
            failures.append(f"FINALE: arena {record.get('arena')} != a fresh "
                            f"E1M8 load's {record.get('ref')}")
    if facts["E1M8"]["exits"]:
        failures.append("WAD: E1M8 carries an exit line, contrary to the record")

    summaries = lines_of(text, "SUMMARY")
    chain_text = ">".join([chain[0][0]] + [step[1] for step in chain])
    if len(summaries) != 1:
        failures.append(f"SUMMARY: {len(summaries)} lines, expected 1")
    else:
        record = summaries[0]
        if record.get("transitions") != "9":
            failures.append(f"SUMMARY: transitions {record.get('transitions')} != 9")
        if record.get("chain") != chain_text:
            failures.append(f"SUMMARY: chain {record.get('chain')} != {chain_text}")
        if record.get("ok") != "1":
            failures.append("SUMMARY: the C reported ok=0")

    if any(line.startswith("FAIL ") for line in text.splitlines()):
        failures.append("the C harness printed a FAIL line")
    if arguments.perturb == "next" and not failures:
        failures.append("--perturb next did not diverge, so the comparison cannot "
                        "fail")

    print(f"level_check: 9 maps, {len(transitions)} level changes, "
          f"{len(failures)} failed assertion(s) (perturb={arguments.perturb})")
    for record in transitions:
        print(f"  {record.get('n')}: {record.get('from')} -> {record.get('to')} "
              f"{record.get('exit')} arena={record.get('arena')} "
              f"ref={record.get('ref')} alive={record.get('alive')}")
    print(f"  chain {chain_text}")
    for record in finale:
        print(f"  finale {record.get('map')}: sector {record.get('sector')} "
              f"special {record.get('special')}, {record.get('before')} -> "
              f"{record.get('health')} health, exit={record.get('exit')}, "
              f"alive={record.get('alive')}, victory={record.get('victory')}, "
              f"next={record.get('next')}, world={record.get('world')}, "
              f"arena={record.get('arena')} ref={record.get('ref')}")
    for failure in failures:
        print(f"level_check: assertion failed: {failure}", file=sys.stderr)
    if failures:
        return EXIT_MISMATCH
    if arguments.perturb == "next":
        # Unreachable unless the C agreed with the wrong table; the branch above
        # already reports it, so keep the exit code honest.
        return EXIT_MISMATCH
    print("level_check: nine level changes, every world the WAD's own, "
          "the arena re-laid and E1M9 reached")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
