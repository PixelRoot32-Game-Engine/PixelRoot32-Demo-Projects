#!/usr/bin/env python3
"""Original artwork for the Minesweeper demo.

Every grid in this file is original work made for this repository and is
released with it under CC0 1.0 (see the demo README's License section). Nothing
here is traced, copied or recoloured from another game or sprite sheet: the
tiles are built from a plain bevel rule and the glyphs are drawn on a small
pixel font that lives below.

This module is the human-editable source of truth. `generate_sprites.py` reads
it, validates every grid and emits the 4bpp headers in ``src/assets/``.

Characters
----------
=========  =========  ==========================================================
Character  Nibble     Meaning
=========  =========  ==========================================================
``L``      4          light ink - raised edges, faces, digits on the HUD band
``m``      3          mid ink - tile bodies, panel interiors
``D``      2          dark ink - recessed edges, digits on an opened cell
``K``      1          black ink - shadows, flag/mine/face features
``.``      0          transparent - the 4bpp sentinel, never drawn
=========  =========  ==========================================================

A ``#`` appears only inside glyph masks (the flag, the mine, the faces, the
pixel fonts): it marks a pixel the helper paints with the ink it is given, so
the masks stay readable next to the tiles they land on.

Families
--------

``BOARD_TILES``
16x16 board cells. Every one is emitted at 16, 12 and 8 px, because a 4bpp
sprite cannot be scaled at draw time.

``CURSOR_FRAMES``
16x16 selection frames. Only ``L`` is opaque; the interior is transparent so
the cell shows through. Phase A and phase B alternate the dash pattern for the
animation.

``SMILEYS``
16x16 state faces: light disc, black features, transparent outside the disc so
the header band shows through.

``DIGITS`` / ``LETTERS``
8x8 glyphs, light ink only, transparent background. The HUD band is a drawn
rectangle, so the glyph background must not be baked in.

``PANEL_PIECES``
8x8 9-slice pieces: light on the top/left edges, black on the bottom/right,
mid body. The centre of a panel is a plain fill and has no piece.

Sizes
-----
``BOARD_TILE_SIZES`` lists the three cell edges the board needs (16/12/8 px,
one per difficulty). ``GLYPH_SIZE`` is the 8 px edge of every HUD glyph and
panel piece.
"""

INKS = {"L": 4, "m": 3, "D": 2, "K": 1}

# The four-tone green ramp the demo renders with. Kept as plain RGB so the
# generator can emit the RGB565 table without reading a palette file.
INK_RGB = {
    "L": (0xC6, 0xD1, 0xA3),
    "m": (0x8D, 0x97, 0x6D),
    "D": (0x4D, 0x53, 0x3C),
    "K": (0x1F, 0x1F, 0x1F),
}

# Pixel value that means "leave the framebuffer alone".
TRANSPARENT = "."

# Edge lengths the board cells are emitted at. 4bpp has no draw-time scaling,
# so each difficulty picks one of these instead.
BOARD_TILE_SIZES = (16, 12, 8)

# Size of every HUD glyph and 9-slice piece.
GLYPH_SIZE = 8


# --------------------------------------------------------------------------- art
# Small helpers. The board is built from a bevel rule instead of 14 hand-written
# 16x16 grids: the rule is easier to keep consistent, and every tile still ends
# up as an explicit character grid the generator validates.

TILE = 16
BEVEL = 2


def _tile(cell):
    """Builds a 16x16 grid from a per-pixel callable."""
    return ["".join(cell(x, y) for x in range(TILE)) for y in range(TILE)]


def _covered():
    """Raised cell: light bevel on the top/left, black shadow on the bottom/right."""
    def cell(x, y):
        if x >= TILE - BEVEL or y >= TILE - BEVEL:
            return "K"
        if x < BEVEL or y < BEVEL:
            return "L"
        return "m"
    return _tile(cell)


def _opened():
    """Recessed cell: dark bevel on the top/left, light lip on the bottom/right."""
    def cell(x, y):
        if x >= TILE - BEVEL or y >= TILE - BEVEL:
            return "L"
        if x < BEVEL or y < BEVEL:
            return "D"
        return "m"
    return _tile(cell)


def _paint(base, mask, x0, y0, ink):
    """Paints a mask onto a copy of a grid at (x0, y0).

    A ``#`` takes the ink passed in; a mask pixel that already names an ink
    (``L``/``m``/``D``/``K``) keeps it, which is how the mine keeps its
    highlight without a second overlay pass.
    """
    out = [list(row) for row in base]
    for gy, row in enumerate(mask):
        for gx, ch in enumerate(row):
            if ch not in INKS and ch != "#":
                continue
            x, y = x0 + gx, y0 + gy
            if 0 <= x < len(out[0]) and 0 <= y < len(out):
                out[y][x] = ink if ch == "#" else ch
    return ["".join(row) for row in out]


# --- 16 px cell digits (7x9, one-pixel strokes) ------------------------------
# Drawn on an opened cell in dark ink. The 8 px board does not resample these:
# it composes the 8x8 HUD glyphs instead, so this font only has to read at 16
# and 12 px.

CELL_DIGITS = {
    "1": [
        "...#...",
        "..##...",
        ".#.#...",
        "...#...",
        "...#...",
        "...#...",
        "...#...",
        "...#...",
        ".#####.",
    ],
    "2": [
        ".#####.",
        "#.....#",
        "......#",
        ".....#.",
        "....#..",
        "...#...",
        "..#....",
        ".#.....",
        "#######",
    ],
    "3": [
        ".#####.",
        "#.....#",
        "......#",
        "......#",
        "..####.",
        "......#",
        "......#",
        "#.....#",
        ".#####.",
    ],
    "4": [
        ".....#.",
        "....##.",
        "...#.#.",
        "..#..#.",
        ".#...#.",
        "#######",
        ".....#.",
        ".....#.",
        ".....#.",
    ],
    "5": [
        "#######",
        "#......",
        "#......",
        "######.",
        "......#",
        "......#",
        "......#",
        "#.....#",
        ".#####.",
    ],
    "6": [
        "..####.",
        ".#.....",
        "#......",
        "#......",
        "######.",
        "#.....#",
        "#.....#",
        "#.....#",
        ".#####.",
    ],
    "7": [
        "#######",
        "......#",
        ".....#.",
        ".....#.",
        "....#..",
        "...#...",
        "...#...",
        "..#....",
        "..#....",
    ],
    "8": [
        ".#####.",
        "#.....#",
        "#.....#",
        "#.....#",
        ".#####.",
        "#.....#",
        "#.....#",
        "#.....#",
        ".#####.",
    ],
}

# --- 8 px HUD font (6x7, one-pixel strokes) ----------------------------------
# Placed at (1, 0) inside the 8x8 cell. Used by the MINES/TIME counters and,
# recoloured to dark ink, by the EXPERT opened-number cells.

HUD_DIGITS = {
    "0": [
        ".####.",
        "#....#",
        "#....#",
        "#....#",
        "#....#",
        "#....#",
        ".####.",
    ],
    "1": [
        "..#...",
        ".##...",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
        ".####.",
    ],
    "2": [
        ".####.",
        "#....#",
        ".....#",
        "....#.",
        "...#..",
        "..#...",
        "######",
    ],
    "3": [
        ".####.",
        "#....#",
        ".....#",
        "..###.",
        ".....#",
        "#....#",
        ".####.",
    ],
    "4": [
        "...#..",
        "..##..",
        ".#.#..",
        "#..#..",
        "######",
        "...#..",
        "...#..",
    ],
    "5": [
        "######",
        "#.....",
        "#.....",
        "#####.",
        ".....#",
        "#....#",
        ".####.",
    ],
    "6": [
        "..###.",
        ".#....",
        "#.....",
        "#####.",
        "#....#",
        "#....#",
        ".####.",
    ],
    "7": [
        "######",
        ".....#",
        "....#.",
        "...#..",
        "..#...",
        "..#...",
        "..#...",
    ],
    "8": [
        ".####.",
        "#....#",
        "#....#",
        ".####.",
        "#....#",
        "#....#",
        ".####.",
    ],
    "9": [
        ".####.",
        "#....#",
        "#....#",
        ".#####",
        ".....#",
        "....#.",
        "..##..",
    ],
}

HUD_LETTERS = {
    "M": [
        "#....#",
        "##..##",
        "#.##.#",
        "#....#",
        "#....#",
        "#....#",
        "#....#",
    ],
    "I": [
        ".####.",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
        ".####.",
    ],
    "N": [
        "#....#",
        "##...#",
        "#.#..#",
        "#..#.#",
        "#...##",
        "#....#",
        "#....#",
    ],
    "E": [
        "######",
        "#.....",
        "#.....",
        "#####.",
        "#.....",
        "#.....",
        "######",
    ],
    "S": [
        ".####.",
        "#....#",
        "#.....",
        ".####.",
        ".....#",
        "#....#",
        ".####.",
    ],
    "T": [
        "######",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
        "..#...",
    ],
}


def _glyph8(mask):
    """Centres a 6x7 mask in an 8x8 transparent cell."""
    blank = [["."] * GLYPH_SIZE for _ in range(GLYPH_SIZE)]
    for gy, row in enumerate(mask):
        for gx, ch in enumerate(row):
            if ch == "#":
                blank[gy][gx + 1] = "L"
    return ["".join(row) for row in blank]


# --- distinctive masks --------------------------------------------------------

FLAG_MASK = [
    "................",
    "................",
    "................",
    "....#######.....",
    "....#######.....",
    "....#######.....",
    ".....######.....",
    "......#####.....",
    "..........#.....",
    "..........#.....",
    "..........#.....",
    "..........#.....",
    ".......######...",
    "................",
    "................",
    "................",
]

QUESTION_MASK = [
    "................",
    "................",
    "................",
    "................",
    "....#####.......",
    "...#.....#......",
    ".........#......",
    "........#.......",
    ".......#........",
    ".......#........",
    "................",
    ".......#........",
    "................",
    "................",
    "................",
    "................",
]

MINE_MASK = [
    "................",
    "................",
    ".......##.......",
    "...#...##...#...",
    "....#..##..#....",
    ".....######.....",
    "....########....",
    "...##LL######...",
    "...##LL######...",
    "....########....",
    ".....######.....",
    "....#..##..#....",
    "...#...##...#...",
    ".......##.......",
    "................",
    "................",
]

EXPLOSION_MASK = [
    "................",
    "................",
    ".......##.......",
    "..#....##....#..",
    "...##..##..##...",
    "....########....",
    "..############..",
    ".#.###LLLL###.#.",
    ".#.###LLLL###.#.",
    "..############..",
    "....########....",
    "...##..##..##...",
    "..#....##....#..",
    ".......##.......",
    "................",
    "................",
]

# --- faces --------------------------------------------------------------------


def _face(features):
    """Light disc with transparent corners and black feature pixels."""
    rows = []
    for y in range(TILE):
        row = []
        for x in range(TILE):
            dx, dy = x - 7.5, y - 7.5
            row.append("L" if dx * dx + dy * dy <= 51.84 else ".")  # radius 7.2
        rows.append(row)
    for fy, frow in enumerate(features):
        for fx, ch in enumerate(frow):
            if ch == "#":
                rows[fy][fx] = "K"
    return ["".join(row) for row in rows]


SMILEY_FEATURES = {
    "SMILEY_NORMAL": [
        "................",
        "................",
        "................",
        "................",
        "................",
        "....##....##....",
        "....##....##....",
        "................",
        "................",
        "...#........#...",
        "....#......#....",
        ".....#....#.....",
        "......####......",
        "................",
        "................",
        "................",
    ],
    "SMILEY_COOL": [
        "................",
        "................",
        "................",
        "................",
        "................",
        "..############..",
        "..##..####..##..",
        "..##..####..##..",
        "................",
        "...#........#...",
        "....#......#....",
        ".....#....#.....",
        "......####......",
        "................",
        "................",
        "................",
    ],
    "SMILEY_SURPRISED": [
        "................",
        "................",
        "................",
        "................",
        "................",
        "....##....##....",
        "....##....##....",
        "................",
        "................",
        "......####......",
        ".....#....#.....",
        ".....#....#.....",
        "......####......",
        "................",
        "................",
        "................",
    ],
    "SMILEY_DEAD": [
        "................",
        "................",
        "................",
        "................",
        "...#.#....#.#...",
        "....#......#....",
        "...#.#....#.#...",
        "................",
        "................",
        "......####......",
        ".....#....#.....",
        "....#......#....",
        "................",
        "................",
        "................",
        "................",
    ],
}


# --- cursor frames ------------------------------------------------------------


def _cursor(phase):
    """Dashed border frame; the interior stays transparent.

    ``phase`` 0 starts the top/left edges on a dash and the bottom/right on a
    gap; phase 1 swaps them, which is what animates the selector. The generator
    rebuilds the frame inset onto the tile body and reads these corner phases
    from the grid, so the border pattern is the part that matters here.
    """
    rows = [["."] * TILE for _ in range(TILE)]
    for i in range(TILE):
        if (i + phase * 2) % 4 < 2:
            rows[0][i] = "L"
            rows[i][0] = "L"
        if (i + (2 - phase * 2) % 4) % 4 < 2:
            rows[TILE - 1][i] = "L"
            rows[i][TILE - 1] = "L"
    return ["".join(row) for row in rows]


# --- 9-slice pieces -----------------------------------------------------------


def _panel(kind):
    """One 8x8 9-slice piece: light top/left, black bottom/right, mid body."""
    rows = []
    for y in range(GLYPH_SIZE):
        row = []
        for x in range(GLYPH_SIZE):
            light = (kind in ("TL", "T", "TR") and y == 0) or (
                kind in ("TL", "L", "BL") and x == 0
            )
            dark = (kind in ("BL", "B", "BR") and y == GLYPH_SIZE - 1) or (
                kind in ("TR", "R", "BR") and x == GLYPH_SIZE - 1
            )
            row.append("L" if light else "K" if dark else "m")
        rows.append("".join(row))
    return rows


# ---------------------------------------------------------------------------
# The grids the generator reads. Keys and sizes are part of its contract.
# ---------------------------------------------------------------------------

BOARD_TILES = {
    "COVERED": _covered(),
    "COVERED_FLAG": _paint(_covered(), FLAG_MASK, 0, 0, "K"),
    "COVERED_QUESTION": _paint(_covered(), QUESTION_MASK, 0, 0, "K"),
    "OPEN_0": _opened(),
    "OPEN_1": _paint(_opened(), CELL_DIGITS["1"], 4, 4, "K"),
    "OPEN_2": _paint(_opened(), CELL_DIGITS["2"], 4, 4, "K"),
    "OPEN_3": _paint(_opened(), CELL_DIGITS["3"], 4, 4, "K"),
    "OPEN_4": _paint(_opened(), CELL_DIGITS["4"], 4, 4, "K"),
    "OPEN_5": _paint(_opened(), CELL_DIGITS["5"], 4, 4, "K"),
    "OPEN_6": _paint(_opened(), CELL_DIGITS["6"], 4, 4, "K"),
    "OPEN_7": _paint(_opened(), CELL_DIGITS["7"], 4, 4, "K"),
    "OPEN_8": _paint(_opened(), CELL_DIGITS["8"], 4, 4, "K"),
    "MINE": _paint(_opened(), MINE_MASK, 0, 0, "K"),
    "EXPLOSION": _paint(_opened(), EXPLOSION_MASK, 0, 0, "K"),
}

CURSOR_FRAMES = {
    "CURSOR_A": _cursor(0),
    "CURSOR_B": _cursor(1),
}

SMILEYS = {name: _face(mask) for name, mask in SMILEY_FEATURES.items()}

DIGITS = {f"DIGIT_{key}": _glyph8(mask) for key, mask in HUD_DIGITS.items()}

LETTERS = {f"LETTER_{key}": _glyph8(mask) for key, mask in HUD_LETTERS.items()}

PANEL_PIECES = {
    "PANEL_TL": _panel("TL"),
    "PANEL_T": _panel("T"),
    "PANEL_TR": _panel("TR"),
    "PANEL_L": _panel("L"),
    "PANEL_R": _panel("R"),
    "PANEL_BL": _panel("BL"),
    "PANEL_B": _panel("B"),
    "PANEL_BR": _panel("BR"),
}
