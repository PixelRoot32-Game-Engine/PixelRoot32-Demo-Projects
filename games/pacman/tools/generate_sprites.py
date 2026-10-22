#!/usr/bin/env python3
"""Generate the Pac-Man clone's 1bpp sprite tables.

The demo ships original artwork. This script is the source of that artwork:
the shapes are written here as ASCII art, rotated where a rotated copy is
needed, and emitted as the engine's packed 1bpp row format. Re-run it after
editing the art:

    python tools/generate_sprites.py

Bit order
---------
The engine's ``Renderer::drawSprite`` walks a row from its most significant
bit to its least significant bit and paints column 0 first, so the most
significant bit of a row is the LEFTMOST pixel. The tables below are emitted
with that convention. ``test/unit/test_pacman_sprite_convention`` locks the
convention down so a future edit cannot silently mirror every sprite.
"""

from __future__ import annotations

import os
from typing import Dict, List

SPRITE_SIZE = 8

# --- Artwork -----------------------------------------------------------------
# Original artwork, created from scratch for this demo. Not derived from any
# copyrighted game artwork; free to use and redistribute.

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
    "#..#..#.",
]

GHOST_SCARED_BODY = [
    "..####..",
    ".######.",
    "########",
    "########",
    "########",
    "#.#..#.#",
    "##.##.##",
    "#..#..#.",
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


def rotate_cw(grid: List[str]) -> List[str]:
    """Rotates a square grid 90 degrees clockwise."""
    n = len(grid)
    return ["".join(grid[n - 1 - c][r] for c in range(n)) for r in range(n)]


def rotate_ccw(grid: List[str]) -> List[str]:
    """Rotates a square grid 90 degrees counter-clockwise."""
    n = len(grid)
    return ["".join(grid[c][n - 1 - r] for c in range(n)) for r in range(n)]


def pack(grid: List[str]) -> List[int]:
    """Packs ASCII art into the engine's 1bpp rows, most significant bit first."""
    if len(grid) != SPRITE_SIZE:
        raise ValueError(f"sprite must be {SPRITE_SIZE} rows tall")
    rows: List[int] = []
    for line in grid:
        if len(line) != SPRITE_SIZE:
            raise ValueError(f"sprite row must be {SPRITE_SIZE} characters wide: {line!r}")
        value = 0
        for column, ch in enumerate(line):
            if ch not in (".", "#"):
                raise ValueError(f"unexpected character {ch!r} in sprite art")
            if ch == "#":
                value |= 1 << (SPRITE_SIZE - 1 - column)
        rows.append(value)
    return rows


def art_comment(name: str, grid: List[str]) -> str:
    body = "\n".join(f" *     {line}" for line in grid)
    return f" * {name}:\n{body}\n *\n"


def emit(name: str, grid: List[str], note: str, out: List[str]) -> None:
    rows = pack(grid)
    out.append(f"/**\n * @brief {note}\n *\n{art_comment(name, grid)} * Packed rows, most significant bit = leftmost pixel.\n */")
    values = ", ".join(f"0x{value:04X}" for value in rows)
    out.append(f"static const uint16_t {name}[{SPRITE_SIZE}] = {{\n    {values}\n}};")
    out.append(
        f"static const pixelroot32::graphics::Sprite {name}_SPRITE = {{ {name}, {SPRITE_SIZE}, {SPRITE_SIZE} }};"
    )
    out.append("")


def main() -> None:
    sprites: Dict[str, List[str]] = {
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

    notes = {
        "PLAYER_RIGHT_OPEN": "Player, mouth open, facing right. Flipped horizontally for the left-facing frame.",
        "PLAYER_RIGHT_CLOSED": "Player, mouth closed, facing right.",
        "PLAYER_UP_OPEN": "Player, mouth open, facing up.",
        "PLAYER_UP_CLOSED": "Player, mouth closed, facing up.",
        "PLAYER_DOWN_OPEN": "Player, mouth open, facing down.",
        "PLAYER_DOWN_CLOSED": "Player, mouth closed, facing down.",
        "GHOST_BODY": "Enemy body in its normal colour.",
        "GHOST_SCARED_BODY": "Enemy body while the power effect is active.",
        "GHOST_EYES": "Enemy eyes, drawn in white on top of the body.",
        "GHOST_SCARED_EYES": "Enemy eyes while the power effect is active.",
    }

    out: List[str] = []
    out.append("// Generated by tools/generate_sprites.py -- do not edit by hand.")
    out.append("//")
    out.append("// Original CC0 pixel art created from scratch for the PixelRoot32 Pac-Man")
    out.append("// clone demo. Not derived from any copyrighted game artwork; free to use and")
    out.append("// redistribute.")
    out.append("")
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("")
    out.append('#include "graphics/Renderer.h"')
    out.append("")
    out.append("namespace pacman {")
    out.append("")
    out.append("namespace sprites {")
    out.append("")

    for name, grid in sprites.items():
        emit(name, grid, notes[name], out)

    out.append("}  // namespace sprites")
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
