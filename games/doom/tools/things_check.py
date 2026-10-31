#!/usr/bin/env python3
"""Compare the C THINGS run against the Python oracle on the real WAD (D4.1a).

WHAT THIS PROVES

``test/unit/test_world`` pins the spawn path on synthetic maps, because a CI
machine has no WAD. This script is the other half: it compiles
``tools/things_check.c`` -- which calls ``lib/doom/p_setup.c`` and everything it
reaches, with the generated ``info_tables.h`` -- and compares its E1M1 dump,
line for line, with ``tools/doom_ref.py --things E1M1``, an independent
implementation that reads THINGS itself and applies P_SpawnMapThing()'s rules
from the upstream source.

The compared lines are the protocol both sides document:

    THINGS ...          the map, the skill and the record count
    MOBJ ...            every spawned mobj, in thinker (insertion) order
    MOBJ-COUNT ...
    COUNTERS ...        the totals Phase 4 reads, and which filter dropped the rest
    PLAYERSTART ...     the four recorded starts
    DEATHMATCHSTART ... the first ten recorded deathmatch starts
    PLAYER ...          the console player's mobj index
    RNG ...             the P_Random() position the load reached

Lines starting with ``INFO `` are C-only measurements (record sizes, the
generated table sizes, the arena's accounting) and are printed for the report
but excluded from the comparison: the oracle has no arena.

WHY ALL FIVE SKILLS

The skill bit is one of the filters, and a single skill would only exercise one
of its five branches. Each run is a full pass over the same 138 records, so all
five cost about a second and prove the bit, the totals and the random position
for every starting difficulty.

USAGE

    python tools/things_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed or copied into the
repository; point this tool at a file outside it. Nothing here writes into the
worktree: the harness is compiled into a temporary directory that is removed
before the script returns.

EXIT CODES

    0  every line matched, for every skill
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
SKILLS = ("baby", "easy", "medium", "hard", "nightmare")

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/things_check.c against the real world core."""
    root = project_root()
    executable = workdir / ("things_check.exe" if sys.platform == "win32" else "things_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "things_check.c"),
        str(root / "lib" / "doom" / "p_setup.c"),
        str(root / "lib" / "doom" / "p_mobj.c"),
        str(root / "lib" / "doom" / "p_tick.c"),
        str(root / "lib" / "doom" / "p_action.c"),
        str(root / "lib" / "doom" / "m_random.c"),
        str(root / "lib" / "doom" / "z_zone.c"),
        str(root / "lib" / "doom" / "doom_map.c"),
        str(root / "lib" / "doom" / "r_bsp.c"),
        str(root / "lib" / "doom" / "r_plane.c"),
        str(root / "lib" / "doom" / "r_tables.c"),
        str(root / "lib" / "doom" / "w_wad.c"),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does "
                           "not accept:\n" + result.stderr)
    return executable


def protocol_lines(text: str, drop_info: bool) -> list[str]:
    """The lines both sides print, with the banners and C-only measurements out."""
    lines = []
    for line in text.splitlines():
        if not line or line.startswith("WAD "):
            continue
        if drop_info and line.startswith("INFO "):
            continue
        lines.append(line)
    return lines


def compare(label: str, c_lines: list[str], ref_lines: list[str]) -> list[str]:
    """Every differing line, by position, with both values."""
    mismatches: list[str] = []
    for index in range(max(len(c_lines), len(ref_lines))):
        left = c_lines[index] if index < len(c_lines) else "<missing>"
        right = ref_lines[index] if index < len(ref_lines) else "<missing>"
        if left != right:
            mismatches.append(f"{label} line {index + 1}: C {left!r} oracle {right!r}")
    return mismatches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="things_check.py",
        description="Compare lib/doom/p_setup.c's THINGS run against "
                    "tools/doom_ref.py on a real WAD's E1M1, for all five skills.",
        epilog="The harness is built in a temporary directory and nothing is written "
               "into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--map", default=MAP_NAME, metavar="NAME",
                        help=f"the map to load (default: {MAP_NAME})")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"things_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="things_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc)

        mismatches: list[str] = []
        info_lines: list[str] = []
        for skill, skill_name in enumerate(SKILLS):
            c_result = subprocess.run([str(executable), str(arguments.wad), str(skill),
                                       arguments.map],
                                      capture_output=True, text=True)
            if c_result.returncode != 0:
                print(c_result.stderr or c_result.stdout, file=sys.stderr)
                print(f"things_check: the C harness failed for skill {skill_name}",
                      file=sys.stderr)
                return EXIT_USAGE

            ref_result = subprocess.run(
                [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
                 "--things", arguments.map, "--skill", skill_name],
                capture_output=True, text=True)
            if ref_result.returncode != 0:
                print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
                print(f"things_check: the oracle failed for skill {skill_name}",
                      file=sys.stderr)
                return EXIT_USAGE

            # The medium run is the reported one: it is the level's default
            # skill, and its arena numbers are the ones a budget is sized from.
            if skill == 2:
                info_lines = [line for line in c_result.stdout.splitlines()
                              if line.startswith("INFO ")]

            c_lines = protocol_lines(c_result.stdout, drop_info=True)
            ref_lines = protocol_lines(ref_result.stdout, drop_info=True)
            found = compare(skill_name, c_lines, ref_lines)
            mismatches.extend(found)

            counters = next((line for line in c_lines if line.startswith("COUNTERS")), "")
            count = next((line for line in c_lines if line.startswith("MOBJ-COUNT")), "")
            print(f"{skill_name:>9}: {count}  {counters}")
            for line in found[:3]:
                print(f"           {line}", file=sys.stderr)
    except RuntimeError as error:
        print(f"things_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    for line in info_lines:
        print(line)

    if mismatches:
        print(f"things_check: {len(mismatches)} divergent line(s)", file=sys.stderr)
        for line in mismatches[:20]:
            print(f"  {line}", file=sys.stderr)
        return EXIT_MISMATCH

    print("things_check: C and the oracle agree on every line, for all five skills")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
