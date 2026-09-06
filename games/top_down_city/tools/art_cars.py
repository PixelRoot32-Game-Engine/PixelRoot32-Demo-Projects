"""Vehicles for the top-down city: seven car colours in four headings, with
their asphalt cast shadow baked into the frame.

Stage 2 turned cars from scenery into actors, so this module no longer feeds
the Items tileset -- it is a sprite source, like ``art_player``. Every car is
drawn once pointing north (16x16, an 8x13 outline box with the wheels peeking
one pixel out of each side) and the other three headings are ``art_dsl.rot90``
of it, so the headlights always end up at the front. The shadow that used to
live on the Details layer as a separate tile is composited into the frame
instead: a moving sprite cannot leave a tile behind it.

The palette is still a scene slot (``SLOT = "CARS"``): the slot is what the
generator exports as ``CARS_PALETTE``, and the sprite bank installs the same
sixteen colours in a sprite palette slot at runtime.
"""
import art_dsl

SLOT = "CARS"

PALETTE = [
    ("OUTLINE",    0x1E, 0x1E, 0x1E),
    ("GLASS",      0x2B, 0x2B, 0x2B),
    ("WHITE",      0xDE, 0xDE, 0xDE),
    ("LIGHT_GREY", 0xC9, 0xC7, 0xC5),
    ("METAL",      0x4E, 0x4E, 0x4E),
    ("SHADOW",     0x57, 0x56, 0x56),
    ("RED",        0xD7, 0x29, 0x29),
    ("RED_DARK",   0xAF, 0x20, 0x20),
    ("RED_DEEP",   0x99, 0x16, 0x16),
    ("BLUE",       0x25, 0x44, 0xE3),
    ("BLUE_DARK",  0x26, 0x5C, 0xA0),
    ("AMBER",      0xD7, 0x91, 0x29),
    ("BROWN",      0x7C, 0x4A, 0x1E),
    ("BROWN_DARK", 0x5B, 0x36, 0x16),
]

INK = {
    "#": "OUTLINE",
    "g": "GLASS",
    "W": "WHITE",
    "c": "LIGHT_GREY",
    "m": "METAL",
    "s": "SHADOW",
    "r": "RED",
    "a": "RED_DARK",
    "q": "RED_DEEP",
    "b": "BLUE",
    "n": "BLUE_DARK",
    "y": "AMBER",
    "w": "BROWN",
    "k": "BROWN_DARK",
}

# ---------------------------------------------------------------------------
# Car template, nose up. Placeholders: B body, R roof / shaded right column,
# L headlight, T tail light. Wheels (m) peek one pixel out of the outline.
#
# The drawing occupies columns 3..12 and rows 1..13 of the cell. The C++ side
# derives the collision box from those bounds (kVehicleBox* in
# CityConstants.h), so moving the art inside the cell means moving the box.
# ---------------------------------------------------------------------------
_CAR_V = [
    "................",
    "....########....",
    "....#LBBBBL#....",
    "...m#BBBBBR#m...",
    "...m#BggggR#m...",
    "....#cggggc#....",
    "....#gRRRRg#....",
    "....#gRRRRg#....",
    "....#gRRRRg#....",
    "....#cggggc#....",
    "...m#BggggR#m...",
    "...m#BBBBBR#m...",
    "....#TBBBBT#....",
    "....########....",
    "................",
    "................",
]

# Taxi: amber body, black-and-white checker band across the roof.
_TAXI_V = [
    "................",
    "....########....",
    "....#WyyyyW#....",
    "...m#yyyyyw#m...",
    "...m#yggggw#m...",
    "....#cggggc#....",
    "....##W#W#W#....",
    "....#W#W#W##....",
    "....##W#W#W#....",
    "....#cggggc#....",
    "...m#yggggw#m...",
    "...m#yyyyyw#m...",
    "....#ryyyyr#....",
    "....########....",
    "................",
    "................",
]

# Police: white body, blue hood mark, red/blue light bar on the roof and a
# blue band across the trunk.
_POLICE_V = [
    "................",
    "....########....",
    "....#cWWWWc#....",
    "...m#WWbbWc#m...",
    "...m#Wggggc#m...",
    "....#cggggc#....",
    "....#gWWWWg#....",
    "....#grrbbg#....",
    "....#gWWWWg#....",
    "....#cggggc#....",
    "...m#Wggggc#m...",
    "...m#bbbbbb#m...",
    "....#rWWWWr#....",
    "....########....",
    "................",
    "................",
]


def _car(body: str, roof: str, head: str = "W", tail: str = "r") -> list[str]:
    """Fill the car template with one colour scheme."""
    table = str.maketrans({"B": body, "R": roof, "L": head, "T": tail})
    return [row.translate(table) for row in _CAR_V]


def _shadow(rows: list[str], dx: int = 2, dy: int = 2) -> list[str]:
    """Asphalt shadow: the (+dx, +dy) silhouette minus the body itself."""
    h, w = len(rows), len(rows[0])
    out = []
    for y in range(h):
        line = []
        for x in range(w):
            sx, sy = x - dx, y - dy
            shifted = 0 <= sx < w and 0 <= sy < h and rows[sy][sx] != "."
            line.append("s" if shifted and rows[y][x] == "." else ".")
        out.append("".join(line))
    return out


def _with_shadow(rows: list[str]) -> list[str]:
    """Composite the cast shadow into the frame, under the body."""
    shade = _shadow(rows)
    return ["".join(body if body != "." else shade_px
                    for body, shade_px in zip(body_row, shade_row))
            for body_row, shade_row in zip(rows, shade)]


# Order is the heading order the C++ VehicleActor indexes with: N, E, S, W.
# rot90 is clockwise, so each application turns the car a quarter to the
# right; the shadow is composited afterwards so it always falls south-east,
# whichever way the car points.
HEADINGS = ("N", "E", "S", "W")

_CARS_V = {
    "CAR_RED":    _car("r", "a", tail="q"),
    "CAR_BLUE":   _car("b", "n"),
    "CAR_TAXI":   _TAXI_V,
    "CAR_WHITE":  _car("W", "c", head="c"),
    "CAR_DARK":   _car("m", "g"),
    "CAR_BROWN":  _car("w", "k"),
    "CAR_POLICE": _POLICE_V,
}


def _headings(rows: list[str]) -> dict[str, list[str]]:
    north = rows
    east = art_dsl.rot90(north)
    south = art_dsl.rot90(east)
    west = art_dsl.rot90(south)
    return {name: _with_shadow(frame)
            for name, frame in zip(HEADINGS, (north, east, south, west))}


# {car name: {heading: 16x16 rows}}. Consumed by city_art.vehicle_frames().
SPRITES = {name: _headings(rows) for name, rows in _CARS_V.items()}

# Not a tile module any more: cars are actors, so nothing here reaches a
# tileset. The palette slot stays because the scene still exports it.
TILES = {}
STAMPS = {}
SHADOWS = {}
DETAILS = {}
FAMILY = {}
