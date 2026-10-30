#!/usr/bin/env python3
"""Compare the C picture decoder against the Python oracle on the real WAD (D2.4).

WHAT THIS PROVES

``test/unit/test_rdat`` pins the decoders on synthetic lumps, because a CI
machine has no WAD. This script is the other half: it compiles
``tools/rdat_check.c`` -- which calls ``lib/doom/r_data.c`` and
``lib/doom/w_wad.c`` -- and compares every hash it prints with the same item
decoded by ``tools/doom_ref.py``, an independent implementation that reads the
raw WAD with its own parser.

The comparison is a whole-set diff, not a sample: all 646 patch lumps the WAD
names (PNAMES entries plus sprites), all 125 textures in TEXTURE1 and all 54
real flats. On top of that it reports the three sets this task asks for
explicitly: 100 sampled patches, every texture E1M1 references, and 20 sampled
flats.

The sample rule is stated rather than random, so two runs of this script -- on
any machine -- pick the same items: take the sorted names and pick index
``floor(i * len / count)`` for i in 0..count-1.

USAGE

    python tools/rdat_check.py --wad path/to/DOOM1.WAD

The WAD is copyrighted shareware data and is never committed or copied into the
repository; point this tool at a file outside it. Nothing here writes into the
worktree: the harness is compiled into a temporary directory that is removed
before the script returns, and no decoded byte is printed -- only names, counts
and, on a mismatch, the two hashes.

EXIT CODES

    0  every item matched
    1  at least one item differed or was missing from one side
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

KINDS = ("PATCH", "TEXTURE", "FLAT")
PROBLEM_PREFIXES = ("PATCH-INVALID", "TEXTURE-INVALID", "TEXTURE-UNRESOLVED", "FLAT-SKIP")

SAMPLE_PATCHES = 100
SAMPLE_FLATS = 20
MAP_NAME = "E1M1"

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/rdat_check.c against the real decoder sources."""
    root = project_root()
    executable = workdir / ("rdat_check.exe" if sys.platform == "win32" else "rdat_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "rdat_check.c"),
        str(root / "lib" / "doom" / "r_data.c"),
        str(root / "lib" / "doom" / "w_wad.c"),
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile with the C compiler:\n"
                           + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does not "
                           "accept:\n" + result.stderr)
    return executable


def parse_output(text: str) -> tuple[dict[str, dict[str, str]], list[str], list[str], list[str]]:
    """Splits one side's output into hashes, problem lines and the map listing."""
    hashes: dict[str, dict[str, str]] = {kind: {} for kind in KINDS}
    problems: list[str] = []
    map_textures: list[str] = []
    map_flats: list[str] = []

    for line in text.splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] in KINDS and len(parts) == 3:
            hashes[parts[0]][parts[1]] = parts[2]
        elif parts[0] in PROBLEM_PREFIXES:
            problems.append(line)
        elif parts[0] == "MAP-TEXTURE" and len(parts) == 2:
            map_textures.append(parts[1])
        elif parts[0] == "MAP-FLAT" and len(parts) == 2:
            map_flats.append(parts[1])

    return hashes, problems, map_textures, map_flats


def sample(names: list[str], count: int) -> list[str]:
    """Deterministic spread: floor(i * len / count), i in 0..count-1."""
    ordered = sorted(names)
    if len(ordered) <= count:
        return ordered
    return [ordered[(index * len(ordered)) // count] for index in range(count)]


def compare(decoder: dict[str, str], oracle: dict[str, str]) -> list[str]:
    """Every name in either side, with a hash difference reported by name."""
    mismatches: list[str] = []
    for name in sorted(set(decoder) | set(oracle)):
        left = decoder.get(name)
        right = oracle.get(name)
        if left is None:
            mismatches.append(f"missing from the C decoder: {name}")
        elif right is None:
            mismatches.append(f"missing from the oracle: {name}")
        elif left != right:
            mismatches.append(f"{name}: C {left} oracle {right}")
    return mismatches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="rdat_check.py",
        description="Compare lib/doom/r_data.c against tools/doom_ref.py on a real WAD: "
                    "the whole directory, plus the 100-patch / E1M1-texture / 20-flat "
                    "sets this task names.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="The harness is built in a temporary directory and nothing is written "
               "into the worktree.\n",
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to decode; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"rdat_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="rdat_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc)

        c_result = subprocess.run([str(executable), str(arguments.wad)],
                                  capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("rdat_check: the C harness did not run cleanly", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--all", "--map", MAP_NAME],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("rdat_check: the oracle did not run cleanly", file=sys.stderr)
            return EXIT_USAGE
    except RuntimeError as error:
        print(f"rdat_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    c_hashes, c_problems, _, _ = parse_output(c_result.stdout)
    ref_hashes, ref_problems, map_textures, map_flats = parse_output(ref_result.stdout)

    print(f"rdat_check: {arguments.wad}")
    for kind in KINDS:
        print(f"  {kind.lower() + ' lumps':10s} C {len(c_hashes[kind]):4d}   "
              f"oracle {len(ref_hashes[kind]):4d}")

    mismatches: list[str] = []
    for kind in KINDS:
        mismatches.extend(f"[{kind}] {line}" for line in
                          compare(c_hashes[kind], ref_hashes[kind]))

    # The three sets this task names, reported separately from the whole diff.
    sampled_patches = sample(set(ref_hashes["PATCH"]) | set(c_hashes["PATCH"]), SAMPLE_PATCHES)
    sampled_flats = sample(set(ref_hashes["FLAT"]) | set(c_hashes["FLAT"]), SAMPLE_FLATS)

    def matched(kind: str, names: list[str]) -> tuple[int, int]:
        good = sum(1 for name in names
                   if c_hashes[kind].get(name) is not None
                   and c_hashes[kind].get(name) == ref_hashes[kind].get(name))
        return good, len(names)

    patch_good, patch_total = matched("PATCH", sampled_patches)
    flat_good, flat_total = matched("FLAT", sampled_flats)
    texture_good, texture_total = matched("TEXTURE", map_textures)

    print(f"  sample      {patch_good}/{patch_total} patches, "
          f"{texture_good}/{texture_total} {MAP_NAME} textures, "
          f"{flat_good}/{flat_total} flats")
    print(f"  whole set   {sum(1 for kind in KINDS for _ in ref_hashes[kind])} names, "
          f"{len(mismatches)} mismatches")

    if c_problems or ref_problems:
        for line in c_problems:
            print(f"  C {line}")
        for line in ref_problems:
            print(f"  oracle {line}")
        if c_problems != ref_problems:
            mismatches.append("the two sides reported different per-item problems")

    unresolved = [name for name in map_textures
                  if ref_hashes["TEXTURE"].get(name) is None]
    if unresolved:
        print(f"  {MAP_NAME} names with no texture definition: {', '.join(unresolved)}")

    if mismatches:
        for line in mismatches[:20]:
            print(f"  MISMATCH {line}")
        if len(mismatches) > 20:
            print(f"  ... and {len(mismatches) - 20} more")
        return EXIT_MISMATCH

    print("  result      every item matched")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
