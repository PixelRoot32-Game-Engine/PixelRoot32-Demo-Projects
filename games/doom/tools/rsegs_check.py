#!/usr/bin/env python3
"""Compare the C wall emitter against the Python oracle on the real WAD (D3.2).

WHAT THIS PROVES

``test/unit/test_rsegs`` pins the emitter on synthetic maps, textures and
colormaps, because a CI machine has no WAD. This script is the other half: it
compiles ``tools/rsegs_check.c`` -- which calls ``lib/doom/r_segs.c``,
``r_light.c``, ``r_bsp.c``, ``r_plane.c``, ``r_tables.c``, ``r_data.c``,
``doom_map.c`` and ``w_wad.c`` -- and compares its 20 sampled E1M1 frames, line
for line, with ``tools/doom_ref.py --segs E1M1``, an independent implementation
with its own WAD reader, map parser, tables, walk, texture compositor, colormap
handling and column loop.

The compared lines are the protocol both sides document:

    SEGS <map> frames=<n>
    frame NN: <sha256>          the pixel buffer followed by the covered mask
    FRAMESTAT NN ...            counts, per-tier pixels and both partial hashes
    SEGS-SUMMARY ...

A mismatch is localised in three steps instead of being reported as one hash: the
frames are compared first, then the columns of the first differing frame
(``--segs-detail``), then the rows of that column (``--segs-column``), so the
report names the first divergent pixel and its column and row. Those two extra
passes print decoded palette indices for one column; they are diagnostics of
record and must not be committed.

USAGE

    python tools/rsegs_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed or copied into the
repository; point this tool at a file outside it. Nothing here writes into the
worktree: the harness is compiled into a temporary directory that is removed
before the script returns. The normal run prints counts and hashes only.

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
    """Builds tools/rsegs_check.c against the real walk, emitter and light code."""
    root = project_root()
    executable = workdir / ("rsegs_check.exe" if sys.platform == "win32" else "rsegs_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "rsegs_check.c"),
        str(root / "lib" / "doom" / "r_segs.c"),
        str(root / "lib" / "doom" / "r_light.c"),
        str(root / "lib" / "doom" / "r_bsp.c"),
        str(root / "lib" / "doom" / "r_plane.c"),
        str(root / "lib" / "doom" / "r_tables.c"),
        str(root / "lib" / "doom" / "r_data.c"),
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


def run_c(executable: Path, wad: Path, extra: list[str]) -> str:
    result = subprocess.run([str(executable), str(wad)] + extra,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the C harness did not run cleanly:\n"
                           + (result.stderr or result.stdout))
    return result.stdout


def run_oracle(script: Path, wad: Path, extra: list[str]) -> str:
    result = subprocess.run([sys.executable, str(script), "--wad", str(wad),
                             "--segs", MAP_NAME] + extra,
                            capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the oracle did not run cleanly:\n"
                           + (result.stderr or result.stdout))
    return result.stdout


def first_difference(left: list[str], right: list[str]) -> tuple[int, str, str] | None:
    for index in range(max(len(left), len(right))):
        a = left[index] if index < len(left) else "<missing>"
        b = right[index] if index < len(right) else "<missing>"
        if a != b:
            return index, a, b
    return None


def differing_frames(left: list[str], right: list[str]) -> list[int]:
    frames: list[int] = []
    for a, b in zip(left, right):
        if a.startswith("frame ") and a != b:
            frames.append(int(a.split()[1].rstrip(":")))
    return frames


def by_key(lines: list[str], prefix: str, key_at: int) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in lines:
        if line.startswith(prefix):
            found[line.split()[key_at]] = line
    return found


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="rsegs_check.py",
        description="Compare lib/doom/r_segs.c against tools/doom_ref.py on the "
                    "20 sampled wall-only frames of a real WAD's E1M1.",
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
        print(f"rsegs_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="rsegs_check_"))
    diagnostics: list[str] = []

    try:
        executable = compile_harness(workdir, arguments.cc)
        c_text = run_c(executable, arguments.wad, [])
        oracle_text = run_oracle(oracle_script, arguments.wad, [])
        c_lines = protocol_lines(c_text)
        oracle_lines = protocol_lines(oracle_text)

        mismatch = first_difference(c_lines, oracle_lines)
        if mismatch is not None:
            for frame in differing_frames(c_lines, oracle_lines)[:1]:
                c_columns = protocol_lines(
                    run_c(executable, arguments.wad, ["--detail", str(frame)]))
                oracle_columns = protocol_lines(
                    run_oracle(oracle_script, arguments.wad,
                               ["--segs-detail", str(frame)]))
                c_col = by_key(c_columns, "COLUMN", 2)
                o_col = by_key(oracle_columns, "COLUMN", 2)
                column = None
                for index in range(320):
                    key = str(index)
                    if c_col.get(key) != o_col.get(key):
                        column = index
                        break
                if column is not None:
                    c_rows = protocol_lines(
                        run_c(executable, arguments.wad,
                              ["--column", f"{frame},{column}"]))
                    o_rows = protocol_lines(
                        run_oracle(oracle_script, arguments.wad,
                                   ["--segs-column", f"{frame},{column}"]))
                    c_row = by_key(c_rows, "ROW", 3)
                    o_row = by_key(o_rows, "ROW", 3)
                    for row in range(200):
                        if c_row.get(str(row)) != o_row.get(str(row)):
                            diagnostics.append(
                                f"frame {frame}: first divergence at column {column} "
                                f"row {row}: C {c_row.get(str(row))!r} "
                                f"oracle {o_row.get(str(row))!r} "
                                f"(ROW lines carry the two palette indices and the "
                                f"covered byte: decoded WAD data, not for a commit)")
                            break
                    else:
                        diagnostics.append(
                            f"frame {frame}: column {column} first diverges in the "
                            f"covered byte, not in the pixels")
    except RuntimeError as error:
        print(f"rsegs_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    for line in c_lines:
        if line.startswith("frame ") or line.startswith("SEGS-SUMMARY") \
                or line.startswith("ERROR"):
            print(line)

    if mismatch is not None:
        index, left, right = mismatch
        print(f"rsegs_check: line {index + 1} differs", file=sys.stderr)
        print(f"  C      {left!r}", file=sys.stderr)
        print(f"  oracle {right!r}", file=sys.stderr)
        for line in diagnostics:
            print(f"  {line}", file=sys.stderr)
        return EXIT_MISMATCH

    print(f"rsegs_check: C and the oracle agree on all {len(c_lines)} lines")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
