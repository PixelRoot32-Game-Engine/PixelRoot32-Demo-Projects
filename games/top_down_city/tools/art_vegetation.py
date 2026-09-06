"""Vegetation props for the top-down city: round trees, bushes, hedges, palms
seen from above and a flower bed, plus their cast shadows and two grass
decoration tiles.

Every prop is a cut-out on a transparent surround (the ground tile shows
through). Shadows are separate tiles drawn on the Details layer: one flat
colour, the prop's own silhouette pushed down-right, with the part hidden
under the prop carved away so the layer order does not matter.
"""
from __future__ import annotations

import art_dsl

SLOT = "VEGETATION"

PALETTE = [
    ("OUTLINE",      0x1E, 0x1E, 0x1E),
    ("WHITE",        0xDE, 0xDE, 0xDE),
    ("RED",          0xD7, 0x29, 0x29),
    ("LEAF_LIGHT",   0x15, 0xCD, 0x16),
    ("GRASS_LIGHT",  0x2B, 0xAA, 0x1C),
    ("LEAF_MID",     0x14, 0x9F, 0x15),
    ("LEAF_DARK",    0x14, 0x77, 0x15),
    ("LEAF_DEEP",    0x14, 0x59, 0x14),
    ("PALM_LIGHT",   0x0B, 0xB2, 0x36),
    ("PALM",         0x09, 0x90, 0x2B),
    ("PALM_DARK",    0x07, 0x6B, 0x1F),
    ("TRUNK",        0x65, 0x3C, 0x18),
    ("SHADOW_GRASS", 0x2E, 0x59, 0x28),
    ("SHADOW_PAVE",  0x5C, 0x49, 0x40),
    ("SHADOW_SAND",  0xF4, 0xAD, 0x48),
]

INK = {
    "#": "OUTLINE",
    "w": "WHITE",
    "r": "RED",
    "L": "LEAF_LIGHT",
    "l": "GRASS_LIGHT",
    "m": "LEAF_MID",
    "d": "LEAF_DARK",
    "k": "LEAF_DEEP",
    "P": "PALM_LIGHT",
    "p": "PALM",
    "q": "PALM_DARK",
    "t": "TRUNK",
    "g": "SHADOW_GRASS",
    "v": "SHADOW_PAVE",
    "s": "SHADOW_SAND",
}

T = art_dsl.TILE
DOT = art_dsl.TRANSPARENT


# --- helpers ---------------------------------------------------------------

def outlined(rows: list[str]) -> list[str]:
    """Add a 1px OUTLINE around every filled pixel (4-neighbour dilation)."""
    grid = [list(r) for r in rows]
    h, w = len(grid), len(grid[0])
    out = [r[:] for r in grid]
    for y in range(h):
        for x in range(w):
            if grid[y][x] != DOT:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and grid[ny][nx] not in (DOT, "#"):
                    out[y][x] = "#"
                    break
    return ["".join(r) for r in out]


def cast(prop: list[str], dx: int, dy: int, ink: str) -> tuple[list[str], list[str]]:
    """Shadow of a prop: its silhouette shifted by (dx, dy), one colour, with
    the part that sits under the prop carved out. Returns (tile, spill): the
    spill holds whatever overflows into the cell to the right."""
    tile = [[DOT] * T for _ in range(T)]
    spill = [[DOT] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            if prop[y][x] == DOT:
                continue
            sx, sy = x + dx, y + dy
            if not 0 <= sy < T:
                continue
            if sx < T:
                if prop[sy][sx] == DOT:
                    tile[sy][sx] = ink
            elif sx < 2 * T:
                spill[sy][sx - T] = ink
    return ["".join(r) for r in tile], ["".join(r) for r in spill]


def carve(shadow: list[str], prop: list[str]) -> list[str]:
    """Remove the shadow pixels hidden under the prop."""
    return ["".join(DOT if p != DOT else s for s, p in zip(srow, prow))
            for srow, prow in zip(shadow, prop)]


def transpose(rows: list[str]) -> list[str]:
    """Mirror across the main diagonal: a horizontal hedge becomes a vertical
    one whose lobes face left and whose dark side faces right."""
    return art_dsl.hflip(art_dsl.rot90(rows))


def strip(x0: int, x1: int, y0: int, y1: int, ink: str) -> list[str]:
    """A filled rectangle (inclusive bounds) on a transparent tile."""
    return ["".join(ink if x0 <= x <= x1 and y0 <= y <= y1 else DOT for x in range(T))
            for y in range(T)]


# --- round tree: 12 x 12 "cauliflower" canopy, no trunk visible from above --

TREE = [
    "................",
    ".....######.....",
    "...##mLdmLd##...",
    "...#LmdlmdLm#...",
    "..#mLdmLdmdkd#..",
    "..#dmLdmdLmdm#..",
    "..##mdlmdmdkd#..",
    "..#Lmdmdkmdkm#..",
    "..#dmLdmdkdk##..",
    "..#mdmkdmkdkd#..",
    "...#dkdmkdkk#...",
    "...##kdkkdk##...",
    ".....######.....",
    "................",
    "................",
    "................",
]

# --- bush: 9 x 9 round ------------------------------------------------------

BUSH = [
    "................",
    "................",
    "................",
    "......#####.....",
    ".....#mLdmL#....",
    "....#LmdmdLm#...",
    "....#mdLmdkd#...",
    "....#dmdkmdk#...",
    "....#mLdmkdk#...",
    "....#dmkdkdk#...",
    ".....#kdkkd#....",
    "......#####.....",
    "................",
    "................",
    "................",
    "................",
]

# --- hedge: edge-to-edge horizontally so it chains, 3 lobes, dark underside -

HEDGE_H = [
    "................",
    "................",
    "................",
    ".####.####.####.",
    "#LmmL#mLmL#mLLL#",
    "mmLdmLmmLdmmLmdL",
    "LdmmLdmLmmLdmmLm",
    "mLdmdmLdmdLmdLmd",
    "dmmLmdmmLdmdLmmd",
    "mdLmdkmdLmkdmdkm",
    "dkdmkdkmdkdmkdmk",
    "kdkkdkkdkdkkdkkd",
    "################",
    "................",
    "................",
    "................",
]

HEDGE_V = transpose(HEDGE_H)

# --- palm from above: six fronds around a brown crown -----------------------

PALM = outlined([
    "................",
    "................",
    ".......PP.......",
    ".......Pp.......",
    "..PP...Pp...pp..",
    "..PPPP.Pp.pppp..",
    "....PPPPppppp...",
    "......Pttqq.....",
    "......Pttqq.....",
    "....PPPPqqqqq...",
    "..PPPP.pq.qqqq..",
    "..PP...pq...qq..",
    ".......pq.......",
    ".......pq.......",
    "................",
    "................",
])

# --- flower bed: 8 x 6 oval planter with red and white blooms ---------------

FLOWERBED = [
    "................",
    "................",
    "................",
    "................",
    ".....######.....",
    "....#drdkdw#....",
    "...#ddkdrddk#...",
    "...#kdwddkdd#...",
    "...#drdkddrk#...",
    "...#kdkdwkdk#...",
    "....#dkdkdk#....",
    ".....######.....",
    "................",
    "................",
    "................",
    "................",
]

TILES = {
    "TREE_G":    TREE,
    "TREE_P":    TREE,
    "BUSH_G":    BUSH,
    "BUSH_P":    BUSH,
    "HEDGE_H":   HEDGE_H,
    "HEDGE_V":   HEDGE_V,
    "PALM_S":    PALM,
    "PALM_P":    PALM,
    "FLOWERBED": FLOWERBED,
}

STAMPS: dict = {}

FAMILY = {
    "TREE_G":    "grass",
    "TREE_P":    "pave",
    "BUSH_G":    "grass",
    "BUSH_P":    "pave",
    "HEDGE_H":   "grass",
    "HEDGE_V":   "grass",
    "PALM_S":    "sand",
    "PALM_P":    "pave",
    "FLOWERBED": "any",
}

# --- shadows: the reference sets the blob beside the canopy, so the tree's ---
# --- silhouette is pushed (+9, +1) and the bush's (+6, +1); the overflow ------
# --- lands in the _SPILL tile for the cell to the right ----------------------

# A palm casts a round blob rather than its frond star.
PALM_BLOB = [
    "................",
    "................",
    "................",
    "................",
    "................",
    ".........sss....",
    ".......sssssss..",
    "......sssssssss.",
    "......sssssssss.",
    "......sssssssss.",
    "......sssssssss.",
    ".......sssssss..",
    "........sssss...",
    "..........s.....",
    "................",
    "................",
]

TREE_SH_G, TREE_SP_G = cast(TREE, 9, 1, "g")
TREE_SH_P, TREE_SP_P = cast(TREE, 9, 1, "v")
BUSH_SH_G, BUSH_SP_G = cast(BUSH, 6, 1, "g")
BUSH_SH_P, BUSH_SP_P = cast(BUSH, 6, 1, "v")

SHADOWS = {
    "TREE_G":       TREE_SH_G,
    "TREE_G_SPILL": TREE_SP_G,
    "TREE_P":       TREE_SH_P,
    "TREE_P_SPILL": TREE_SP_P,
    "BUSH_G":       BUSH_SH_G,
    "BUSH_G_SPILL": BUSH_SP_G,
    "BUSH_P":       BUSH_SH_P,
    "BUSH_P_SPILL": BUSH_SP_P,
    "PALM_S":       carve(PALM_BLOB, PALM),
    "PALM_P":       carve([r.replace("s", "v") for r in PALM_BLOB], PALM),
    "HEDGE_H":      strip(0, 15, 13, 14, "g"),
    "HEDGE_V":      strip(13, 14, 0, 15, "g"),
}

# --- decoration: grass tufts and scattered flowers, no outline --------------

DETAILS = {
    "GRASS_TUFT": [
        "................",
        "................",
        "....l...........",
        "...l.l..........",
        "....l.......l...",
        "...........l.l..",
        "............l...",
        "................",
        "................",
        "........l.......",
        ".......l.l......",
        "..l.....l.......",
        ".l.l............",
        "..l.............",
        "................",
        "................",
    ],
    "FLOWERS": [
        "................",
        "................",
        "...r............",
        "..l........w....",
        "..........l.....",
        "................",
        "................",
        "......w.........",
        ".....l..........",
        "............r...",
        "...........l....",
        "................",
        "...w............",
        "..l.............",
        "................",
        "................",
    ],
}
