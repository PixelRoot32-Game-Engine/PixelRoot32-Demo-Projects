#!/usr/bin/env python3
"""Render the starting position to a PNG, straight from the generator grids.

This exists because the art has to be reviewable without a build: it resolves
the same character grids and the same PR32 palette slots the firmware does, so
what it draws is what the board will look like. Standard library only - no
Pillow, no PlatformIO, no SDL2.

The board and the pieces are on separate engine palettes at runtime, but a
single table is enough here: this renders one palette at a time, which is what
you want when checking whether a palette is readable before flashing it.

Usage:
    python tools/preview_board.py board.png            # PR32, the default
    python tools/preview_board.py wood.png Wood        # any entry in PALETTES
"""

import sys, zlib, struct
sys.path.insert(0, "tools")
from generate_pieces import (PIECES, ORDER, SIZE, BOARD_SCALE, INDEX_FOR, scaled,
                             PALETTES, WHITE_PALETTE, BLACK_PALETTE,
                             LIGHT_SQUARE, DARK_SQUARE)

if len(sys.argv) < 2:
    raise SystemExit(__doc__)

NAME = sys.argv[2] if len(sys.argv) > 2 else "PR32"
if NAME not in PALETTES:
    raise SystemExit(f"unknown palette {NAME!r}; try one of {sorted(PALETTES)}")
PR32 = PALETTES[NAME]

# The palettes and the board colours come from the generator, so this preview
# cannot disagree with what the firmware draws. It used to keep its own copy,
# which is precisely how a piece colour and a board square drifted into being
# the same #CECECE without anything noticing.
def rgb(palette):
    return {index: PR32[slot] for index, slot in palette.items() if index != 0}

WHITE_PAL = rgb(WHITE_PALETTE)
BLACK_PAL = rgb(BLACK_PALETTE)
LIGHT_SQ, DARK_SQ = PR32[LIGHT_SQUARE], PR32[DARK_SQUARE]

CELL, N = 30, 8
W = H = CELL * N
fb = [[(0,0,0)] * W for _ in range(H)]

BACK = ["Rook","Knight","Bishop","Queen","King","Bishop","Knight","Rook"]
board = {}
for f, name in enumerate(BACK):
    board[(f,0)] = (name,"w"); board[(f,7)] = (name,"b")
for f in range(8):
    board[(f,1)] = ("Pawn","w"); board[(f,6)] = ("Pawn","b")

for rank in range(8):
    for file in range(8):
        x0, y0 = file*CELL, (7-rank)*CELL
        col = LIGHT_SQ if (file+rank) & 1 else DARK_SQ
        for y in range(y0, y0+CELL):
            for x in range(x0, x0+CELL):
                fb[y][x] = col
        if (file,rank) not in board: continue
        name, side = board[(file,rank)]
        pal = WHITE_PAL if side == "w" else BLACK_PAL
        grid = scaled(PIECES[name], BOARD_SCALE)
        for gy, row in enumerate(grid):
            for gx, ch in enumerate(row):
                idx = INDEX_FOR[ch]
                if idx == 0: continue
                fb[y0+1+gy][x0+1+gx] = pal[idx]

raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in fb)
def chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)
png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
open(sys.argv[1], "wb").write(png)
print(f"wrote {sys.argv[1]} ({W}x{H}, {NAME} palette)")
