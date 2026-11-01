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
the exit switch (the request is recorded and the line cleared) and eight
scrolling lines (deferred).

Tic 0 is after the spawn scan and the triggers; tic 1 is after one
``p_level_tick()``. The default run is 420 tics, long enough for a door to open,
wait 150 tics and close.

WHAT IS COMPARED

    SPECIALS ...         the map, the tic count and the table sizes
    TRIGGER ...          every triggered line, its kind and whether an EV ran
    SECTOR ...           every sector's floor, ceiling and light every tic
    THINKER ...          the sector thinkers in list order every tic
    DAMAGE ...           one P_PlayerInSpecialSector fixture (see below)
    HEALTH ...           the player's health, exit request and secret count
    SUMMARY ...          the counters (secrets, P_ChangeSector calls, deferrals)

``INFO `` lines are C-only measurements and ``WAD `` is the oracle's banner;
both are excluded.

THE SECOND STAGE: P_PlayerInSpecialSector, ON EVERY EPISODE-1 MAP

The sector special a *player* feels is a different set from the one a level
spawns: P_PlayerInSpecialSector switches on 4, 5, 7, 9, 11 and 16, and every
other number has already been cleared by its own spawn thinker. The shareware
maps carry 7 on all nine of them, 9 on all nine, 16 on E1M3 and E1M9, and 11 on
E1M8 only; 4 and 5 appear in none, so the fixtures write them into a sector the
map really has and say ``synthetic=1``.

Each fixture loads the map fresh, stands the avatar on the fixture sector's own
floor, sets the health and the radiation suit, then applies the effect once per
tic and prints the state. The ``--perturb`` switch moves one constant in the
*oracle* only -- the damage amounts, the 10% exit threshold, or the suit's
immunity -- so the comparison is shown to be able to fail rather than assumed to
be able to.

USAGE

    python tools/specials_check.py --wad path/to/DOOM1.WAD
    python tools/specials_check.py --wad ... --perturb threshold

The WAD is copyrighted shareware data and is never committed; point this tool
at a file outside the repository. The harness is compiled into a temporary
directory that is removed before the script returns.

EXIT CODES

    0  every line matched
    1  at least one line differed, or --perturb did not diverge
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

sys.path.insert(0, str(Path(__file__).resolve().parent))
import doom_ref  # noqa: E402  (path-dependent by design)

MAP_NAME = "E1M1"
TICS = 420

# The nine maps the sector-damage stage covers, and the constants the perturb
# switch can move in the oracle.
DAMAGE_MAPS = tuple(f"E1M{i}" for i in range(1, 10))
PERTURBATIONS = ("none", "damage", "threshold", "ironfeet")

# --- T34: stairs and teleports ---------------------------------------------
#
# The numbers the stage measures, split the way upstream splits them:
#
#   7    P_UseSpecialLine,   EV_BuildStairs(build8),   the clear is conditional
#   127  P_UseSpecialLine,   EV_BuildStairs(turbo16),  the clear is conditional
#   8    P_CrossSpecialLine, EV_BuildStairs(build8),   the clear is unconditional
#   100  P_CrossSpecialLine, EV_BuildStairs(turbo16),  the clear is unconditional
#   39   P_CrossSpecialLine, EV_Teleport, one-shot
#   97   P_CrossSpecialLine, EV_Teleport, retrigger
#   125  P_CrossSpecialLine, EV_Teleport, monster-only, one-shot
#   126  P_CrossSpecialLine, EV_Teleport, monster-only, retrigger
STAIR_SPECIALS = (7, 8, 100, 127)
TELEPORT_SPECIALS = (39, 97, 125, 126)
STAIR_TELEPORT_SPECIALS = STAIR_SPECIALS + TELEPORT_SPECIALS

#: A staircase's last step is 32 tics apart on E1M3's build8 chain (10 sectors)
#: and E1M8's runs 14 sectors, i.e. 448 tics; 480 finishes every chain the nine
#: maps carry and every turbo16 variant needs at most 60.
STAIR_TICS = 480
#: A teleport does not move a sector: two tics are enough for the fog thinkers
#: to exist and show up in the dump.
TELEPORT_TICS = 2

#: The fixtures for numbers no episode-1 map carries. The carrier line is a real
#: line of the map, and `synthetic=1` in the protocol says so, the same
#: disclosure the sector-damage stage makes for 4 and 5. 127 and 100 are
#: EV_BuildStairs' turbo16 on the two trigger functions; 39/125/126 are
#: EV_Teleport's one-shot, monster-only and monster-only-retrigger numbers.
SYNTHETIC_FIXTURES = (
    ("E1M3", 967, 100, 0, STAIR_TICS),
    ("E1M3", 967, 127, 0, STAIR_TICS),
    ("E1M5", 787, 39, 0, TELEPORT_TICS),
    ("E1M5", 787, 125, 0, TELEPORT_TICS),
    ("E1M5", 787, 126, 0, TELEPORT_TICS),
)

STAIR_PERTURBATIONS = ("none", "stairs", "teleport")

EXIT_OK = 0
EXIT_MISMATCH = 1
EXIT_USAGE = 2

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_tick.c", "p_action.c", "p_inter.c",
    "p_pspr.c", "p_sight.c", "p_enemy.c",
    "m_random.c", "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c",
    "w_wad.c", "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c",
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


def run_harness(executable: Path, wad: Path, map_name: str, tics: int,
                mode: str) -> list[str]:
    """The C harness's stdout lines, verbatim (banners included)."""
    command = [str(executable), str(wad), map_name, str(tics)]
    if mode:
        command.append(mode)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the C harness failed on "
                           f"{map_name}{' ' + mode if mode else ''}:\n"
                           + (result.stderr or result.stdout))
    return result.stdout.splitlines()

def run_oracle(oracle_script: Path, wad: Path, arguments: list[str]) -> list[str]:
    """The oracle's protocol lines, banners removed."""
    result = subprocess.run(
        [sys.executable, str(oracle_script), "--wad", str(wad), *arguments],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the oracle failed:\n"
                           + (result.stderr or result.stdout))
    return protocol_lines(result.stdout)


def compare(c_lines: list[str], ref_lines: list[str]) -> tuple[int, str]:
    """(differing line count, the first divergence described)."""
    left_lines = protocol_lines("\n".join(c_lines))
    mismatches = 0
    first = ""
    for index in range(max(len(left_lines), len(ref_lines))):
        left = left_lines[index] if index < len(left_lines) else "<missing>"
        right = ref_lines[index] if index < len(ref_lines) else "<missing>"
        if left == right:
            continue
        mismatches += 1
        if not first:
            first = first_divergence(left, right)
    return mismatches, first


# --- T34: the stair and teleport stage --------------------------------------

def stair_census(wad: Path) -> dict[tuple[str, int], list[int]]:
    """(map, special) -> the lines carrying it, for the eight numbers.

    Counted from the WAD, not assumed: this is what decides which fixtures the
    maps themselves justify and which need a synthetic carrier.
    """
    _identification, lumps = doom_ref.read_wad(wad)
    census: dict[tuple[str, int], list[int]] = {}
    for number in range(1, 10):
        map_name = f"E1M{number}"
        bsp = doom_ref.read_bsp_map(lumps, map_name)
        for index, line in enumerate(bsp.linedefs):
            special = line["special"]
            if special in STAIR_TELEPORT_SPECIALS:
                census.setdefault((map_name, special), []).append(index)
    return census


def stair_fixtures(census: dict[tuple[str, int], list[int]]) -> list[tuple]:
    """(map, line, special, side, tics) for every line the maps carry, plus the
    synthetic carriers for the numbers no map carries."""
    fixtures = []
    for (map_name, special), lines in sorted(census.items()):
        tics = STAIR_TICS if special in STAIR_SPECIALS else TELEPORT_TICS
        for line in lines:
            fixtures.append((map_name, line, special, 0, tics))
    fixtures.extend(SYNTHETIC_FIXTURES)
    return fixtures


def report_census(census: dict[tuple[str, int], list[int]]) -> None:
    """One line per number the maps carry, then the numbers they do not.

    This is the measurement the fixtures follow: the map decides which numbers
    are real and which need a synthetic carrier.
    """
    print("stairs/teleports carried by the nine maps:")
    for special in STAIR_TELEPORT_SPECIALS:
        where = sorted((map_name, len(lines)) for (map_name, number), lines
                       in census.items() if number == special)
        kind = "use" if special in (7, 127) else "cross"
        if where:
            print(f"  {special:3d} {kind:5s} "
                  + " ".join(f"{name}x{count}" for name, count in where))
    absent = [special for special in STAIR_TELEPORT_SPECIALS
              if not any(number == special for (_map, number) in census)]
    print("  absent: " + (" ".join(str(s) for s in absent) if absent else "none")
          + " -- synthetic carriers: "
          + " ".join(f"{map_name}:{line}={special}"
                     for map_name, line, special, _side, _tics
                     in SYNTHETIC_FIXTURES))


def run_stair_fixture(executable: Path, oracle: Path, wad: Path,
                      fixture: tuple, perturb: str) -> tuple[int, str, int]:
    """One fixture both ways: (compared lines, first divergence, mismatches)."""
    map_name, line, special, side, tics = fixture
    result = subprocess.run(
        [str(executable), str(wad), map_name, str(tics), "stairs", str(line),
         str(special), str(side)],
        capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"the C harness failed on {map_name} line {line} "
                           f"special {special}:\n"
                           + (result.stderr or result.stdout))
    ref_lines = run_oracle(oracle, wad,
                           ["--stairs", map_name,
                            "--stairs-fixture",
                            f"{line},{special},{side},{tics}",
                            "--stairs-perturb", perturb])
    mismatches, first = compare(result.stdout.splitlines(), ref_lines)
    return len(protocol_lines(result.stdout)), first, mismatches


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="specials_check.py",
        description="Compare lib/doom/p_spec.c and the mover files against "
                    "tools/doom_ref.py --specials on a real WAD's E1M1, and "
                    "P_PlayerInSpecialSector against --sector-damage on all nine "
                    "episode-1 maps.",
        epilog="The harness is built in a temporary directory and nothing is "
               "written into the worktree.\n",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--wad", type=Path, required=True, metavar="PATH",
                        help="the IWAD or PWAD to read; keep it outside the repository")
    parser.add_argument("--cc", default="gcc", metavar="NAME",
                        help="C compiler to build the harness with (default: gcc)")
    parser.add_argument("--map", default=MAP_NAME, metavar="NAME",
                        help=f"the map the trigger script loads (default: {MAP_NAME})")
    parser.add_argument("--tics", type=int, default=TICS, metavar="N",
                        help=f"tics to step after tic 0 (default: {TICS})")
    parser.add_argument("--perturb", choices=PERTURBATIONS, default="none",
                        help="move one sector-damage constant in the oracle only, "
                             "and require the comparison to fail")
    parser.add_argument("--perturb-stairs", choices=STAIR_PERTURBATIONS,
                        default="none",
                        help="move one stair/teleport constant in the oracle only, "
                             "and require the stair stage to fail")
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

        # --- Stage 1: the map's specials and the trigger script ---------------
        c_raw = run_harness(executable, arguments.wad, arguments.map,
                            arguments.tics, "")
        ref_lines = run_oracle(oracle_script, arguments.wad,
                               ["--specials", arguments.map,
                                "--tics", str(arguments.tics),
                                "--sector-perturb", arguments.perturb])
        mismatches, first = compare(c_raw, ref_lines)

        c_lines = protocol_lines("\n".join(c_raw))
        sectors = next((line for line in c_lines if line.startswith("SPECIALS ")), "")
        summary = next((line for line in c_lines if line.startswith("SUMMARY ")), "")
        info = next((line for line in c_raw if line.startswith("INFO ")), "")
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

        # --- Stage 2: P_PlayerInSpecialSector on every episode-1 map ----------
        damage_lines = 0
        damage_mismatches = 0
        for map_name in DAMAGE_MAPS:
            c_damage = run_harness(executable, arguments.wad, map_name, 0, "damage")
            ref_damage = run_oracle(oracle_script, arguments.wad,
                                    ["--sector-damage", map_name,
                                     "--sector-perturb", arguments.perturb])
            count, first_damage = compare(c_damage, ref_damage)
            c_damage_lines = protocol_lines("\n".join(c_damage))
            damage_lines += len(c_damage_lines)
            damage_mismatches += count
            fixtures = sum(1 for line in c_damage_lines
                           if line.startswith("DAMAGE "))
            print(f"{map_name}: {fixtures} sector fixtures, "
                  f"{len(c_damage_lines)} compared lines, {count} divergent")
            if count:
                print(f"specials_check: {count} divergent line(s) on {map_name}; "
                      f"first: {first_damage}", file=sys.stderr)
        print(f"damage: {len(DAMAGE_MAPS)} maps, {damage_lines} compared lines, "
              f"{damage_mismatches} divergent (perturb={arguments.perturb})")

        if damage_mismatches:
            return EXIT_MISMATCH
        if arguments.perturb != "none":
            print(f"specials_check: --perturb {arguments.perturb} did not diverge, "
                  "so the sector-damage comparison cannot fail", file=sys.stderr)
            return EXIT_MISMATCH

        # --- Stage 3: EV_BuildStairs and EV_Teleport --------------------------
        census = stair_census(arguments.wad)
        report_census(census)
        fixtures = stair_fixtures(census)
        stair_lines = 0
        stair_mismatches = 0
        diverged = 0
        for fixture in fixtures:
            count, first_stair, mismatches_here = run_stair_fixture(
                executable, oracle_script, arguments.wad, fixture,
                arguments.perturb_stairs)
            stair_lines += count
            stair_mismatches += mismatches_here
            if mismatches_here:
                diverged += 1
                print(f"specials_check: {mismatches_here} divergent line(s) on "
                      f"{fixture[0]} line {fixture[1]} special {fixture[2]}; "
                      f"first: {first_stair}", file=sys.stderr)
        print(f"stairs: {len(fixtures)} fixtures, {stair_lines} compared lines, "
              f"{stair_mismatches} divergent (perturb={arguments.perturb_stairs})")
        if arguments.perturb_stairs != "none":
            if not diverged:
                print(f"specials_check: --perturb-stairs "
                      f"{arguments.perturb_stairs} did not diverge, so the stair "
                      "and teleport comparison cannot fail", file=sys.stderr)
                return EXIT_MISMATCH
            print(f"stairs: --perturb-stairs {arguments.perturb_stairs} moved "
                  f"{diverged} of {len(fixtures)} fixtures")
            # The perturbed run is expected to fail: that is the proof the
            # comparison can fail at all. Every other gate in this repository
            # (`--perturb frame`, `--perturb viewheight`, `--perturb eye`,
            # `--perturb next`, `--perturb damage`) exits 1 under a perturbation
            # that diverges, and a canary whose success and failure both exit 0
            # cannot be run from CI.
            return EXIT_MISMATCH
        if stair_mismatches:
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
