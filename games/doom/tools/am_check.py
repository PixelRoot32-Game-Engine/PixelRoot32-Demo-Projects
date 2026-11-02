#!/usr/bin/env python3
"""Compare the C automap against an independent Python oracle, pixel by pixel (D5.3).

WHAT THIS PROVES

``lib/doom/am_map.c`` is a translation of linuxdoom-1.10's ``am_map.c``: it
fills the 320x168 rows above the status bar with the level's walls -- only the
lines the wall pass has marked as seen -- the block-aligned grid, the player's
arrow and the crosshair, in the colours upstream's ``AM_*`` constants name. This
script runs a scripted sequence of states through two implementations and
requires the whole 320x200 surface to match byte for byte after every ``draw``:

* the C side is ``tools/am_check.c`` linked against the real ``am_map.c``,
  ``doom_map.c``, the real BSP walk and the real wall pass, fed a real WAD's
  E1M1/E1M2 lumps. The ``walk`` command runs the walk and the wall pass for one
  viewpoint, so the mapped bits the map draws come from the pass that sets them;
* the oracle is written in ``tools/doom_ref.py`` (``Automap``) from the same
  source and shares no code with the C except the fixed-point helpers and the
  fine-sine table -- and it derives the mapped set from its own walk's drawsegs.

Rows 168..199 of both surfaces are the ``0xEE`` sentinel: ``ST_Drawer`` owns
them, and the gate fails if the automap writes a byte there.

THE SAMPLED STATES (``SCRIPT`` below)

    spawn         E1M1's player start, nothing mapped, the entry scale of
                  AM_LevelInit, following: the arrow, the crosshair, the void
    spawn_mapped  the same view after the real walk + wall pass: some lines are
                  mapped and drawn, most of the level's 475 are not
    strobe        13 x AM_updateLightLev: every wall colour moves with lightlev
    grid          the grid on, over the mapped walls
    zoomin        20 tics of A: mtof_zoommul = M_ZOOMIN, then released
    panned        40 tics of the d-pad with follow cancelled: the window moves and
                  stops at the level's edge
    zoomout       60 tics of B: clamped at min_scale_mtof, the whole level fits
    allmap        the computer-map power: every line, mapped or not, in GRAYS+3
    e1m2          E1M2's start with the grid on: the level bounds and the entry
                  scale are not E1M1's numbers
    empty         the map stopped: the surface is the sentinel, untouched

PROVING THE GATE CAN FAIL

``--perturb`` makes the oracle move one constant: ``colour`` draws the floor
difference walls in the ceiling colour, ``scale`` enters the level at 0.71x
instead of 0.70x. Either must make the run exit 1; a comparison that cannot
fail is not a gate.

USAGE

    python tools/am_check.py --wad tools/DOOM1.WAD [--perturb colour|scale] [--keep]

EXIT CODES: 0 every pixel matched, 1 a pixel differed, 2 unusable input or build.
Standard library only (plus tools/doom_ref.py).
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# The automap's window is DOOM's 168-row view: the C is compiled with DOOM_HUD so
# R_VIEW_HEIGHT and the walk's window are the ones the panel renders.
os.environ.setdefault("DOOM_REF_VIEW_HEIGHT", "168")

import doom_ref  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

WIDTH = 320
HEIGHT = 200
WINDOW_HEIGHT = 168
SENTINEL = 0xEE

SOURCES = (
    "am_map.c", "doom_map.c", "r_bsp.c", "r_segs.c", "r_plane.c", "r_tables.c",
    "r_light.c",
)

# DOOM's binary angles, for the script below.
ANG90 = 0x40000000

# --- The scripted states ------------------------------------------------------

SCRIPT = f"""
map E1M1
player 1056 -3616 {ANG90}
start
tick 1
draw spawn

walk 1056 -3616 {ANG90}
draw spawn_mapped

light 13
draw strobe

grid 1
draw grid
grid 0

zoom in
tick 20
zoom release
tick 1
draw zoomin

pan right
tick 40
pan none
tick 1
draw panned

zoom out
tick 60
zoom release
tick 1
draw zoomout

clear
follow 1
allmap 1
draw allmap
allmap 0

map E1M2
player -32 -240 {ANG90}
start
tick 1
walk -32 -240 {ANG90}
grid 1
draw e1m2

stop
draw empty
"""

# --- The oracle ---------------------------------------------------------------

# The two perturbations move one constant of this implementation only.
PERTURBATIONS = ("none", "colour", "scale")


class Perturber:
    """Applies --perturb to the oracle's own constants, never to the C."""

    def __init__(self, mode: str) -> None:
        self.mode = mode

    def floor_color(self) -> int:
        if self.mode == "colour":
            return doom_ref.AM_CDWALLCOLORS
        return doom_ref.AM_FDWALLCOLORS

    def entry_scale(self) -> int:
        if self.mode == "scale":
            return doom_ref.FRACUNIT * 71 // 100
        return doom_ref.AM_ENTRYSCALE


def open_map(wad_path: Path, name: str):
    identification, lumps = doom_ref.read_wad(wad_path)
    del identification
    bsp = doom_ref.read_bsp_map(lumps, name)
    origin = doom_ref.blockmap_origin(lumps, name)
    return bsp, origin


def walk_view(bsp, automap, x: int, y: int, angle: int) -> int:
    """Runs the oracle's own walk and folds its stored drawsegs into the bitmap."""
    state = doom_ref.WalkState(bsp, 256, 64)
    state.render({"x": x, "y": y, "z": 41 * doom_ref.FRACUNIT, "angle": angle,
                  "extralight": 0, "sky": b"F_SKY1\x00\x00"})
    automap.mapped |= {bsp.segs[drawseg["seg"]]["linedef"] for drawseg in state.drawsegs}
    return len(state.drawsegs)


class OracleRun:
    """The script interpreter over doom_ref.Automap."""

    def __init__(self, wad_path: Path, script: str, perturb: Perturber) -> None:
        self.wad = wad_path
        self.script = script
        self.perturb = perturb
        self.automap: doom_ref.Automap | None = None
        self.bsp = None
        self.player = (0, 0)
        self.player_angle = 0
        self.surface = bytearray([SENTINEL]) * (WIDTH * HEIGHT)
        self.frames: list[tuple[str, dict, bytes]] = []
        self.walks: list[tuple[int, int]] = []

    def run(self) -> list[tuple[str, dict, bytes]]:
        for line in self.script.splitlines():
            words = line.split()
            if not words or words[0].startswith("#"):
                continue
            command = words[0]
            if command == "map":
                self.bsp, origin = open_map(self.wad, words[1])
                self.automap = doom_ref.Automap(self.bsp, origin)
                self.automap.entry_scale = self.perturb.entry_scale()
                self.automap.floor_color = self.perturb.floor_color()
                self.automap.level_init()
            elif command == "player":
                self.player = (int(words[1]) << 16, int(words[2]) << 16)
                self.player_angle = int(words[3])
                self.automap.player = list(self.player)
                self.automap.player_angle = self.player_angle
            elif command == "walk":
                x = int(words[1]) << 16
                y = int(words[2]) << 16
                segs = walk_view(self.bsp, self.automap, x, y, int(words[3]))
                self.walks.append((segs, len(self.automap.mapped)))
            elif command == "clear":
                self.automap.mapped = set()
            elif command == "grid":
                self.automap.grid = words[1] != "0"
            elif command == "follow":
                self.automap.followplayer = words[1] != "0"
            elif command == "allmap":
                self.automap.allmap = words[1] != "0"
            elif command == "start":
                self.automap.start(self.automap.player[0], self.automap.player[1])
            elif command == "stop":
                self.automap.stop()
            elif command == "zoom":
                direction = {"in": doom_ref.AM_ZOOM_IN, "out": doom_ref.AM_ZOOM_OUT}.get(
                    words[1], doom_ref.AM_ZOOM_RELEASE)
                self.automap.zoom(direction)
            elif command == "pan":
                bits = 0
                for word in words[1].split("+"):
                    bits |= {"up": doom_ref.AM_PAN_UP, "down": doom_ref.AM_PAN_DOWN,
                             "left": doom_ref.AM_PAN_LEFT,
                             "right": doom_ref.AM_PAN_RIGHT}.get(word, 0)
                self.automap.pan(bits)
            elif command == "tick":
                for _ in range(int(words[1])):
                    self.automap.ticker(self.automap.player[0], self.automap.player[1],
                                        self.automap.player_angle)
            elif command == "light":
                for _ in range(int(words[1])):
                    self.automap.update_light_lev()
            elif command == "draw":
                self.surface = bytearray([SENTINEL]) * (WIDTH * HEIGHT)
                _pixels, self.draw_numbers = self.automap.draw(self.surface, WIDTH)
                self.frames.append((words[1], self.stats(), bytes(self.surface)))
        return self.frames

    def stats(self) -> dict:
        a = self.automap
        mapped = sum(1 for index in range(len(self.bsp.linedefs)) if index in a.mapped)
        return {
            "active": 1 if a.automapactive else 0,
            "follow": 1 if a.followplayer else 0,
            "grid": 1 if a.grid else 0,
            "mx": a.m_x, "my": a.m_y, "mw": a.m_w, "mh": a.m_h,
            "scale": a.scale_mtof, "clock": a.amclock, "light": a.lightlev,
            "allmap": 1 if a.allmap else 0,
            "mapped": mapped,
            "drawn": self.draw_numbers,
        }


def run_oracle(wad_path: Path, script: str, perturb: Perturber):
    run = OracleRun(wad_path, script, perturb)
    frames = run.run()
    return frames, run.walks


# --- The C side -----------------------------------------------------------------

def build_harness(workdir: Path, compiler: str) -> Path:
    executable = workdir / ("am_check.exe" if sys.platform == "win32" else "am_check")
    lib = ROOT / "lib" / "doom"
    command = [compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-DDOOM_HUD=1",
               "-I", str(lib), "-o", str(executable), str(ROOT / "tools" / "am_check.c")]
    command += [str(lib / name) for name in SOURCES]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    if result.stderr.strip():
        raise RuntimeError("the harness compiled with warnings, which this project does not "
                           "accept:\n" + result.stderr)
    return executable


def parse_c(output: str):
    """The C protocol: WALK / MAP / DRAW / 200 hex rows / BOTTOM, per state."""
    lines = output.splitlines()
    frames = []
    walks = []
    bottoms: dict[str, bool] = {}
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.startswith("WALK "):
            fields = dict(item.split("=", 1) for item in line.split()[2:4])
            walks.append((int(fields["segs"]), int(fields["marked"])))
            index += 1
        elif line.startswith("MAP "):
            name = line.split()[1]
            fields = dict(item.split("=", 1) for item in line.split()[2:])
            stats = {key: int(value) for key, value in fields.items()}
            index += 1
            if not lines[index].startswith("DRAW "):
                raise RuntimeError(f"am_check: {name}: no DRAW line after MAP")
            stats["drawn"] = int(lines[index].split("drawn=")[1])
            index += 1
            rows = lines[index:index + HEIGHT]
            index += HEIGHT
            bottom = lines[index]
            bottoms[name] = bottom.split("unchanged=")[1] == "1"
            index += 1
            frames.append((name, stats, bytes.fromhex("".join(rows))))
        else:
            index += 1
    return frames, walks, bottoms


# The automap frame's SHA-256 over the 320x168 window, as the board's own
# `SHOW,map` line printed it for the same state (task record L42). The state is
# `spawn_mapped`: E1M1's player start facing north, the lines the wall pass stores
# from that view, the entry scale, following. It is pinned here so a change to the
# picture is a named failure and not only a C/oracle disagreement.
SPAWN_MAPPED_HASH = "b1677b75de8175a5afd3f563605c160cd29a1beaee5bcbbefbd7fc73b5b12ceb"


def window_hash(surface: bytes) -> str:
    return hashlib.sha256(surface[:WIDTH * WINDOW_HEIGHT]).hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--perturb", choices=PERTURBATIONS, default="none",
                        help="move one oracle constant; the run must then fail")
    parser.add_argument("--keep", action="store_true", help="keep the build directory")
    parser.add_argument("--print-hashes", action="store_true",
                        help="print every state's window hash")
    arguments = parser.parse_args(argv)

    if not arguments.wad.exists():
        print(f"am_check: cannot read {arguments.wad}", file=sys.stderr)
        return 2

    workdir = Path(tempfile.mkdtemp(prefix="am_check_"))
    try:
        try:
            harness = build_harness(workdir, arguments.cc)
        except (RuntimeError, OSError) as error:
            print(f"am_check: {error}", file=sys.stderr)
            return 2
        script_path = workdir / "script.txt"
        script_path.write_text(SCRIPT, encoding="utf-8")
        result = subprocess.run([str(harness), str(arguments.wad), str(script_path)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"am_check: harness exited {result.returncode}: {result.stderr}",
                  file=sys.stderr)
            return 2
        actual, c_walks, bottoms = parse_c(result.stdout)

        perturb = Perturber(arguments.perturb)
        expected, o_walks = run_oracle(arguments.wad, SCRIPT, perturb)
        if c_walks != o_walks:
            print(f"FAIL walks: C {c_walks} oracle {o_walks}")
            return 1

        failures = 0
        if len(actual) != len(expected):
            print(f"FAIL: {len(actual)} frames from C, {len(expected)} from the oracle")
            return 1
        for (c_name, c_stats, c_pixels), (o_name, o_stats, o_pixels) in zip(actual, expected):
            differing = [index for index in range(WIDTH * HEIGHT)
                         if o_pixels[index] != c_pixels[index]]
            field_failures = [key for key in o_stats if o_stats[key] != c_stats.get(key)]
            outside = [index for index in differing if index >= WINDOW_HEIGHT * WIDTH]
            status = "ok" if not differing and not field_failures else "FAIL"
            detail = ""
            if differing:
                at = differing[0]
                detail = (f" first=({at % WIDTH},{at // WIDTH}) oracle={o_pixels[at]:02x}"
                          f" c={c_pixels[at]:02x}")
            if field_failures:
                detail += " fields=" + ",".join(
                    f"{key}:{c_stats.get(key)}!={o_stats[key]}" for key in field_failures)
            if not bottoms.get(c_name, False):
                detail += " BOTTOM_WRITTEN"
            print(f"{status:4s} {c_name:13s} differing={len(differing):6d}"
                  f" outside_window={len(outside)} drawn={c_stats['drawn']:4d}"
                  f" scale={c_stats['scale']:7d} mapped={c_stats['mapped']:4d}{detail}")
            if arguments.print_hashes:
                print(f"     hash {c_name:13s} {window_hash(c_pixels)}")
            if differing or field_failures or not bottoms.get(c_name, False):
                failures += 1

        spawn = next(frame for frame in actual if frame[0] == "spawn")
        spawn_hash = window_hash(spawn[2])
        mapped = next(frame for frame in actual if frame[0] == "spawn_mapped")
        mapped_hash = window_hash(mapped[2])
        if mapped_hash != SPAWN_MAPPED_HASH:
            print(f"FAIL spawn_mapped hash {mapped_hash} pinned={SPAWN_MAPPED_HASH}")
            failures += 1
        else:
            print(f"ok   spawn_mapped hash {mapped_hash} (the board printed it)")
        print(f"ok   spawn hash {spawn_hash}")

        print(f"am_check: {len(expected)} states, {failures} failing")
        return 1 if failures else 0
    finally:
        if arguments.keep:
            print(f"am_check: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
