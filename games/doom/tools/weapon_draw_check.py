#!/usr/bin/env python3
"""Compare the C weapon widget against an independent Python oracle, pixel by pixel.

WHAT THIS PROVES

``lib/doom/r_psprite.c`` + ``r_things.c`` (``r_psprites_emit``) draw the player's
weapon the way linuxdoom's ``R_DrawPlayerSprites()`` does, into the 168-row view
of a 320x200 index surface. This script runs a scripted list of psprite states
through two implementations and requires the whole surface -- the view, and the
32 bar rows the weapon must never touch -- to match byte for byte:

* the C side is ``tools/weapon_draw_check.c`` linked against the real library,
  fed the real patches and the real COLORMAP of a real WAD;
* the oracle is written here from linuxdoom-1.10's ``R_DrawPSprite``,
  ``R_DrawVisSprite``, ``R_DrawMaskedColumn``, ``R_DrawColumn`` and
  ``R_InitLightTables`` and shares no code with the C: its own patch reader, its
  own light table (computed from the COLORMAP lump, not looked up), its own column
  loop and its own state-to-lump naming. The state table is parsed from the
  generated ``info_tables.h`` (data both sides must agree on).

The oracle is NOT a run of vanilla DOOM; no DOOM binary is available here. It is
a second implementation of the same source, so what this gate rules out is a
transcription slip in the C, not a misreading shared by both.

THE SAMPLED STATES (``SAMPLES`` below)

    pistol_ready      PISGA0 at WEAPONTOP, a lit sector
    pistol_fire       PISGB0 + the full-bright PISFA0 flash on a dark sector
    pistol_bob        PISGA0 with a bob offset in sx and sy, extralight 2
    pistol_rising     PISGA0 part way up the raise (sy 64): the gun reaches the window bottom
    shotgun_ready     SHTGA0
    shotgun_fire      SHTGB0 + the SHTFA0 flash
    shotgun_pump      SHTGC0 + no flash
    fist_lowering     PUNGA0 lowering (sy 48)
    fist_punch        PUNGC0, the extended fist
    chaingun_flash    CHGGA0 + the CHGFB0 flash (the offset-selected flash frame)
    rocket_flash      MISGB0 + the MISFC0 flash
    saw_ready         SAWGC0
    nothing           no active layer: the background must be untouched
    off_right/left    a layer wholly outside the view: upstream returns early

PROVING THE GATE CAN FAIL

``--perturb`` makes the oracle draw the weapon one pixel lower. The run must then
exit 1; a comparison that cannot fail is not a gate.

USAGE

    python tools/weapon_draw_check.py --wad tools/DOOM1.WAD [--perturb] [--keep]

EXIT CODES: 0 every pixel matched, 1 a pixel differed, 2 unusable input or build.
Standard library only.
"""
from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / "lib" / "doom"

WIDTH = 320
HEIGHT = 200
VIEW_HEIGHT = 168  # DOOM's screen size 9: the 3D window above the status bar
FRAC = 1 << 16
FF_FRAMEMASK = 0x7FFF
FF_FULLBRIGHT = 0x8000
WEAPONTOP = 32 * FRAC
WEAPONBOTTOM = 128 * FRAC
BASEYCENTER = 100

SOURCES = (
    "r_psprite.c", "r_things.c", "r_data.c", "r_segs.c", "r_light.c",
    "m_random.c", "p_map.c", "p_setup.c", "p_mobj.c", "p_user.c",
    "p_tick.c", "p_action.c", "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c",
    "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c",
    "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c",
)

# --- The tables the oracle reads -------------------------------------------------

STATE_ROW = re.compile(
    r"\{(-?\d+), (-?\d+), (-?\d+), (-?\d+), (\w+)\},\s*/\* (S_\w+) \*/")


class Tables:
    """states[] and sprnames[] parsed from the generated info_tables.h."""

    def __init__(self) -> None:
        text = (LIB / "info_tables.h").read_text(encoding="utf-8")
        self.states = [(int(f), int(s)) for f, s, _t, _n, _a, _name in STATE_ROW.findall(text)]
        self.number = {name: index for index, (_f, _s, _t, _n, _a, name)
                       in enumerate(STATE_ROW.findall(text))}
        block = text[text.index("const char* const sprnames[NUM_SPRITES]"):]
        self.sprnames = re.findall(r'"([A-Z0-9]{4})"', block[:block.index("};")])

    def lump(self, state: int) -> tuple[str, bool]:
        frame, sprite = self.states[state]
        name = f"{self.sprnames[sprite]}{chr(ord('A') + (frame & FF_FRAMEMASK))}0"
        return name, bool(frame & FF_FULLBRIGHT)


class Wad:
    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        count, directory = struct.unpack_from("<II", data, 4)
        self.lumps: dict[str, bytes] = {}
        for index in range(count):
            offset, size, raw = struct.unpack_from("<II8s", data, directory + index * 16)
            name = raw.split(b"\0")[0].decode("latin-1").upper()
            self.lumps.setdefault(name, data[offset:offset + size])


# --- The oracle: R_DrawPlayerSprites, rewritten ---------------------------------

def fixed_mul(a: int, b: int) -> int:
    return (a * b) >> 16


def light_table(colormaps: bytes, lightnum: int) -> bytes:
    """scalelight[lightnum][MAXLIGHTSCALE-1], from R_InitLightTables().

    ``startmap = ((LIGHTLEVELS-1-i)*2)*NUMCOLORMAPS/LIGHTLEVELS`` and
    ``level = startmap - j*SCREENWIDTH/(viewwidth<<detailshift)/DISTMAP``, clamped
    to the 32 light maps. With viewwidth 320 and detailshift 0 the middle term is j.
    """
    lightnum = max(0, min(15, lightnum))
    startmap = ((15 - lightnum) * 2) * 32 // 16
    j = 47
    level = startmap - (j * 320 // 320) // 2
    level = max(0, min(31, level))
    return colormaps[level * 256:(level + 1) * 256]


def draw_psprite(surface: bytearray, patch: bytes, sx: int, sy: int, colormap: bytes,
                 lower_by: int) -> None:
    """R_DrawPSprite() + R_DrawVisSprite() + R_DrawMaskedColumn() + R_DrawColumn()."""
    width, _height, left, top = struct.unpack_from("<hhhh", patch, 0)
    centerxfrac = (WIDTH // 2) * FRAC
    centeryfrac = (VIEW_HEIGHT // 2) * FRAC
    pspritescale = FRAC  # FRACUNIT*viewwidth/SCREENWIDTH
    pspriteiscale = FRAC

    tx = sx - 160 * FRAC
    tx -= left * FRAC
    x1 = (centerxfrac + fixed_mul(tx, pspritescale)) >> 16
    if x1 > WIDTH:
        return
    tx += width * FRAC
    x2 = ((centerxfrac + fixed_mul(tx, pspritescale)) >> 16) - 1
    if x2 < 0:
        return

    texturemid = (BASEYCENTER << 16) + FRAC // 2 - (sy - top * FRAC)
    texturemid -= lower_by * FRAC  # the perturbation: draw the weapon lower
    first = max(x1, 0)
    last = min(x2, WIDTH - 1)
    startfrac = 0
    if first > x1:
        startfrac += pspriteiscale * (first - x1)

    sprtopscreen = centeryfrac - fixed_mul(texturemid, pspritescale)
    frac = startfrac
    for x in range(first, last + 1):
        column = frac >> 16
        frac += pspriteiscale
        if column < 0 or column >= width:
            continue
        (offset,) = struct.unpack_from("<I", patch, 8 + 4 * column)
        while patch[offset] != 0xFF:
            topdelta, length = patch[offset], patch[offset + 1]
            topscreen = sprtopscreen + pspritescale * topdelta
            bottomscreen = topscreen + pspritescale * length
            yl = (topscreen + FRAC - 1) >> 16
            yh = (bottomscreen - 1) >> 16
            if yh >= VIEW_HEIGHT:  # mfloorclip = screenheightarray
                yh = VIEW_HEIGHT - 1
            if yl <= -1:  # mceilingclip = negonearray
                yl = 0
            if yl <= yh:
                source = offset + 3
                columnmid = texturemid - topdelta * FRAC
                position = columnmid + (yl - VIEW_HEIGHT // 2) * pspriteiscale
                for row in range(yl, yh + 1):
                    surface[row * WIDTH + x] = colormap[patch[source + ((position >> 16) & 127)]]
                    position += pspriteiscale
            offset += length + 4


def background() -> bytearray:
    surface = bytearray(WIDTH * HEIGHT)
    for row in range(HEIGHT):
        for column in range(WIDTH):
            surface[row * WIDTH + column] = (column * 3 + row * 5) & 0xFF
    return surface


class Oracle:
    def __init__(self, wad: Wad, tables: Tables, perturb: bool) -> None:
        self.wad = wad
        self.tables = tables
        self.lower_by = 1 if perturb else 0
        self.colormaps = wad.lumps["COLORMAP"]
        self.layers = [(0, 0, 0), (0, 0, 0)]
        self.sector_light = 160
        self.extralight = 0

    def draw(self) -> tuple[bytes, int]:
        surface = background()
        missing = 0
        table = light_table(self.colormaps, (self.sector_light >> 4) + self.extralight)
        for state, sx, sy in self.layers:
            if state == 0:
                continue
            name, fullbright = self.tables.lump(state)
            patch = self.wad.lumps.get(name)
            if patch is None:
                missing += 1
                continue
            draw_psprite(surface, patch, sx, sy,
                         self.colormaps[:256] if fullbright else table, self.lower_by)
        return bytes(surface), missing


def run_oracle(wad: Wad, tables: Tables, script: str, perturb: bool) -> list[tuple[str, int, bytes]]:
    oracle = Oracle(wad, tables, perturb)
    out = []
    for line in script.splitlines():
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        if words[0] == "light":
            oracle.sector_light, oracle.extralight = int(words[1]), int(words[2])
        elif words[0] == "layer":
            oracle.layers[int(words[1])] = (int(words[2]), int(words[3]), int(words[4]))
        elif words[0] == "draw":
            pixels, missing = oracle.draw()
            out.append((words[1], missing, pixels))
    return out


# States that must leave the background untouched.
NOTHING_DRAWN = ("nothing", "off_right", "off_left")


# --- The sampled states ----------------------------------------------------------

def build_script(tables: Tables) -> str:
    n = tables.number

    def sample(name: str, light: tuple[int, int], weapon: tuple[str, int, int],
               flash: tuple[str, int, int] | None = None) -> str:
        lines = [f"light {light[0]} {light[1]}",
                 f"layer 0 {n[weapon[0]]} {weapon[1]} {weapon[2]}" if weapon[0] else "layer 0 0 0 0"]
        if flash is None:
            lines.append("layer 1 0 0 0")
        else:
            lines.append(f"layer 1 {n[flash[0]]} {flash[1]} {flash[2]}")
        lines.append(f"draw {name}")
        return "\n".join(lines) + "\n"

    top = (FRAC, WEAPONTOP)
    return "".join((
        sample("pistol_ready", (160, 0), ("S_PISTOL", *top)),
        sample("pistol_fire", (96, 0), ("S_PISTOL2", *top), ("S_PISTOLFLASH", *top)),
        sample("pistol_bob", (200, 2), ("S_PISTOL", FRAC + 0x2C000, WEAPONTOP + 0xA800)),
        sample("pistol_rising", (160, 0), ("S_PISTOL", FRAC, 64 * FRAC)),
        sample("shotgun_ready", (128, 0), ("S_SGUN", *top)),
        sample("shotgun_fire", (64, 0), ("S_SGUN2", *top), ("S_SGUNFLASH1", *top)),
        sample("shotgun_pump", (176, 1), ("S_SGUN3", FRAC - 0x18000, WEAPONTOP + 0x4000)),
        sample("fist_lowering", (144, 0), ("S_PUNCH", FRAC, 48 * FRAC)),
        sample("fist_punch", (144, 0), ("S_PUNCH2", *top)),
        sample("chaingun_flash", (112, 0), ("S_CHAIN1", *top), ("S_CHAINFLASH2", *top)),
        sample("rocket_flash", (80, 0), ("S_MISSILE2", *top), ("S_MISSILEFLASH3", *top)),
        sample("saw_ready", (255, 0), ("S_SAW3", FRAC + 0x6000, WEAPONTOP + 0x2000)),
        sample("nothing", (160, 0), ("", 0, 0)),
        sample("off_right", (160, 0), ("S_PISTOL", 700 * FRAC, WEAPONTOP)),
        sample("off_left", (160, 0), ("S_PISTOL", -500 * FRAC, WEAPONTOP)),
    ))


# --- The C side -------------------------------------------------------------------

def build_harness(workdir: Path, compiler: str, sources: tuple[str, ...] = SOURCES) -> Path:
    executable = workdir / ("weapon_draw_check.exe" if sys.platform == "win32"
                            else "weapon_draw_check")
    command = [compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-DDOOM_HUD", "-I", str(LIB),
               "-o", str(executable), str(ROOT / "tools" / "weapon_draw_check.c")]
    command += [str(LIB / name) for name in sources]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    return executable


def parse_c(output: str) -> list[tuple[str, int, bytes]]:
    lines = output.splitlines()
    frames = []
    index = 0
    while index < len(lines):
        if lines[index].startswith("DRAW "):
            _, name, missing, _written = lines[index].split()
            rows = lines[index + 1:index + 1 + HEIGHT]
            frames.append((name, int(missing.split("=")[1]), bytes.fromhex("".join(rows))))
            index += 1 + HEIGHT
        else:
            index += 1
    return frames


# --- The board's frame, rebuilt on the host ----------------------------------------

# E1M1's player 1 start as the game renders it once the weapon has finished
# rising: the x, y and eye height the board printed in `SHOW,frame=` at tics=16
# (esp32dev_ILI9341, COM4, 2026-11-01), facing north. A stationary
# player's pistol then rests at S_PISTOL, sx 1.0, sy 32.0 (no bob: no momentum).
BOARD_VIEW = (69206016, -236978176, 2686976, 1073741824)

# The box the firmware hashes around the resting pistol (kWeaponRect* in
# FrameDriver.cpp): the pistol paints x 132..183, y 122..167. A whole-view hash is
# not rebuildable at tic 16 -- the animated pickups and torches have moved on from
# the static THINGS frames the renderer oracle draws -- but this box holds only
# floor, wall and the gun.
RECT = (128, 116, 64, 52)  # x, y, width, height


def board_rect_hashes(wad_path: Path, perturb: bool = False) -> tuple[str, str]:
    """(before, after) SHA-256 of the box around the resting pistol, from the oracles.

    ``doom_ref`` renders the 3D view at 168 rows (``DOOM_REF_VIEW_HEIGHT``); the
    weapon oracle above then overlays the pistol, lit by the sector the player
    stands in. The firmware prints the same two numbers as
    ``SHOW,weaponparts,...,rect_pre=...,rect=...``: pre must match first (it says
    the world in the box is the oracle's), then post says the gun is.
    """
    import hashlib
    import os
    os.environ.setdefault("DOOM_REF_VIEW_HEIGHT", "168")
    sys.path.insert(0, str(ROOT / "tools"))
    import doom_ref

    def rect_of(surface: bytes) -> str:
        x0, y0, width, height = RECT
        data = b"".join(bytes(surface[(y0 + row) * WIDTH + x0:(y0 + row) * WIDTH + x0 + width])
                        for row in range(height))
        return hashlib.sha256(data).hexdigest()

    _identification, lumps = doom_ref.read_wad(wad_path)
    pixels, _covered = doom_ref.render_frame_pixels(lumps, "E1M1", view=BOARD_VIEW)
    before = bytearray(pixels[:WIDTH * VIEW_HEIGHT]) + bytearray(WIDTH * (HEIGHT - VIEW_HEIGHT))
    after = bytearray(before)
    bsp = doom_ref.read_bsp_map(lumps, "E1M1")
    sub = doom_ref.point_in_subsector(bsp, BOARD_VIEW[0], BOARD_VIEW[1])
    light = bsp.sectors[doom_ref.sector_of_subsector(bsp, sub)]["light"]
    wad = Wad(wad_path)
    tables = Tables()
    name, _fullbright = tables.lump(tables.number["S_PISTOL"])
    draw_psprite(after, wad.lumps[name], FRAC, WEAPONTOP,
                 light_table(wad.lumps["COLORMAP"], light >> 4), 1 if perturb else 0)
    return rect_of(before), rect_of(after)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--perturb", action="store_true",
                        help="draw the oracle's weapon 1 px lower; the run must fail")
    parser.add_argument("--keep", action="store_true", help="keep the build directory")
    parser.add_argument("--board-rect", default=None, metavar="PRE,POST|print",
                        help="also rebuild the board's weapon-at-rest box hashes and compare "
                             "them with PRE,POST (or just print them)")
    parser.add_argument("--stub-dir", type=Path, default=None,
                        help="compile r_psprite.c from this directory instead (RED proof)")
    arguments = parser.parse_args(argv)

    try:
        wad = Wad(arguments.wad)
        tables = Tables()
    except (OSError, KeyError, ValueError) as error:
        print(f"weapon_draw_check: {error}", file=sys.stderr)
        return 2

    script = build_script(tables)
    workdir = Path(tempfile.mkdtemp(prefix="weapon_draw_check_"))
    try:
        try:
            sources = SOURCES
            if arguments.stub_dir is not None:
                shutil.copy(arguments.stub_dir / "r_psprite.c", workdir / "r_psprite_stub.c")
                sources = tuple(s for s in SOURCES if s != "r_psprite.c")
            harness = build_harness(workdir, arguments.cc, sources) if arguments.stub_dir is None \
                else build_stub(workdir, arguments.cc, sources)
        except (RuntimeError, OSError) as error:
            print(f"weapon_draw_check: {error}", file=sys.stderr)
            return 2
        script_path = workdir / "script.txt"
        script_path.write_text(script, encoding="utf-8")
        result = subprocess.run([str(harness), str(arguments.wad), str(script_path)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"weapon_draw_check: harness exited {result.returncode}: {result.stderr}",
                  file=sys.stderr)
            return 2
        actual = parse_c(result.stdout)
        expected = run_oracle(wad, tables, script, arguments.perturb)

        failures = 0
        if len(actual) != len(expected):
            print(f"FAIL: {len(actual)} frames from C, {len(expected)} from the oracle")
            return 1
        blank = background()
        for (name, o_missing, pixels), (_c_name, c_missing, c_pixels) in zip(expected, actual):
            differing = [i for i in range(WIDTH * HEIGHT) if pixels[i] != c_pixels[i]]
            bar = [i for i in differing if i >= VIEW_HEIGHT * WIDTH]
            painted = sum(1 for i in range(WIDTH * HEIGHT) if pixels[i] != blank[i])
            # A comparison of two blank surfaces proves nothing: a state that is
            # meant to draw must paint pixels, one that is not must paint none.
            expect_paint = name not in NOTHING_DRAWN
            vacuous = (painted == 0) == expect_paint
            status = "ok" if not differing and not vacuous and c_missing == o_missing == 0                 else "FAIL"
            first = ""
            if differing:
                at = differing[0]
                first = (f" first=({at % WIDTH},{at // WIDTH}) oracle={pixels[at]:02x}"
                         f" c={c_pixels[at]:02x}")
            print(f"{status:4s} {name:15s} painted={painted:5d} differing={len(differing):5d}"
                  f" in_bar={len(bar)} missing_c={c_missing} missing_oracle={o_missing}{first}")
            if status == "FAIL":
                failures += 1
        print(f"weapon_draw_check: {len(expected)} states, {failures} failing")
        if arguments.board_rect:
            pre, post = board_rect_hashes(arguments.wad, arguments.perturb)
            print(f"board box oracle: pre={pre} post={post}")
            if arguments.board_rect != "print":
                want_pre, want_post = arguments.board_rect.split(",")
                for label, got, want in (("pre (world only)", pre, want_pre),
                                         ("post (world + weapon)", post, want_post)):
                    if got != want:
                        print(f"FAIL board box {label}: oracle={got} board={want}")
                        failures += 1
                    else:
                        print(f"ok   board box {label} equals the number the board printed")
        return 1 if failures else 0
    finally:
        if arguments.keep:
            print(f"weapon_draw_check: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


def build_stub(workdir: Path, compiler: str, sources: tuple[str, ...]) -> Path:
    """Compile the harness with a stand-in r_psprite.c (the RED run)."""
    executable = workdir / ("weapon_draw_check.exe" if sys.platform == "win32"
                            else "weapon_draw_check")
    command = [compiler, "-std=gnu99", "-O2", "-DDOOM_HUD", "-I", str(LIB), "-o", str(executable),
               str(ROOT / "tools" / "weapon_draw_check.c"), str(workdir / "r_psprite_stub.c")]
    command += [str(LIB / name) for name in sources]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the stub harness did not compile:\n" + (result.stderr or result.stdout))
    return executable


if __name__ == "__main__":
    sys.exit(main())
