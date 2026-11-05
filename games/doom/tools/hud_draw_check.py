#!/usr/bin/env python3
"""Compare the C status bar against an independent Python oracle, pixel by pixel.

WHAT THIS PROVES

``lib/doom/st_stuff.c`` repaints DOOM's status bar -- STBAR, the ammo / health /
armor numbers, STARMS and its six weapon digits, the three key boxes and the face
-- into the bottom 32 rows of a 320x200 index surface. This script runs a
scripted sequence of player states through two implementations and requires the
whole surface to match byte for byte after every ``draw``:

* the C side is ``tools/hud_draw_check.c`` linked against the real ``st_stuff.c``,
  fed the real patches of a real WAD;
* the oracle is written here from linuxdoom-1.10's ``st_stuff.c``/``st_lib.c``
  and ``v_video.c`` and shares no code with the C: its own patch reader, its own
  column blitter, its own number/percent/multi-icon widgets and its own
  ``ST_updateFaceWidget``. Only the RNG table and ``R_PointToAngle`` come from
  ``tools/doom_ref.py`` (the repository's existing oracle, itself independent of
  the C).

The oracle is NOT a run of vanilla DOOM; no DOOM binary is available here. It is
a second implementation of the same source, so what this gate rules out is a
transcription slip in the C, not a misreading of the layout shared by both.

THE SAMPLED STATES (``SCRIPT`` below)

    spawn        100 % health, pistol, 50 bullets: the first frame of E1M1
    spawn_b      the same state with the face widget off (Option B, no faces)
    hurt_left    37 %, green armor, shotgun, a blue card and a yellow skull,
                 damaged from the left: the turn-left face
    hurt_right   the same, damaged from the right: the turn-right face
    head_on      damaged from straight ahead: the rampage face
    ouch         health jumps 30 points while damaged (upstream's `health - oldhealth
                 > ST_MUCHPAIN`, which is positive only when health rose): the ouch face
    evil_grin    a weapon was picked up: the evil grin
    rampage      the fire button held for 71 tics: the rampage face
    dead         health 0: STFDEAD0
    god          invulnerable, 200 % blue armor, every weapon and key: STFGOD0
    overkill     health -12 with the face off: the minus sign and a two-digit number
                 (unreachable in play, where health is clamped at 0, and a face
                 drawn from it would index past STFDEAD0, so the widget is off)

PROVING THE GATE CAN FAIL

``--perturb`` makes the oracle draw the status bar's health number one pixel to
the left. The run must then exit 1; a comparison that cannot fail is not a gate.

It ends with the HUD frame-00 hash: E1M1's frame 00 rendered by the oracle in a
168-row window with the bar overlaid must equal the number the board printed.

USAGE

    python tools/hud_draw_check.py --wad tools/DOOM1.WAD [--perturb] [--keep]

EXIT CODES: 0 every pixel matched, 1 a pixel differed, 2 unusable input or build.
Standard library only (plus tools/doom_ref.py).
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

# This gate describes the DOOM_HUD configuration: a 168-row 3D window. doom_ref
# reads the variable at import time; the status-bar oracle below never uses it.
os.environ.setdefault("DOOM_REF_VIEW_HEIGHT", "168")

import doom_ref  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent

WIDTH = 320
HEIGHT = 200
ST_HEIGHT = 32
ST_Y = HEIGHT - ST_HEIGHT
SENTINEL = 0xEE

SOURCES = (
    "st_stuff.c", "m_random.c", "p_map.c", "p_setup.c", "p_mobj.c", "p_user.c",
    "p_tick.c", "p_action.c", "p_inter.c", "p_pspr.c", "p_sight.c", "p_enemy.c",
    "z_zone.c", "doom_map.c", "r_bsp.c", "r_plane.c", "r_tables.c", "w_wad.c",
    "p_spec.c", "p_floor.c", "p_doors.c", "p_plats.c", "p_ceilng.c", "p_lights.c",
)

# --- The scripted player states --------------------------------------------------

BASE = """
set health 100
set armor 0
set armortype 0
set ammo 50 0 0 0
set maxammo 200 50 300 50
set weapons 1 1 0 0 0 0 0 0 0
set ready 1
set cards 0 0 0 0 0 0
set powers 0 0 0 0 0 0
set angle 0
set damagecount 0
set bonuscount 0
set attackdown 0
set attacker none
"""

HURT = """
set health 37
set armor 85
set armortype 1
set ammo 23 8 0 3
set maxammo 400 100 600 100
set weapons 1 1 1 1 1 0 0 0 0
set ready 2
set cards 1 0 0 0 1 0
set powers 0 0 0 0 0 0
set angle 0
set damagecount 0
set bonuscount 0
set attackdown 0
set attacker none
"""

SCRIPT = (
    BASE + """
start
tick 1
draw spawn 1
draw spawn_b 0
"""
    + HURT + """
start
tick 1
set damagecount 12
set attacker 0 100
tick 1
draw hurt_left 1
"""
    + HURT + """
start
tick 1
set damagecount 12
set attacker 0 -100
tick 1
draw hurt_right 1
"""
    + HURT + """
start
tick 1
set damagecount 12
set attacker 100 40
tick 1
draw head_on 1
"""
    + HURT + """
start
tick 40
set health 30
tick 1
set health 60
set damagecount 20
set attacker 0 100
tick 1
draw ouch 1
"""
    + BASE + """
start
tick 3
set weapons 1 1 1 0 0 0 0 0 0
set bonuscount 6
tick 1
draw evil_grin 1
"""
    + BASE + """
start
tick 2
set attackdown 1
tick 71
draw rampage 1
"""
    + BASE + """
set health 0
set ready 0
start
tick 2
draw dead 1
"""
    + """
set health 100
set armor 200
set armortype 2
set ammo 200 50 300 50
set maxammo 200 50 300 50
set weapons 1 1 1 1 1 1 1 1 1
set ready 6
set cards 1 1 1 1 1 1
set powers 1000 0 0 0 0 0
set attackdown 0
start
tick 2
draw god 1
"""
    + BASE + """
set health -12
set ready 4
set ammo 3 4 5 6
start
tick 1
draw overkill 0
"""
)

# --- The oracle: v_video.c, st_lib.c and st_stuff.c, rewritten -------------------

WEAPON_AMMO = (5, 0, 1, 0, 3, 2, 2, 5, 1)  # d_items.c's weaponinfo[].ammo; 5 = noammo

ANG45 = 0x20000000
ANG180 = 0x80000000

# st_stuff.c layout constants.
ST_AMMOX, ST_AMMOY = 44, 171
ST_HEALTHX, ST_HEALTHY = 90, 171
ST_ARMSX, ST_ARMSY = 111, 172
ST_ARMSBGX, ST_ARMSBGY = 104, 168
ST_ARMSXSPACE, ST_ARMSYSPACE = 12, 10
ST_ARMORX, ST_ARMORY = 221, 171
ST_FACESX, ST_FACESY = 143, 168
ST_KEY_X = (239, 239, 239)
ST_KEY_Y = (171, 181, 191)
ST_AMMO_X, ST_AMMO_Y = 288, (173, 179, 191, 185)
ST_MAXAMMO_X, ST_MAXAMMO_Y = 314, (173, 179, 191, 185)

ST_NUMPAINFACES = 5
ST_FACESTRIDE = 8
ST_TURNOFFSET = 3
ST_OUCHOFFSET = 5
ST_EVILGRINOFFSET = 6
ST_RAMPAGEOFFSET = 7
ST_GODFACE = 40
ST_DEADFACE = 41
ST_STRAIGHTFACECOUNT = 17
ST_TURNCOUNT = 35
ST_RAMPAGEDELAY = 70
ST_EVILGRINCOUNT = 70
ST_MUCHPAIN = 20


class Wad:
    """The lumps of an IWAD by name (first occurrence wins, as W_CheckNumForName's
    last-to-first scan does for a name that appears once)."""

    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        count, directory = struct.unpack_from("<II", data, 4)
        self.lumps: dict[str, bytes] = {}
        for index in range(count):
            offset, size, raw = struct.unpack_from("<II8s", data, directory + index * 16)
            name = raw.split(b"\0")[0].decode("latin-1").upper()
            self.lumps.setdefault(name, data[offset:offset + size])


def draw_patch(surface: bytearray, patch: bytes, x: int, y: int) -> None:
    """V_DrawPatch(): the patch's offsets move it, columns are post chains."""
    width, _height, left, top = struct.unpack_from("<hhhh", patch, 0)
    x -= left
    y -= top
    for column in range(width):
        (offset,) = struct.unpack_from("<I", patch, 8 + 4 * column)
        while patch[offset] != 0xFF:
            topdelta, length = patch[offset], patch[offset + 1]
            for row in range(length):
                surface[(y + topdelta + row) * WIDTH + x + column] = patch[offset + 3 + row]
            offset += length + 4


def patch_size(patch: bytes) -> tuple[int, int]:
    width, height = struct.unpack_from("<hh", patch, 0)
    return width, height


class Bar:
    """ST_Start/ST_Ticker/ST_Drawer for one player."""

    def __init__(self, wad: Wad, perturb: bool) -> None:
        self.wad = wad
        self.perturb = perturb
        self.player = {
            "health": 100, "armor": 0, "ammo": [0] * 4, "maxammo": [0] * 4,
            "weapons": [0] * 9, "ready": 1, "cards": [0] * 6, "powers": [0] * 6,
            "angle": 0, "damagecount": 0, "bonuscount": 0, "attackdown": 0,
            "attacker": None,
        }
        self.rng = doom_ref.RandomState(0)
        self.start()

    # --- ST_Start / ST_initData -----------------------------------------------
    def start(self) -> None:
        self.faceindex = 0
        self.oldhealth = -1
        self.facecount = 0
        self.priority = 0
        self.lastattackdown = -1
        self.randomnumber = 0
        self.pain_old = -1
        self.pain_last = 0
        self.oldweapons = list(self.player["weapons"])
        self.rng = doom_ref.RandomState(0)

    # --- ST_Ticker --------------------------------------------------------------
    def tick(self) -> None:
        self.randomnumber = self.rng.next()
        self.update_face()
        self.oldhealth = self.player["health"]

    def pain_offset(self) -> int:
        health = min(self.player["health"], 100)
        if health != self.pain_old:
            self.pain_last = ST_FACESTRIDE * (((100 - health) * ST_NUMPAINFACES) // 101)
            self.pain_old = health
        return self.pain_last

    def update_face(self) -> None:
        p = self.player
        if self.priority < 10 and p["health"] == 0:
            self.priority = 9
            self.faceindex = ST_DEADFACE
            self.facecount = 1
        if self.priority < 9 and p["bonuscount"]:
            grin = False
            for index in range(9):
                if self.oldweapons[index] != p["weapons"][index]:
                    grin = True
                    self.oldweapons[index] = p["weapons"][index]
            if grin:
                self.priority = 8
                self.facecount = ST_EVILGRINCOUNT
                self.faceindex = self.pain_offset() + ST_EVILGRINOFFSET
        if self.priority < 8 and p["damagecount"] and p["attacker"] is not None \
                and p["attacker"] != "self":
            self.priority = 7
            if p["health"] - self.oldhealth > ST_MUCHPAIN:
                self.facecount = ST_TURNCOUNT
                self.faceindex = self.pain_offset() + ST_OUCHOFFSET
            else:
                badguy = doom_ref.point_to_angle(0, 0, p["attacker"][0] << 16,
                                                 p["attacker"][1] << 16)
                if badguy > p["angle"]:
                    diff = (badguy - p["angle"]) & 0xFFFFFFFF
                    right = diff > ANG180
                else:
                    diff = (p["angle"] - badguy) & 0xFFFFFFFF
                    right = diff <= ANG180
                self.facecount = ST_TURNCOUNT
                self.faceindex = self.pain_offset()
                if diff < ANG45:
                    self.faceindex += ST_RAMPAGEOFFSET
                elif right:
                    self.faceindex += ST_TURNOFFSET
                else:
                    self.faceindex += ST_TURNOFFSET + 1
        if self.priority < 7 and p["damagecount"]:
            if p["health"] - self.oldhealth > ST_MUCHPAIN:
                self.priority = 7
                self.facecount = ST_TURNCOUNT
                self.faceindex = self.pain_offset() + ST_OUCHOFFSET
            else:
                self.priority = 6
                self.facecount = ST_TURNCOUNT
                self.faceindex = self.pain_offset() + ST_RAMPAGEOFFSET
        if self.priority < 6:
            if p["attackdown"]:
                if self.lastattackdown == -1:
                    self.lastattackdown = ST_RAMPAGEDELAY
                else:
                    self.lastattackdown -= 1
                    if self.lastattackdown == 0:
                        self.priority = 5
                        self.faceindex = self.pain_offset() + ST_RAMPAGEOFFSET
                        self.facecount = 1
                        self.lastattackdown = 1
            else:
                self.lastattackdown = -1
        if self.priority < 5 and p["powers"][0]:
            self.priority = 4
            self.faceindex = ST_GODFACE
            self.facecount = 1
        if not self.facecount:
            self.faceindex = self.pain_offset() + (self.randomnumber % 3)
            self.facecount = ST_STRAIGHTFACECOUNT
            self.priority = 0
        self.facecount -= 1

    # --- the face lump names, in ST_loadGraphics order --------------------------
    @staticmethod
    def face_name(index: int) -> str:
        if index == ST_GODFACE:
            return "STFGOD0"
        if index == ST_DEADFACE:
            return "STFDEAD0"
        pain, within = divmod(index, ST_FACESTRIDE)
        if within < 3:
            return f"STFST{pain}{within}"
        return (f"STFTR{pain}0", f"STFTL{pain}0", f"STFOUCH{pain}", f"STFEVL{pain}",
                f"STFKILL{pain}")[within - 3]

    # --- st_lib.c widgets -------------------------------------------------------
    def number(self, surface: bytearray, x: int, y: int, width: int, value: int,
               digits: str) -> None:
        """STlib_drawNum with the digit patches named ``digits``0..9."""
        if value == 1994:
            return
        glyph_w = patch_size(self.wad.lumps[f"{digits}0"])[0]
        negative = value < 0
        if negative:
            if width == 2 and value < -9:
                value = -9
            elif width == 3 and value < -99:
                value = -99
            value = -value
        if value == 0:
            draw_patch(surface, self.wad.lumps[f"{digits}0"], x - glyph_w, y)
        remaining = width
        while value and remaining:
            remaining -= 1
            x -= glyph_w
            draw_patch(surface, self.wad.lumps[f"{digits}{value % 10}"], x, y)
            value //= 10
        if negative:
            draw_patch(surface, self.wad.lumps["STTMINUS"], x - 8, y)

    def percent(self, surface: bytearray, x: int, y: int, value: int) -> None:
        draw_patch(surface, self.wad.lumps["STTPRCNT"], x, y)
        self.number(surface, x, y, 3, value, "STTNUM")

    # --- ST_Drawer ----------------------------------------------------------------
    def draw(self, faces: bool) -> bytes:
        p = self.player
        surface = bytearray([SENTINEL]) * (WIDTH * HEIGHT)
        draw_patch(surface, self.wad.lumps["STBAR"], 0, ST_Y)

        ammo_type = WEAPON_AMMO[p["ready"]]
        ready = 1994 if ammo_type == 5 else p["ammo"][ammo_type]
        self.number(surface, ST_AMMOX, ST_AMMOY, 3, ready, "STTNUM")
        for index in range(4):
            self.number(surface, ST_AMMO_X, ST_AMMO_Y[index], 3, p["ammo"][index], "STYSNUM")
            self.number(surface, ST_MAXAMMO_X, ST_MAXAMMO_Y[index], 3, p["maxammo"][index],
                        "STYSNUM")
        health_x = ST_HEALTHX - 1 if self.perturb else ST_HEALTHX
        self.percent(surface, health_x, ST_HEALTHY, p["health"])
        self.percent(surface, ST_ARMORX, ST_ARMORY, p["armor"])

        draw_patch(surface, self.wad.lumps["STARMS"], ST_ARMSBGX, ST_ARMSBGY)
        for index in range(6):
            owned = 1 if p["weapons"][index + 1] else 0
            prefix = "STYSNUM" if owned else "STGNUM"
            draw_patch(surface, self.wad.lumps[f"{prefix}{index + 2}"],
                       ST_ARMSX + (index % 3) * ST_ARMSXSPACE,
                       ST_ARMSY + (index // 3) * ST_ARMSYSPACE)

        if faces:
            draw_patch(surface, self.wad.lumps[self.face_name(self.faceindex)],
                       ST_FACESX, ST_FACESY)

        for slot in range(3):
            key = slot if p["cards"][slot] else -1
            if p["cards"][slot + 3]:
                key = slot + 3
            if key != -1:
                draw_patch(surface, self.wad.lumps[f"STKEYS{key}"], ST_KEY_X[slot],
                           ST_KEY_Y[slot])
        return bytes(surface)

    # --- the script ---------------------------------------------------------------
    def set(self, key: str, values: list[str]) -> None:
        p = self.player
        numbers = [int(value) for value in values if value not in ("none",)]
        if key in ("health", "armor", "ready", "angle", "damagecount", "bonuscount",
                   "attackdown"):
            p[key] = numbers[0]
        elif key == "armortype":
            pass
        elif key in ("ammo", "maxammo", "weapons", "cards", "powers"):
            p[key] = numbers
        elif key == "attacker":
            p["attacker"] = None if values[0] == "none" else (numbers[0], numbers[1])


def run_oracle(wad: Wad, script: str, perturb: bool) -> list[tuple[str, str, bytes]]:
    bar = Bar(wad, perturb)
    out: list[tuple[str, str, bytes]] = []
    for line in script.splitlines():
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        if words[0] == "set":
            bar.set(words[1], words[2:])
        elif words[0] == "start":
            bar.start()
        elif words[0] == "tick":
            for _ in range(int(words[1])):
                bar.tick()
        elif words[0] == "draw":
            faces = words[2] == "1"
            out.append((words[1], f"index={bar.faceindex}", bar.draw(faces)))
    return out


# --- The C side ---------------------------------------------------------------------

def build_harness(workdir: Path, compiler: str) -> Path:
    executable = workdir / ("hud_draw_check.exe" if sys.platform == "win32" else "hud_draw_check")
    lib = ROOT / "lib" / "doom"
    command = [compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(lib),
               "-o", str(executable), str(ROOT / "tools" / "hud_draw_check.c")]
    command += [str(lib / name) for name in SOURCES]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError("the harness did not compile:\n" + (result.stderr or result.stdout))
    return executable


def parse_c(output: str) -> list[tuple[str, str, int, bytes]]:
    lines = output.splitlines()
    frames = []
    index = 0
    face = ""
    while index < len(lines):
        if lines[index].startswith("FACE "):
            face = lines[index].split()[2]
            index += 1
        elif lines[index].startswith("DRAW "):
            _, name, _faces, missing = lines[index].split()
            rows = lines[index + 1:index + 1 + HEIGHT]
            frames.append((name, face, int(missing.split("=")[1]),
                           bytes.fromhex("".join(rows))))
            index += 1 + HEIGHT
        else:
            index += 1
    return frames


# The viewpoint the board renders for frame 00: E1M1's player 1 start, eye 41
# units above the floor, facing east. The same four numbers `SHOW,frame00` prints.
FRAME00_VIEW = (69206016, -236978176, 2686976, 0)


# The HUD frame-00 hash the board printed (`SHOW,frame00` in env
# esp32dev_ILI9341_hud, COM4, 2026-11-01). The oracle rebuilds it from the
# renderer oracle at a 168-row window plus the status-bar oracle; the view half
# (8c5d2289...) and the bar half (5c8a45fb...) matched the board separately.
# The 200-row `50fb3d9c...` is the `_show` environment's and is not this number.
HUD_FRAME00 = "1d8b3849ab3f76750056937f2827bf2bcbb2b98e82976fb3a7ce263d5273c9d3"


def frame00_hash(wad_path: Path, perturb: bool = False) -> str:
    """The SHA-256 of E1M1's frame 00 as the HUD build draws it, from the oracle.

    The renderer oracle is run with a 168-row window (``DOOM_REF_VIEW_HEIGHT``),
    its 320x200 surface keeps rows 0..167 and the bottom 32 rows are replaced by
    the status bar of a fresh player (ST_Start, no tic yet, so face 0 = STFST00).
    The hash covers the 64,000 index bytes then the 320 column-coverage bytes,
    as the driver's hashSurface() does. The board prints the same number as ``SHOW,frame00`` in the
    ``esp32dev_ILI9341_hud`` environment; the 200-row ``50fb3d9c`` is the
    ``_show`` environment's, untouched.
    """
    import hashlib

    _identification, lumps = doom_ref.read_wad(wad_path)
    pixels, covered = doom_ref.render_frame_pixels(lumps, "E1M1", view=FRAME00_VIEW)
    surface = bytearray(pixels)
    bar = Bar(Wad(wad_path), perturb)
    bar.player.update({"health": 100, "armor": 0, "ammo": [50, 0, 0, 0],
                       "maxammo": [200, 50, 300, 50], "weapons": [1, 1, 0, 0, 0, 0, 0, 0, 0],
                       "ready": 1})
    bar.start()
    bar_rows = bar.draw(True)
    surface[ST_Y * WIDTH:] = bar_rows[ST_Y * WIDTH:]
    return hashlib.sha256(bytes(surface) + covered).hexdigest()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--perturb", action="store_true",
                        help="draw the oracle's health number 1 px left; the run must fail")
    parser.add_argument("--keep", action="store_true", help="keep the build directory")
    arguments = parser.parse_args(argv)

    try:
        wad = Wad(arguments.wad)
    except OSError as error:
        print(f"hud_draw_check: {error}", file=sys.stderr)
        return 2

    workdir = Path(tempfile.mkdtemp(prefix="hud_draw_check_"))
    try:
        try:
            harness = build_harness(workdir, arguments.cc)
        except (RuntimeError, OSError) as error:
            print(f"hud_draw_check: {error}", file=sys.stderr)
            return 2
        script_path = workdir / "script.txt"
        script_path.write_text(SCRIPT, encoding="utf-8")
        result = subprocess.run([str(harness), str(arguments.wad), str(script_path)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"hud_draw_check: harness exited {result.returncode}: {result.stderr}",
                  file=sys.stderr)
            return 2
        actual = parse_c(result.stdout)
        expected = run_oracle(wad, SCRIPT, arguments.perturb)

        failures = 0
        if len(actual) != len(expected):
            print(f"FAIL: {len(actual)} frames from C, {len(expected)} from the oracle")
            return 1
        for (name, _tag, pixels), (c_name, c_face, missing, c_pixels) in zip(expected, actual):
            differing = [i for i in range(WIDTH * HEIGHT) if pixels[i] != c_pixels[i]]
            outside = [i for i in differing if i < ST_Y * WIDTH]
            status = "ok" if not differing and missing == 0 else "FAIL"
            first = ""
            if differing:
                at = differing[0]
                first = (f" first=({at % WIDTH},{at // WIDTH}) oracle={pixels[at]:02x}"
                         f" c={c_pixels[at]:02x}")
            print(f"{status:4s} {name:11s} {c_face:10s} differing={len(differing):5d}"
                  f" outside_bar={len(outside)} missing_patches={missing}{first}")
            if differing or missing:
                failures += 1
        print(f"hud_draw_check: {len(expected)} states, {failures} failing")
        digest = frame00_hash(arguments.wad, arguments.perturb)
        if digest != HUD_FRAME00:
            print(f"FAIL frame00 oracle={digest} pinned={HUD_FRAME00}")
            failures += 1
        else:
            print(f"ok   frame00 (168-row view + bar) {digest}")
        return 1 if failures else 0
    finally:
        if arguments.keep:
            print(f"hud_draw_check: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
