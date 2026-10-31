#!/usr/bin/env python3
"""Compare the C movement core and the player against the Python oracle.

WHAT THIS PROVES

``test/unit/test_world`` pins each rule on a synthetic map. This script is the
other half: it builds ``tools/movement_check.c`` -- which calls
``lib/doom/p_setup.c``, ``lib/doom/p_mobj.c``, ``lib/doom/p_map.c``,
``lib/doom/p_user.c`` and the special dispatch -- runs it on a real WAD's E1M1,
and compares its per-tic dump, line for line, with ``tools/doom_ref.py``, an
independent implementation written from linuxdoom-1.10.

TWO PROTOCOLS

``--movement`` (the default) is D4.2b's walk. The harness sets the angle and
calls P_Thrust/P_MobjThinker directly, so the collision core is exercised
without a player record. The script (documented in movement_check.c) is:

    tics   0..31   angle 0,      thrust 0x19 * 2048  (walk speed)
    tics  32..63   angle 90,     thrust 0x19 * 2048  (into a wall, slides)
    tics  64..95   angle 45,     thrust 0x32 * 2048  (run speed, into a corner)
    tics  96..127  no thrust                         (friction only)
    tics 128..159  angle 180,    thrust 0x19 * 2048  (back into the room)

``--player`` is D4.2c's tic. A real ``p_ticcmd_t`` drives the avatar through
``p_level_tic()``, so P_PlayerThink, P_MovePlayer, P_CalcHeight and the use
trace run in upstream's order (200 tics):

    tics   0..31   forwardmove 0x19                  (walk forward)
    tics  32..63   forwardmove 0x32                  (run into the wall, slide)
    tics  64..95   forwardmove 0x19, angleturn -640  (turn right while walking)
    tics  96..127  sidemove 0x19                     (strafe right)
    tics 128..159  forwardmove -0x19                 (backpedal)
    tics 160..167  buttons BT_USE                    (one press, held)
    tics 168..199  no input                          (friction to rest)

Both runs keep only the first player start's mobj (the one P_LoadThings()
already spawned through P_SpawnPlayer) and remove every other thing, so the walk
exercises the map's linedefs rather than E1M1's population. Neither calls
P_SpawnSpecials.

WHAT IS COMPARED

    MOVEMENT/PLAYER ...   the map, the tic count and the script name
    TIC ...               every field the protocol names (see movement_check.c)
    SUMMARY ...           the final state and the counters

``INFO `` lines are C-only measurements (the mean and peak microseconds of one
P_CheckPosition and the size of the brute-force candidate set); ``WAD `` is the
oracle's banner. Both are excluded.

PROVING THE GATE CAN FAIL

    python tools/movement_check.py --wad ... --perturb friction
    python tools/movement_check.py --wad ... --player --perturb viewheight

adds one compile-time definition to the C build only. The outputs must diverge
and the script must exit 1; a comparison that cannot fail is not a gate. The
view-height perturbation changes only the PLAYER protocol, so using it without
--player is refused.

USAGE

    python tools/movement_check.py --wad path/to/DOOM1.WAD [--player]

The WAD is copyrighted shareware data and is never committed; point this tool
at a file outside the repository. The harness is compiled into a temporary
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
TICS = 160
PLAYER_TICS = 200

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # One constant, one compile-time definition, C side only. The value is a
    # different friction scale, so every tic after the first thrust diverges.
    "friction": ["-DP_ORIG_FRICTION=0xE000"],
    # P_CalcHeight's base eye height, 41 -> 40 map units. Only the PLAYER
    # protocol calls P_CalcHeight, so this one needs --player.
    "viewheight": ["-DP_VIEWHEIGHT=(40*FRACUNIT)"],
}


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/movement_check.c against the real movement core."""
    root = project_root()
    executable = workdir / ("movement_check.exe" if sys.platform == "win32"
                            else "movement_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "movement_check.c"),
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
    """The tic a mismatch is about, as far as the line says."""
    kind, values = fields_of(line)
    where = []
    if "tic" in values:
        where.append(f"tic {values['tic']}")
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


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="movement_check.py",
        description="Compare lib/doom/p_map.c and p_mobj.c's movement against "
                    "tools/doom_ref.py --movement on a real WAD's E1M1.",
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
    parser.add_argument("--player", action="store_true",
                        help="run the 200-tic player script through p_level_tic() "
                             "(P_PlayerThink) instead of the raw movement walk")
    parser.add_argument("--tics", type=int, default=None, metavar="N",
                        help=f"tics to run (default: {TICS}; {PLAYER_TICS} with "
                             "--player)")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="build the C side with one deliberate constant changed, "
                             "to prove the comparison can fail (default: none)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"movement_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    tics = (arguments.tics if arguments.tics is not None
            else (PLAYER_TICS if arguments.player else TICS))
    if tics < 0:
        print("movement_check: --tics cannot be negative", file=sys.stderr)
        return EXIT_USAGE
    if arguments.perturb == "viewheight" and not arguments.player:
        print("movement_check: --perturb viewheight only changes the PLAYER "
              "protocol; add --player", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="movement_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_command = [str(executable)]
        if arguments.player:
            c_command.append("--player")
        c_command.extend([str(arguments.wad), arguments.map, str(tics)])
        c_result = subprocess.run(c_command, capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("movement_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--player" if arguments.player else "--movement", arguments.map,
             "--tics", str(tics)],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("movement_check: the oracle failed", file=sys.stderr)
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

        movement = next((line for line in c_lines
                         if line.startswith("MOVEMENT ")
                         or line.startswith("PLAYER ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        info = next((line for line in c_result.stdout.splitlines()
                     if line.startswith("INFO check")), "")
        print(f"{arguments.map}: {tics} tics, {len(c_lines)} compared "
              f"lines, {mismatches} divergent (perturb={arguments.perturb})")
        print(f"  {movement}")
        print(f"  {summary}")
        if info:
            print(f"  {info}")
        if mismatches:
            print(f"movement_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"movement_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("movement_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
