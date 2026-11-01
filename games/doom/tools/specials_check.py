#!/usr/bin/env python3
"""Compare the C sector specials against the Python oracle (D4.1b).

WHAT THIS PROVES

``test/unit/test_world`` pins each state machine on a synthetic map. This script
is the other half: it builds ``tools/specials_check.c`` -- which calls
``lib/doom/p_setup.c``, ``lib/doom/p_spec.c`` and the five mover files -- runs
it on a real WAD's E1M1, and compares its per-tic dump, line for line, with
``tools/doom_ref.py --specials``, an independent implementation written from
linuxdoom-1.10's p_spec.c / p_floor.c / p_doors.c / p_plats.c / p_ceilng.c /
p_lights.c.

THE SCRIPT (identical in both implementations, documented in specials_check.c)

Every line special in the map is triggered once, in ascending line index, at
tic 0, before the first tick. The kind is the documented rule: the
switch/manual numbers of P_UseSpecialLine are ``use``, every other special is
``cross``. For E1M1 that is 8 manual doors, one lift, one turbo-lowering floor,
the exit switch (the request is recorded and the line cleared) and eight
scrolling lines (deferred).

Tic 0 is after the spawn scan and the triggers; tic 1 is after one
``p_level_tick()``. The default run is 420 tics, long enough for a door to open,
wait 150 tics and close.

WHAT IS COMPARED

    SPECIALS ...         the map, the tic count and the table sizes
    TRIGGER ...          every triggered line, its kind and whether an EV ran
    SECTOR ...           every sector's floor, ceiling and light every tic
    THINKER ...          the sector thinkers in list order every tic
    DAMAGE ...           one P_PlayerInSpecialSector fixture (see below)
    HEALTH ...           the player's health, exit request and secret count
    SUMMARY ...          the counters (secrets, P_ChangeSector calls, deferrals)

``INFO `` lines are C-only measurements and ``WAD `` is the oracle's banner;
both are excluded.

THE SECOND STAGE: P_PlayerInSpecialSector, ON EVERY EPISODE-1 MAP

The sector special a *player* feels is a different set from the one a level
spawns: P_PlayerInSpecialSector switches on 4, 5, 7, 9, 11 and 16, and every
other number has already been cleared by its own spawn thinker. The shareware
maps carry 7 on all nine of them, 9 on all nine, 16 on E1M3 and E1M9, and 11 on
E1M8 only; 4 and 5 appear in none, so the fixtures write them into a sector the
map really has and say ``synthetic=1``.

Each fixture loads the map fresh, stands the avatar on the fixture sector's own
floor, sets the health and the radiation suit, then applies the effect once per
tic and prints the state. The ``--perturb`` switch moves one constant in the
*oracle* only -- the damage amounts, the 10% exit threshold, or the suit's
immunity -- so the comparison is shown to be able to fail rather than assumed to
be able to.

USAGE

    python tools/specials_check.py --wad path/to/DOOM1.WAD
    python tools/specials_check.py --wad ... --perturb threshold

The WAD is copyrighted shareware data and is never committed; point this tool
at a file outside the repository. The harness is compiled into a temporary
directory that is removed before the script returns.

EXIT CODES

    0  every line matched
    1  at least one line differed, or --perturb did not diverge
    2  the command line, the compiler or the input was unusable

Standard library only.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MAP_NAME = "E1M1"
TICS = 420

# The nine maps the sector-damage stage covers, and the constants the perturb
# switch can move in the oracle.
DAMAGE_MAPS = tuple(f"E1M{i}" for i in range(1, 10))
PERTURBATIONS = ("none", "damage", "threshold", "ironfeet")

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_tick.c", "p_action.c", "p_inter.c",
    "p_pspr.c", "p_sight.c", "p_enemy.c",
    "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/specials_check.c against the real world core."""
    root = project_root()
    executable = workdir / ("specials_check.exe" if sys.platform == "win32"
                            else "specials_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "specials_check.c"),
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


def protocol_lines(text: str) -> list[str]:
    """The lines both sides print, with the banners and C-only measurements out."""
    lines = []
    for line in text.splitlines():
        if not line or line.startswith("WAD ") or line.startswith("INFO "):
            continue
        lines.append(line)
    return lines


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    """(record kind, key/value pairs) for one protocol line."""
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def describe(line: str) -> str:
    """The tic/sector/field a mismatch is about, as far as the line says."""
    kind, values = fields_of(line)
    where = []
    if "tic" in values:
        where.append(f"tic {values['tic']}")
    if "sector" in values:
        where.append(f"sector {values['sector']}")
    if kind == "THINKER" and "kind" in values:
        where.append(f"thinker kind {values['kind']}")
    return f"{kind} ({', '.join(where) or 'no position'})"


def first_divergence(left: str, right: str) -> str:
    """The first differing field of two protocol lines, both values, and where."""
    left_kind, left_values = fields_of(left)
    right_kind, right_values = fields_of(right)
    if left_kind != right_kind:
        return f"record kind: C {left_kind!r} oracle {right_kind!r}"
    for key in list(left_values) + [k for k in right_values if k not in left_values]:
        lvalue = left_values.get(key, "<missing>")
        rvalue = right_values.get(key, "<missing>")
        if lvalue != rvalue:
            return (f"{describe(left)}: field {key!r}: C {lvalue} "
                    f"oracle {rvalue} (C line {left!r}, oracle line {right!r})")
    return f"line text differs: C {left!r} oracle {right!r}"


def run_harness(executable: Path, wad: Path, map_name: str, tics: int,
                mode: str) -> list[str]:
    """The C harness's stdout lines, verbatim (banners included)."""
    command = [str(executable), str(wad), map_name, str(tics)]
    if mode:
        command.append(mode)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the C harness failed on "
                           f"{map_name}{' ' + mode if mode else ''}:\n"
                           + (result.stderr or result.stdout))
    return result.stdout.splitlines()


def run_oracle(oracle_script: Path, wad: Path, arguments: list[str]) -> list[str]:
    """The oracle's protocol lines, banners removed."""
    result = subprocess.run(
        [sys.executable, str(oracle_script), "--wad", str(wad), *arguments],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the oracle failed:\n"
                           + (result.stderr or result.stdout))
    return protocol_lines(result.stdout)


def compare(c_lines: list[str], ref_lines: list[str]) -> tuple[int, str]:
    """(differing line count, the first divergence described)."""
    left_lines = protocol_lines("\n".join(c_lines))
    mismatches = 0
    first = ""
    for index in range(max(len(left_lines), len(ref_lines))):
        left = left_lines[index] if index < len(left_lines) else "<missing>"
        right = ref_lines[index] if index < len(ref_lines) else "<missing>"
        if left == right:
            continue
        mismatches += 1
        if not first:
            first = first_divergence(left, right)
    return mismatches, first


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="specials_check.py",
        description="Compare lib/doom/p_spec.c and the mover files against "
                    "tools/doom_ref.py --specials on a real WAD's E1M1, and "
                    "P_PlayerInSpecialSector against --sector-damage on all nine "
                    "episode-1 maps.",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--map", default=MAP_NAME, metavar="NAME",
                        help=f"the map the trigger script loads (default: {MAP_NAME})")
    parser.add_argument("--tics", type=int, default=TICS, metavar="N",
                        help=f"tics to step after tic 0 (default: {TICS})")
    parser.add_argument("--perturb", choices=PERTURBATIONS, default="none",
                        help="move one sector-damage constant in the oracle only, "
                             "and require the comparison to fail")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"specials_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    if arguments.tics < 0:
        print("specials_check: --tics cannot be negative", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="specials_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc)

        # --- Stage 1: the map's specials and the trigger script ---------------
        c_raw = run_harness(executable, arguments.wad, arguments.map,
                            arguments.tics, "")
        ref_lines = run_oracle(oracle_script, arguments.wad,
                               ["--specials", arguments.map,
                                "--tics", str(arguments.tics),
                                "--sector-perturb", arguments.perturb])
        mismatches, first = compare(c_raw, ref_lines)

        c_lines = protocol_lines("\n".join(c_raw))
        sectors = next((line for line in c_lines if line.startswith("SPECIALS ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        info = next((line for line in c_raw if line.startswith("INFO ")), "")
        print(f"{arguments.map}: {arguments.tics + 1} tics, {len(c_lines)} compared "
              f"lines, {mismatches} divergent")
        print(f"  {sectors}")
        print(f"  {summary}")
        if info:
            print(f"  {info}")
        if mismatches:
            print(f"specials_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH

        # --- Stage 2: P_PlayerInSpecialSector on every episode-1 map ----------
        damage_lines = 0
        damage_mismatches = 0
        for map_name in DAMAGE_MAPS:
            c_damage = run_harness(executable, arguments.wad, map_name, 0, "damage")
            ref_damage = run_oracle(oracle_script, arguments.wad,
                                    ["--sector-damage", map_name,
                                     "--sector-perturb", arguments.perturb])
            count, first_damage = compare(c_damage, ref_damage)
            c_damage_lines = protocol_lines("\n".join(c_damage))
            damage_lines += len(c_damage_lines)
            damage_mismatches += count
            fixtures = sum(1 for line in c_damage_lines
                           if line.startswith("DAMAGE "))
            print(f"{map_name}: {fixtures} sector fixtures, "
                  f"{len(c_damage_lines)} compared lines, {count} divergent")
            if count:
                print(f"specials_check: {count} divergent line(s) on {map_name}; "
                      f"first: {first_damage}", file=sys.stderr)
        print(f"damage: {len(DAMAGE_MAPS)} maps, {damage_lines} compared lines, "
              f"{damage_mismatches} divergent (perturb={arguments.perturb})")

        if damage_mismatches:
            return EXIT_MISMATCH
        if arguments.perturb != "none":
            print(f"specials_check: --perturb {arguments.perturb} did not diverge, "
                  "so the sector-damage comparison cannot fail", file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"specials_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("specials_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
