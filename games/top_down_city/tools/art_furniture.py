"""Street furniture and beach props for the top-down city (palette slot FURNITURE).

Items-layer cut-outs on a transparent surround: street lamps, a traffic light,
a park bench, railings, a hydrant, a bin, a warning sign, a boulder, a fountain,
beach parasol / loungers / balls, and a 2x1 swimming-pool stamp. Cast shadows
are generated from each prop's silhouette (down-right offset, clipped to the
prop) and exported separately; shells and pebbles are Details decoration.
"""
from __future__ import annotations

import art_dsl

SLOT = "FURNITURE"

PALETTE = [
    ("OUTLINE",  0x1E, 0x1E, 0x1E),
    ("WHITE",    0xDE, 0xDE, 0xDE),
    ("LIGHT",    0xC9, 0xC7, 0xC5),
    ("CONCRETE", 0xA6, 0xA3, 0xA3),
    ("GREY",     0x6D, 0x6B, 0x6B),
    ("WOOD",     0x97, 0x61, 0x47),
    ("RED",      0xD7, 0x29, 0x29),
    ("AMBER",    0xD7, 0x91, 0x29),
    ("GREEN",    0x09, 0x90, 0x2B),
    ("POOL",     0x63, 0xDC, 0xD7),
    # The lamp's own light, and the only entry in this slot the daylight art
    # never draws. The day/night tint would darken any ordinary colour along
    # with the street, so the after-dark art paints the lamp head and its pool
    # of light in this entry and NightLights.h leaves it alone. It took the
    # slot RIPPLE had -- fourteen pixels of sparkle on the two pool tiles, now
    # drawn in LIGHT, which reads as sun on water at least as well.
    ("LAMP_LIT",  0xFF, 0xEC, 0xAA),
    ("WATER",    0x27, 0x95, 0xF2),
    ("SH_GRASS", 0x2E, 0x59, 0x28),
    ("SH_PAVE",  0x5C, 0x49, 0x40),
    ("SH_SAND",  0xF4, 0xAD, 0x48),
]

INK = {
    "K": "OUTLINE",
    "W": "WHITE",
    "L": "LIGHT",
    "C": "CONCRETE",
    "G": "GREY",
    "B": "WOOD",
    "R": "RED",
    "A": "AMBER",
    "E": "GREEN",
    "P": "POOL",
    "*": "LAMP_LIT",
    "U": "WATER",
    "g": "SH_GRASS",
    "p": "SH_PAVE",
    "s": "SH_SAND",
}


# --- helpers -----------------------------------------------------------------

def rot270(rows):
    """Rotate counter-clockwise (top edge ends up on the left)."""
    return art_dsl.rot90(art_dsl.rot90(art_dsl.rot90(rows)))


def shift(rows, dx):
    """Slide a tile horizontally by dx pixels, padding with transparency."""
    w = len(rows[0])
    out = []
    for row in rows:
        if dx >= 0:
            out.append(("." * dx + row)[:w])
        else:
            out.append((row[-dx:] + "." * (-dx))[:w])
    return out


def cast(rows, dx, dy, ch, spill=False):
    """Cast shadow of a prop: its silhouette offset by (dx, dy) in ink ``ch``,
    keeping only the pixels that stick out from under the prop itself. With
    ``spill`` the part that crosses into the cell to the right is returned
    instead (for a ``<name>_SPILL`` shadow tile)."""
    h, w = len(rows), len(rows[0])
    wide = [row + "." * w for row in rows]
    out = [["."] * (2 * w) for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if rows[y][x] == ".":
                continue
            nx, ny = x + dx, y + dy
            if 0 <= ny < h and 0 <= nx < 2 * w and wide[ny][nx] == ".":
                out[ny][nx] = ch
    lo, hi = (w, 2 * w) if spill else (0, w)
    return ["".join(r[lo:hi]) for r in out]


# --- street lamp: round grey head upper-left, pole running down-right --------

LAMP = [
    "................",
    "...KKK..........",
    "..KGGGK.........",
    ".KGWCCGK........",
    ".KGCCCGK........",
    ".KGCCCGK........",
    "..KGGGKK........",
    "...KKKGGK.......",
    ".....KGGGK......",
    "......KGGGK.....",
    ".......KGGK.....",
    "........KGK.....",
    "........KKK.....",
    "................",
    "................",
    "................",
]

# The same lamp after dark. Only the glass changes: the head's
# white-and-concrete lens is repainted in LAMP_LIT, which the tint leaves
# alone, so nine pixels stay bright while the street goes to a third of
# daylight. The pole, the ring and the outline darken with everything else,
# which is what stops a lit lamp reading as a lamp-shaped hole in the night.
LAMP_LIT_ROWS = [row.replace("W", "*").replace("C", "*") for row in LAMP]


def head_glow(rows, dx, dy, ch, last_row):
    """The pool of light a lamp throws, in the footprint its shadow had.

    Deliberately the shadow's own geometry rather than a ring around the
    lamp: the Details layer is drawn OVER Items, so a pool centred on the
    lamp would paint across the lamp itself. ``cast`` already refuses to
    write a pixel that sits under the prop, which makes the offset silhouette
    the one shape that is guaranteed not to cover it.

    Only the rows the HEAD casts into are kept -- past ``last_row`` the
    silhouette is the pole, and a pole does not glow.
    """
    full = cast(rows, dx, dy, ch)
    return [row if y <= last_row else "." * len(row)
            for y, row in enumerate(full)]


# --- traffic light: dark box with red / amber / green lenses, short pole -----

TRAFFIC = [
    "................",
    ".....KKKKKK.....",
    ".....KGGGGK.....",
    ".....KGRRGK.....",
    ".....KGRRGK.....",
    ".....KGAAGK.....",
    ".....KGAAGK.....",
    ".....KGEEGK.....",
    ".....KGEEGK.....",
    ".....KGGGGK.....",
    ".....KKKKKK.....",
    ".......KGK......",
    ".......KGK......",
    ".......KKK......",
    "................",
    "................",
]

# --- park bench: three wooden slats, dark underside -------------------------

BENCH = [
    "................",
    "................",
    "................",
    "................",
    "...KKKKKKKKKK...",
    "...KBBBBBBBBK...",
    "...KKKKKKKKKK...",
    "...KBBBBBBBBK...",
    "...KKKKKKKKKK...",
    "...KBBBBBBBBK...",
    "...KppppppppK...",
    "...KKKKKKKKKK...",
    "................",
    "................",
    "................",
    "................",
]

# --- railing: checker band, white rail, pickets, posts every 4 px -----------

FENCE_H = [
    "................",
    "................",
    "................",
    "KWKWKWKWKWKWKWKW",
    "WKWKWKWKWKWKWKWK",
    "WWWWWWWWWWWWWWWW",
    "KLKLKLKLKLKLKLKL",
    "KLKLKLKLKLKLKLKL",
    "KKKKKKKKKKKKKKKK",
    ".pK..pK..pK..pK.",
    ".pK..pK..pK..pK.",
    "................",
    "................",
    "................",
    "................",
    "................",
]

# --- hydrant: red body, white cap, side nozzles -----------------------------

HYDRANT = [
    "................",
    "................",
    "................",
    "................",
    "......KKK.......",
    ".....KWWWK......",
    "....KKRRRKK.....",
    "....KRRRRRK.....",
    "....KKRRRKK.....",
    ".....KRRRK......",
    ".....KKKKK......",
    "................",
    "................",
    "................",
    "................",
    "................",
]

# --- bin: dark green box, grey lid ------------------------------------------

BIN = [
    "................",
    "................",
    "................",
    "................",
    ".....KKKKKK.....",
    ".....KWCCCK.....",
    ".....KKKKKK.....",
    ".....KgEEgK.....",
    ".....KgEEgK.....",
    ".....KggggK.....",
    ".....KKKKKK.....",
    "................",
    "................",
    "................",
    "................",
    "................",
]

# --- warning sign: red-bordered white triangle on a grey pole ---------------

SIGN = [
    "................",
    "................",
    "................",
    ".......K........",
    "......KRK.......",
    ".....KRWRK......",
    "....KRWWWRK.....",
    "...KRRRRRRRK....",
    "...KKKKKKKKK....",
    "......KGK.......",
    "......KGK.......",
    "......KKK.......",
    "................",
    "................",
    "................",
    "................",
]

# --- boulder ----------------------------------------------------------------

ROCK = [
    "................",
    "................",
    "................",
    "................",
    "................",
    "......KKK.......",
    "....KKCCCK......",
    "...KCWCCCCK.....",
    "..KCCCCCCGK.....",
    "..KCCCCGGGK.....",
    "..KGGGGGGGK.....",
    "...KKKKKKK......",
    "................",
    "................",
    "................",
    "................",
]

# --- fountain: white rim, concrete ring, blue basin, light spray ------------

FOUNTAIN = [
    "................",
    ".....KKKKK......",
    "...KKWWWWWKK....",
    "..KWWWWWWWWWK...",
    "..KWWCCCCCWWK...",
    ".KWWCCUUUCCWWK..",
    ".KWWCUWUWUCWWK..",
    ".KWWCUUWUUCWWK..",
    ".KWWCUWUWUCWWK..",
    ".KWWCCUUUCCWWK..",
    "..KWWCCCCCWWK...",
    "..KWWWWWWWWWK...",
    "...KKWWWWWKK....",
    ".....KKKKK......",
    "................",
    "................",
]

# --- parasol: red / amber / white quadrants, light pole dot -----------------

PARASOL = [
    "................",
    "................",
    "....KKKK........",
    "..KKRRAAKK......",
    ".KRRRRAAAAK.....",
    ".KRRRRAAAAK.....",
    ".KRRRWWAAAK.....",
    ".KWWWWWRRRK.....",
    ".KWWWWRRRRK.....",
    ".KWWWWRRRRK.....",
    "..KKWWRRKK......",
    "....KKKK........",
    "................",
    "................",
    "................",
    "................",
]

# --- beach lounger: backrest strip on the left, striped canvas seat ---------

LOUNGER = [
    "................",
    "................",
    "................",
    "................",
    "..KK............",
    ".KGWK...........",
    ".KGWKKKKKKKK....",
    ".KGWLLLLLLLLK...",
    ".KGWLWLWLWLWK...",
    ".KGWLWLWLWLWK...",
    "..KGLLLLLLLLK...",
    "...KKKKKKKKKK...",
    "................",
    "................",
    "................",
    "................",
]

# --- beach balls: three-colour stripes -------------------------------------

BEACH_BALL = [
    "................",
    "................",
    "................",
    "................",
    ".....KKK........",
    "....KRWUK.......",
    "...KRRWUUK......",
    "...KRRWUUK......",
    "...KRRWUUK......",
    "....KRWUK.......",
    ".....KKK........",
    "................",
    "................",
    "................",
    "................",
    "................",
]

BEACH_BALL_B = [row.replace("R", "E").replace("U", "A") for row in BEACH_BALL]

# --- swimming pool (2x1 stamp): white deck, light inner rim, cyan water -----

POOL_ROWS = [
    "................................",
    "................................",
    "..KKKKKKKKKKKKKKKKKKKKKKKKKKKK..",
    "..KWWWWWWWWWWWWWWWWWWWWWWWWWWK..",
    "..KWLLLLLLLLLLLLLLLLLLLLLLLLWK..",
    "..KWLPPPPPPPPPPPPPPPPPPPPPPLWK..",
    "..KWLPPLLLPPPPPPPPPPPLLLPPPLWK..",
    "..KWLPPPPPPPPPPPPPPPPPPPPPPLWK..",
    "..KWLPPPPPPPLLLPPPPPPPPPPPPLWK..",
    "..KWLPPPPPPPPPPPPPPPPPPPPPPLWK..",
    "..KWLPPPLLPPPPPPPPPPPLLLPPPLWK..",
    "..KWLPPPPPPPPPPPPPPPPPPPPPPLWK..",
    "..KWLLLLLLLLLLLLLLLLLLLLLLLLWK..",
    "..KWWWWWWWWWWWWWWWWWWWWWWWWWWK..",
    "..KKKKKKKKLLLKKKKKKKKKKKKKKKKK..",
    "................................",
]

# --- details: shells and pebbles -------------------------------------------

SHELLS = [
    "................",
    "................",
    "..KK............",
    ".KAWK...........",
    "..KK............",
    "................",
    "........KK......",
    ".......KWAK.....",
    "........KK......",
    "................",
    "................",
    "....KK..........",
    "...KAWK.........",
    "....KK..........",
    "................",
    "................",
]

PEBBLES = [
    "................",
    "................",
    "...CG...........",
    "...GG...........",
    "................",
    "..........CG....",
    "..........GG....",
    "................",
    "................",
    ".....CG.........",
    ".....GG.........",
    "................",
    "............CG..",
    "............GG..",
    "................",
    "................",
]


# --- exports ----------------------------------------------------------------

TILES = {
    "LAMP_G":       LAMP,
    "LAMP_P":       LAMP,
    "TRAFFIC":      TRAFFIC,
    "BENCH":        BENCH,
    "BENCH_P":      BENCH,
    "FENCE_H":      FENCE_H,
    "FENCE_V":      rot270(FENCE_H),
    "HYDRANT":      HYDRANT,
    "BIN":          BIN,
    "SIGN":         SIGN,
    "ROCK":         ROCK,
    "FOUNTAIN":     FOUNTAIN,
    "FOUNTAIN_G":   FOUNTAIN,
    "PARASOL":      PARASOL,
    "LOUNGER":      LOUNGER,
    "LOUNGER_B":    shift(art_dsl.hflip(LOUNGER), -2),
    "BEACH_BALL":   BEACH_BALL,
    "BEACH_BALL_B": BEACH_BALL_B,
}

STAMPS = {
    "POOL": (2, 1, POOL_ROWS),
}

SHADOWS = {
    "LAMP_G":       cast(LAMP, 4, 2, "g"),
    "LAMP_P":       cast(LAMP, 4, 2, "p"),
    "TRAFFIC":      cast(TRAFFIC, 4, 2, "p"),
    "BENCH":        cast(BENCH, 3, 1, "g"),
    "BENCH_P":      cast(BENCH, 3, 1, "C"),
    "HYDRANT":      cast(HYDRANT, 3, 1, "p"),
    "BIN":          cast(BIN, 3, 1, "p"),
    "ROCK":         cast(ROCK, 3, 1, "s"),
    "FOUNTAIN":     cast(FOUNTAIN, 3, 1, "p"),
    "FOUNTAIN_SPILL": cast(FOUNTAIN, 3, 1, "p", spill=True),
    "FOUNTAIN_G":   cast(FOUNTAIN, 3, 1, "g"),
    "FOUNTAIN_G_SPILL": cast(FOUNTAIN, 3, 1, "g", spill=True),
    "PARASOL":      cast(PARASOL, 4, 2, "s"),
    "LOUNGER":      cast(TILES["LOUNGER"], 3, 1, "s"),
    "LOUNGER_B":    cast(TILES["LOUNGER_B"], 3, 1, "s"),
    "BEACH_BALL":   cast(BEACH_BALL, 3, 1, "s"),
    "BEACH_BALL_B": cast(BEACH_BALL_B, 3, 1, "s"),
}

DETAILS = {
    "SHELLS":  SHELLS,
    "PEBBLES": PEBBLES,
}

# The entry a lit thing in this slot is painted in, and the one entry here the
# daylight art never draws.
LIT = "LAMP_LIT"

# After dark, keyed by the name the tile carries in the tileset. A lamp lights
# its lens and stops casting a shadow, because the thing that was casting it
# is now the brightest object in the cell.
NIGHT = {
    "LAMP_G":    LAMP_LIT_ROWS,
    "LAMP_P":    LAMP_LIT_ROWS,
    "LAMP_G_SH": head_glow(LAMP, 4, 2, "*", 8),
    "LAMP_P_SH": head_glow(LAMP, 4, 2, "*", 8),
}

FAMILY = {
    "LAMP_G":       "grass",
    "LAMP_P":       "pave",
    "TRAFFIC":      "any",
    "BENCH":        "grass",
    "BENCH_P":      "pave",
    "FENCE_H":      "any",
    "FENCE_V":      "any",
    "HYDRANT":      "any",
    "BIN":          "any",
    "SIGN":         "any",
    "ROCK":         "sand",
    "FOUNTAIN":     "pave",
    "FOUNTAIN_G":   "grass",
    "PARASOL":      "sand",
    "LOUNGER":      "sand",
    "LOUNGER_B":    "sand",
    "BEACH_BALL":   "sand",
    "BEACH_BALL_B": "sand",
    "POOL":         "grass",
    "SHELLS":       "sand",
    "PEBBLES":      "sand",
}
