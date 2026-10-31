#!/usr/bin/env python3
"""Compare the C weapon and attack rules against the Python oracle (D4.2d2).

WHAT THIS PROVES

``test/unit/test_world`` pins each rule on a synthetic map. This script is the
other half: it builds ``tools/attack_check.c`` -- which calls ``lib/doom/p_setup.c``,
``p_mobj.c``, ``p_map.c``, ``p_user.c``, ``p_pspr.c``, ``p_inter.c`` and the special
dispatch -- runs it on a real WAD's E1M1, and compares its per-step dump, line for
line, with ``tools/doom_ref.py --attack``, an independent implementation written
from linuxdoom-1.10.

THE SCRIPT

    setup        remove every THING but the avatar; spawn a possessed 40 map units
                 along the spawn facing and aim at it; give the shotgun and the
                 chaingun; force the ammo to 50 clips and 20 shells
    psprites     P_SetupPsprites: both layers cleared, the pistol's raise started
    15 tics      the raise, one step per tic
    fire-pistol  BT_ATTACK: A_WeaponReady fires, the shot resolves four tics later
                 when the psprite reaches S_PISTOL2
    14 tics      the trigger held: A_ReFire re-fires, and `refire` makes the
                 second bullet drift
    change       BT_CHANGE to 2: the pistol lowers (15 tics), the shotgun raises
                 (16), then one shot and 20 held tics
    chaingun     the same with weapon 3, then 10 held tics
    clips-one    the clip pool is forced to 1; 40 held tics make the gun click
                 empty, A_ReFire ask P_CheckAmmo, and the preference order pick the
                 shotgun -- which then lowers and raises by itself
    fist         BT_CHANGE to 0, 15 + 16 tics, a fresh possessed where the first
                 stood, and one punch with 6 held tics

WHAT IS COMPARED

    ATTACK/SETUP/STEP ...  the map, the script, the RNG position and the flags
    PSP <0|1> ...          both overlay layers: state, tics, sx, sy
    MOBJ ...               every mobj in thinker order: position, momentum, health,
                           flags, state, tics and its target's slot -- so a puff,
                           a blood spray or a dropped clip is a line
    PLAYER ...             health, the four ammo pools, the owned-weapon mask, both
                           weapon slots, `refire`, `attackdown`, `extralight` and
                           the two counters
    SUMMARY ...            the final state and the counters

``INFO `` lines are the C harness's own self-check (the script reached the outcome
it documents) and ``WAD `` is the oracle's banner. Both are excluded.

PROVING THE GATE CAN FAIL

    python tools/attack_check.py --wad ... --perturb damage

adds one compile-time definition to the C build only. The outputs must diverge and
the script must exit 1; a comparison that cannot fail is not a gate. `damage`
changes P_GunShot's base damage (5 -> 6), `spread` the refire drift's shift
(18 -> 19), `weapontop` the ready weapon's screen height (32 -> 34), `lower` the
weapon's lowering speed (6 -> 8) and `range` the bullet's reach (2,048 -> 1,024).

USAGE

    python tools/attack_check.py --wad path/to/DOOM1.WAD [--perturb NAME]

The WAD is copyrighted shareware data and is never committed; point this tool at a
file outside the repository. The harness is compiled into a temporary directory that
is removed before the script returns.

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
    "p_inter.c", "p_pspr.c", "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c",
    "r_plane.c", "r_tables.c", "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c",
    "p_plats.c", "p_ceilng.c", "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # P_GunShot's base damage: 5*(1..3) becomes 6*(1..3), so every bullet that
    # lands moves the target's health.
    "damage": ["-DP_GUNSHOT_DAMAGE=6"],
    # The refire drift's shift: 18 -> 19 doubles it, so a held trigger's bullets
    # land somewhere else.
    "spread": ["-DP_SHOT_SPREAD_SHIFT=19"],
    # A_WeaponReady's bob origin: the ready weapon's sy is two units higher.
    "weapontop": ["-DP_WEAPONTOP=(34*FRACUNIT)"],
    # A_Lower's step: 6 -> 8, so the swap happens two tics early.
    "lower": ["-DP_LOWERSPEED=(FRACUNIT*8)"],
    # The bullet's reach: 2,048 -> 1,024 map units, so a shot at a distant wall
    # stops short of it.
    "range": ["-DP_MISSILERANGE=(16*64*FRACUNIT)"],
}


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/attack_check.c against the real weapon and attack core."""
    root = project_root()
    executable = workdir / ("attack_check.exe" if sys.platform == "win32"
                            else "attack_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "attack_check.c"),
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
        prog="attack_check.py",
        description="Compare lib/doom/p_pspr.c's weapon state machine and "
                    "lib/doom/p_map.c's attack path against tools/doom_ref.py "
                    "--attack on a real WAD's E1M1.",
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
        print(f"attack_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="attack_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_result = subprocess.run([str(executable), str(arguments.wad),
                                   arguments.map],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("attack_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--attack", arguments.map],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("attack_check: the oracle failed", file=sys.stderr)
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

        banner = next((line for line in c_lines if line.startswith("ATTACK ")), "")
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
            print(f"attack_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"attack_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("attack_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
