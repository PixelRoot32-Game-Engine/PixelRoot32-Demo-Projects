#!/usr/bin/env python3
"""Pixel oracle for the menu drawing (T39, D5.2).

``lib/doom/m_menu.c`` draws the main menu, the New Game / skill screen and the
setup-lite screen into the 8bpp surface by patch name. ``tools/menu_draw_check.c``
runs that code from a script and dumps the whole 320x200 index surface; this file
runs the SAME script through an independent drawer and compares the two byte for
byte.

The drawer is written from linuxdoom's ``m_menu.c`` and ``v_video.c``:
``M_Drawer`` (the title patch, each row's picture 16 px apart, the skull at
``x - 32, y - 5 + itemOn * 16`` and its two-frame animation), ``M_DrawNewGame``
(``M_NEWG`` at 96,14 and ``M_SKILL`` at 54,38), ``M_WriteText`` (the HU font,
a space advancing 4) and ``M_DrawThermo`` (left cap, middle pieces, right cap, dot).
It has its own patch reader and blitter; the key handling comes from the reference
model in ``menu_check.py``, which shares no code with the C either. It is a second
implementation of the same rules, NOT a run of vanilla DOOM (no binary is available),
so it rules out transcription slips, not a shared misreading of the source.

Eleven scripted states cover every screen, the first and last row of each (the mute
row of the setup-lite screen is the last one), both skull frames and the closed menu
(which must paint nothing). The surface starts as the sentinel 0xEE so a pixel
written outside a patch's footprint shows up. A state that paints nothing when it
should is itself a failure, so two blank surfaces cannot pass.

    python tools/menu_draw_check.py --wad tools/DOOM1.WAD
    python tools/menu_draw_check.py --wad tools/DOOM1.WAD --perturb   # must FAIL

Exit 0 when every state matches, 1 when one differs, 2 on unusable input.
"""
from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import menu_check  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
WIDTH, HEIGHT = 320, 200
SENTINEL = 0xEE

SOURCES = ("m_menu.c", "m_gamma.c")

SCRIPT = """
# main screen, first row, first skull frame
init 2 8 0
open
draw main_new_game
# main screen, second row, second skull frame
init 2 8 0
open
tick 10
key down
draw main_options_skull2
# new game: the cursor starts on the saved skill
init 2 8 0
open
key enter
draw newgame_default
# new game: the last row (nightmare), saved as the skill
init 4 8 0
open
key enter
draw newgame_nightmare
# new game: the first row, second skull frame
init 0 8 0
open
key enter
tick 10
draw newgame_baby_skull2
# setup-lite, first row
init 2 8 0
open
key down
key enter
draw options_skill
# setup-lite, volume row with every slider at its top
init 4 15 4 0
open
key down
key enter
key down
draw options_volume_max
# setup-lite, gamma row with every slider at its bottom
init 0 0 0 0
open
key down
key enter
key down
key down
draw options_gamma_zero
# setup-lite, the fourth row (mute) ON: the value is the word, not a thermometer
init 2 8 0 1
open
key down
key enter
key down
key down
key down
draw options_mute_on
# setup-lite, mid values, second skull frame
init 1 7 2 0
open
key down
key enter
tick 10
draw options_mid_skull2
# a closed menu paints nothing
init 2 8 0
draw closed
"""


class Wad:
    def __init__(self, path: Path) -> None:
        data = path.read_bytes()
        count, directory = struct.unpack_from("<II", data, 4)
        self.lumps: dict[str, bytes] = {}
        for index in range(count):
            offset, size, raw = struct.unpack_from("<II8s", data, directory + index * 16)
            name = raw.split(b"\0")[0].decode("latin-1").upper()
            self.lumps.setdefault(name, data[offset:offset + size])


def draw_patch(surface: bytearray, patch: bytes, x: int, y: int) -> None:
    """V_DrawPatch(): the patch's offsets move it, columns are post chains, and
    every pixel is clipped to the 320x200 surface."""
    width, _height, left, top = struct.unpack_from("<hhhh", patch, 0)
    x -= left
    y -= top
    for column in range(width):
        (offset,) = struct.unpack_from("<I", patch, 8 + 4 * column)
        while patch[offset] != 0xFF:
            topdelta, length = patch[offset], patch[offset + 1]
            for row in range(length):
                px, py = x + column, y + topdelta + row
                if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                    surface[py * WIDTH + px] = patch[offset + 3 + row]
            offset += length + 4


SKILL_NAMES = ("I'M TOO YOUNG TO DIE.", "HEY, NOT TOO ROUGH.", "HURT ME PLENTY.",
               "ULTRA-VIOLENCE.", "NIGHTMARE!")


class Drawer:
    def __init__(self, wad: Wad, perturb: bool) -> None:
        self.wad = wad
        self.perturb = perturb

    def patch(self, surface, name, x, y):
        draw_patch(surface, self.wad.lumps[name], x, y)

    def write_text(self, surface, x, y, text):
        """M_WriteText()."""
        cx, cy = x, y
        for char in text:
            if char == "\n":
                cx, cy = x, cy + 12
                continue
            code = ord(char.upper())
            if not 33 <= code <= 95:
                cx += 4
                continue
            patch = self.wad.lumps[f"STCFN{code:03d}"]
            width = struct.unpack_from("<h", patch, 0)[0]
            if cx + width > WIDTH:
                break
            draw_patch(surface, patch, cx, cy)
            cx += width

    def thermo(self, surface, x, y, width, dot):
        """M_DrawThermo()."""
        xx = x
        self.patch(surface, "M_THERML", xx, y)
        xx += 8
        for _ in range(width):
            self.patch(surface, "M_THERMM", xx, y)
            xx += 8
        self.patch(surface, "M_THERMR", xx, y)
        self.patch(surface, "M_THERMO", (x + 8) + dot * 8, y)

    def draw(self, model: menu_check.Model) -> bytes:
        surface = bytearray([SENTINEL]) * (WIDTH * HEIGHT)
        if not model.active:
            return bytes(surface)
        item = model.item[model.screen]
        if model.screen == 0:
            self.patch(surface, "M_DOOM", 94, 2)
            for index, name in enumerate(("M_NGAME", "M_OPTION")):
                self.patch(surface, name, 97, 64 + 16 * index)
            skull_x, skull_y = 97 - 32, 64 - 5 + item * 16
        elif model.screen == 1:
            self.patch(surface, "M_NEWG", 96, 14)
            self.patch(surface, "M_SKILL", 54, 38)
            for index, name in enumerate(("M_JKILL", "M_ROUGH", "M_HURT", "M_ULTRA", "M_NMARE")):
                self.patch(surface, name, 48, 63 + 16 * index)
            skull_x, skull_y = 48 - 32, 63 - 5 + item * 16
        else:
            self.patch(surface, "M_OPTTTL", 108, 15)
            self.write_text(surface, 60, 40, "SKILL LEVEL")
            self.write_text(surface, 60, 40 + 14, SKILL_NAMES[model.skill])
            self.write_text(surface, 60, 76, "SOUND VOLUME")
            self.thermo(surface, 60, 76 + 14, 16, model.volume)
            self.write_text(surface, 60, 112, "GAMMA CORRECTION")
            self.thermo(surface, 60, 112 + 14, 5, model.gamma)
            # T49's fourth row: a slider with two positions, drawn as the word.
            self.write_text(surface, 60, 148, "SOUND MUTE")
            self.write_text(surface, 60, 148 + 14, "ON" if model.muted else "OFF")
            skull_x, skull_y = 60 - 32, 40 - 5 + item * 36
        if self.perturb:
            skull_y += 1  # the injected bug: the cursor one pixel low
        self.patch(surface, "M_SKULL2" if model.skull else "M_SKULL1", skull_x, skull_y)
        return bytes(surface)


def run_oracle(wad: Wad, script: str, perturb: bool) -> list[tuple[str, bytes]]:
    drawer = Drawer(wad, perturb)
    model = menu_check.Model()
    out = []
    for line in script.splitlines():
        words = line.split()
        if not words or words[0].startswith("#"):
            continue
        if words[0] == "init":
            values = [int(word) for word in words[1:4]]
            muted = int(words[4]) if len(words) > 4 else 0
            model = menu_check.Model((values[0], values[1], values[2], muted))
        elif words[0] == "open":
            model.open()
        elif words[0] == "key":
            model.key(words[1])
        elif words[0] == "tick":
            model.tick(int(words[1]))
        elif words[0] == "draw":
            out.append((words[1], drawer.draw(model)))
    return out


def build_harness(workdir: Path, compiler: str) -> Path:
    executable = workdir / ("menu_draw_check.exe" if sys.platform == "win32" else "menu_draw_check")
    lib = ROOT / "lib" / "doom"
    command = [compiler, "-std=gnu99", "-O2", "-Wall", "-Wextra", "-I", str(lib),
               "-o", str(executable), str(ROOT / "tools" / "menu_draw_check.c")]
    command += [str(lib / name) for name in SOURCES]
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
            fields = lines[index].split()
            missing = int(fields[-1].split("=")[1])
            rows = lines[index + 1:index + 1 + HEIGHT]
            frames.append((fields[1], missing, bytes.fromhex("".join(rows))))
            index += 1 + HEIGHT
        else:
            index += 1
    return frames


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--wad", type=Path, required=True)
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--perturb", action="store_true",
                        help="draw the oracle's cursor 1 px low; the run must fail")
    parser.add_argument("--keep", action="store_true", help="keep the build directory")
    arguments = parser.parse_args(argv)

    try:
        wad = Wad(arguments.wad)
    except OSError as error:
        print(f"menu_draw_check: {error}", file=sys.stderr)
        return 2

    workdir = Path(tempfile.mkdtemp(prefix="menu_draw_check_"))
    try:
        try:
            harness = build_harness(workdir, arguments.cc)
        except (RuntimeError, OSError) as error:
            print(f"menu_draw_check: {error}", file=sys.stderr)
            return 2
        script_path = workdir / "script.txt"
        script_path.write_text(SCRIPT, encoding="utf-8")
        result = subprocess.run([str(harness), str(arguments.wad), str(script_path)],
                                capture_output=True, text=True)
        if result.returncode != 0:
            print(f"menu_draw_check: harness exited {result.returncode}: {result.stderr}",
                  file=sys.stderr)
            return 2
        actual = parse_c(result.stdout)
        expected = run_oracle(wad, SCRIPT, arguments.perturb)

        if len(actual) != len(expected):
            print(f"FAIL: {len(actual)} frames from C, {len(expected)} from the oracle")
            return 1
        failures = 0
        for (name, pixels), (c_name, missing, c_pixels) in zip(expected, actual):
            differing = [i for i in range(WIDTH * HEIGHT) if pixels[i] != c_pixels[i]]
            painted = sum(1 for value in c_pixels if value != SENTINEL)
            should_paint = name != "closed"
            blank = (painted == 0) == should_paint  # painted nothing when it should, or the reverse
            status = "ok" if not differing and missing == 0 and not blank else "FAIL"
            first = ""
            if differing:
                at = differing[0]
                first = (f" first=({at % WIDTH},{at // WIDTH}) oracle={pixels[at]:02x}"
                         f" c={c_pixels[at]:02x}")
            print(f"{status:4s} {name:22s} differing={len(differing):5d} painted={painted:5d}"
                  f" missing_patches={missing}{first}")
            if status == "FAIL" or c_name != name:
                failures += 1
        print(f"menu_draw_check: {len(expected)} states, {failures} failing")
        return 1 if failures else 0
    finally:
        if arguments.keep:
            print(f"menu_draw_check: kept {workdir}")
        else:
            shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
