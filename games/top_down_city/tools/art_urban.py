"""Urban ground for the top-down city: asphalt, road markings, pavement, plaza.

Every tile is a fully opaque, seamless 16x16 background tile. Road markings
are drawn on the same asphalt base so a marked tile sits invisibly next to a
plain ROAD tile. The kerb tiles are composed at import time from ONE
north-edge overlay rotated clockwise to the other sides:

    SIDEWALK_K<mask>  pavement cell, ROAD on the masked sides (light kerb)

Mask bits: 1 = N, 2 = E, 4 = S, 8 = W (mask 1..15). Corners simply overlap:
the outer light line wins over the inner shade where two sides meet.
"""
from __future__ import annotations

import art_dsl

SLOT = "URBAN"

PALETTE = [
    ("ASPHALT",        0x88, 0x86, 0x86),
    ("GREY_DARK",      0x6D, 0x6B, 0x6B),
    ("ASPHALT_SHADOW", 0x57, 0x56, 0x56),
    ("WHITE",          0xDE, 0xDE, 0xDE),
    ("PAVE",           0xA6, 0x72, 0x59),
    ("PAVE_DARK",      0x95, 0x67, 0x50),
    ("PAVE_LIGHT",     0xB0, 0x83, 0x6D),
    ("CONCRETE_LIGHT", 0xC9, 0xC7, 0xC5),
    ("CONCRETE",       0xA6, 0xA3, 0xA3),
    ("PLAZA",          0xDD, 0xDC, 0xDC),
    ("PLAZA_DARK",     0xD4, 0xD3, 0xD3),
    ("OUTLINE",        0x1E, 0x1E, 0x1E),
]

INK = {
    "a": "ASPHALT",
    "d": "GREY_DARK",
    "h": "ASPHALT_SHADOW",
    "W": "WHITE",
    "p": "PAVE",
    "q": "PAVE_DARK",
    "l": "PAVE_LIGHT",
    "c": "CONCRETE_LIGHT",
    "n": "CONCRETE",
    "z": "PLAZA",
    "k": "PLAZA_DARK",
    "o": "OUTLINE",
}

# ----------------------------------------------------------------- road --
# Flat asphalt with six single dark grains, spread so a 3x3 repeat shows no
# lattice.
ROAD = [
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaadaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaadaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaadaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaadaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "adaaaaaaaaaaaaaa",
    "aaaaaaaaadaaaaaa",
    "aaaaaaaaaaaaaaaa",
]

# Centre line: a 2 px tall, 12 px long dash centred vertically, with a 2 px
# gap at each tile end so a run of tiles reads as a dashed line.
ROAD_LINE_H = [
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaadaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaadaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "aaWWWWWWWWWWWWaa",
    "aaWWWWWWWWWWWWaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaadaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "adaaaaaaaaaaaaaa",
    "aaaaaaaaadaaaaaa",
    "aaaaaaaaaaaaaaaa",
]
ROAD_LINE_V = art_dsl.rot90(ROAD_LINE_H)

# Zebra crossing on a HORIZONTAL street: bars run with the traffic, i.e.
# vertical stripes 2 px wide at a 4 px pitch, spanning the whole tile so
# stacked tiles cover the road width. CROSSWALK_V is the rotation.
CROSSWALK_H = [
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
    "aWWaaWWaaWWaaWWa",
]
CROSSWALK_V = art_dsl.rot90(CROSSWALK_H)

# Parking: one white bay line down the left edge; repeated along a row the
# tiles read as 16 px wide bays.
PARKING = [
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waadaaaaaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaadaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaadaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaaaaada",
    "Waaaaaaaaaaaaaaa",
    "Waaaaaaaaaaaaaaa",
    "Wadaaaaaaaaaaaaa",
    "Waaaaaaaaadaaaaa",
    "Waaaaaaaaaaaaaaa",
]

# Round 8 px manhole cover: dark rim, asphalt-grey hatch with two darker
# rivet lines.
ROAD_MANHOLE = [
    "aaaaaaaaaaaaaaaa",
    "aadaaaaaaaaaaaaa",
    "aaaaaaaaaaaaadaa",
    "aaaaaaaaaaaaaaaa",
    "aaaaaaddddaaaaaa",
    "aaaaadaddadaaaaa",
    "aaaadahaahadaaaa",
    "aaaaddaddaddaaaa",
    "aaaaddaddaddaaaa",
    "aaaadahaahadaaaa",
    "aaaaadaddadaaaaa",
    "aaaaaaddddaaaaaa",
    "aaaaaaaaaaaaaaaa",
    "adaaaaaaaaaaaaaa",
    "aaaaaaaaaaaadaaa",
    "aaaaaaaaaaaaaaaa",
]

# ------------------------------------------------------------- pavement --
# 3x3 dark bricks with 1 px light mortar, the brick columns shifted by one
# pixel every course (period 8 rows), plus four highlight pixels.
SIDEWALK = [
    "qqqpqqqpqqqpqqqp",
    "ppplpppppppppppp",
    "qqpqqqpqqqpqqqpq",
    "qqpqqqpqqqpqqqpq",
    "qqpqqqpqqqpqqqpq",
    "ppppppppplpppppp",
    "qqqpqqqpqqqpqqql",
    "qqqpqqqpqqqpqqqp",
    "qqqpqqqpqqqpqqqp",
    "pppppppppppppppp",
    "qqpqqqpqqqpqqqpq",
    "qqlqqqpqqqpqqqpq",
    "qqpqqqpqqqpqqqpq",
    "pppppppppppppppp",
    "qqqpqqqpqqqpqqqp",
    "qqqpqqqpqqqpqqqp",
]

# Kerb where pavement meets road: one light line on the outermost pixels
# and one concrete-grey line just inside it.
SIDEWALK_KERB_N = [
    "cccccccccccccccc",
    "nnnnnnnnnnnnnnnn",
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
    "................",
]
KERB_RANK = {"c": 2, "n": 1}

# ---------------------------------------------------------------- plaza --
# Light concrete with a faint three-pixel checker, alternating each row.
PLAZA = [
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
    "kzkkzkkzkkzkkzkk",
    "zkzzkzzkzzkzzkzz",
]


# ----------------------------------------------------------- composition --
def compose(base: list[str], edge_n: list[str], mask: int, rank: dict[str, int]) -> list[str]:
    """Merge the north edge overlay, rotated onto each masked side, over ``base``.

    Where two overlays meet (outer corners) the higher-ranked ink wins, so
    the light kerb line stays continuous around the corner. ``.`` keeps the
    base pixel.
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


TILES: dict[str, list[str]] = {
    "ROAD": ROAD,
    "ROAD_LINE_H": ROAD_LINE_H,
    "ROAD_LINE_V": ROAD_LINE_V,
    "CROSSWALK_H": CROSSWALK_H,
    "CROSSWALK_V": CROSSWALK_V,
    "PARKING": PARKING,
    "ROAD_MANHOLE": ROAD_MANHOLE,
    "SIDEWALK": SIDEWALK,
    "PLAZA": PLAZA,
}
TILES.update({f"SIDEWALK_K{mask}": compose(SIDEWALK, SIDEWALK_KERB_N, mask, KERB_RANK)
              for mask in range(1, 16)})

STAMPS: dict = {}
SHADOWS: dict = {}
DETAILS: dict = {}
FAMILY: dict = {}
