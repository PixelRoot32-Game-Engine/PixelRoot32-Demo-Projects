"""Nature terrain for the top-down city: water, sand, grass and dirt.

Every tile here is a fully opaque, seamless 16x16 background tile. The plain
tiles (WATER, SAND, GRASS, DIRT and their _B variants) are drawn by hand as
row literals. The transition tiles are composed at import time from ONE
hand-drawn north-edge overlay per family, rotated clockwise to the other
three sides and merged by priority:

    WATER_E<mask>  water cell, LAND on the masked sides (foam + deep band)
    SAND_E<mask>   sand cell, WATER on the masked sides (orange rim)
    GRASS_S<mask>  grass cell, SAND on the masked sides (ragged boundary)

Mask bits: 1 = N, 2 = E, 4 = S, 8 = W (mask 1..15). Inner corners (foreign
terrain only diagonally) are not produced; the plain tile is used there.
"""
from __future__ import annotations

import art_dsl

SLOT = "NATURE"

PALETTE = [
    ("WATER",       0x27, 0x95, 0xF2),
    ("WATER_LIGHT", 0x52, 0xA9, 0xF5),
    ("WATER_DEEP",  0x0D, 0x77, 0xD4),
    ("SAND",        0xF6, 0xBC, 0x69),
    ("SAND_MID",    0xF4, 0xAD, 0x48),
    ("SAND_RIM",    0xF2, 0x9F, 0x27),
    ("GRASS",       0x24, 0x8C, 0x17),
    ("GRASS_LIGHT", 0x2B, 0xAA, 0x1C),
    ("DIRT",        0x97, 0x61, 0x47),
    ("TAN_2",       0x9D, 0x65, 0x4A),
    ("BROWN",       0x88, 0x57, 0x40),
    ("TRUNK",       0x65, 0x3C, 0x18),
    ("OUTLINE",     0x1E, 0x1E, 0x1E),
    ("WHITE",       0xDE, 0xDE, 0xDE),
]

INK = {
    "w": "WATER",
    "l": "WATER_LIGHT",
    "d": "WATER_DEEP",
    "s": "SAND",
    "m": "SAND_MID",
    "r": "SAND_RIM",
    "g": "GRASS",
    "G": "GRASS_LIGHT",
    "D": "DIRT",
    "t": "TAN_2",
    "b": "BROWN",
    "k": "TRUNK",
    "o": "OUTLINE",
    "W": "WHITE",
}

# ---------------------------------------------------------------- water --
# Flat water with three-pixel wave dashes; WATER_B moves the dashes so two
# tiles picked at random do not show a repeating grid.
WATER = [
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwlllwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwlllw",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwlllwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwlllwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
]

WATER_B = [
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwlllwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwlllwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwlllwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwlllw",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwlllwwwwwwwww",
    "wwwwwwwwwwwwwwww",
    "wwwwwwwwwwwwwwww",
]

# Shore seen from the water: a zigzag foam line hugging the land, one solid
# row of deep blue, then a deep row broken by water. Rows 4..15 keep the base.
WATER_EDGE_N = [
    "llldddddlllldddd",
    "dddlllllddddllll",
    "dddddddddddddddd",
    "dddwwwdddddwwwdd",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
]
WATER_RANK = {"l": 3, "d": 2, "w": 1}

# ----------------------------------------------------------------- sand --
# Beach sand with small "smile" arcs (two pixels up, three below) and a few
# single grains.
SAND = [
    "ssssssssssssssss",
    "sssssssssssssmss",
    "ssssssssssssssss",
    "ssmsssmsssssssss",
    "sssmmmssssssssss",
    "ssssssssssssssss",
    "ssssssssssssssss",
    "sssssssssmsssmss",
    "ssssssssssmmmsss",
    "smssssssssssssss",
    "ssssssssssssssss",
    "ssssssssssssssss",
    "sssssmsssmssssss",
    "ssssssmmmsssssms",
    "ssssssssssssssss",
    "ssssssssssssssss",
]

SAND_B = [
    "ssssssssssssssss",
    "sssmssssssssssss",
    "ssssssssssmsssms",
    "sssssssssssmmmss",
    "ssssssssssssssss",
    "ssssssssssssssss",
    "smsssmssssssssss",
    "ssmmmsssssssssss",
    "ssssssssssssmsss",
    "ssssssssssssssss",
    "ssssssssssssssss",
    "ssssssmsssmsssss",
    "sssssssmmmssssss",
    "ssssssssssssssss",
    "ssssssssmsssssss",
    "ssssssssssssssss",
]

# Sand meeting water: a solid two-pixel orange rim, then a sparse mid-tone
# dither one pixel further in. '.' keeps the base sand (and its arcs).
SAND_EDGE_N = [
    "rrrrrrrrrrrrrrrr",
    "rrrrrrrrrrrrrrrr",
    ".m..m.m...m..m..",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
]
SAND_RANK = {"r": 3, "m": 2, "s": 1}

# ---------------------------------------------------------------- grass --
# Flat grass with three four-pixel diamond flecks per tile.
GRASS = [
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggGgggg",
    "gggGggggggGgGggg",
    "ggGgGggggggGgggg",
    "gggGgggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggGgggggggg",
    "ggggggGgGggggggg",
    "gggggggGgggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
]

GRASS_B = [
    "ggggggggGggggggg",
    "gggggggGgGgggggg",
    "ggggggggGggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
    "ggggGggggggggggg",
    "gggGgGgggggggggg",
    "ggggGggggggggggg",
    "ggggggggggggGggg",
    "gggggggggggGgGgg",
    "ggggggggggggGggg",
    "gggggggggggggggg",
    "gggggggggggggggg",
]

# Grass meeting sand: the outer row is sand with green nubs poking into it,
# the next row is grass with one- and two-pixel sand bites and a couple of
# light-green tufts beside them, then one stray grain deeper in.
GRASS_EDGE_N = [
    "sssssgsssssssgss",
    "ggsGgggggssgggGg",
    ".........s......",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
    "................",
]
GRASS_RANK = {"s": 3, "G": 2, "g": 1}

# ----------------------------------------------------------------- dirt --
# Park path: flat dirt with paired lighter specks, a few darker grains and
# one deep-brown dot.
DIRT = [
    "DDDDDDDDDDDDDDDD",
    "DDDDDDDDDbDDDDDD",
    "DDDDDttDDDDDDDDD",
    "DDDDDDDDDDDDDDDD",
    "DDDDDDDDDDDDtDDD",
    "DDDbDDDDDDDDDDDD",
    "DDDDDDDDDDDDDDDD",
    "DDttDDDDDDDkDDDD",
    "DDDDDDDDDDDDDbDD",
    "DDDDDDDDDDDDDDDD",
    "DDDDDDDDDDttDDDD",
    "DDDDDDDDDDDDDDDD",
    "DDDDDDbDDDDDDDDD",
    "DDDDDDDDDDDDDDtD",
    "DbDDDDDtDDDDDDDD",
    "DDDDDDDDDDDDDDDD",
]


# ----------------------------------------------------------- composition --
def compose(base: list[str], edge_n: list[str], mask: int, rank: dict[str, int]) -> list[str]:
    """Merge the north edge overlay, rotated onto each masked side, over ``base``.

    Where two overlays cover the same pixel (outer corners) the ink with the
    higher rank wins, so foam beats deep water, rim beats dither, sand beats
    grass. ``.`` in an overlay keeps the base pixel.
    """
    overlays = []
    overlay = list(edge_n)
    for bit in (1, 2, 4, 8):  # N, E, S, W: rot90 is clockwise, top -> right
        if mask & bit:
            overlays.append(overlay)
        overlay = art_dsl.rot90(overlay)
    rows = []
    for y in range(art_dsl.TILE):
        line = []
        for x in range(art_dsl.TILE):
            best = None
            for overlay in overlays:
                ch = overlay[y][x]
                if ch != art_dsl.TRANSPARENT and (best is None or rank[ch] > rank[best]):
                    best = ch
            line.append(best if best is not None else base[y][x])
        rows.append("".join(line))
    return rows


def _variants(prefix: str, base: list[str], edge_n: list[str], rank: dict[str, int]) -> dict[str, list[str]]:
    return {f"{prefix}{mask}": compose(base, edge_n, mask, rank) for mask in range(1, 16)}


TILES: dict[str, list[str]] = {
    "WATER": WATER,
    "WATER_B": WATER_B,
    "SAND": SAND,
    "SAND_B": SAND_B,
    "GRASS": GRASS,
    "GRASS_B": GRASS_B,
    "DIRT": DIRT,
}
TILES.update(_variants("WATER_E", WATER, WATER_EDGE_N, WATER_RANK))
TILES.update(_variants("SAND_E", SAND, SAND_EDGE_N, SAND_RANK))
TILES.update(_variants("GRASS_S", GRASS, GRASS_EDGE_N, GRASS_RANK))

STAMPS: dict = {}
SHADOWS: dict = {}
DETAILS: dict = {}
FAMILY: dict = {}
