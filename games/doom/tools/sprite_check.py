#!/usr/bin/env python3
"""The sprite-submission gate (T23) against the real WAD and the real renderer.

WHAT IT PROVES

``test/unit/test_world`` pins the world's rules on synthetic maps. This script
is the other half for the sprite path: it builds ``tools/sprite_check.c`` --
which links ``lib/doom/p_setup.c``, ``p_mobj.c``, the specials, the four
renderer passes and the WAD reader -- loads E1M1's world through
``p_level_init()`` + ``p_spawn_specials()``, and asserts:

    A1  the static THINGS sprite list and the live-mobj list render the same
        frame at the host harness's frame-00 viewpoint;
    A2  the static list and a live list PINNED to each mobj's spawn-state row
        render the same frame at the player's spawn viewpoint at tic 1 and after
        10 and 35 tics, so a sector-light thinker cannot quietly move a sprite's
        light between the two lists. Pinning is the honest comparison: the
        static list is a load-time snapshot, so A2 is about the submission
        agreeing at spawn, not about the panel being frozen;
    A2b the live list actually advances a chain: at tics 10 and 35 a submitted
        mobj asks for a baked row other than its type's spawn row, and no chain
        frame falls back. The one counted fallback is the player avatar, whose
        S_PLAY_STND is deliberately not baked;
    B1  a live puff changes the frame and the masked pass asks the resolver for
        PUFFA0 (the puff's own lump, not "nothing" and not another type's row);
    C1  the puff walks S_PUFF1 -> S_PUFF2 -> S_PUFF3 -> S_PUFF4;
    C2  each of those states asks for PUFFA0/B0/C0/D0 in turn;
    C3  each state renders a different frame at one fixed position, so the
        change is the sprite and not the puff's z drift.

The board's own evidence is its serial log: the frame-00 hash
``50fb3d9c...``, the settled spawn-view hash ``fffb3c5c...`` and
``refused=0``. This tool is the pixel proof that the rule producing them is the
right one; it cannot call ``src/doomc/FrameDriver.cpp`` because that file's
board half is compiled only for ESP32.

PROVING THE GATE CAN FAIL

    python tools/sprite_check.py --wad ... --perturb frame

adds ``-DSPRITE_CHECK_PERTURB_FRAME`` to the C build only. Every live list then
uses each mobj's spawn-state row instead of its live frame -- the static
behaviour T23 replaced -- so A2b sees zero advances and the puff stops
advancing: A2b, C1, C2 and C3 fail and the process exits 1. A comparison that
cannot fail is not a gate.

USAGE

    python tools/sprite_check.py --wad path/to/DOOM1.WAD [--perturb frame]

The WAD is copyrighted shareware data and is never committed; point this tool at
a file outside the repository. The harness is compiled into a temporary
directory that is removed before the script returns and nothing is written into
the worktree.

EXIT CODES

    0  every assertion passed
    1  at least one assertion failed
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
    "p_inter.c", "p_pspr.c", "m_random.c", "z_zone.c", "doom_map.c",
    "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c", "p_spec.c", "p_floor.c",
    "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c",
    "r_segs.c", "r_light.c", "r_things.c", "r_data.c",
)

PERTURBATIONS = {
    "none": [],
    # Use every mobj's spawn-state row instead of its live frame. This is the
    # old static list's behaviour: a puff stays on its first frame.
    "frame": ["-DSPRITE_CHECK_PERTURB_FRAME=1"],
}


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compiler_name() -> str:
    """The C compiler this project's other checks already use."""
    for candidate in ("gcc", "cc", "clang"):
        if shutil.which(candidate):
            return candidate
    return "gcc"


def compile_harness(workdir: Path, compiler: str, perturbation: str) -> Path:
    """Builds tools/sprite_check.c against the real world and the renderer."""
    root = project_root()
    executable = workdir / ("sprite_check.exe" if sys.platform == "win32"
                            else "sprite_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        *PERTURBATIONS[perturbation],
        "-I", str(root / "lib" / "doom"),
        "-I", str(root / "tools"),
        "-o", str(executable),
        str(root / "tools" / "sprite_check.c"),
        *(str(root / "lib" / "doom" / source) for source in SOURCES),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does "
                           "not accept:\n" + result.stderr)
    return executable


def run_harness(executable: Path, wad: Path) -> int:
    """Runs the harness on the WAD, streaming its lines, and returns its code."""
    result = subprocess.run([str(executable), str(wad)], text=True)
    return result.returncode


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--wad", required=True, metavar="PATH",
                        help="path to DOOM1.WAD (v1.9 shareware)")
    parser.add_argument("--perturb", choices=sorted(PERTURBATIONS), default="none",
                        help="compile a wrong rule in the C harness so the gate must fail")
    parser.add_argument("--keep", action="store_true", help="keep the temporary build directory")
    arguments = parser.parse_args(argv)

    wad = Path(arguments.wad)
    if not wad.is_file():
        print(f"sprite_check.py: no such WAD: {wad}", file=sys.stderr)
        return EXIT_USAGE

    workdir = Path(tempfile.mkdtemp(prefix="sprite_check_"))
    try:
        executable = compile_harness(workdir, compiler_name(), arguments.perturb)
        code = run_harness(executable, wad)
    except RuntimeError as error:
        print(f"sprite_check.py: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        if not arguments.keep:
            shutil.rmtree(workdir, ignore_errors=True)

    if code == 0:
        print(f"SPRITE-CHECK script map={MAP_NAME} perturb={arguments.perturb} status=pass")
        return EXIT_OK

    print(f"SPRITE-CHECK script map={MAP_NAME} perturb={arguments.perturb} "
          f"status=fail exit={code}")
    return EXIT_MISMATCH


if __name__ == "__main__":
    sys.exit(main())
