#!/usr/bin/env python3
"""Compare the door rules and the doorway frame against the Python oracle.

WHAT THIS PROVES

A door has to block the player while closed, open on use, and keep the doorway
view drawn while the player walks through it. This script builds
``tools/door_check.c``, which loads E1M1 through ``lib/doom/p_setup.c``, keeps
only the avatar, finds the nearest closed manual door to the player start and
runs three measurements through ``p_level_tic()`` -- the same entry point the
panel's frame loop calls:

    1. A CLOSED DOOR BLOCKS. The player stands one radius from the closed line.
       The next tic's forward move reports ``blocked=1`` and the position into
       the door does not change.
    2. USE OPENS IT. A held ``BT_USE`` press (the panel's B button) starts the
       door thinker; the sector's ceiling rises two map units per tic and the
       line becomes passable only once ``openTop - mo->z`` reaches the player's
       height.
    3. THE DOORWAY VIEW IS NOT BLANK. The C harness renders the real 320x200
       surface at the threshold (closed), the same spot once the use press
       opened the simulation's door, inside the door sector mid-traversal, and
       on the far side. It reports how many pixels are still the ``0xFF``
       prefill and a hash of each frame. A frame that is mostly prefill while
       the walk succeeded is the reported white screen; an OPEN frame identical
       to the CLOSED one is the door that never visibly opens.

The per-tic lines are compared line for line with ``tools/doom_ref.py --door``,
an independent implementation written from linuxdoom-1.10's p_user.c and
p_map.c. The frame lines are C-only: the oracle has no renderer aimed at an
arbitrary viewpoint, and the third assertion is an invariant, not an equality.

THE SCRIPT (identical in both implementations, documented in door_check.c)

    tics   0        no input                          (settle; render CLOSED)
    tics   1..2     forwardmove 0x19                  (walk into the closed leaf)
    tics   3..5     no input                          (release the use latch)
    tics   6..12    buttons BT_USE, held              (one press)
    tics  13..44    no input                          (the door opens; render OPEN
                                                       at the first passable tic)
    tics  45..      forwardmove 0x19                  (walk through; render MID
                                                       inside the door sector and
                                                       AFTER on the far side)

PROVING THE GATE CAN FAIL

    python tools/door_check.py --wad ... --perturb viewheight

adds one compile-time definition to the C build only, so the per-tic view height
diverges from the oracle and the comparison must fail. A comparison that cannot
fail is not a gate.

USAGE

    python tools/door_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed; point this tool
at a file outside the repository. The harness is compiled into a temporary
directory that is removed before the script returns.

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
TICS = 120

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c",
    "r_segs.c", "r_light.c", "r_bsp.c", "r_plane.c", "r_tables.c", "r_data.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)

PERTURBATIONS = {
    "none": [],
    # P_CalcHeight's base eye height, 41 -> 40 map units. The viewz column then
    # diverges from the oracle on every tic after the first.
    "viewheight": ["-DP_VIEWHEIGHT=(40*FRACUNIT)"],
}

# A frame is "mostly prefill" above this fraction; the panel's white screen is
# every pixel at once, and a rendered view leaves at most the sky and the wall
# gaps uncovered.
MAX_PREFILL_FRACTION = 0.5


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/door_check.c against the real world and renderer."""
    root = project_root()
    executable = workdir / ("door_check.exe" if sys.platform == "win32"
                            else "door_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "door_check.c"),
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
    """The lines both sides print. FRAME/FRAMES are C-only measurements."""
    lines = []
    for line in text.splitlines():
        if not line or line.startswith("WAD ") or line.startswith("INFO "):
            continue
        if line.startswith("FRAME ") or line.startswith("FRAMES "):
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


def parse_frames(text: str) -> dict[str, dict[str, str]]:
    frames: dict[str, dict[str, str]] = {}
    for line in text.splitlines():
        if not line.startswith("FRAME "):
            continue
        kind, values = fields_of(line)
        parts = line.split()
        if len(parts) >= 2:
            frames[parts[1]] = values
    return frames


def prefill_fraction(frame: dict[str, str]) -> float | None:
    raw = frame.get("prefill", "")
    if "/" not in raw:
        return None
    filled, total = raw.split("/", 1)
    try:
        return int(filled) / int(total)
    except (ValueError, ZeroDivisionError):
        return None


def check_assertions(text: str) -> list[str]:
    """Each returned string is one failed assertion, named independently."""
    failures: list[str] = []
    tics = []
    for line in text.splitlines():
        if line.startswith("TIC "):
            _, values = fields_of(line)
            tics.append(values)
    summary = next((fields_of(line)[1] for line in text.splitlines()
                    if line.startswith("SUMMARY ")), {})
    frames = parse_frames(text)

    # 1. A closed door blocks: the forward tic leaves the position on the door's
    #    axis unchanged and reports the refusal.
    if len(tics) < 2:
        failures.append("closed door: the tic dump is too short to see a move")
    else:
        before, moving = tics[0], tics[1]
        if moving.get("blocked") != "1":
            failures.append("closed door: the forward tic did not report blocked=1")
        if moving.get("x") != before.get("x"):
            failures.append(f"closed door: x moved {before.get('x')} -> "
                            f"{moving.get('x')} into a closed leaf")
        if abs(int(moving.get("y", "0")) - int(before.get("y", "0"))) > 65536:
            failures.append("closed door: the player slid more than one map unit "
                            "along the closed leaf")

    # 2. Use opens it: the press triggered, the thinker ran, the ceiling rose,
    #    and passability begins only once the opening clears the player's height.
    if summary.get("used") in (None, "0"):
        failures.append("use: BT_USE did not trigger the door")
    if summary.get("opens") in (None, "-1"):
        failures.append("use: the door never became passable")
    else:
        opens_at = int(summary["opens"])
        if opens_at < 0 or opens_at >= len(tics):
            failures.append("use: the passable tic is outside the dump")
        else:
            first = tics[opens_at]
            if first.get("passable") != "1":
                failures.append("use: the reported first passable tic is not passable")
            if int(first.get("ceiling", "0")) < 56 * 65536:
                failures.append("use: passability began below the player's height")
            if any(tic.get("passable") == "1" for tic in tics[:opens_at]):
                failures.append("use: an earlier tic was passable before the "
                                "opening cleared the player")
            if first.get("door") != "1":
                failures.append("use: no door thinker is active in the sector")
    if summary.get("finalceiling") in (None, "0"):
        failures.append("use: the door's final ceiling is still the floor")

    # 3. The doorway frame is not blank, and the door visibly opens.
    for label in ("CLOSED", "OPEN", "MID", "AFTER"):
        frame = frames.get(label)
        if frame is None:
            failures.append(f"frame {label}: missing")
            continue
        fraction = prefill_fraction(frame)
        if fraction is None:
            failures.append(f"frame {label}: no prefill fraction")
        elif fraction > MAX_PREFILL_FRACTION:
            failures.append(f"frame {label}: {fraction:.1%} of the frame is the "
                            f"0xFF prefill (the blank doorway)")
    if "CLOSED" in frames and "OPEN" in frames:
        if frames["CLOSED"].get("hash") == frames["OPEN"].get("hash"):
            failures.append("door animation: the OPEN frame is identical to the "
                            "CLOSED frame, so the door never visibly opens")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="door_check.py",
        description="Compare lib/doom's door rules against tools/doom_ref.py "
                    "--door, and assert that the doorway frame is not blank.",
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
    parser.add_argument("--tics", type=int, default=TICS, metavar="N",
                        help=f"tics to run (default: {TICS})")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="build the C side with one deliberate constant changed, "
                             "to prove the comparison can fail (default: none)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"door_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    if arguments.tics < 0:
        print("door_check: --tics cannot be negative", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="door_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc, arguments.perturb)

        c_result = subprocess.run(
            [str(executable), str(arguments.wad), arguments.map,
             str(arguments.tics)],
            capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("door_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--door", arguments.map, "--tics", str(arguments.tics)],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("door_check: the oracle failed", file=sys.stderr)
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

        banner = next((line for line in c_lines if line.startswith("DOOR ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        print(f"{arguments.map}: {arguments.tics} tics, {len(c_lines)} compared "
              f"lines, {mismatches} divergent (perturb={arguments.perturb})")
        print(f"  {banner}")
        print(f"  {summary}")
        for line in c_result.stdout.splitlines():
            if line.startswith("FRAME "):
                print(f"  {line}")
        if mismatches:
            print(f"door_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH
        if arguments.perturb != "none":
            print(f"door_check: --perturb {arguments.perturb} did not diverge, so "
                  "it does not prove the comparison can fail", file=sys.stderr)
            return EXIT_MISMATCH

        failures = check_assertions(c_result.stdout)
        for failure in failures:
            print(f"door_check: assertion failed: {failure}", file=sys.stderr)

        if failures:
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"door_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("door_check: C and the oracle agree, and all three door assertions hold")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
