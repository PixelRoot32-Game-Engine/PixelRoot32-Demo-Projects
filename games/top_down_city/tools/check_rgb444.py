#!/usr/bin/env python3
"""Report RGB444 collisions across every palette this demo ships.

`[env:esp32dev]` sets PIXELROOT32_TFT_12BIT_COLOR=1, which sends four bits per
channel instead of five and six. Two RGB565 entries that differ only in the
bits that are dropped become the same colour on the panel, and the shading
between them disappears -- on the simulator, which is RGB565 throughout, it
does not.

The day/night cycle changes the answer: darkening compresses the palettes
toward black, so entries that were distinguishable at noon can collide at
midnight. Pass the ambient multiplier to check a given light; the values must
match the keyframes in ../src/game/rules/DayNight.cpp, which this script
deliberately does not import -- it is a check on the shipped art, not on the
C++.

    python tools/check_rgb444.py                       # daylight
    python tools/check_rgb444.py --ambient 88,104,160  # kNight
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

HEADERS = (
    "src/generated/tilemaps/city_scene.h",
    "src/generated/tilemaps/police_station.h",
    "src/generated/sprites/PlayerSprites.h",
    "src/generated/sprites/VehicleSprites.h",
    "src/generated/sprites/PedestrianSprites.h",
)

# name, then the declared bounds, then the body. The bounds are captured
# because a generated palette may declare [16] and list fewer entries -- the
# vehicle palette does, leaving the last one implicitly zero -- and a parser
# that silently drops a short palette is a checker that silently under-counts.
PALETTE_RE = re.compile(
    r"(\w*PALETTE\w*)\s*((?:\[[^\]]*\])+)\s*=\s*\{(.*?)\}\s*;", re.S
)


def load_palettes(root: pathlib.Path) -> dict[str, list[int]]:
    """Every 16-entry palette in the generated headers, keyed by name."""
    palettes: dict[str, list[int]] = {}
    for header in HEADERS:
        text = (root / header).read_text(encoding="utf-8")
        for name, bounds, body in PALETTE_RE.findall(text):
            values = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)\b", body)]
            if not values:
                continue            # a Color mapping table, not a palette
            # The LAST bound is the palette's own length. The outer one, where
            # there is one, may be a named constant rather than a literal
            # (PEDESTRIAN_PALETTES is [kPedestrianTintCount][16]), so the
            # number of palettes is counted from the data instead.
            literals = re.findall(r"\[(\d+)\]", bounds)
            if not literals:
                raise ValueError(f"{name}: no literal palette length in "
                                 f"{bounds!r}")
            size = int(literals[-1])
            # C++ zero-fills the tail of a short initialiser -- the vehicle
            # palette declares 16 and lists 15 -- so padding up to a whole
            # palette is what the compiler does, not a guess.
            if len(values) % size:
                values += [0] * (size - len(values) % size)
            for offset in range(0, len(values), size):
                key = (name if len(values) == size
                       else f"{name}[{offset // size}]")
                palettes[key] = values[offset : offset + size]
    return palettes


def tint(color: int, light: tuple[int, int, int]) -> int:
    """The same transform as daynight::tint in src/game/rules/DayNight.cpp."""
    if color == 0:
        return 0

    def scale(value: int, multiplier: int) -> int:
        return (value * multiplier + 127) // 255

    red = scale((color >> 11) & 0x1F, light[0])
    green = scale((color >> 5) & 0x3F, light[1])
    blue = scale(color & 0x1F, light[2])
    return ((red << 11) | (green << 5) | blue) or 1


def rgb444(color: int) -> int:
    """What the panel actually receives with COLMOD 0x03: the top four bits."""
    return (((color >> 12) & 0xF) << 8) | (((color >> 7) & 0xF) << 4) | ((color >> 1) & 0xF)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--ambient",
        default="255,255,255",
        help="per-channel multiplier 'r,g,b', 0..255. Default is daylight.",
    )
    args = parser.parse_args()

    light = tuple(int(part) for part in args.ambient.split(","))
    if len(light) != 3 or not all(0 <= channel <= 255 for channel in light):
        parser.error("--ambient takes three values in 0..255")

    root = pathlib.Path(__file__).resolve().parent.parent
    palettes = load_palettes(root)

    collisions = 0
    for name, entries in sorted(palettes.items()):
        # Entry 0 is the transparent index and duplicate entries are the art's
        # own choice, so only DISTINCT visible colours can collide.
        seen: dict[int, int] = {}
        for color in sorted({c for c in entries if c}):
            packed = rgb444(tint(color, light))
            if packed in seen:
                collisions += 1
                print(f"{name}: 0x{seen[packed]:04X} and 0x{color:04X} both send 0x{packed:03X}")
            else:
                seen[packed] = color

    print(f"\n{len(palettes)} palettes under ambient {light}: {collisions} collisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
