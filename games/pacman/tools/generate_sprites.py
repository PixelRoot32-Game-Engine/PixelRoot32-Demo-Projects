#!/usr/bin/env python3
"""Generate the Pac-Man clone's 1bpp sprite tables.

The demo ships original artwork. This script is the source of that artwork:
the shapes are written here as ASCII art at eight pixels, resampled to the
cell size the build uses, and emitted as the engine's packed 1bpp row format.

Both sizes are emitted because the level is 28 cells wide and 31 cells tall:
a 240x320 screen holds it at the arcade's own eight pixels, and a 240x240
screen has to make do with seven. Re-run it after editing the art:

    python tools/generate_sprites.py

Bit order
---------
The engine's ``Renderer::drawSprite`` walks a row from its most significant
bit to its least significant bit and paints column 0 first, so the most
significant bit of a row is the LEFTMOST pixel. The tables below are emitted
with that convention.
"""

from __future__ import annotations

import os
from typing import Dict, List, Tuple

ART_SIZE = 8
SIZES = (7, 8)

# --- Artwork ----------------------------------------------------------------
# Original artwork, created from scratch for this demo. Not derived from any
# copyrighted game's assets; free to use and redistribute.

PLAYER_RIGHT_OPEN = [
    "........",
    "..####..",
    ".######.",
    "####....",
    "####....",
    ".######.",
    "..####..",
    "........",
]

PLAYER_RIGHT_CLOSED = [
    "........",
    "..####..",
    ".######.",
    "#######.",
    "#######.",
    ".######.",
    "..####..",
    "........",
]

GHOST_BODY = [
    "..####..",
    ".######.",
    "########",
    "########",
    "########",
    "########",
    "##.##.##",
    "#.#..#.#",
]

GHOST_SCARED_BODY = [
    "..####..",
    ".######.",
    "########",
    "########",
    "########",
    "#.#..#.#",
    "##.##.##",
    "#.#..#.#",
]

GHOST_EYES = [
    "........",
    "........",
    ".##..##.",
    ".##..##.",
    "........",
    "........",
    "........",
    "........",
]

GHOST_SCARED_EYES = [
    "........",
    "........",
    "..#..#..",
    "..#..#..",
    "........",
    "........",
    "........",
    "........",
]


def rotate_cw(art: List[str]) -> List[str]:
    """Rotates a square grid 90 degrees clockwise."""
    size = len(art)
    return ["".join(art[size - 1 - column][row] for column in range(size)) for row in range(size)]


def rotate_ccw(art: List[str]) -> List[str]:
    """Rotates a square grid 90 degrees counter-clockwise."""
    size = len(art)
    return ["".join(art[column][size - 1 - row] for column in range(size)) for row in range(size)]


def resample(art: List[str], size: int) -> List[str]:
    """Resamples square ASCII art to `size` pixels, sampling the centre of
    every destination cell so the shape keeps its proportions."""
    if size == ART_SIZE:
        return list(art)
    out = []
    for y in range(size):
        row = []
        for x in range(size):
            source_x = (x * ART_SIZE + ART_SIZE // 2) // size
            source_y = (y * ART_SIZE + ART_SIZE // 2) // size
            row.append(art[source_y][source_x])
        out.append("".join(row))
    return out


def pack(art: List[str]) -> List[int]:
    """Packs ASCII art into the engine's 1bpp rows, most significant bit first."""
    width = len(art[0])
    rows: List[int] = []
    for line in art:
        if len(line) != width:
            raise ValueError(f"sprite row must be {width} characters wide: {line!r}")
        value = 0
        for column, ch in enumerate(line):
            if ch not in (".", "#"):
                raise ValueError(f"unexpected character {ch!r} in sprite art")
            if ch == "#":
                value |= 1 << (width - 1 - column)
        rows.append(value)
    return rows


def art_comment(art: List[str]) -> str:
    body = "\n".join(f" *     {line}" for line in art)
    return f" *\n{body}\n *\n"


def emit(name: str, art: List[str], note: str, out: List[str]) -> None:
    rows = pack(art)
    out.append(
        f"/**\n * @brief {note}\n *\n{art_comment(art)}"
        " * Packed rows, most significant bit = leftmost pixel.\n */"
    )
    values = ", ".join(f"0x{value:04X}" for value in rows)
    size = len(art[0])
    out.append(f"static const uint16_t {name}[{size}] = {{\n    {values}\n}};")
    out.append(
        f"static const pixelroot32::graphics::Sprite {name}_SPRITE = {{ {name}, {size}, {size} }};"
    )
    out.append("")


def main() -> None:
    art: Dict[str, List[str]] = {
        "PLAYER_RIGHT_OPEN": PLAYER_RIGHT_OPEN,
        "PLAYER_RIGHT_CLOSED": PLAYER_RIGHT_CLOSED,
        "PLAYER_UP_OPEN": rotate_ccw(PLAYER_RIGHT_OPEN),
        "PLAYER_UP_CLOSED": rotate_ccw(PLAYER_RIGHT_CLOSED),
        "PLAYER_DOWN_OPEN": rotate_cw(PLAYER_RIGHT_OPEN),
        "PLAYER_DOWN_CLOSED": rotate_cw(PLAYER_RIGHT_CLOSED),
        "GHOST_BODY": GHOST_BODY,
        "GHOST_SCARED_BODY": GHOST_SCARED_BODY,
        "GHOST_EYES": GHOST_EYES,
        "GHOST_SCARED_EYES": GHOST_SCARED_EYES,
    }
    notes: Dict[str, str] = {
        "PLAYER_RIGHT_OPEN": "Player, mouth open, facing right. Flipped horizontally for the left-facing frame.",
        "PLAYER_RIGHT_CLOSED": "Player, mouth closed, facing right.",
        "PLAYER_UP_OPEN": "Player, mouth open, facing up.",
        "PLAYER_UP_CLOSED": "Player, mouth closed, facing up.",
        "PLAYER_DOWN_OPEN": "Player, mouth open, facing down.",
        "PLAYER_DOWN_CLOSED": "Player, mouth closed, facing down.",
        "GHOST_BODY": "Hunter body in its normal colour.",
        "GHOST_SCARED_BODY": "Hunter body while the power effect is active.",
        "GHOST_EYES": "Hunter eyes, drawn in white on top of the body.",
        "GHOST_SCARED_EYES": "Hunter eyes while the power effect is active.",
    }

    out: List[str] = []
    out.append("// Generated by tools/generate_sprites.py -- do not edit by hand.")
    out.append("//")
    out.append("// Original CC0 pixel art created from scratch for the PixelRoot32 Pac-Man")
    out.append("// clone demo. Not derived from any copyrighted game's assets; free to use and")
    out.append("// redistribute.")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append('#include "graphics/Renderer.h"')
    out.append("")
    out.append("namespace pacman {")

    for size in SIZES:
        out.append(f"namespace sprites{size} {{")
        out.append("")
        for name, drawing in art.items():
            emit(name, resample(drawing, size), notes[name], out)
        out.append("}  // namespace sprites%d" % size)
        out.append("")

    out.append("}  // namespace pacman")
    out.append("")

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    target = os.path.join(root, "src", "assets", "PacmanSprites.h")
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(target, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(out))
    print(f"wrote {target}")


if __name__ == "__main__":
    main()
