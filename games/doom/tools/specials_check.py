#!/usr/bin/env python3
"""Compare the C sector specials against the Python oracle (D4.1b).

WHAT THIS PROVES

``test/unit/test_world`` pins each state machine on a synthetic map. This script
is the other half: it builds ``tools/specials_check.c`` -- which calls
``lib/doom/p_setup.c``, ``lib/doom/p_spec.c`` and the five mover files -- runs
it on a real WAD's E1M1, and compares its per-tic dump, line for line, with
``tools/doom_ref.py --specials``, an independent implementation written from
linuxdoom-1.10's p_spec.c / p_floor.c / p_doors.c / p_plats.c / p_ceilng.c /
p_lights.c.

THE SCRIPT (identical in both implementations, documented in specials_check.c)

Every line special in the map is triggered once, in ascending line index, at
tic 0, before the first tick. The kind is the documented rule: the
switch/manual numbers of P_UseSpecialLine are ``use``, every other special is
``cross``. For E1M1 that is 8 manual doors, one lift, one turbo-lowering floor,
the exit switch (deferred) and eight scrolling lines (deferred).

Tic 0 is after the spawn scan and the triggers; tic 1 is after one
``p_level_tick()``. The default run is 420 tics, long enough for a door to open,
wait 150 tics and close.

WHAT IS COMPARED

    SPECIALS ...         the map, the tic count and the table sizes
    TRIGGER ...          every triggered line, its kind and whether an EV ran
    SECTOR ...           every sector's floor, ceiling and light every tic
    THINKER ...          the sector thinkers in list order every tic
    SUMMARY ...          the counters (secrets, P_ChangeSector calls, deferrals)

``INFO `` lines are C-only measurements and ``WAD `` is the oracle's banner;
both are excluded.

USAGE

    python tools/specials_check.py --wad path/to/DOOM1.WAD

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
TICS = 420

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_tick.c", "p_action.c", "m_random.c",
    "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c",
    "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
    "p_lights.c",
)


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def compile_harness(workdir: Path, compiler: str) -> Path:
    """Builds tools/specials_check.c against the real world core."""
    root = project_root()
    executable = workdir / ("specials_check.exe" if sys.platform == "win32"
                            else "specials_check")
    command = [
        compiler,
        "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable),
        str(root / "tools" / "specials_check.c"),
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
    """The tic/sector/field a mismatch is about, as far as the line says."""
    kind, values = fields_of(line)
    where = []
    if "tic" in values:
        where.append(f"tic {values['tic']}")
    if "sector" in values:
        where.append(f"sector {values['sector']}")
    if kind == "THINKER" and "kind" in values:
        where.append(f"thinker kind {values['kind']}")
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
        prog="specials_check.py",
        description="Compare lib/doom/p_spec.c and the mover files against "
                    "tools/doom_ref.py --specials on a real WAD's E1M1.",
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
                        help=f"tics to step after tic 0 (default: {TICS})")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"specials_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE
    if arguments.tics < 0:
        print("specials_check: --tics cannot be negative", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    oracle_script = root / "tools" / "doom_ref.py"
    workdir = Path(tempfile.mkdtemp(prefix="specials_check_"))

    try:
        executable = compile_harness(workdir, arguments.cc)

        c_result = subprocess.run(
            [str(executable), str(arguments.wad), arguments.map,
             str(arguments.tics)],
            capture_output=True, text=True)
        if c_result.returncode != 0:
            print(c_result.stderr or c_result.stdout, file=sys.stderr)
            print("specials_check: the C harness failed", file=sys.stderr)
            return EXIT_USAGE

        ref_result = subprocess.run(
            [sys.executable, str(oracle_script), "--wad", str(arguments.wad),
             "--specials", arguments.map, "--tics", str(arguments.tics)],
            capture_output=True, text=True)
        if ref_result.returncode != 0:
            print(ref_result.stderr or ref_result.stdout, file=sys.stderr)
            print("specials_check: the oracle failed", file=sys.stderr)
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

        sectors = next((line for line in c_lines if line.startswith("SPECIALS ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        info = next((line for line in c_result.stdout.splitlines()
                     if line.startswith("INFO ")), "")
        print(f"{arguments.map}: {arguments.tics + 1} tics, {len(c_lines)} compared "
              f"lines, {mismatches} divergent")
        print(f"  {sectors}")
        print(f"  {summary}")
        if info:
            print(f"  {info}")
        if mismatches:
            print(f"specials_check: {mismatches} divergent line(s); first: {first}",
                  file=sys.stderr)
            return EXIT_MISMATCH
    except RuntimeError as error:
        print(f"specials_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    print("specials_check: C and the oracle agree on every line")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
