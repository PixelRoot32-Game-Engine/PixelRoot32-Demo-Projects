#!/usr/bin/env python3
"""Compare the C renderer against the Python oracle on the real WAD.

WHAT THIS PROVES

``test/unit/test_rsegs`` pins the wall, plane, sprite and masked passes on
synthetic maps, textures, flats and sprite patches, because a CI machine has no
WAD. This script is the other half: it compiles ``tools/rsegs_check.c`` -- which
calls ``lib/doom/r_segs.c``, ``r_plane.c``, ``r_things.c``, ``r_light.c``,
``r_bsp.c``, ``r_tables.c``, ``r_data.c``, ``doom_map.c`` and ``w_wad.c`` -- and
compares its 20 sampled E1M1 frames, line for line, with
``tools/doom_ref.py --segs E1M1``, an independent implementation with its own
WAD reader, map parser, tables, walk, texture compositor, flat lookup, colormap
handling, column loop, span maker, sprite projection, sprite sort, sprite clip
seed and patch walker.

The compared lines are the protocol both sides document:

    SEGS <map> frames=<n>
    frame NN: <sha256>          the wall pass's pixels followed by its coverage
    FRAMESTAT NN ...            wall counts, per-tier pixels and both partial hashes
    PLANESTAT NN ...            plane counts and the wall, plane-mask and frame hashes
    SPRSTAT NN ...              sprite counters, the pool peaks, the masked-pass
                                mask hash and the finished frame's hash
    SEGS-SUMMARY ...            the wall totals
    PLANE-SUMMARY ...           the plane totals
    SPR-SUMMARY ...             the sprite totals and the audited pool peaks

`--segs-detail` and `--segs-column` report the *finished* frame (after the
masked pass) rather than the frame after the planes, because that is the frame a
reader is comparing; the ROW mask field is the masked pass's mask.

A mismatch is localised in three steps instead of being reported as one hash: the
frames are compared first, then the columns of the first differing frame
(``--segs-detail``), then the rows of that column (``--segs-column``). The COLUMN
line carries both the wall column's hash and the finished frame's, and the ROW
line carries the wall pixel, the final pixel, the wall coverage and the masked
pass's mask, so the report can name the first divergent pixel and say which pass
wrote it. Those two extra passes print decoded palette indices for one column;
they are diagnostics of record and must not be committed.

THE SECOND STAGE: THE SCROLL, IN PIXELS

The 20 sampled viewpoints do not look at E1M1's scrolling wall -- line special
48's eight lines there are the lower tier of the nukage ring -- so a scroll probe
built on them would pass while proving nothing. This stage therefore points a
camera at the wall from numbers it derives from the map itself (the line's
midpoint, 48 map units along the front side's normal, the front floor + 41, and
the angle back at the midpoint) and hands the same four numbers to both
implementations with ``--view`` / ``--segs-view``.

Three claims, each able to fail:

  1. the *unscrolled* frame agrees  -- the camera and the static render;
  2. the *scrolled* frame agrees    -- the C harness adds the delta through
     p_scroll_adjust_side(), the function the firmware's map seam runs, and the
     oracle adds it to the sidedef's textureoffset before rendering; equal
     hashes mean the renderer read the number the simulation wrote;
  3. the two frames differ          -- the pixels really moved.

``--perturb scroll`` moves the oracle's delta by one map unit and requires the
comparison to fail.

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
import math
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import doom_ref  # noqa: E402  (path-dependent by design)

MAP_NAME = "E1M1"

# --- The scroll proof --------------------------------------------------------
#
# The scrolling wall is line special 48's front side, and it is *not* in any of
# the 20 sampled views: E1M1's eight scrolling lines (352..359) are the lower
# tier of the nukage ring, and no sampled camera looks along it. So this stage
# points a camera at it, from numbers the gate derives from the map and hands to
# both implementations:
#
#   position  the line's midpoint, moved SCROLL_CAMERA_DISTANCE map units along
#             the front side's outward normal (dy, -dx of the v1->v2 direction);
#   height    the front sector's floor + 41 map units (P_SpawnPlayer's view);
#   angle     atan2 back to the midpoint, in binary angles.
#
# The camera's z is above the front floor and below the back floor, which is
# where a lower tier exists at all.
SCROLL_CAMERA_DISTANCE = 48.0
SCROLL_VIEW_HEIGHT = 41
SCROLL_TICS = 8            # 8 map units: one eighth of a 64-wide texture
SCROLL_PERTURBATIONS = ("none", "scroll")

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
        str(root / "lib" / "doom" / "r_things.c"),
        str(root / "lib" / "doom" / "r_view_diag.c"),
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
        if a != b and (a.startswith("frame ") or a.startswith("PLANESTAT ")
                       or a.startswith("SPRSTAT ")):
            frames.append(int(a.split()[1].rstrip(":")))
    return frames


def by_key(lines: list[str], prefix: str, key_at: int) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in lines:
        if line.startswith(prefix):
            found[line.split()[key_at]] = line
    return found


# --- The scroll proof -------------------------------------------------------

def trunc_div(value: int, divisor: int) -> int:
    """C's integer division: truncate toward zero, whatever the sign."""
    quotient = abs(value) // abs(divisor)
    if (value < 0) != (divisor < 0):
        return -quotient
    return quotient


def scroll_camera(wad: Path) -> tuple[str, int, int, int, int, int, int, int]:
    """(map, line, side, x, y, z, angle, dx) for the scroll proof.

    The line is the lowest-indexed one special 48 whose front side really draws a
    lower tier -- back floor above front floor and a bottom texture -- because a
    two-sided line with no tier height has nothing for a scroll to move.
    """
    _identification, lumps = doom_ref.read_wad(wad)
    for number in range(1, 10):
        map_name = f"E1M{number}"
        bsp = doom_ref.read_bsp_map(lumps, map_name)
        for index, line in enumerate(bsp.linedefs):
            if line["special"] != 48 or line["side0"] < 0 or line["side1"] < 0:
                continue
            side = bsp.sidedefs[line["side0"]]
            front = bsp.sectors[side["sector"]]
            back = bsp.sectors[bsp.sidedefs[line["side1"]]["sector"]]
            if back["floor"] <= front["floor"]:
                continue
            if not doom_ref.has_texture(side["bottom"]):
                continue

            v1 = bsp.vertexes[line["v1"]]
            v2 = bsp.vertexes[line["v2"]]
            dx = v2["x"] - v1["x"]
            dy = v2["y"] - v1["y"]
            length = math.hypot(dx, dy)
            if length == 0.0:
                continue
            mid_x = trunc_div(v1["x"] + v2["x"], 2)
            mid_y = trunc_div(v1["y"] + v2["y"], 2)
            # The front side of a linedef is its right side looked down v1->v2.
            camera_x = mid_x + int(round(dy / length * SCROLL_CAMERA_DISTANCE
                                         * doom_ref.FRACUNIT))
            camera_y = mid_y + int(round(-dx / length * SCROLL_CAMERA_DISTANCE
                                         * doom_ref.FRACUNIT))
            angle = int(round(math.atan2(mid_y - camera_y, mid_x - camera_x)
                              / (2.0 * math.pi) * (1 << 32))) & 0xFFFFFFFF
            z = front["floor"] + SCROLL_VIEW_HEIGHT * doom_ref.FRACUNIT
            return (map_name, index, line["side0"], camera_x, camera_y, z,
                    angle, SCROLL_TICS * doom_ref.FRACUNIT)
    raise RuntimeError("no episode-1 map carries a scrolling line with a drawn tier")


def frame_hashes(lines: list[str]) -> dict[int, str]:
    """frame index -> the frame line's hash, from a SEGS protocol."""
    found: dict[int, str] = {}
    for line in lines:
        if line.startswith("frame "):
            index = int(line.split()[1].rstrip(":"))
            found[index] = line
    return found


class ScrollError(Exception):
    """The scroll proof's own failure: a mismatch, or no pixel moved."""


def run_scroll_stage(executable: Path, oracle: Path, wad: Path,
                     perturb: str) -> str:
    """The scroll proof: one camera, two offsets, both implementations.

    Returns a one-line report; raises RuntimeError when a comparison fails. The
    three claims, in order:

      1. the *unscrolled* frame agrees  -- the camera and the static render;
      2. the *scrolled* frame agrees    -- the renderer reads the offset the
         simulation writes, and the oracle predicts the same pixels;
      3. the two frames differ          -- the pixels really moved.
    """
    map_name, line, side, x, y, z, angle, delta = scroll_camera(wad)
    view = f"{x},{y},{z},{angle}"
    oracle_delta = delta + (doom_ref.FRACUNIT if perturb == "scroll" else 0)

    base_c = protocol_lines(run_c(executable, wad, ["--view", view]))
    base_o = protocol_lines(run_oracle(oracle, wad, [f"--segs-view={view}"]))
    mismatch = first_difference(base_c, base_o)
    if mismatch is not None:
        index, left, right = mismatch
        raise ScrollError(f"the unscrolled frame differs at line {index + 1}:\n"
                           f"  C      {left!r}\n  oracle {right!r}")

    scroll_c = protocol_lines(run_c(executable, wad,
                                    ["--view", view, "--scroll",
                                     f"{side},{delta}"]))
    scroll_o = protocol_lines(run_oracle(oracle, wad,
                                         [f"--segs-view={view}", "--segs-scroll",
                                          f"{side},{oracle_delta}"]))
    mismatch = first_difference(scroll_c, scroll_o)
    if mismatch is not None:
        index, left, right = mismatch
        raise ScrollError(f"the scrolled frame differs at line {index + 1}:\n"
                           f"  C      {left!r}\n  oracle {right!r}\n"
                           f"  (line {line}, side {side}, dx {delta})")

    moved = [index for index, base in frame_hashes(base_c).items()
             if frame_hashes(scroll_c).get(index) != base]
    if not moved:
        raise ScrollError(f"the scroll of side {side} by {delta} changed no "
                           "pixel: the renderer is not reading the offset")
    return (f"{map_name} line {line} side {side} at ({x},{y},{z},{angle}): "
            f"{len(moved)} of {len(frame_hashes(base_c))} frame(s) moved by "
            f"dx={delta}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="rsegs_check.py",
        description="Compare the four DOOM software-renderer passes (walls, planes, "
                    "sprites, masked mids) against tools/doom_ref.py on the 20 "
                    "sampled frames of a real WAD's E1M1.",
        epilog="The harness is built in a temporary directory and nothing is written "
               "into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--perturb", choices=SCROLL_PERTURBATIONS, default="none",
                        help="move the oracle's scroll delta by one map unit, so "
                             "the scroll stage is shown to be able to fail")
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
                    c_parts = c_col.get(str(column), "").split()
                    o_parts = o_col.get(str(column), "").split()
                    wall_column = (len(c_parts) > 3 and len(o_parts) > 3
                                   and c_parts[3] != o_parts[3])
                    diagnostics.append(
                        f"frame {frame}: first divergent column {column} "
                        f"({'the wall pass' if wall_column else 'only the final frame, so the plane pass'})")
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
                            c_fields = c_row.get(str(row), "").split()
                            o_fields = o_row.get(str(row), "").split()
                            if len(c_fields) >= 8 and len(o_fields) >= 8:
                                wall_c, frame_c = c_fields[4], c_fields[5]
                                wall_o, frame_o = o_fields[4], o_fields[5]
                                covered_c, mask_c = c_fields[6], c_fields[7]
                                covered_o, mask_o = o_fields[6], o_fields[7]
                                if wall_c != wall_o:
                                    writer = ("the wall pass (its pixel differs "
                                              "before the plane pass draws)")
                                elif frame_c != frame_o and mask_c != "00":
                                    writer = ("the plane or masked pass (a later "
                                              "pass wrote this pixel)")
                                elif frame_c != frame_o:
                                    writer = ("a later pass, by coverage: its mask "
                                              "is 0, so it did not write here")
                                else:
                                    writer = ("the coverage records, not the pixels "
                                              f"(wall covered {covered_c}/{covered_o}, "
                                              f"later mask {mask_c}/{mask_o})")
                            else:
                                writer = "an unparsable ROW line"
                            diagnostics.append(
                                f"frame {frame}: first divergence at column {column} "
                                f"row {row}: C {c_row.get(str(row))!r} "
                                f"oracle {o_row.get(str(row))!r} -> {writer} "
                                f"(ROW lines carry decoded WAD bytes, not for a commit)")
                            break
                    else:
                        diagnostics.append(
                            f"frame {frame}: column {column} first diverges in the "
                            f"coverage byte, not in the pixels")

        # --- The scroll proof ----------------------------------------------
        # Inside the try, because it runs the harness the temporary directory
        # holds; and only when the 20-frame comparison agreed, because a
        # divergent renderer has nothing to prove about one more frame.
        if mismatch is None:
            try:
                scroll_report = run_scroll_stage(executable, oracle_script,
                                                 arguments.wad, arguments.perturb)
            except ScrollError as error:
                if arguments.perturb != "none":
                    scroll_report = (f"--perturb {arguments.perturb} moved the "
                                     f"comparison: {error}")
                else:
                    print(f"rsegs_check: {error}", file=sys.stderr)
                    return EXIT_MISMATCH
        else:
            scroll_report = "not run: the 20-frame comparison diverged"
    except RuntimeError as error:
        print(f"rsegs_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    for line in c_lines:
        if line.startswith("frame ") or line.startswith("PLANESTAT ") \
                or line.startswith("SPRSTAT ") \
                or line.startswith("SEGS-SUMMARY") \
                or line.startswith("PLANE-SUMMARY") \
                or line.startswith("SPR-SUMMARY") \
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
    print(f"scroll: {scroll_report}")

    if arguments.perturb != "none":
        # The perturbed delta is expected to have been caught above. Reaching
        # here without a divergence means a one-map-unit scroll is invisible to
        # this comparison, which is the failure the switch exists to rule out.
        # Either way the switch exits 1, as every other gate's does.
        print(f"rsegs_check: --perturb {arguments.perturb}", file=sys.stderr)
        return EXIT_MISMATCH
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
