#!/usr/bin/env python3
"""Gate for the palette flashes: which PLAYPAL page, and the colours it becomes (D5.3).

linuxdoom's ``ST_doPaletteStuff()`` picks one of PLAYPAL's 14 pages every frame
from counters the simulation owns: red for damage (and for berserk), gold for a
pickup, green for the radiation suit, normal otherwise. ``lib/doom/palflash.c``
is that choice; ``src/palette/DoomPalette.cpp`` turns a page into the panel's
tables at the player's gamma. This script proves three things against an oracle
written here from the upstream source, which shares no code with the C:

1. SELECTION. ``palflash_index()`` over the counter space (every damage count
   0..100 against berserk fade stages, the bonus bands, the suit's ``> 128`` and
   ``& 8`` blink, four weapons to prove the weapon is irrelevant, then 6,000
   seeded random states) equals the oracle's page.
2. PALETTE. For all 14 pages at gamma 0..4 the RGB565 and RGB332 tables the
   firmware builds (``initFromPlaypal`` for page 0, then ``applyGamma`` with the
   page's bytes, as the scene does) equal the oracle's, which regrades the WAD's
   own PLAYPAL by the gamma formula and packs it.
3. DECAY. On the real E1M1 world, driven by the real ``p_level_tic`` /
   ``P_PlayerThink``: a damage hit, an overkill clamped to 100, a health-bonus
   pickup, a berserk pickup (the strength power counts UP, fading the red tint
   over ~770 tics) and a radiation suit (2,100 tics, then the blink) each decay
   by the upstream rule, the counters match the oracle's model tic by tic, and
   the page steps down through every reachable band back to 0 (red page 1 is never
   chosen upstream either: `(cnt + 7) >> 3` is 0 only for cnt == 0).

``--container`` additionally proves the shipped container carries all 14 PLAYPAL
pages byte for byte (the firmware decodes a page from it when the flash changes).

PROVING THE GATE CAN FAIL: ``--perturb`` makes the oracle round the red bands one
count late and skip gamma on the palette; the run must then exit 1.

    python tools/palflash_check.py --wad tools/DOOM1.WAD [--container tools/DOOM1.whd]
    python tools/palflash_check.py --wad tools/DOOM1.WAD --perturb      # must FAIL

Exit 0 every check passed, 1 a check failed, 2 unusable input or build.
"""
from __future__ import annotations

import argparse
import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SOURCES = (
    "p_setup.c", "p_mobj.c", "p_map.c", "p_user.c", "p_tick.c", "p_action.c",
    "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c", "m_random.c", "z_zone.c",
    "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c", "p_spec.c",
    "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c", "palflash.c",
)

# upstream st_stuff.c
STARTREDPALS, NUMREDPALS, STARTBONUSPALS, NUMBONUSPALS, RADIATIONPAL = 1, 8, 9, 4, 13
IRONTICS = 60 * 35
BONUSADD = 6


class Oracle:
    """ST_doPaletteStuff() and I_SetPalette()'s gamma, from the linuxdoom source."""

    def __init__(self, perturb: bool) -> None:
        self.perturb = perturb

    def page(self, damage: int, bonus: int, strength: int, iron: int) -> int:
        cnt = damage
        if strength:
            bzc = 12 - (strength >> 6)  # "slowly fade the berserk out"
            if bzc > cnt:
                cnt = bzc
        if cnt:
            palette = (cnt + (8 if self.perturb else 7)) >> 3
            if palette >= NUMREDPALS:
                palette = NUMREDPALS - 1
            return palette + STARTREDPALS
        if bonus:
            palette = (bonus + 7) >> 3
            if palette >= NUMBONUSPALS:
                palette = NUMBONUSPALS - 1
            return palette + STARTBONUSPALS
        if iron > 4 * 32 or (iron & 8):
            return RADIATIONPAL
        return 0

    def gamma_table(self, level: int) -> list[int]:
        if level == 0:
            return list(range(256))
        return [min(255, int(255.0 * (v / 255.0) ** (1.0 - level / 8.0) + 0.5))
                for v in range(256)]

    def tables(self, playpal: bytes, page: int, gamma: int) -> tuple[list[int], list[int]]:
        table = self.gamma_table(0 if self.perturb else gamma)
        t565: list[int] = []
        t332: list[int] = []
        base = page * 768
        for i in range(256):
            r, g, b = (table[playpal[base + i * 3 + k]] for k in range(3))
            t565.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
            t332.append((r & 0xE0) | ((g & 0xE0) >> 3) | (b >> 6))
        return t565, t332


def read_playpal(wad: bytes) -> bytes:
    count, directory = struct.unpack_from("<II", wad, 4)
    for i in range(count):
        offset, size, name = struct.unpack_from("<II8s", wad, directory + i * 16)
        if name.rstrip(b"\0") == b"PLAYPAL":
            return wad[offset:offset + size]
    raise SystemExit("palflash_check: no PLAYPAL in the WAD")


def run(command: list[str], what: str) -> subprocess.CompletedProcess:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{what} failed:\n{result.stderr or result.stdout}")
    if result.stderr.strip():
        raise RuntimeError(f"{what} wrote warnings, which this project does not accept:\n"
                           + result.stderr)
    return result


def build(workdir: Path, cc: str, cxx: str) -> tuple[Path, Path]:
    lib = ROOT / "lib" / "doom"
    ext = ".exe" if sys.platform == "win32" else ""
    check = workdir / f"palflash_check{ext}"
    run([cc, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(lib), "-o", str(check),
         str(ROOT / "tools" / "palflash_check.c"), *(str(lib / s) for s in SOURCES)],
        "building palflash_check")
    gamma_obj = workdir / "m_gamma.o"
    run([cc, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(lib), "-c", "-o",
         str(gamma_obj), str(lib / "m_gamma.c")], "building m_gamma.c")
    pal = workdir / f"palflash_pal{ext}"
    run([cxx, "-std=c++17", "-O2", "-Wall", "-Wextra", "-I", str(ROOT / "src"),
         "-I", str(lib), "-o", str(pal), str(ROOT / "tools" / "palflash_pal.cpp"),
         str(ROOT / "src" / "palette" / "DoomPalette.cpp"), str(gamma_obj)],
        "building palflash_pal")
    return check, pal


# --- 1. selection ------------------------------------------------------------

def selection_states() -> list[tuple[int, int, int, int, int]]:
    states = []
    strengths = [0, 1, 63, 64, 255, 256, 511, 512, 767, 768, 769, 1000]
    for damage in range(0, 101):
        for strength in strengths:
            for weapon in (0, 7):
                states.append((damage, 0, strength, 0, weapon))
    for bonus in range(0, 71):
        for iron in (0, 8, 129):
            states.append((0, bonus, 0, iron, 1))
    for iron in list(range(0, 140)) + [2099, 2100]:
        states.append((0, 0, 0, iron, 2))
    rng = random.Random(0x7A1F)
    for _ in range(6000):
        states.append((rng.choice([0, 0, rng.randrange(0, 101)]),
                       rng.choice([0, 0, rng.randrange(0, 70)]),
                       rng.choice([0, 0, rng.randrange(0, 1100)]),
                       rng.choice([0, 0, rng.randrange(0, 2101)]), rng.randrange(0, 9)))
    return states


def check_selection(check: Path, wad: Path, workdir: Path, oracle: Oracle,
                    failures: list[str]) -> int:
    states = selection_states()
    script = workdir / "selection.txt"
    script.write_text("".join("state %d %d %d %d %d\n" % s for s in states))
    out = run([str(check), str(wad), str(script)], "palflash_check (selection)").stdout
    got = [int(line.split()[1]) for line in out.splitlines() if line.startswith("IDX ")]
    if len(got) != len(states):
        failures.append(f"selection: {len(got)} answers for {len(states)} states")
        return len(states)
    bad = 0
    for state, value in zip(states, got):
        want = oracle.page(state[0], state[1], state[2], state[3])
        if value != want:
            bad += 1
            if bad <= 5:
                failures.append(f"selection: dmg={state[0]} bonus={state[1]} str={state[2]} "
                                f"iron={state[3]} weapon={state[4]}: C {value}, oracle {want}")
    if bad:
        failures.append(f"selection: {bad} of {len(states)} states differ")
    return len(states)


# --- 2. palette --------------------------------------------------------------

def check_palette(pal: Path, wad: Path, playpal: bytes, workdir: Path, oracle: Oracle,
                  failures: list[str]) -> int:
    requests = [(page, gamma) for page in range(14) for gamma in range(5)]
    script = workdir / "palette.txt"
    script.write_text("".join("pal %d %d\n" % r for r in requests))
    lines = run([str(pal), str(wad), str(script)], "palflash_pal").stdout.splitlines()
    if len(lines) != 3 * len(requests):
        failures.append(f"palette: {len(lines)} lines for {len(requests)} requests")
        return len(requests)
    bad = 0
    for n, (page, gamma) in enumerate(requests):
        header, hex565, hex332 = lines[3 * n:3 * n + 3]
        got565 = [int(hex565[i:i + 4], 16) for i in range(0, 1024, 4)]
        got332 = [int(hex332[i:i + 2], 16) for i in range(0, 512, 2)]
        want565, want332 = oracle.tables(playpal, page, gamma)
        if header != f"PAL {page} {gamma}" or got565 != want565 or got332 != want332:
            bad += 1
            if bad <= 5:
                diff = sum(a != b for a, b in zip(got565, want565))
                failures.append(f"palette: page {page} gamma {gamma}: {diff} of 256 "
                                f"RGB565 entries differ")
    if bad:
        failures.append(f"palette: {bad} of {len(requests)} (page, gamma) tables differ")
    return len(requests)


# --- 3. decay on the real world ----------------------------------------------

def parse_tic(line: str) -> tuple[int, int, int, int, int, int]:
    fields = line.split()
    values = [fields[1]] + [f.split("=")[1] for f in fields[2:]]
    return tuple(int(v) for v in values)  # type: ignore[return-value]


def decay_scenarios() -> list[tuple[str, list[str], int, tuple[int, int, int, int]]]:
    """name, script lines, ticks, expected counters right after the action."""
    return [
        ("damage-40", ["reset", "damage 40"], 45, (40, 0, 0, 0)),
        ("bonus-pickup", ["reset", "pickup bonus"], 8, (0, BONUSADD, 0, 0)),
        ("berserk", ["reset", "pickup berserk"], 800, (0, BONUSADD, 1, 0)),
        ("radsuit", ["reset", "pickup suit"], IRONTICS + 20, (0, BONUSADD, 0, IRONTICS)),
        ("overkill-clamp", ["reset", "damage 70", "damage 70"], 105, (100, 0, 0, 0)),
    ]


def check_decay(check: Path, wad: Path, workdir: Path, oracle: Oracle,
                failures: list[str]) -> dict[str, str]:
    scenarios = decay_scenarios()
    lines: list[str] = []
    for _, script_lines, ticks, _ in scenarios:
        lines += script_lines + [f"tick {ticks}"]
    script = workdir / "decay.txt"
    script.write_text("\n".join(lines) + "\n")
    out = run([str(check), str(wad), str(script)], "palflash_check (decay)").stdout
    tics = [parse_tic(line) for line in out.splitlines() if line.startswith("TIC ")]

    summary: dict[str, str] = {}
    cursor = 0
    for name, script_lines, ticks, start in scenarios:
        actions = sum(1 for s in script_lines if s.split()[0] in ("damage", "pickup"))
        rows = tics[cursor:cursor + actions + ticks]
        cursor += actions + ticks
        if len(rows) != actions + ticks:
            failures.append(f"decay {name}: short output")
            continue
        action_row, series = rows[actions - 1], rows[actions:]
        if (action_row[1], action_row[2], action_row[3], action_row[4]) != start:
            failures.append(f"decay {name}: after the action the counters are "
                            f"{action_row[1:5]}, expected {start}")
        dmg, bonus, strength, iron = start
        pages: list[int] = []
        for index, row in enumerate(series, start=1):
            dmg = max(dmg - 1, 0)
            bonus = max(bonus - 1, 0)
            strength = strength + 1 if strength else 0
            iron = iron - 1 if iron else 0
            if (row[0], row[1], row[2], row[3], row[4]) != (index, dmg, bonus, strength, iron):
                failures.append(f"decay {name}: tic {index} counters {row[1:5]}, "
                                f"expected {(dmg, bonus, strength, iron)}")
                break
            want = oracle.page(dmg, bonus, strength, iron)
            if row[5] != want:
                failures.append(f"decay {name}: tic {index} page {row[5]}, oracle {want}")
                break
            pages.append(row[5])
        else:
            if name in ("damage-40", "overkill-clamp", "bonus-pickup", "berserk"):
                visits = [pages[i] for i in range(len(pages)) if i == 0 or pages[i] != pages[i - 1]]
                if name == "damage-40" and visits != [6, 5, 4, 3, 2, 0]:
                    failures.append(f"decay {name}: pages visited {visits}")
                if name == "overkill-clamp" and visits != [8, 7, 6, 5, 4, 3, 2, 0]:
                    failures.append(f"decay {name}: pages visited {visits}")
                if name == "berserk" and visits != [3, 2, 0]:
                    failures.append(f"decay {name}: pages visited {visits}")
                if name == "bonus-pickup" and visits != [10, 0]:
                    failures.append(f"decay {name}: pages visited {visits}")
            if name == "radsuit":
                visits = [pages[i] for i in range(len(pages)) if i == 0 or pages[i] != pages[i - 1]]
                if visits[0] != 10 or visits.count(RADIATIONPAL) < 3 or visits[-1] != 0:
                    failures.append(f"decay {name}: pages visited {visits[:8]}...")
            summary[name] = "".join(f"{p:x}" for p in
                                    [pages[i] for i in range(len(pages))
                                     if i == 0 or pages[i] != pages[i - 1]][:24])
    return summary


# --- container ----------------------------------------------------------------

def check_container(path: Path, playpal: bytes, failures: list[str]) -> None:
    sys.path.insert(0, str(ROOT / "tools"))
    import hud_check
    import whd_encode
    import zlib

    data = path.read_bytes()
    names, rows = hud_check.lump_table(data)
    index = names.get("PLAYPAL")
    if index is None:
        failures.append("container: no PLAYPAL lump")
        return
    start, stored, uncompressed, method = rows[index]
    payload = data[start:start + stored]
    if method == whd_encode.METHOD_RAW:
        decoded = payload
    else:
        try:
            decoded = zlib.decompress(payload, -15)
        except zlib.error:
            decoded = zlib.decompress(payload)
    if len(decoded) != 14 * 768 or decoded != playpal:
        failures.append(f"container: PLAYPAL is {len(decoded)} B and "
                        f"{'equals' if decoded == playpal else 'differs from'} the WAD's")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--container", type=Path)
    parser.add_argument("--perturb", action="store_true",
                        help="make the oracle wrong in two places; the run must then fail")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cxx", default="g++")
    arguments = parser.parse_args(argv)

    try:
        wad = arguments.wad.read_bytes()
    except OSError as error:
        print(f"palflash_check: {error}", file=sys.stderr)
        return 2
    playpal = read_playpal(wad)
    oracle = Oracle(arguments.perturb)
    failures: list[str] = []

    with tempfile.TemporaryDirectory(prefix="palflash_") as temp:
        workdir = Path(temp)
        try:
            check, pal = build(workdir, arguments.cc, arguments.cxx)
            states = check_selection(check, arguments.wad, workdir, oracle, failures)
            tables = check_palette(pal, arguments.wad, playpal, workdir, oracle, failures)
            decay = check_decay(check, arguments.wad, workdir, oracle, failures)
        except (RuntimeError, OSError) as error:
            print(f"palflash_check: {error}", file=sys.stderr)
            return 2
    if arguments.container is not None:
        check_container(arguments.container, playpal, failures)

    for failure in failures:
        print(f"PALFLASH,fail,{failure}")
    for name, pages in decay.items():
        print(f"PALFLASH,decay,{name},pages={pages}")
    print(f"PALFLASH,states={states},tables={tables},decay={len(decay)},"
          f"failures={len(failures)},perturb={int(arguments.perturb)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
