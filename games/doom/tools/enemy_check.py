#!/usr/bin/env python3
"""Compare lib/doom's sight and waking rules against the Python oracle (T15a).

WHAT THIS PROVES

Monster AI starts with two questions: can this monster see the player, and did it
hear anything? This gate answers both with the real code and a 40-tic script, so
each half can fail on its own:

    1. SEES AND WAKES. A zombieman 40 map units from the player, in the same room,
       must set `target` on its first state cycle and enter its see state. Before
       T15a it stands in S_POSS_STND forever.
    2. DOES NOT SEE THROUGH A WALL. The same monster at a spot whose straight line
       to the player crosses a one-sided linedef must stay in its spawn state for
       the whole script. The fixture is chosen by that geometry, not by
       P_CheckSight, so a sight check that answers "visible" through a wall makes
       this assertion fail.
    3. HEARS A SHOT. The wall monster, with the player firing through the real
       weapon path at tic 20: P_FireWeapon's P_NoiseAlert must flood the sectors,
       set the monster's sector soundTarget and wake it without sight. The same
       script with the monster inside the nearest closed manual door's sector must
       leave the soundTarget empty: a closed door's opening is zero, so the flood
       cannot cross it.
    4. AGREES WITH THE ORACLE. P_CheckSight is compared line for line against
       tools/doom_ref.py --enemy on a deterministic sweep of E1M1's own THINGS
       population (644 ordered pairs, both directions). The sweep exercises the
       REJECT pre-test and the BSP walk; CSTAT asserts that all three answers
       (reject, accept, blocked) occurred.

The per-tic lines are the same shape tools/attack_check.py uses: the step and RNG
position, both psprite layers, every mobj's state/position/target, and the player
record. The oracle runs the identical four scenes on its own state table, and the
comparison is per line -- a single divergent tic fails the gate.

THE SCRIPT (identical in both implementations, documented in enemy_check.c)

    every scene:  40 tics, no movement input
    noise, door:  tic 20 sets BT_ATTACK, which is one pistol shot

PROVING THE GATE CAN FAIL

    python tools/enemy_check.py --wad ... --perturb eye

builds the C side with P_SIGHT_EYE_SHIFT=0, so `sightzstart` moves from three
quarters of the body to its top. Seven swept verdicts flip, so the comparison
must diverge. A comparison that cannot fail is not a gate.

USAGE

    python tools/enemy_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed; point this tool at
a file outside the repository. The harness is compiled into a temporary directory
that is removed before the script returns.

EXIT CODES

    0  the lines matched and every assertion held
    1  a line differed, or an assertion failed
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
TICS = 40

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c", "p_spec.c",
    "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # P_CheckSight's eye height: height>>0 (the top of the body) instead of
    # upstream's height>>2, so sightzstart rises and the slope windows move.
    # Seven swept verdicts flip, which is what proves the comparison can fail.
    "eye": ["-DP_SIGHT_EYE_SHIFT=0"],
}

MIN_PAIRS = 400


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/enemy_check.c against the real world and AI core."""
    root = project_root()
    executable = workdir / ("enemy_check.exe" if sys.platform == "win32"
                            else "enemy_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "enemy_check.c"),
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
    """The lines both sides print. CSTAT is a C-only path measurement."""
    lines = []
    for line in text.splitlines():
        if not line or line.startswith("WAD ") or line.startswith("CSTAT "):
            continue
        lines.append(line)
    return lines


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def first_divergence(left: str, right: str) -> str:
    left_kind, left_values = fields_of(left)
    right_kind, right_values = fields_of(right)
    if left_kind != right_kind:
        return f"record kind: C {left_kind!r} oracle {right_kind!r}"
    for key in list(left_values) + [k for k in right_values if k not in left_values]:
        lvalue = left_values.get(key, "<missing>")
        rvalue = right_values.get(key, "<missing>")
        if lvalue != rvalue:
            return (f"{left_kind}: field {key!r}: C {lvalue} "
                    f"oracle {rvalue} (C {left!r}, oracle {right!r})")
    return f"line text differs: C {left!r} oracle {right!r}"


def parse_scenes(text: str) -> dict[str, dict[str, str]]:
    scenes: dict[str, dict[str, str]] = {}
    for line in text.splitlines():
        if not line.startswith("SCENE-END "):
            continue
        _, values = fields_of(line)
        if "name" in values:
            scenes[values["name"]] = values
    return scenes


def parse_cstat(text: str) -> dict[str, str]:
    for line in text.splitlines():
        if line.startswith("CSTAT "):
            return fields_of(line)[1]
    return {}


def sweep_counts(text: str) -> tuple[int, int, int, int]:
    """(declared pairs, sight lines, visible, hidden)."""
    declared = 0
    for line in text.splitlines():
        if line.startswith("SWEEP "):
            _, values = fields_of(line)
            declared = int(values.get("pairs", "0"))
    lines = [line for line in text.splitlines() if line.startswith("SIGHT ")]
    visible = sum(1 for line in lines if fields_of(line)[1].get("v") == "1")
    hidden = sum(1 for line in lines if fields_of(line)[1].get("v") == "0")
    return declared, len(lines), visible, hidden


def check_assertions(text: str) -> list[str]:
    """Each returned string is one failed assertion, named independently."""
    failures: list[str] = []
    scenes = parse_scenes(text)

    def scene(name: str) -> dict[str, str] | None:
        if name not in scenes:
            failures.append(f"scene {name}: no SCENE-END line")
            return None
        return scenes[name]

    # 1. Sees and wakes: target set, wake tic recorded, left the spawn pair.
    sees = scene("sees")
    if sees is not None:
        if sees.get("woketic", "-1") == "-1":
            failures.append("sees: the monster never set a target")
        if sees.get("target") != "0":
            failures.append(f"sees: target is {sees.get('target')!r}, not the "
                            "player's slot 0")
        if sees.get("stayed") != "0":
            failures.append("sees: the monster never left its spawn state pair")

    # 2. A wall hides the player: no target, no wake, still in the spawn pair.
    wall = scene("wall")
    if wall is not None:
        if wall.get("woketic") != "-1":
            failures.append(f"wall: the monster woke at tic {wall.get('woketic')} "
                            "through a wall")
        if wall.get("target") != "-1":
            failures.append("wall: the monster acquired a target through a wall")
        if wall.get("stayed") != "1":
            failures.append("wall: the monster left its spawn state pair")

    # 3a. A shot is heard through an open path.
    noise = scene("noise")
    if noise is not None:
        if noise.get("sndtarget") != "0":
            failures.append("noise: the sector's soundTarget is not the player")
        if noise.get("woketic", "-1") == "-1":
            failures.append("noise: the monster did not wake when the player fired")
        if noise.get("target") != "0":
            failures.append("noise: the woken monster's target is not the player")

    # 3b. A closed door blocks the sound.
    door = scene("door")
    if door is not None:
        if door.get("sndtarget") != "-1":
            failures.append("door: the closed door did not block the sound")
        if door.get("woketic") != "-1":
            failures.append("door: the monster woke behind a closed door")
        if door.get("stayed") != "1":
            failures.append("door: the monster left its spawn state pair")

    # 4. The sweep ran, and both answers and both sight paths exist.
    declared, lines, visible, hidden = sweep_counts(text)
    if declared < MIN_PAIRS:
        failures.append(f"sweep: only {declared} pairs, want at least {MIN_PAIRS}")
    if lines != declared:
        failures.append(f"sweep: {declared} pairs declared but {lines} SIGHT lines")
    if visible == 0:
        failures.append("sweep: no pair is visible")
    if hidden == 0:
        failures.append("sweep: no pair is blocked")
    cstat = parse_cstat(text)
    if int(cstat.get("reject", "0")) == 0:
        failures.append("sight: the REJECT pre-test never answered blocked")
    if int(cstat.get("bsp", "0")) == 0:
        failures.append("sight: the BSP walk never ran")
    if int(cstat.get("accepted", "0")) == 0:
        failures.append("sight: the BSP walk never accepted a line")
    if int(cstat.get("bsp_blocked", "0")) == 0:
        failures.append("sight: the BSP walk never blocked a line")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="enemy_check.py",
        description="Compare lib/doom's P_CheckSight/P_NoiseAlert/A_Look "
                    "against tools/doom_ref.py --enemy, and assert the four "
                    "waking behaviours.",
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
        print(f"enemy_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="enemy_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_result = subprocess.run([str(executable), str(arguments.wad),
                                   arguments.map],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("enemy_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--enemy", arguments.map],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("enemy_check: the oracle failed", file=sys.stderr)
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

        banner = next((line for line in c_lines if line.startswith("ENEMY ")), "")
        declared, lines, visible, hidden = sweep_counts(c_result.stdout)
        print(f"{arguments.map}: {len(c_lines)} compared lines, {mismatches} "
              f"divergent (perturb={arguments.perturb})")
        print(f"  {banner}")
        print(f"  sweep: {declared} pairs, {lines} SIGHT lines, "
              f"{visible} visible, {hidden} blocked")
        for line in c_result.stdout.splitlines():
            if line.startswith("SCENE-END "):
                print(f"  {line}")
        if mismatches:
            print(f"enemy_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)

        failures = check_assertions(c_result.stdout)
        for failure in failures:
            print(f"enemy_check: assertion failed: {failure}", file=sys.stderr)

        if mismatches or failures:
            return EXIT_MISMATCH
        if arguments.perturb != "none":
            print(f"enemy_check: --perturb {arguments.perturb} did not diverge, so "
                  "it does not prove the comparison can fail", file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"enemy_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("enemy_check: C and the oracle agree, and all four waking assertions hold")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())

