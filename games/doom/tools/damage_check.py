#!/usr/bin/env python3
"""Compare the C damage/death/pickup rules against the Python oracle (D4.2d1).

WHAT THIS PROVES

``test/unit/test_world`` pins each rule on a synthetic map. This script is the
other half: it builds ``tools/damage_check.c`` -- which calls ``lib/doom/p_setup.c``,
``p_mobj.c``, ``p_map.c``, ``p_inter.c`` and the special dispatch -- runs it on a
real WAD's E1M1, and compares its per-step dump, line for line, with
``tools/doom_ref.py --damage``, an independent implementation written from
linuxdoom-1.10.

THE SCRIPT

    setup       remove every THING but the avatar; spawn MG0/MG1/MG2 (possessed),
                MB0 (a barrel) and MH0 (the inflictor of the fall-forward hit)
    step 0      damage MG2 25 from MH0: the `damage < 40 && damage > health &&
                z - inflictor->z > 64` coin is drawn
    step 1..3   a health bonus, an armor bonus and a clip, picked up through a
                real p_try_move (PIT_CheckThing's special branch)
    step 4      a thinker walk, which releases the three picked-up mobjs
    step 5..11  P_GiveArmor(1), 40 damage, 30 damage from MG0, 1 damage,
                P_GiveArmor(2), 60 damage, 5 damage
    step 12     green armor refused while blue armor is held
    step 13..20 a pain, a normal death with a clip drop, a barrel death that is
                not a kill, and a gib, each followed by a tic
    step 21..24 P_SpawnBlood 15 / 10 / 5 and P_SpawnPuff
    step 25..27 three ticks, which walk the sprays
    step 28..   the player's sector floor rises 8 units a tic for 32 tics with
                `crush = 1`; every fourth tic the player takes 10, dies and is
                gibbed
    last 24     thinker walks that carry the death chain through A_PlayerScream
                and A_Fall

WHAT IS COMPARED

    DAMAGE/SETUP/STEP ...  the map, the script, the RNG position and the crush count
    MOBJ ...               every mobj in thinker order: position, momentum, health,
                           flags, state, tics and its target's slot
    PLAYER ...             health, armor, totals, attacker, playerstate, the flash
                           counters, ammo/maxammo, weapons, cards, backpack,
                           pending weapon and powers
    SUMMARY ...            the final state and the counters

``INFO `` lines are the C harness's own self-check (the script reached the
outcome it documents) and ``WAD `` is the oracle's banner. Both are excluded.

PROVING THE GATE CAN FAIL

    python tools/damage_check.py --wad ... --perturb armor

adds one compile-time definition to the C build only. The outputs must diverge
and the script must exit 1; a comparison that cannot fail is not a gate.
`armor` changes P_DamageMobj's green-armor divisor (3 -> 4), `blue` the blue one
(2 -> 3), `crush` the crush damage (10 -> 12) and `blood` the spray's z jitter
(shift 10 -> 12).

USAGE

    python tools/damage_check.py --wad path/to/DOOM1.WAD [--perturb NAME]

The WAD is copyrighted shareware data and is never committed; point this tool at
a file outside the repository. The harness is compiled into a temporary
directory that is removed before the script returns.

EXIT CODES

    0  every line matched
    1  at least one line differed
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

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c",
    # D4.2d2: p_user.c runs the weapon psprite machine and p_inter.c drops a dying
    # player's weapon, so p_pspr.c is part of the link closure now.
    "p_pspr.c",
    # T15a: A_Look is real and P_FireWeapon calls P_NoiseAlert, so the sight and
    # sound modules are in the closure too.
    "p_sight.c", "p_enemy.c",
    "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # P_DamageMobj's green-armor absorption: 40 damage saves 13, not 10.
    "armor": ["-DP_ARMOR_GREEN_DIVISOR=4"],
    # The blue-armor absorption: 60 damage saves 20, not 30.
    "blue": ["-DP_ARMOR_BLUE_DIVISOR=3"],
    # PIT_ChangeSector's crush damage: ten points a fourth tic, not twelve.
    "crush": ["-DP_CRUSH_DAMAGE=12"],
    # P_SpawnBlood/P_SpawnPuff's z jitter: a different band, same draws.
    "blood": ["-DP_BLOOD_Z_SHIFT=12"],
}


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/damage_check.c against the real interaction core."""
    root = project_root()
    executable = workdir / ("damage_check.exe" if sys.platform == "win32"
                            else "damage_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "damage_check.c"),
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
    return [line for line in text.splitlines()
            if line and not line.startswith("WAD ") and not line.startswith("INFO ")]


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
    """The step and record a mismatch is about, as far as the line says."""
    kind, values = fields_of(line)
    where = []
    if "op" in values:
        where.append(f"op {values['op']}")
    elif "type" in values:
        where.append(f"type {values['type']}")
    return f"{kind} ({', '.join(where) or 'no step'})"


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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="damage_check.py",
        description="Compare lib/doom/p_inter.c's damage, death and pickup rules "
                    "against tools/doom_ref.py --damage on a real WAD's E1M1.",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--map", default=MAP_NAME, metavar="NAME",
                        help=f"the map to load (default: {MAP_NAME})")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="build the C side with one deliberate constant changed, "
                             "to prove the comparison can fail (default: none)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"damage_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="damage_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_result = subprocess.run([str(executable), str(arguments.wad),
                                   arguments.map],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("damage_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--damage", arguments.map],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("damage_check: the oracle failed", file=sys.stderr)
            return EXIT_USAGE

        c_lines = protocol_lines(c_result.stdout)
        ref_lines = protocol_lines(ref_result.stdout)

        mismatches = 0
        first = ""
        for index in range(max(len(c_lines), len(ref_lines))):
            left = c_lines[index] if index < len(c_lines) else "<missing>"
            right = ref_lines[index] if index < len(ref_lines) else "<missing>"
            if left == right:
                continue
            mismatches += 1
            if not first:
                first = first_divergence(left, right)

        banner = next((line for line in c_lines if line.startswith("DAMAGE ")), "")
        setup = next((line for line in c_lines if line.startswith("SETUP ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        info = next((line for line in c_result.stdout.splitlines()
                     if line.startswith("INFO scene")), "")
        print(f"{arguments.map}: {len(c_lines)} compared lines, {mismatches} "
              f"divergent (perturb={arguments.perturb})")
        print(f"  {banner}")
        print(f"  {setup}")
        print(f"  {summary}")
        if info:
            print(f"  {info}")
        if mismatches:
            print(f"damage_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"damage_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("damage_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
