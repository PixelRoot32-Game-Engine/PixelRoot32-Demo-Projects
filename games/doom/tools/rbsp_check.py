#!/usr/bin/env python3
"""Compare the C BSP walk against the Python oracle on the real WAD (D3.1).

WHAT THIS PROVES

``test/unit/test_rbsp`` pins the walk on synthetic BSPs, because a CI machine
has no WAD. This script is the other half: it compiles ``tools/rbsp_check.c``
-- which calls ``lib/doom/r_bsp.c``, ``r_plane.c``, ``r_tables.c``,
``doom_map.c`` and ``w_wad.c`` -- and compares its E1M1 spawn view, line for
line, with ``tools/doom_ref.py --bsp E1M1``, an independent implementation
with its own WAD reader, map parser, angle tables and walk.

The compared lines are the protocol both sides document:

    BSP ...            the spawn viewpoint (x, y, z, angle)
    DRAWSEG ...        every visible wall fragment, in emission order
    DRAWSEG-COUNT
    VISPLANE ...       every plane: height, picture, light, minx, maxx
    VISPLANE-COUNT
    VISPLANE-MARKED
    HASH DRAWSEGS
    HASH VISPLANES
    HASH BOTH

The hashes cover the column arrays too, so "the same plane" means the same
top/bottom bytes, not just the same keys. A mismatch is reported by line, with
the first divergent drawseg printed as both sides saw it.

USAGE

    python tools/rbsp_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed or copied into the
repository; point this tool at a file outside it. Nothing here writes into the
worktree: the harness is compiled into a temporary directory that is removed
before the script returns. Map coordinates beyond the spawn viewpoint are not
printed; the output is counts and hashes.

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


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/rbsp_check.c against the real walk sources."""
    root = project_root()
    executable = workdir / ("rbsp_check.exe" if sys.platform == "win32" else "rbsp_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "rbsp_check.c"),
        str(root / "lib" / "doom" / "r_bsp.c"),
        str(root / "lib" / "doom" / "r_plane.c"),
        str(root / "lib" / "doom" / "r_tables.c"),
        str(root / "lib" / "doom" / "doom_map.c"),
        str(root / "lib" / "doom" / "w_wad.c"),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does "
                           "not accept:\n" + result.stderr)
    return executable


def protocol_lines(text: str) -> list[str]:
    """The lines both sides print, in order, with the WAD banner removed."""
    return [line for line in text.splitlines()
            if line and not line.startswith("WAD ")]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="rbsp_check.py",
        description="Compare lib/doom/r_bsp.c against tools/doom_ref.py on a real "
                    "WAD's E1M1 spawn view.",
        epilog="The harness is built in a temporary directory and nothing is written "
               "into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"rbsp_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="rbsp_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc)

        c_result = subprocess.run([str(executable), str(arguments.wad)],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("rbsp_check: the C harness did not run cleanly", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--bsp", MAP_NAME],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("rbsp_check: the oracle did not run cleanly", file=sys.stderr)
            return EXIT_USAGE
    except RuntimeError as error:
        print(f"rbsp_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    c_lines = protocol_lines(c_result.stdout)
    ref_lines = protocol_lines(ref_result.stdout)

    c_drawsegs = {line.split()[1]: line for line in c_lines if line.startswith("DRAWSEG ")}
    ref_drawsegs = {line.split()[1]: line for line in ref_lines if line.startswith("DRAWSEG ")}

    mismatches: list[str] = []
    for index in range(max(len(c_lines), len(ref_lines))):
        left = c_lines[index] if index < len(c_lines) else "<missing>"
        right = ref_lines[index] if index < len(ref_lines) else "<missing>"
        if left != right:
            mismatches.append(f"line {index + 1}: C {left!r} oracle {right!r}")

    for index in sorted(set(c_drawsegs) | set(ref_drawsegs), key=int):
        left = c_drawsegs.get(index)
        right = ref_drawsegs.get(index)
        if left != right:
            mismatches.append(f"drawseg {index}: C {left!r} oracle {right!r}")
            break

    for line in c_lines:
        if line.startswith("DRAWSEG-COUNT") or line.startswith("VISPLANE-COUNT") \
                or line.startswith("VISPLANE-MARKED") or line.startswith("HASH "):
            print(line)

    if mismatches:
        print(f"rbsp_check: {len(mismatches)} divergent line(s)", file=sys.stderr)
        for line in mismatches[:10]:
            print(f"  {line}", file=sys.stderr)
        return EXIT_MISMATCH

    print("rbsp_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
