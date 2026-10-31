#!/usr/bin/env python3
"""Sweep the walk-state capacities over every walkable viewpoint of a level.

WHY THIS EXISTS

``src/doomc/FrameDriver.cpp`` sizes its drawseg array, visplane set, clip-block
pool and opening-block pool from 20 sampled viewpoints (D3.10). A player walking
the map reaches viewpoints nobody sampled, and a pass that runs out of one of
those capacities refuses the whole frame -- which used to paint the Phase 0 shell
over the level, intermittently, exactly as the panel showed.

This script builds ``tools/capacity_check.c`` -- which drives
``lib/doom/r_bsp.c``, ``r_segs.c``, ``r_plane.c`` and ``r_things.c`` over a
generated viewpoint list -- and reports, per level and scope:

* the peak each capacity consumes, with the viewpoint that produced it;
* how many viewpoints each capacity refuses, by stage and error code;
* the same numbers for the shipped 200-tic player script, whose viewpoints it
  takes from ``tools/movement_check.c --player`` (the exact path the game
  walks).

SCOPES

``whole``    every subsector midpoint and every sector bounding-box centre, at 8
             angles, z = the floor of the sector at that point + R_VIEWHEIGHT.
             An upper bound: a point can land inside solid geometry.
``walkable`` the same list restricted to points inside a sector with at least 56
             map units of floor-to-ceiling gap -- the player's height. This is
             the scope the capacities must cover.
``script``   the 200 tics of ``movement_check --player``, one frame per tic.

CAPACITIES

By default the six capacities are read out of ``FrameDriver.cpp`` itself, so the
table always compares the measurement against what the image actually ships.
``--capacities D,P,R,B,V,O`` overrides them, which is how a proposed set is
checked before it is written into the source.

Each scope renders twice: once with pools far above any need (the peak is the
need, not the capacity) and once with the capacities in effect (which viewpoints
refuse). A capacity that is too small is reported, never clamped.

EXIT CODES

    0  every walkable viewpoint of every requested level and every script tic
       rendered: no refusal, no dropped drawseg
    1  at least one refusal or dropped drawseg
    2  the command line, the compiler or the input was unusable

USAGE

    python tools/capacity_check.py --wad path/to/DOOM1.WAD
    python tools/capacity_check.py --wad ... --maps E1M1
    python tools/capacity_check.py --wad ... --capacities 128,64,32,89,256,10

The WAD is copyrighted shareware data and is never committed. The harnesses are
compiled into a temporary directory that is removed before the script returns;
nothing is written into the worktree.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# The movement harness's own source list: the player script must be the one
# tools/movement_check.py runs, not a copy of it.
try:
    from movement_check import SOURCES as MOVEMENT_SOURCES
except ImportError:  # pragma: no cover - only when run from another directory
    MOVEMENT_SOURCES = (
        "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
        "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c",
        "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
        "p_lights.c",
    )

RENDER_SOURCES = (
    "r_segs.c", "r_light.c", "r_bsp.c", "r_plane.c", "r_things.c", "r_tables.c",
    "r_data.c", "doom_map.c", "w_wad.c",
)

MAP_NAME = "E1M1"
SCRIPT_TICS = 200

# The six capacities in the order --capacities takes them, and the constant each
# one is read from in FrameDriver.cpp.
CAPACITY_SOURCES = (
    ("drawsegs", "kDrawsegCapacity"),
    ("planes", "kPlaneCapacity"),
    ("clipranges", "kClipRangeCapacity"),
    ("clipblocks", "kClipBlockCapacity"),
    ("vissprites", "kVisspriteCapacity"),
    ("openings", "kOpeningBlockCapacity"),
)

EXIT_OK = 0
EXIT_SHORT = 1
EXIT_USAGE = 2


def project_root() -> Path:
    """games/doom, the directory that holds lib/ and tools/."""
    return Path(__file__).resolve().parent.parent


def run(command: list[str], what: str) -> str:
    """Runs one command and returns its stdout; raises RuntimeError otherwise."""
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed:\n{result.stderr or result.stdout}")
    return result.stdout


def compile_harness(workdir: Path, compiler: str, name: str, source: str,
                    sources: tuple[str, ...], root: Path) -> Path:
    """Builds one host harness against the real renderer or movement core."""
    executable = workdir / (name + (".exe" if sys.platform == "win32" else ""))
    command = [
        compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "lib" / "doom"),
        "-o", str(executable), str(root / "tools" / source),
    ]
    command.extend(str(root / "lib" / "doom" / item) for item in sources)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"the {name} harness did not compile:\n"
                          + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError(f"the {name} harness compiled with warnings, which this "
                          f"project does not accept:\n{result.stderr}")
    return executable


def shipped_capacities(root: Path) -> tuple[int, ...]:
    """The six capacities exactly as FrameDriver.cpp declares them."""
    text = (root / "src" / "doomc" / "FrameDriver.cpp").read_text(encoding="utf-8")
    values: list[int] = []
    for _, constant in CAPACITY_SOURCES:
        match = re.search(rf"constexpr\s+int\s+{constant}\s*=\s*(\d+)\s*;", text)
        if match is None:
            raise RuntimeError(f"FrameDriver.cpp does not declare {constant}")
        values.append(int(match.group(1)))
    return tuple(values)


def parse_capacities(text: str) -> tuple[int, ...]:
    """Parses --capacities D,P,R,B,V,O."""
    parts = text.split(",")
    if len(parts) != len(CAPACITY_SOURCES):
        raise argparse.ArgumentTypeError(
            f"want {len(CAPACITY_SOURCES)} comma-separated values, got {len(parts)}")
    try:
        values = tuple(int(part) for part in parts)
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    if any(value <= 0 for value in values):
        raise argparse.ArgumentTypeError("every capacity must be positive")
    return values


def fields_of(line: str) -> tuple[str, dict[str, str]]:
    """(record kind, key/value pairs) for one harness line."""
    parts = line.split()
    kind = parts[0] if parts else ""
    values: dict[str, str] = {}
    for part in parts[1:]:
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    return kind, values


def harness_records(text: str) -> dict[str, list[dict[str, str]]]:
    """The CAP/CAP-REFUSE/CAP-OVERFLOW/CAP-SCRIPT records, by kind."""
    records: dict[str, list[dict[str, str]]] = {}
    for line in text.splitlines():
        if not line.startswith("CAP"):
            continue
        kind, values = fields_of(line)
        records.setdefault(kind, []).append(values)
    return records


def peak_of(value: str) -> tuple[int, str]:
    """'99@ss202/a4(2752,-2980)' -> (99, 'ss202/a4(2752,-2980)')."""
    if "@" not in value:
        return 0, "-"
    peak, where = value.split("@", 1)
    return int(peak), where


def player_viewpoints(executable: Path, wad: Path, workdir: Path) -> tuple[Path, str]:
    """Writes movement_check --player's viewpoints and returns the file + map."""
    text = run([str(executable), "--player", str(wad), MAP_NAME, str(SCRIPT_TICS)],
               "the player script")
    rows: list[str] = []
    for line in text.splitlines():
        if not line.startswith("TIC "):
            continue
        values = dict(part.split("=", 1) for part in line.split()[2:])
        # The eye, not the mobj's floor: FrameDriver's frameLevelPlayer() hands
        # playerState.viewz to the renderer, so the sweep must too.
        rows.append(" ".join((values["x"], values["y"], values["viewz"],
                              values["angle"])))
    if len(rows) != SCRIPT_TICS:
        raise RuntimeError(f"the player script produced {len(rows)} tics, "
                           f"expected {SCRIPT_TICS}")
    path = workdir / "player_viewpoints.txt"
    path.write_text("\n".join(rows) + "\n", encoding="utf-8")
    return path, MAP_NAME


def table_line(name: str, peak: int, where: str, capacity: int) -> str:
    """One peak-versus-capacity row, with the verdict and the margin."""
    if peak > capacity:
        verdict = "SHORT"
    elif peak == 0:
        verdict = "unused"
    else:
        verdict = "fits"
    return (f"  {name:<12}{peak:>6}  {where:<24}{capacity:>6}  "
            f"{verdict}")


def report_scope(map_name: str, scope: str, output: str,
                 capacities: tuple[int, ...]) -> tuple[int, int, list[str]]:
    """Prints one scope's table and returns (refusals, overflows, problems)."""
    records = harness_records(output)
    problems: list[str] = []

    caps = next((item for item in records.get("CAP", [])
                 if item.get("scope") == scope), None)
    if caps is None:
        problems.append(f"{map_name} {scope}: the harness printed no CAP line")
        return 0, 0, problems

    print(f"{map_name} {scope}: {caps['viewpoints']} viewpoints")
    print(f"  {'capacity':<12}{'peak':>6}  {'at':<24}{'effective':>10}  verdict")
    for (name, _), capacity in zip(CAPACITY_SOURCES, capacities):
        peak, where = peak_of(caps.get(name, "0@-"))
        print(table_line(name, peak, where, capacity))
        if peak > capacity:
            problems.append(f"{map_name} {scope}: {name} peak {peak} exceeds "
                            f"{capacity} ({where})")

    refusals = 0
    overflows = 0
    for item in records.get("CAP-REFUSE", []):
        if item.get("scope") != scope:
            continue
        count = int(item["viewpoints"])
        refusals += count
        print(f"  refused: {count:>6} viewpoints at stage={item['stage']} "
              f"code={item['code']}, first={item['first']} "
              f"(map units)")
    for item in records.get("CAP-OVERFLOW", []):
        if item.get("scope") != scope:
            continue
        count = int(item["drawsegs"])
        overflows += count
        print(f"  dropped: {count:>6} viewpoints reached the drawseg array and "
              f"lost fragments (drawsegOverflowed)")
    if refusals:
        problems.append(f"{map_name} {scope}: {refusals} refused viewpoints")
    if overflows:
        problems.append(f"{map_name} {scope}: {overflows} viewpoints dropped "
                        f"drawsegs")
    return refusals, overflows, problems


def report_script(map_name: str, output: str) -> tuple[int, list[str]]:
    """Prints the player-script result and returns (refusals, problems)."""
    records = harness_records(output)
    summary = next(iter(records.get("CAP-SCRIPT", [])), None)
    if summary is None:
        return 0, [f"{map_name} script: the harness printed no CAP-SCRIPT line"]
    refused = int(summary["refusals"])
    print(f"{map_name} player script: {summary['tics']} tics, {refused} refused")
    problems: list[str] = []
    for item in records.get("CAP-REFUSE", []):
        if item.get("scope") != "script":
            continue
        print(f"  refused: {item['viewpoints']:>6} tics from tic {item['firsttic']} "
              f"at stage={item['stage']} code={item['code']}")
    if refused:
        problems.append(f"{map_name} player script: {refused} of "
                        f"{summary['tics']} tics refused")
    return refused, problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="capacity_check.py",
        description="Sweep every walkable viewpoint of each level through the four "
                    "render passes and report what each walk-state capacity must "
                    "hold.",
        epilog="The harnesses are built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--maps", default="E1M1,E1M2", metavar="LIST",
                        help="comma-separated levels to sweep (default: E1M1,E1M2)")
    parser.add_argument("--capacities", type=parse_capacities, default=None,
                        metavar="D,P,R,B,V,O",
                        help="compare against these capacities instead of the ones "
                             "FrameDriver.cpp declares")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harnesses with (default: gcc)")
    arguments = parser.parse_args(argv)

    if not arguments.wad.is_file():
        print(f"capacity_check: {arguments.wad} is not a file", file=sys.stderr)
        return EXIT_USAGE

    root = project_root()
    maps = [item.strip() for item in arguments.maps.split(",") if item.strip()]
    if not maps:
        print("capacity_check: --maps is empty", file=sys.stderr)
        return EXIT_USAGE

    workdir = Path(tempfile.mkdtemp(prefix="capacity_check_"))
    problems: list[str] = []
    shortfall = False

    try:
        source = shipped_capacities(root)
        capacities = arguments.capacities or source
        print(f"capacities in force: "
              + ", ".join(f"{name}={value}"
                          for (name, _), value in zip(CAPACITY_SOURCES, capacities)))
        if arguments.capacities is not None and arguments.capacities != source:
            print(f"  (FrameDriver.cpp declares "
                  + ", ".join(f"{name}={value}"
                              for (name, _), value in zip(CAPACITY_SOURCES, source))
                  + ")")

        render = compile_harness(workdir, arguments.cc, "capacity_check",
                                 "capacity_check.c", RENDER_SOURCES, root)
        capacity_text = ",".join(str(value) for value in capacities)

        for map_name in maps:
            for scope in ("whole", "walkable"):
                output = run([str(render), str(arguments.wad), "--map", map_name,
                              "--scope", scope, "--capacities", capacity_text],
                             f"the {map_name} {scope} sweep")
                refusals, overflows, found = report_scope(map_name, scope, output,
                                                          capacities)
                print()
                if scope == "walkable":
                    problems.extend(found)
                    shortfall = shortfall or refusals > 0 or overflows > 0

        movement = compile_harness(workdir, arguments.cc, "movement_check",
                                   "movement_check.c", MOVEMENT_SOURCES, root)
        viewpoints, script_map = player_viewpoints(movement, arguments.wad, workdir)
        output = run([str(render), str(arguments.wad), "--map", script_map,
                      "--script", str(viewpoints), "--capacities", capacity_text],
                     "the player-script sweep")
        refused, found = report_script(script_map, output)
        problems.extend(found)
        shortfall = shortfall or refused > 0
        print()
    except RuntimeError as error:
        print(f"capacity_check: {error}", file=sys.stderr)
        return EXIT_USAGE
    finally:
        shutil.rmtree(workdir, ignore_errors=True)

    if problems:
        for problem in problems:
            print(f"capacity_check: {problem}", file=sys.stderr)
        print("capacity_check: the walkable/scripted peaks do not all fit the "
              "capacities in force", file=sys.stderr)
        return EXIT_SHORT if shortfall else EXIT_USAGE

    print("capacity_check: every walkable viewpoint and every scripted tic "
          "rendered")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
