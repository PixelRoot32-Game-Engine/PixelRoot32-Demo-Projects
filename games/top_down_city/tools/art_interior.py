"""Interior tiles for the top-down city (palette slot INTERIOR).

Every enterable building in the demo is drawn from this one module and this
one palette slot -- slot 4, which stage 2 left empty. That slot used to hold
the CARS palette, back when parked cars were tiles; once they became actors
nothing in any of the three layers referenced it again, so the interior costs
no new slot at all, only the 32 bytes that were already being spent.
The bank is now full, so a SECOND room does not get a palette either:
the shop's shelves and chiller are dithered out of the same fifteen
colours the station's desk is.

Background tiles are opaque and tile seamlessly. Items tiles are cut-outs on a
transparent surround, exactly like the street furniture, so the same per-pixel
collision the player already uses works indoors with no new code: you can walk
up to the front of the desk and round its corner, not into a 16x16 box.
"""
from __future__ import annotations

SLOT = "INTERIOR"

PALETTE = [
    ("OUTLINE",  0x1E, 0x1E, 0x1E),
    ("WALL_HI",  0xC9, 0xC7, 0xC5),
    ("WALL",     0xA6, 0xA3, 0xA3),
    ("WALL_LO",  0x6D, 0x6B, 0x6B),
    ("FLOOR_HI", 0xDE, 0xDE, 0xDE),
    ("FLOOR",    0xB8, 0xB6, 0xB4),
    ("FLOOR_LO", 0x8E, 0x8C, 0x8A),
    ("WOOD",     0x97, 0x61, 0x47),
    ("WOOD_LO",  0x65, 0x3C, 0x18),
    ("METAL",    0x7E, 0x8A, 0x99),
    ("METAL_LO", 0x4A, 0x52, 0x5E),
    ("BLUE",     0x29, 0x4F, 0xD7),
    ("GREEN",    0x09, 0x90, 0x2B),
    ("AMBER",    0xD7, 0x91, 0x29),
    ("RED",      0xD7, 0x29, 0x29),
]

INK = {
    "K": "OUTLINE",
    "H": "WALL_HI",
    "W": "WALL",
    "D": "WALL_LO",
    "F": "FLOOR_HI",
    "f": "FLOOR",
    "d": "FLOOR_LO",
    "B": "WOOD",
    "b": "WOOD_LO",
    "M": "METAL",
    "m": "METAL_LO",
    "U": "BLUE",
    "E": "GREEN",
    "A": "AMBER",
    "R": "RED",
}


def _floor(bright: str, grout: str) -> list[str]:
    """A 2x2 grid of 7x7 slabs with a grout line between them.

    Written as a helper rather than typed out twice because the light and the
    dark floor differ by one character, and two hand-copied 16-row grids that
    have to stay identical are two grids that eventually do not.
    """
    slab = grout + (bright * 7) + grout + (bright * 7)
    line = grout * 16
    return [line] + [slab] * 7 + [line] + [slab] * 7


TILES: dict[str, list[str]] = {
    # The station floor: pale slabs with a darker grout grid. Two variants,
    # laid in a checker by the map, so a 13x13 room does not read as one flat
    # sheet of grey.
    "INT_FLOOR": _floor("F", "d"),
    "INT_FLOOR_ALT": _floor("f", "d"),

    # The wall. Brick courses with the vertical joints offset every other
    # course, so the tile repeats in both directions without a seam.
    "INT_WALL": [
        "KKKKKKKKKKKKKKKK",
        "HHHHHHHKHHHHHHHH",
        "WWWWWWWKWWWWWWWW",
        "DDDDDDDKDDDDDDDD",
        "KKKKKKKKKKKKKKKK",
        "HHHKHHHHHHHHKHHH",
        "WWWKWWWWWWWWKWWW",
        "DDDKDDDDDDDDKDDD",
        "KKKKKKKKKKKKKKKK",
        "HHHHHHHKHHHHHHHH",
        "WWWWWWWKWWWWWWWW",
        "DDDDDDDKDDDDDDDD",
        "KKKKKKKKKKKKKKKK",
        "HHHKHHHHHHHHKHHH",
        "WWWKWWWWWWWWKWWW",
        "DDDKDDDDDDDDKDDD",
    ],

    # The way out. A wooden threshold mat in the bottom wall: the one cell of
    # this room that is not a wall and not a floor, which is what makes it
    # findable without a label.
    "INT_EXIT_MAT": [
        "dddddddddddddddd",
        "dbbbbbbbbbbbbbbd",
        "dbBBBBBBBBBBBBbd",
        "dbBbbbbbbbbbbBbd",
        "dbBbBBBBBBBBbBbd",
        "dbBbBbbbbbbBbBbd",
        "dbBbBbBBBBbBbBbd",
        "dbBbBbBAABbBbBbd",
        "dbBbBbBAABbBbBbd",
        "dbBbBbBBBBbBbBbd",
        "dbBbBbbbbbbBbBbd",
        "dbBbBBBBBBBBbBbd",
        "dbBbbbbbbbbbbBbd",
        "dbBBBBBBBBBBBBbd",
        "dbbbbbbbbbbbbbbd",
        "dddddddddddddddd",
    ],
}


ITEMS: dict[str, list[str]] = {
    # The front desk, seen from above: a wooden top with a raised counter lip
    # along the front edge. Tiles horizontally, so a three-cell desk is this
    # tile three times.
    "INT_DESK": [
        "................",
        "................",
        "KKKKKKKKKKKKKKKK",
        "KBBBBBBBBBBBBBBK",
        "KBBBBBBBBBBBBBBK",
        "KBbbbbbbbbbbbbBK",
        "KBbBBBBBBBBBBbBK",
        "KBbBBBBBBBBBBbBK",
        "KBbBBBBBBBBBBbBK",
        "KBbbbbbbbbbbbbBK",
        "KBBBBBBBBBBBBBBK",
        "KKKKKKKKKKKKKKKK",
        "KbbbbbbbbbbbbbbK",
        "KKKKKKKKKKKKKKKK",
        "................",
        "................",
    ],

    # A filing cabinet against a wall: three drawers with metal pulls.
    "INT_CABINET": [
        "................",
        "..KKKKKKKKKKKK..",
        "..KMMMMMMMMMMK..",
        "..KMmmmmmmmmMK..",
        "..KMmMMMMMMmMK..",
        "..KMmMMmmMMmMK..",
        "..KMmmmmmmmmMK..",
        "..KMmMMMMMMmMK..",
        "..KMmMMmmMMmMK..",
        "..KMmmmmmmmmMK..",
        "..KMmMMMMMMmMK..",
        "..KMmMMmmMMmMK..",
        "..KMmmmmmmmmMK..",
        "..KMMMMMMMMMMK..",
        "..KKKKKKKKKKKK..",
        "................",
    ],

    # The holding cell: vertical bars on a frame. Tiles horizontally.
    "INT_BARS": [
        "KKKKKKKKKKKKKKKK",
        "KmmmmmmmmmmmmmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmmmmmmmmmmmmmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmKMmKMmKMmKMmmK",
        "KmmmmmmmmmmmmmmK",
        "KKKKKKKKKKKKKKKK",
        "................",
    ],

    # A potted plant, because a room with only furniture in it reads as a
    # storeroom.
    "INT_PLANT": [
        "................",
        "................",
        ".....K....K.....",
        "....KEKKKKEK....",
        "...KEEEEEEEEK...",
        "..KEEEEEEEEEEK..",
        "..KEEEEEEEEEEK..",
        "...KEEEEEEEEK...",
        "....KEEEEEEK....",
        ".....KKEEKK.....",
        "......KbbK......",
        ".....KBBBBK.....",
        ".....KBBBBK.....",
        ".....KbbbbK.....",
        "......KKKK......",
        "................",
    ],

    # The duty board on the wall: a cork board with pinned notices.
    "INT_BOARD": [
        "................",
        "..KKKKKKKKKKKK..",
        "..KbbbbbbbbbbK..",
        "..KbFFFbbFFFbK..",
        "..KbFFFbbFFFbK..",
        "..KbFFFbbFFFbK..",
        "..KbRbbbbbbUbK..",
        "..KbbbbbbbbbbK..",
        "..KbFFFFbbFFbK..",
        "..KbFFFFbbFFbK..",
        "..KbFFFFbbFFbK..",
        "..KbbAbbbbbbbK..",
        "..KbbbbbbbbbbK..",
        "..KKKKKKKKKKKK..",
        "................",
        "................",
    ],

    # A chair, drawn from above: seat, back rail and four legs.
    "INT_CHAIR": [
        "................",
        "................",
        "....KKKKKKKK....",
        "....KbbbbbbK....",
        "....KKKKKKKK....",
        "................",
        "...KKKKKKKKKK...",
        "...KBBBBBBBBK...",
        "...KBBBBBBBBK...",
        "...KBBBBBBBBK...",
        "...KBBBBBBBBK...",
        "...KKKKKKKKKK...",
        "...Kb......bK...",
        "...KK......KK...",
        "................",
        "................",
    ],

    # --- The corner shop -------------------------------------------------
    # Added when the station stopped being the only interior. They live in
    # this module rather than one of their own because there is no second
    # palette slot: the bank of eight is full, so everything indoors is drawn
    # from the fifteen colours above, and a shop of wood, metal and four bright
    # box colours is what that palette can honestly do.

    # A stocked shelving unit, seen from above: a wooden carcass with boxes
    # on it. Tiles horizontally, like the desk, so an aisle is this tile
    # repeated.
    "INT_SHELF": [
        "................",
        "KKKKKKKKKKKKKKKK",
        "KbbbbbbbbbbbbbbK",
        "KbRRRKEEEKAAAKbK",
        "KbRRRKEEEKAAAKbK",
        "KbKKKKKKKKKKKKbK",
        "KbUUUKAAAKEEEKbK",
        "KbUUUKAAAKEEEKbK",
        "KbKKKKKKKKKKKKbK",
        "KbEEEKRRRKUUUKbK",
        "KbEEEKRRRKUUUKbK",
        "KbKKKKKKKKKKKKbK",
        "KbbbbbbbbbbbbbbK",
        "KKKKKKKKKKKKKKKK",
        "................",
        "................",
    ],

    # The chiller cabinet: a metal case with a lit glass front. The one thing
    # in the room that is not wood, so the aisles do not read as one texture.
    "INT_FRIDGE": [
        "................",
        "KKKKKKKKKKKKKKKK",
        "KMMMMMMMMMMMMMMK",
        "KMmmmmmmmmmmmmMK",
        "KMmUUUUUUUUUUmMK",
        "KMmUHHHHHHHHUmMK",
        "KMmUHEEEHRRRHmMK",
        "KMmUHEEEHRRRHmMK",
        "KMmUHHHHHHHHUmMK",
        "KMmUHAAAHUUUHmMK",
        "KMmUHAAAHUUUHmMK",
        "KMmUHHHHHHHHUmMK",
        "KMmUUUUUUUUUUmMK",
        "KMmmmmmmmmmmmmMK",
        "KMMMMMMMMMMMMMMK",
        "KKKKKKKKKKKKKKKK",
    ],
}

# This is the one art module that contributes to BOTH tile layers -- the room
# it draws is a room, not a set of props scattered on the city's terrain, so
# it has to own its own ground. The assembler splits them by this list;
# everything not named here is an Items prop on a transparent surround.
BACKGROUND_TILE_NAMES = tuple(TILES)

TILES.update(ITEMS)

STAMPS: dict[str, tuple[int, int, list[str]]] = {}

SHADOWS: dict[str, list[str]] = {}

DETAILS: dict[str, list[str]] = {}

FAMILY: dict[str, str] = {}
