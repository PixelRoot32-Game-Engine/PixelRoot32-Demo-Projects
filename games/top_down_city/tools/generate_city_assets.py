#!/usr/bin/env python3
"""Generate the top_down_city scene in the PixelRoot32 Tilemap Editor's export
format, plus the player sprite sheet.

The demo ships no third-party artwork. Every tile and sprite is original CC0
pixel art hand-authored as character grids in ``tools/art_*.py`` (assembled by
``tools/city_art.py``), and the city layout is produced by this script from a
seeded generator. Re-run after changing anything in either:

    python tools/generate_city_assets.py

Outputs (all overwritten in place):

    src/generated/tilemaps/city_scene.h      palettes, dimensions, behaviour
                                            layers, externs
    src/generated/tilemaps/city_scene.cpp    tileset pools, layer indices, init()
    src/generated/tilemaps/<room>.*          one file pair per interior
    src/generated/sprites/PlayerSprites.h    3 facings x 3 walk frames, the same
                                            nine armed, the ground pickup, and
                                            the sprite palette
    src/generated/sprites/VehicleSprites.h   7 car colours x 4 headings
    src/generated/sprites/PedestrianSprites.h  recolour palettes + squashed pose

Why the editor's format and not a bespoke one
---------------------------------------------
The city is procedural, so nothing is drawn in the Tilemap Editor -- but
emitting what the editor emits buys engine features a single-layer map cannot
have:

  * **Three layers.** Props sit over the terrain instead of having grass baked
    into the tile, which is what makes per-pixel collision possible at all: a
    tree tile with an opaque grass background is solid across its whole 16x16
    cell in every collision mode.
  * **Per-cell palette slots.** Eight 16-colour palettes instead of one, so
    terrain, vegetation, furniture, cars and three building sets each get
    their own ramp.
  * **Behaviour layers.** Collision is `TILE_SOLID` read through
    `pixelroot32::physics::getTileFlags` / `isWorldPixelSolid`, the same path
    `gameplay/room_screen` uses, rather than a lookup table of this demo's own.

Format notes (see the engine's ``graphics/Renderer.h``):

  * 4bpp pixel value 0 is TRANSPARENT -- the blitter skips it. Background tiles
    are fully opaque; Items and Details tiles use 0 as their cut-out.
  * Nibble packing is low-nibble-first: byte[i] = (px[2i+1] << 4) | px[2i].
  * Tile index 0 in a tilemap means "skip this cell", so every tileset's slot 0
    is a reserved blank.
"""

from __future__ import annotations

import os
import random
import sys
import textwrap

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import city_art  # noqa: E402  (the art modules live beside this script)
import art_player  # noqa: E402  (for the weapon-colour self-check)

TILE = 16
MAP_W = 128
MAP_H = 128
SEED = 20260829

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "src", "generated")


def rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# --------------------------------------------------------------------------
# Art binding
# --------------------------------------------------------------------------
# Everything drawn comes from city_art. Entry 0 of every palette slot stays
# 0x0000: the 8bpp framebuffer treats a packed zero as "leave this pixel
# alone", which is what gives Items and Details their cut-out. A cell selects
# its slot through `paletteIndices`, so a layer is not tied to one palette.
PALETTE_SLOTS = city_art.PALETTE_SLOTS

# (name, grid, slot) per tile. Index 0 of every layer is a reserved blank.
BACKGROUND_TILES = city_art.BACKGROUND_TILES
ITEMS_TILES = city_art.ITEMS_TILES
DETAILS_TILES = city_art.DETAILS_TILES

BG = city_art.BG_INDEX
IT = city_art.IT_INDEX
DT = city_art.DT_INDEX

LAYERS = [
    ("BACKGROUND", BACKGROUND_TILES),
    ("ITEMS", ITEMS_TILES),
    ("DETAILS", DETAILS_TILES),
]

# The same two layers again, after dark: windows and lamp lenses lit. Same
# length and order as the daylight list, so a cell's index means the same tile
# under either -- which is what lets the clock swap a whole city of lights with
# one pointer per layer and no RAM. Background has no night form: terrain does
# not light up. What is lit, and in which palette entry, is declared by the art
# modules (NIGHT and WINDOWS in tools/art_*.py); city_art assembles it.
NIGHT_LAYERS = [
    ("ITEMS", city_art.NIGHT_ITEMS_TILES),
    ("DETAILS", city_art.NIGHT_DETAILS_TILES),
]

# {slot: (entry, name)} -- the one palette entry per slot that means "this
# pixel is a light, not a surface". The daylight art never draws it and the
# day/night tint never darkens it; see src/game/rules/NightLights.h.
LIT_ENTRIES = city_art.LIT_ENTRIES

# Props whose after-dark form is what puts light on the street. If the layout
# stops placing them the effect quietly stops existing, so the count is
# checked rather than assumed.
NIGHT_STREET_PROPS = ("LAMP_G", "LAMP_P")

# Every interior is a scene with its own tile pools, indices, behaviour layers
# and init(). Only the palette SLOT is shared, because the engine's bank of
# eight is global and every scene is resident at once -- entering a room is
# Engine::setScene rather than a push.
INTERIOR_BACKGROUND_TILES = city_art.INTERIOR_BACKGROUND_TILES
INTERIOR_ITEMS_TILES = city_art.INTERIOR_ITEMS_TILES
INT_BG = city_art.INTERIOR_BG_INDEX
INT_IT = city_art.INTERIOR_IT_INDEX

INTERIOR_LAYERS = [
    ("BACKGROUND", INTERIOR_BACKGROUND_TILES),
    ("ITEMS", INTERIOR_ITEMS_TILES),
]

INTERIOR_SLOT = city_art.SLOT_INDEX["INTERIOR"]

# Multi-tile objects: {name: (w, h, [tile names row-major])}.
STAMPS = city_art.STAMPS
DOWNTOWN_LOTS = city_art.DOWNTOWN_LOTS
SUBURB_HOUSES = city_art.SUBURB_HOUSES

# Which stamp a stamp tile belongs to and where it sits in it, so a tile can
# be traced back to the whole object when the layout removes one.
STAMP_OF = {tile: (name, i)
            for name, (_w, _h, tiles) in STAMPS.items()
            for i, tile in enumerate(tiles)}

PLAYER_PALETTE = city_art.PLAYER_PALETTE

# Terrain the player can walk on. Everything else in this layer is solid.
BG_WALKABLE = city_art.BG_WALKABLE

# The core of a street band. A crossing band must never lay its kerb or its
# grass verge over one of these, or a junction ends up with brick strips across
# the asphalt.
BAND_CORE = {"ROAD", "ROAD_LINE_H", "ROAD_LINE_V", "CROSSWALK_H",
             "CROSSWALK_V", "DIRT"}

# Vehicles are actors now, so the layout does not place a tile for them: it
# records a spawn (tile, heading, colour) the C++ side turns into a
# VehicleActor. Cars are the colours art_cars draws, in its own order.
# Mirrors weapons::WeaponId. Index 1 is the police sidearm, which nobody drops
# and the player is never handed, so it is deliberately absent here.
WEAPON_PISTOL = 0
WEAPON_SHOTGUN = 2
WEAPON_ID_NAMES = {WEAPON_PISTOL: "pistol", WEAPON_SHOTGUN: "shotgun"}

VEHICLE_NAMES = city_art.VEHICLE_NAMES
VEHICLE_HEADINGS = city_art.VEHICLE_HEADINGS

# What a ROCK on the sand may turn into. The beach is dressed from one
# canonical name so the density rolls stay in one place.
BEACH_ROCKS = ["ROCK", "LOUNGER", "LOUNGER_B", "BEACH_BALL", "BEACH_BALL_B"]

# Items that make a cell part of the beach, and that the promenade margin
# clears so the shore stays reachable from the road.
BEACH_PROPS = {"PALM_S", "PALM_P", "PARASOL"} | set(BEACH_ROCKS)

# How many driveable cars the whole city holds, and how far apart two of them
# must park. Every car is a live actor the player can get into, so the count is
# a budget rather than a density: few enough to simulate and draw without
# streaming, spread far enough apart that a street never reads as a car park.
VEHICLE_COUNT = 24
VEHICLE_MIN_GAP = 7


# --------------------------------------------------------------------------
# Canvas
# --------------------------------------------------------------------------
class Canvas:
    """A 16x16 grid of palette indices, wrapping a city_art grid."""

    def __init__(self, grid: list[list[int]] | None = None):
        if grid is None:
            grid = city_art.blank_grid()
        if len(grid) != TILE or any(len(row) != TILE for row in grid):
            raise ValueError("a canvas is 16x16")
        self.px = [list(row) for row in grid]

    def rows(self) -> list[list[int]]:
        return self.px

    def is_opaque_everywhere(self) -> bool:
        return all(v != 0 for row in self.px for v in row)


def pack4bpp(canvas: Canvas) -> list[int]:
    data: list[int] = []
    for row in canvas.rows():
        for i in range(0, TILE, 2):
            data.append((row[i + 1] << 4) | row[i])
    return data


def player_frame(direction: str, frame: int) -> Canvas:
    """16x16 walk frame. ``direction`` is 'down', 'up' or 'side'."""
    return Canvas(city_art.player_frame(direction, frame))


def armed_player_frame(direction: str, frame: int) -> Canvas:
    """The same frame with the pistol stamped in."""
    return Canvas(city_art.armed_player_frame(direction, frame))
# --------------------------------------------------------------------------
# City layout
# --------------------------------------------------------------------------
# How far from the spawn the first pistol is laid out, in tiles. The viewport
# is fifteen tiles wide (240 px of 16 px tiles), so five is on screen the
# moment the demo opens: the mechanic is shown rather than explained.
VIEWPORT_TILES = 15
WEAPON_SPAWN_RADIUS_TILES = 5

ZONE_NAMES = ["OCEAN", "BEACH", "DOWNTOWN", "PARK", "MARINA", "SUBURBS"]
Z_OCEAN, Z_BEACH, Z_DOWNTOWN, Z_PARK, Z_MARINA, Z_SUBURBS = range(6)

ZONE_LABELS = {
    Z_OCEAN: "OCEAN",
    Z_BEACH: "SUNSET BEACH",
    Z_DOWNTOWN: "DOWNTOWN",
    Z_PARK: "CENTRAL PARK",
    Z_MARINA: "MARINA PLAZA",
    Z_SUBURBS: "EAST SUBURBS",
}

# A street band is five tiles wide: sidewalk, lane, centre line, lane,
# sidewalk. Bands sit on a 16-tile pitch, which leaves 11x11 tile city blocks
# between them -- large enough for a real building footprint at 16px tiles.
ROAD_BAND = 5
BLOCK_PITCH = 16
# The two lanes of a band, as offsets across its width. Traffic keeps right:
# on an east-west street the near lane (1) runs west and the far lane (3) east;
# on a north-south street the near lane runs south and the far one north. The
# rule lives in src/game/rules/Lanes.h -- these are only the offsets the
# emitted table has to agree with, and CityConstants.h asserts that it does.
LANE_NEAR = 1
LANE_FAR = ROAD_BAND - 2

# What a self-driving car may stand on. The painted centre line is asphalt
# too, but it is not a lane: a car sitting on it is nose to nose with the
# oncoming side.
LANE_SURFACE = {"ROAD", "ROAD_LINE_H", "ROAD_LINE_V",
                "CROSSWALK_H", "CROSSWALK_V"}

# --------------------------------------------------------------------------
# The radar's whole vocabulary
# --------------------------------------------------------------------------
# Three values, and one of them is an absence. A terrain-per-colour map is a
# picture of the island, which is what a radar must not be: the player glances
# at it mid-corner asking only where this street goes, and every extra colour
# is one more thing to rule out. So the map is the streets cut through one flat
# ground colour, and the sea is left unpainted so the backing plate shows
# through -- already how the overlay draws the world past the coastline, and
# the coast comes out as a real edge for free.
#
# These index the ENGINE's built-in PR32 palette, not this demo's player
# palette, which is why the radar came out purple the first time: every drawing
# PRIMITIVE -- rectangle, line and glyph in the HUD and the overlay -- resolves
# its Color against the engine's background palette, which this demo never
# replaces, and only SPRITE blits read the custom slots it does set. A swatch
# goes to drawFilledRectangle, so a swatch is an engine index. It hid for five
# stages because the old nearest-colour matcher spread its swatches over the
# whole 0-15 range, and any spread of sixteen colours looks like a city map.
# CityConstants.h static_asserts these against gfx::Color, so the two ends
# cannot drift apart without the build stopping.
MINIMAP_INK_ROAD = 15                     # gfx::Color::Gray,   #8D8D8D
MINIMAP_INK_GROUND = 8                    # gfx::Color::Yellow, #FFD500
MINIMAP_INK_ABSENT = 0xFF                 # minimap::kAbsent, drawn as nothing

# Asphalt, kerb to kerb. LANE_SURFACE is what a car may DRIVE on and the radar
# needs one tile more: the manholes along the carriageway are not lanes, but
# leaving them out puts holes in the streets.
MINIMAP_ROAD = LANE_SURFACE | {"ROAD_MANHOLE"}

# Drawn as nothing, so the island has an edge.
MINIMAP_SEA_FAMILY = "WATER"

# A drivable stretch shorter than one city block is not worth emitting: a car
# spawned into it would reach the end before the player noticed it was there.
LANE_MIN_RUN = BLOCK_PITCH
# The grid is inset a full block from the map edge. Starting it at 8 put the
# outermost avenue within three tiles of the shore along most of the island's
# curve, and the water-margin clipping then reduced it to a stub.
H_AVENUES = [12, 28, 44, 60, 76, 92]
V_STREETS = [12, 28, 44, 60, 76, 92, 108]
PROMENADE_Y = 104                       # the coastal road, south of everything

# --------------------------------------------------------------------------
# The interiors
# --------------------------------------------------------------------------
# Every room is hand-laid: each is a single screen, each is the same every
# time, and a procedural rule for a fifteen-by-fifteen space would be more code
# than the space itself. Fifteen square is the viewport, not a round number --
# 15 * 16 px is exactly the 240x240 panel, so the camera has nothing to scroll
# and a room reads as one screen, which is what interiors were on the GBA and
# the cheapest thing to draw.
#
# One legend, one tile pool and one palette slot across all rooms, as a
# constraint rather than a convenience: the engine's bank of eight background
# slots is full (see city_art.PALETTE_SLOTS), so a second room gets a different
# arrangement of the same fifteen colours, not a palette of its own.
#
#   #  wall        .  floor        M  the way out
#   C  cell floor  B  cell bars    D  front desk / shop counter
#   h  chair       p  plant        F  filing cabinet   N  duty board
#   S  shelving    G  chiller cabinet
#   O  an officer's post -- plain floor with a police spawn on it
#   T  the spot you are served at -- plain floor, in front of a counter

# char -> (Background tile, Items tile or None)
INTERIOR_LEGEND = {
    "#": ("INT_WALL", None),
    ".": (None, None),                  # floor, checkered below
    "C": ("INT_FLOOR_ALT", None),
    "M": ("INT_EXIT_MAT", None),
    "B": (None, "INT_BARS"),
    "D": (None, "INT_DESK"),
    "h": (None, "INT_CHAIR"),
    "p": (None, "INT_PLANT"),
    "F": (None, "INT_CABINET"),
    "N": (None, "INT_BOARD"),
    "S": (None, "INT_SHELF"),
    "G": (None, "INT_FRIDGE"),
    # An officer stands on plain floor; the figure is an actor, not a tile.
    "O": (None, None),
    # And so does a customer. The till is a coordinate, not a tile: the counter
    # beside it is already drawn and solid, and a second graphic for "stand
    # here" would be a label the room does not need.
    "T": (None, None),
}


class Interior:
    """One room, and everything the emitters need to write it out.

    A room is data, not code: a third interior is another entry in INTERIORS
    plus a door in the city, and no new branch anywhere. It stopped being
    module-level constants once "the interior" had been spelled into nine
    functions and a validation pass.
    """

    def __init__(self, key, room, sealed="", staffed=False,
                 title="", doc=()):
        self.key = key                  # the generated file's basename
        self.namespace = key            # and its C++ namespace, deliberately
        self.room = room
        self.sealed = sealed            # floor the player must NOT reach
        self.staffed = staffed          # does it emit police spawn posts
        self.title = title
        self.doc = list(doc)            # header comment, one line per entry
        self.width = len(room[0])
        self.height = len(room)

    def layers(self) -> dict[str, list[int]]:
        """The room as the same arrays every city layer is exported as."""
        for y, row in enumerate(self.room):
            if len(row) != self.width:
                raise ValueError(f"{self.key} row {y} is {len(row)} wide, "
                                 f"expected {self.width}")

        bg: list[int] = []
        items: list[int] = []
        for y in range(self.height):
            for x in range(self.width):
                ch = self.room[y][x]
                if ch not in INTERIOR_LEGEND:
                    raise ValueError(f"{self.key}: unknown character {ch!r} "
                                     f"at ({x}, {y})")
                bg_name, item_name = INTERIOR_LEGEND[ch]
                if bg_name is None:
                    # Checkered so a thirteen-cell room does not read as one
                    # flat sheet of grey.
                    bg_name = "INT_FLOOR" if (x + y) % 2 == 0 else "INT_FLOOR_ALT"
                bg.append(INT_BG[bg_name])
                items.append(INT_IT[item_name] if item_name else 0)

        # Solidity, by exactly the rules the city uses: a Background tile is
        # solid when it is not walkable, an Items cell when it holds anything.
        bg_flags = [1 if INTERIOR_BACKGROUND_TILES[t][0] == "INT_WALL" else 0
                    for t in bg]
        item_flags = [1 if t else 0 for t in items]

        return {
            "background": bg,
            "items": items,
            "background_flags": bg_flags,
            "items_flags": item_flags,
            "background_slots": [INTERIOR_BACKGROUND_TILES[t][2] for t in bg],
            "items_slots": [INTERIOR_ITEMS_TILES[t][2] for t in items],
        }

    def officers(self) -> list[tuple[int, int]]:
        """Where the room's officers start, in room tiles.

        They wander from there on the same four-direction rule the street crowd
        uses, so these are opening positions rather than posts -- spread out
        enough that the room is not a queue on the first frame. Empty for every
        room but the station, where nobody is on duty.
        """
        return [(x, y)
                for y in range(self.height) for x in range(self.width)
                if self.room[y][x] == "O"]

    def exit(self) -> tuple[int, int]:
        """Where the mat is: the trigger to step back out into the city."""
        for y in range(self.height):
            for x in range(self.width):
                if self.room[y][x] == "M":
                    return x, y
        raise ValueError(f"{self.key} has no way out")

    def walk_distances(self) -> dict[tuple[int, int], int]:
        """Steps from the exit mat to every cell of the room it can reach.

        The room's one measure of distance, in one place. A straight line
        across a fifteen-cell room is not what walking it costs -- the front
        desk runs seven cells wide and the holding cell is behind bars -- so
        anything asking "how far in is this" asks the floor, here.
        """
        room = self.layers()
        w, h = self.width, self.height

        def free(x: int, y: int) -> bool:
            i = y * w + x
            return not room["background_flags"][i] and not room["items_flags"][i]

        start = self.exit()
        dist = {start: 0}
        queue = [start]
        while queue:
            x, y = queue.pop(0)
            for nx, ny in ((x, y - 1), (x + 1, y), (x, y + 1), (x - 1, y)):
                if (0 <= nx < w and 0 <= ny < h
                        and (nx, ny) not in dist and free(nx, ny)):
                    dist[(nx, ny)] = dist[(x, y)] + 1
                    queue.append((nx, ny))
        return dist

    def marked_officer(self) -> int:
        """Which officer chapter 2 sends the player in to shoot.

        The one FURTHEST from the mat: a target by the door is shot from the
        doorway and the room never happens, while the far one has to be walked
        to past everybody else on duty, pistol already drawn. In steps and not
        a straight line, because the desk is solid and a target three cells
        away behind it is a target eleven cells away.

        Ties break on the earlier post, so the answer follows the room's plan
        and not the flood fill's order. Returns -1 for a room with nobody in it
        -- every room but the station -- which validate() turns into a failure
        rather than a silent -1.
        """
        posts = self.officers()
        if not posts:
            return -1
        dist = self.walk_distances()
        return max(range(len(posts)),
                   key=lambda i: (dist.get(posts[i], -1), -i))

    def service(self) -> tuple[int, int] | None:
        """Where the player is served, or None for a room that sells nothing.

        One per room at most: two tills would be two prompts on the same
        counter and the scene would have to pick, which is a decision with no
        right answer rather than a feature.
        """
        spots = [(x, y)
                 for y in range(self.height) for x in range(self.width)
                 if self.room[y][x] == "T"]
        if len(spots) > 1:
            raise ValueError(f"{self.key} has {len(spots)} tills")
        return spots[0] if spots else None


POLICE_STATION = Interior(
    key="police_station",
    title="The police station's ground floor",
    doc=[
        "The place the city takes you when it catches you, and the only",
        "interior with anybody in it who cares what your wanted level is.",
        "",
        "Where the door is, is the city's business and lives in city_scene.h",
        "as POLICE_DOOR_TILE_X/Y. What is behind it is here.",
    ],
    staffed=True,
    # Floor the player is deliberately not meant to reach. The holding cell is
    # behind bars, so the reachability check exempts it -- and fails if it ever
    # becomes reachable, because a cell you can walk into and not out of is
    # worse than no cell at all.
    sealed="C",
    room=[
        "###############",
        "#.....#CCCCCCC#",
        "#..O..#CCCCCCC#",
        "#.....#CCCCCCC#",
        "#.....#BBBBBBB#",
        "#....O........#",
        "#..DDDDDDD....#",
        "#..h.......O..#",
        "#.............#",
        "#..p.......F..#",
        "#.............#",
        "#............N#",
        "#.............#",
        "#.............#",
        "#######M#######",
    ],
)

CORNER_SHOP = Interior(
    key="corner_shop",
    title="The corner shop's floor",
    doc=[
        "A room with nobody in it, which is the entire point of it. The",
        "station is full of police and hides nobody; this is the first door",
        "in the demo that can.",
        "",
        "It is not free. game/rules/Hideout.h holds the wanted clock for",
        "eight seconds when the force watched the player come through the",
        "door, so a shopfront is cover that has to be earned rather than a",
        "button that clears the level.",
        "",
        "Where the door is lives in city_scene.h as SHOP_DOOR_TILE_X/Y.",
    ],
    room=[
        "###############",
        "#.............#",
        "#.SSSSS.SSSSS.#",
        "#.............#",
        "#.SSSSS.SSSSS.#",
        "#.............#",
        "#.GGG.....p...#",
        "#.............#",
        "#....DDDDD....#",
        "#......T......#",
        "#.............#",
        "#.p.........F.#",
        "#.............#",
        "#.............#",
        "#######M#######",
    ],
)

# The order here is the order they are written and the order CityWorld binds
# them; the city's door constants name them individually, so nothing depends
# on the index.
INTERIORS = [POLICE_STATION, CORNER_SHOP]
STREET_TOP = 10

# The player's collision box inside their 16x16 cell, mirrored from
# game/CityConstants.h (kPlayerBoxOffsetX/Y, kPlayerBoxWidth/Height). One check
# uses it, for one thing: an entrance is drawn as transparent pixels rather
# than declared as a door tile, so whether a doorway is a doorway is a question
# about the ART, and the art is here.
PLAYER_BOX = (3, 9, 10, 6)

# How far a street band must stay clear of the water. Streets are clipped to
# the stretch where their whole five-tile width is this far inland, which is
# what stops asphalt from running off the shore into the sea.
BAND_WATER_MARGIN = 3

# The kerb cap is allowed one tile closer to the shore than the band it closes,
# but never close enough to touch the water.
CAP_WATER_MARGIN = 2

# A clipped band shorter than this is dropped rather than left as an orphan
# stub that connects nothing.
BAND_MIN_RUN = BLOCK_PITCH

# District boundaries, in tile rows/columns. Each one falls on the far edge of
# a street band rather than inside it, so a band is never half gravel path and
# half asphalt.
DOWNTOWN_BOTTOM = 64                    # through the y=60 avenue
BELT_BOTTOM = PROMENADE_Y - 1
PARK_RIGHT = 59                         # up to the x=60 street, exclusive
MARINA_RIGHT = 80                       # through the x=76 street

ZONE_CHUNK = 8          # district grid resolution, in tiles


def zone_at(x: int, y: int) -> int:
    if y <= DOWNTOWN_BOTTOM:
        return Z_DOWNTOWN
    if y <= BELT_BOTTOM:
        if x <= PARK_RIGHT:
            return Z_PARK
        if x <= MARINA_RIGHT:
            return Z_MARINA
        return Z_SUBURBS
    return Z_BEACH


def smooth_noise(rng: random.Random, length: int, control: int,
                 amplitude: float) -> list[float]:
    """Piecewise-linear noise over ``length`` samples.

    Low frequency on purpose: a per-tile random offset would give the island a
    saw-toothed coast, which reads as noise rather than as a shoreline.
    """
    points = [rng.uniform(-amplitude, amplitude) for _ in range(control + 1)]
    out = []
    for i in range(length):
        t = i * control / max(1, length - 1)
        lo = int(t)
        hi = min(control, lo + 1)
        frac = t - lo
        out.append(points[lo] * (1.0 - frac) + points[hi] * frac)
    return out


class City:
    """Three parallel tile grids, exactly as the Tilemap Editor models them."""

    def __init__(self, seed: int):
        self.rng = random.Random(seed)
        self.bg = [[BG["WATER"]] * MAP_W for _ in range(MAP_H)]
        self.items = [[0] * MAP_W for _ in range(MAP_H)]
        self.details = [[0] * MAP_W for _ in range(MAP_H)]
        self.zone = [[Z_OCEAN] * MAP_W for _ in range(MAP_H)]
        self.shore: list[list[int]] = []                   # filled by inland()
        self.junctions: set[tuple[int, int]] = set()        # filled by roads()
        self.bands: list[tuple[int, bool, int, int]] = []   # filled by roads()
        # (start, horizontal, first, last), filled by lanes(). The same bands
        # clipped to the stretches that are really carriageway -- a band that
        # crosses the park is two records, and the gravel path is neither.
        self.lane_bands: list[tuple[int, bool, int, int]] = []
        # (x, y, length, horizontal) per painted zebra strip, filled by
        # crosswalks(). Grouped from the tiles that were really painted, not
        # from where the junctions ought to be: a crossing is only drawn where
        # the road it crosses got as far as that approach.
        self.crossings: list[tuple[int, int, int, int]] = []
        # (tile x, tile y, heading index, colour index), filled by
        # place_vehicles(). Nothing is written to a tile layer for these.
        self.vehicles: list[tuple[int, int, int, int]] = []
        # Top-left tile of the one police station, filled by
        # single_police_station(), and of the one enterable shop, filled by
        # single_corner_shop(). One interior each; police_door() and
        # shop_door() are where the per-stamp entrance offset is written once.
        self.police: tuple[int, int] = (0, 0)
        self.shop: tuple[int, int] = (0, 0)
        # (tile x, tile y, weapons::WeaponId) per pickup, filled by
        # place_weapons(). Like the cars, nothing is written to a tile layer
        # for these: a pickup is an actor.
        self.weapons: list[tuple[int, int, int]] = []
        # Courier drop points, filled by place_missions(). One per district,
        # so that every leg of a run crosses the city rather than a block.
        self.missions: list[tuple[int, int]] = []
        # Contract payphones -- one per chapter of the main story, filled by
        # place_contract_work(), which appends all three; validate() fails the
        # build if fewer than three came out. Nothing is stamped: the phone is
        # a ring on the ground, the same way the courier drop is.
        self.phones: list[tuple[int, int]] = []
        # Chapter 1, "Boost": which parked car is the mark and which drop it
        # has to be brought to. Indices into self.vehicles and self.missions,
        # because both of those are already emitted and a second copy of a
        # coordinate is a second thing to keep in step.
        self.boost_vehicle: int = -1
        self.boost_drop: int = -1
        # Memo for district_at(). zone_chunks() is a full sweep of the map and
        # the placement below asks it once per car; it is only valid after
        # classify_zones(), which is why it is cleared here rather than built.
        self._zone_chunks_cache: list[int] | None = None

    # -- accessors -------------------------------------------------------
    def in_bounds(self, x: int, y: int) -> bool:
        return 0 <= x < MAP_W and 0 <= y < MAP_H

    def bg_name(self, x: int, y: int) -> str:
        if not self.in_bounds(x, y):
            return "WATER"
        return BACKGROUND_TILES[self.bg[y][x]][0]

    def item_name(self, x: int, y: int) -> str:
        if not self.in_bounds(x, y):
            return "BLANK"
        return ITEMS_TILES[self.items[y][x]][0]

    def put_bg(self, x: int, y: int, tile: int) -> None:
        if self.in_bounds(x, y):
            self.bg[y][x] = tile

    def put_item(self, x: int, y: int, tile: int) -> None:
        if self.in_bounds(x, y):
            self.items[y][x] = tile

    # -- props ------------------------------------------------------------
    def _soft_ground(self, x: int, y: int) -> bool:
        return self.bg_name(x, y) in ("GRASS", "DIRT", "SAND")

    def _variant(self, x: int, y: int, name: str) -> str | None:
        """Canonical prop name -> the art variant drawn on this ground.

        The layout only knows TREE, LAMP, BENCH and so on; the art has one
        drawing per ground because the cast shadow is baked in the prop's
        own colour family (green over grass, brown over pavement, orange
        over sand). This is the one table that maps between the two.
        """
        soft = self._soft_ground(x, y)
        sand = self.bg_name(x, y) == "SAND"
        if name == "TREE":
            return "TREE_G" if soft else "TREE_P"
        if name == "BUSH":
            return "BUSH_G" if soft else "BUSH_P"
        if name == "LAMP":
            return "LAMP_G" if soft else "LAMP_P"
        if name == "BENCH":
            return "BENCH" if soft else "BENCH_P"
        if name == "FOUNTAIN":
            return "FOUNTAIN_G" if soft else "FOUNTAIN"
        if name == "PALM":
            return "PALM_S" if sand else "PALM_P"
        if name == "ROCK" and sand:
            return self.rng.choice(BEACH_ROCKS)
        return name if name in IT else None

    def put_prop(self, x: int, y: int, name: str) -> None:
        """Place a single-tile prop by its canonical name.

        The prop's cast shadow, when the art has one, goes on the Details
        layer so collision ignores it; a "_SPILL" shadow continues it into
        the cell to the right when that cell is free and on the same ground.
        """
        if not self.in_bounds(x, y):
            return
        variant = self._variant(x, y, name)
        if variant is None:
            return
        self.put_item(x, y, IT[variant])
        self.put_detail(x, y, DT.get(variant + "_SH", 0))
        spill = DT.get(variant + "_SPILL_SH")
        if (spill is not None and self.is_empty(x + 1, y)
                and self.bg_name(x + 1, y) == self.bg_name(x, y)):
            self.put_detail(x + 1, y, spill)

    def put_detail(self, x: int, y: int, tile: int) -> None:
        if self.in_bounds(x, y):
            self.details[y][x] = tile

    def remove_prop(self, x: int, y: int) -> None:
        """Clear a cell and everything that belongs with it.

        A prop takes its shadow with it, including the spill in the next
        cell, and a stamp tile takes the whole stamp: half a truck is worse
        than no truck.
        """
        if not self.in_bounds(x, y) or self.items[y][x] == 0:
            return
        name = self.item_name(x, y)
        stamp = STAMP_OF.get(name)
        if stamp is not None:
            stamp_name, index = stamp
            w, h, _tiles = STAMPS[stamp_name]
            x0, y0 = x - index % w, y - index // w
            cells = [(x0 + i, y0 + j) for j in range(h) for i in range(w)]
        else:
            cells = [(x, y)]
        for (cx, cy) in cells:
            self.put_item(cx, cy, 0)
            self.put_detail(cx, cy, 0)
        spill = DT.get(name + "_SPILL_SH")
        if (spill is not None and self.in_bounds(x + 1, y)
                and self.details[y][x + 1] == spill):
            self.put_detail(x + 1, y, 0)

    def fill_bg(self, x0: int, y0: int, x1: int, y1: int, tile: int) -> None:
        """Repaint terrain and clear whatever stood on it."""
        for y in range(max(0, y0), min(MAP_H, y1 + 1)):
            for x in range(max(0, x0), min(MAP_W, x1 + 1)):
                self.remove_prop(x, y)
                self.bg[y][x] = tile
                self.details[y][x] = 0

    def is_land(self, x: int, y: int) -> bool:
        return self.bg_name(x, y) != "WATER"

    def block_is_land(self, x0: int, y0: int, x1: int, y1: int) -> bool:
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if not self.is_land(x, y):
                    return False
        return True

    def is_empty(self, x: int, y: int) -> bool:
        return self.in_bounds(x, y) and self.items[y][x] == 0
    # -- pass 1: the island ---------------------------------------------
    def coastline(self) -> None:
        """Carve a rounded island out of open water.

        The outline is a superellipse (``|dx|^4 + |dy|^4 <= 1``) so the corners
        round off without the shape becoming a circle, plus a low-frequency
        wobble per axis so the shore is not a machine-cut curve. Foam is not
        painted here: dress() picks the water edges after the ponds are in.
        """
        cx = (MAP_W - 1) / 2.0
        cy = (MAP_H - 1) / 2.0
        rx = MAP_W / 2.0 - 5.0
        ry = MAP_H / 2.0 - 6.0
        wob_x = smooth_noise(self.rng, MAP_H, 7, 2.5)
        wob_y = smooth_noise(self.rng, MAP_W, 7, 2.5)

        for y in range(MAP_H):
            for x in range(MAP_W):
                ax = abs(x - cx) / (rx + wob_x[y])
                ay = abs(y - cy) / (ry + wob_y[x])
                if ax ** 4 + ay ** 4 <= 1.0:
                    self.bg[y][x] = BG["SAND"]
    def inland(self) -> None:
        """Turn the island's interior into buildable grass.

        The rim stays sand, which is what makes the beach read as a distinct
        district all the way around the island rather than only in the south.
        """
        # Cached before anything paints over the coastline: _paint_band()'s
        # street clipping reads it, and later ponds and pools are not shore.
        self.shore = self._shore_distance()
        for y in range(MAP_H):
            for x in range(MAP_W):
                if self.bg[y][x] != BG["SAND"]:
                    continue
                if y > BELT_BOTTOM:
                    continue        # the whole southern strip stays beach
                if self.shore[y][x] >= 4:
                    self.bg[y][x] = BG["GRASS"]

    def _shore_distance(self) -> list[list[int]]:
        """Chebyshev distance from every cell to the nearest water cell."""
        far = MAP_W + MAP_H
        dist = [[0 if self.bg_name(x, y) == "WATER" else far
                 for x in range(MAP_W)] for y in range(MAP_H)]
        for y in range(MAP_H):
            for x in range(MAP_W):
                best = dist[y][x]
                for dy in (-1, 0):
                    for dx in (-1, 0, 1):
                        if dy == 0 and dx >= 0:
                            continue
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < MAP_H and 0 <= nx < MAP_W:
                            best = min(best, dist[ny][nx] + 1)
                dist[y][x] = best
        for y in range(MAP_H - 1, -1, -1):
            for x in range(MAP_W - 1, -1, -1):
                best = dist[y][x]
                for dy in (0, 1):
                    for dx in (-1, 0, 1):
                        if dy == 0 and dx <= 0:
                            continue
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < MAP_H and 0 <= nx < MAP_W:
                            best = min(best, dist[ny][nx] + 1)
                dist[y][x] = best
        return dist

    # -- pass 2: the street grid -----------------------------------------
    def roads(self) -> None:
        """Resolve every band, derive the junction boxes, then paint.

        Two phases because a junction has to be known before either of the two
        bands that form it is drawn: the centre lines inside the box have to be
        suppressed on the *first* band as well as the second.
        """
        bands: list[tuple[int, bool, int, int]] = []

        def resolve(start: int, horizontal: bool, lo: int, hi: int) -> None:
            run = self._supported_run(start, horizontal, lo, hi)
            if run is None or (run[1] - run[0] + 1) < BAND_MIN_RUN:
                return
            bands.append((start, horizontal, run[0], run[1]))

        for y0 in H_AVENUES + [PROMENADE_Y]:
            resolve(y0, True, 0, MAP_W - 1)
        for x0 in V_STREETS:
            # Vertical streets stop inside the promenade rather than running on
            # into the open sand. They T-junction with the coastal road, which
            # is what carries traffic along the shore.
            resolve(x0, False, STREET_TOP, PROMENADE_Y + ROAD_BAND - 1)

        self.junctions = set()
        for (hy, h_is_h, hf, hl) in bands:
            if not h_is_h:
                continue
            for (vx, v_is_h, vf, vl) in bands:
                if v_is_h:
                    continue
                for y in range(max(vf, hy), min(vl, hy + ROAD_BAND - 1) + 1):
                    for x in range(max(hf, vx), min(hl, vx + ROAD_BAND - 1) + 1):
                        self.junctions.add((x, y))

        self.bands = bands
        for (start, horizontal, first, last) in bands:
            self._paint_band(start, horizontal, first, last)

    def _supported_run(self, start: int, horizontal: bool,
                       lo: int, hi: int) -> tuple[int, int] | None:
        """Longest stretch of a band whose full width sits on solid inland.

        A band is five tiles wide, so painting it wherever there happens to be
        land leaves half-width roads hanging over the shore and asphalt running
        into the sea. Requiring the whole cross-section to be inland, and
        `BAND_WATER_MARGIN` tiles clear of the water, is what makes every
        street end on solid ground. The longest *contiguous* run rather than
        every supported tile, so a band interrupted by a bay does not reappear
        as an orphan fragment on the far side.
        """
        length = MAP_W if horizontal else MAP_H
        lo = max(0, lo)
        hi = min(length - 1, hi)
        if lo > hi:
            return None

        best: tuple[int, int] | None = None
        run_start: int | None = None
        for i in range(lo, hi + 2):          # one past the end, to close a run
            supported = i <= hi and all(
                self._is_inland(*((i, start + o) if horizontal else (start + o, i)))
                for o in range(ROAD_BAND)
            )
            if supported:
                if run_start is None:
                    run_start = i
            elif run_start is not None:
                if best is None or (i - run_start) > (best[1] - best[0] + 1):
                    best = (run_start, i - 1)
                run_start = None
        return best

    def _is_inland(self, x: int, y: int) -> bool:
        if not self.in_bounds(x, y):
            return False
        return self.shore[y][x] >= BAND_WATER_MARGIN

    def _paint_band(self, start: int, horizontal: bool,
                    first: int, last: int) -> None:
        # One cross-section of kerb past each end, so a street terminates in a
        # pavement cap instead of a raw cut of asphalt against the sand.
        for i in range(first - 1, last + 2):
            cap = i < first or i > last
            for offset in range(ROAD_BAND):
                x, y = (i, start + offset) if horizontal else (start + offset, i)
                if not self.is_land(x, y):
                    continue
                if cap and self.shore[y][x] < CAP_WATER_MARGIN:
                    # The cap sits one step outside the supported run, so it is
                    # the one piece of a street that can reach the shore, and
                    # without this guard it is where pavement touches the sea.
                    continue
                zone = zone_at(x, y)
                if zone == Z_BEACH and self.bg_name(x, y) == "SAND":
                    # Keep the promenade paved but never pour asphalt over the
                    # open sand south of it.
                    if start != PROMENADE_Y:
                        continue
                if zone == Z_PARK:
                    # The grid still runs through the park, so every district
                    # stays connected, but as a gravel path with grass verges:
                    # the park never reads as a road.
                    tile = (BG["GRASS"] if cap or offset in (0, ROAD_BAND - 1)
                            else BG["DIRT"])
                elif cap or offset in (0, ROAD_BAND - 1):
                    tile = BG["SIDEWALK"]
                elif offset == ROAD_BAND // 2:
                    tile = BG["ROAD_LINE_H"] if horizontal else BG["ROAD_LINE_V"]
                else:
                    tile = BG["ROAD"]

                # A crossing band must not pave over the other band's core, or
                # its kerb columns cut two brick strips clean across the
                # asphalt, through the middle of the junction.
                if (tile in (BG["SIDEWALK"], BG["GRASS"])
                        and self.bg_name(x, y) in BAND_CORE):
                    continue

                if (x, y) in self.junctions and tile in (BG["ROAD_LINE_H"],
                                                         BG["ROAD_LINE_V"]):
                    # A junction box is bare asphalt. Two crossing dashed
                    # centre lines read as a mistake, and real junctions do not
                    # have them.
                    tile = BG["ROAD"]

                self.put_bg(x, y, tile)
                self.put_item(x, y, 0)
                self.put_detail(x, y, 0)

    def crosswalks(self) -> None:
        """Zebra crossings on every approach to every intersection.

        The painted tiles are recorded as well as drawn. Pedestrians are kept
        off the carriageway except here (see src/game/rules/Lanes.h), and the
        rule has to hear where "here" is from whatever put the paint down:
        deriving it from the junction grid would mark an approach the
        `_is_road` guard skipped, opening a hole in the kerb exactly where the
        road it was supposed to cross does not exist.
        """
        painted: set[tuple[int, int, bool]] = set()
        for y0 in H_AVENUES + [PROMENADE_Y]:
            for x0 in V_STREETS:
                for lane in (1, 2, 3):
                    for dy in (-1, ROAD_BAND):
                        if self._is_road(x0 + lane, y0 + dy):
                            self.put_bg(x0 + lane, y0 + dy, BG["CROSSWALK_V"])
                            painted.add((x0 + lane, y0 + dy, True))
                    for dx in (-1, ROAD_BAND):
                        if self._is_road(x0 + dx, y0 + lane):
                            self.put_bg(x0 + dx, y0 + lane, BG["CROSSWALK_H"])
                            painted.add((x0 + dx, y0 + lane, False))
        self.crossings = self._group_crossings(painted)

    @staticmethod
    def _group_crossings(
            painted: set[tuple[int, int, bool]]
    ) -> list[tuple[int, int, int, int]]:
        """Collapse painted crossing tiles into maximal one-tile-thick runs.

        A strip that runs east-west is a run of x at one y, and one that runs
        north-south is a run of y at one x. Grouping keeps the emitted table at
        a few hundred bytes instead of one record per tile, and the runtime
        test a handful of comparisons instead of a scan of every stripe.
        """
        runs: list[tuple[int, int, int, int]] = []
        for horizontal in (True, False):
            cells = sorted((x, y) for (x, y, h) in painted if h == horizontal)
            # Sort along the strip's axis so consecutive tiles are adjacent in
            # the list: for a north-south strip, y within x.
            cells.sort(key=lambda c: (c[1], c[0]) if horizontal
                       else (c[0], c[1]))
            start: tuple[int, int] | None = None
            length = 0
            previous: tuple[int, int] | None = None
            for cell in cells:
                contiguous = (
                    previous is not None
                    and (cell[1] == previous[1] and cell[0] == previous[0] + 1
                         if horizontal
                         else cell[0] == previous[0]
                         and cell[1] == previous[1] + 1)
                )
                if not contiguous:
                    if start is not None:
                        runs.append((start[0], start[1], length,
                                     1 if horizontal else 0))
                    start = cell
                    length = 0
                length += 1
                previous = cell
            if start is not None:
                runs.append((start[0], start[1], length,
                             1 if horizontal else 0))
        return runs

    def _is_road(self, x: int, y: int) -> bool:
        return self.bg_name(x, y) in ("ROAD", "ROAD_LINE_H", "ROAD_LINE_V")

    # -- pass 2c: what the traffic may drive on ---------------------------
    def lanes(self) -> None:
        """Clip every band to the stretches that are really carriageway.

        `self.bands` is where a street was resolved, not where a car may drive:
        a band crossing the park is painted as a gravel path with grass verges,
        and both ends finish in a pavement cap. Emitting the raw bands would
        put self-driving traffic on the park path and drive it into the kerb at
        the shore. Every qualifying run is kept, not just the longest -- a band
        interrupted by the park is two streets in line, and dropping one would
        silently halve the traffic network on that row. Runs after
        `crosswalks()` on purpose: a zebra is painted over the lane it crosses,
        so a crossing tile is a lane tile, and treating it as anything else
        makes every junction approach a hole traffic refuses.
        """
        self.lane_bands = []
        for (start, horizontal, first, last) in self.bands:
            for (lo, hi) in self._drivable_runs(start, horizontal, first, last):
                self.lane_bands.append((start, horizontal, lo, hi))

    def _drivable_runs(self, start: int, horizontal: bool,
                       first: int, last: int) -> list[tuple[int, int]]:
        """Every run of at least LANE_MIN_RUN tiles where BOTH lanes are road.

        Both, not either: the lanes are the two directions of travel, and a
        stretch where only one is asphalt is one where half the traffic drives
        onto grass.
        """
        runs: list[tuple[int, int]] = []
        run_start: int | None = None
        for i in range(first, last + 2):        # one past the end, to close it
            drivable = i <= last and all(
                self.bg_name(*((i, start + o) if horizontal else (start + o, i)))
                in LANE_SURFACE
                for o in (LANE_NEAR, LANE_FAR)
            )
            if drivable:
                if run_start is None:
                    run_start = i
            elif run_start is not None:
                if (i - run_start) >= LANE_MIN_RUN:
                    runs.append((run_start, i - 1))
                run_start = None
        return runs

    # -- pass 3: the blocks between the streets ---------------------------
    def blocks(self) -> None:
        """Walk every rectangle the street grid leaves behind.

        One dispatch point, four district treatments: this is what makes the
        districts read differently while sharing a single road network.
        """
        v_edges = [(x + ROAD_BAND, x + BLOCK_PITCH - 1) for x in V_STREETS]
        # Clamped to the row above the promenade: otherwise the last block row
        # runs to y = 103 and paints over four of the coastal road's five rows,
        # leaving the seafront looking like a row of back gardens.
        h_edges = [(y + ROAD_BAND, min(y + BLOCK_PITCH - 1, PROMENADE_Y - 1))
                   for y in H_AVENUES]

        for (y0, y1) in h_edges:
            for (x0, x1) in v_edges:
                x1 = min(x1, MAP_W - 1)
                y1 = min(y1, MAP_H - 1)
                if x1 - x0 < 3 or y1 - y0 < 3:
                    continue
                if not self.block_is_land(x0, y0, x1, y1):
                    self._shore_block(x0, y0, x1, y1)
                    continue
                zone = zone_at((x0 + x1) // 2, (y0 + y1) // 2)
                if zone == Z_DOWNTOWN:
                    self._downtown_block(x0, y0, x1, y1)
                elif zone == Z_PARK:
                    self._park_block(x0, y0, x1, y1)
                elif zone == Z_MARINA:
                    self._marina_block(x0, y0, x1, y1)
                elif zone == Z_SUBURBS:
                    self._suburb_block(x0, y0, x1, y1)

    def _shore_block(self, x0: int, y0: int, x1: int, y1: int) -> None:
        """A block the coastline cuts through: keep only its land part green."""
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if self.bg_name(x, y) != "GRASS":
                    continue
                roll = self.rng.random()
                if roll < 0.10:
                    self.put_prop(x, y, "TREE")
                elif roll < 0.22:
                    self.put_detail(x, y, DT["GRASS_TUFT"])

    # -- downtown ---------------------------------------------------------
    def _downtown_block(self, x0: int, y0: int, x1: int, y1: int) -> None:
        """Pack the block interior with fixed-footprint building stamps.

        Packed in rows across the whole interior rather than carving the block
        into lots first, which ends up as four small buildings adrift in
        pavement.
        """
        self.fill_bg(x0, y0, x1, y1, BG["SIDEWALK"])
        roll = self.rng.random()
        if roll < 0.08:
            self.fill_bg(x0 + 1, y0 + 1, x1 - 1, y1 - 1, BG["PARKING"])
            self._stamp_rows(x0 + 1, y0 + 1, x1 - 1, (y0 + y1) // 2 - 1,
                             DOWNTOWN_LOTS)
        elif roll < 0.18:
            # A pocket square: greenery and a fountain.
            self.fill_bg(x0 + 1, y0 + 1, x1 - 1, y1 - 1, BG["GRASS"])
            self.put_prop((x0 + x1) // 2, (y0 + y1) // 2, "FOUNTAIN")
            for _ in range(8):
                px = self.rng.randint(x0 + 1, x1 - 1)
                py = self.rng.randint(y0 + 1, y1 - 1)
                if self.is_empty(px, py):
                    self.put_prop(px, py, "TREE")
        else:
            self._stamp_rows(x0 + 1, y0 + 1, x1 - 1, y1 - 1, DOWNTOWN_LOTS)
        self._dress_pavement(x0, y0, x1, y1)
    # A city never shows a bare pavement: every block edge carries lamps, a
    # traffic light at the corner, bins, hydrants, signs and the odd street
    # tree. An empty terracotta ring is most of what made this demo read as a
    # diagram rather than a city.
    PAVEMENT_FURNITURE = ["BIN", "HYDRANT", "SIGN", "TREE", "BUSH"]

    def _dress_pavement(self, x0: int, y0: int, x1: int, y1: int) -> None:
        for (px, py) in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
            self.put_prop(px, py, "TRAFFIC")
        self.put_prop((x0 + x1) // 2, y0, "LAMP")
        self.put_prop((x0 + x1) // 2, y1, "LAMP")
        self.put_prop(x0, (y0 + y1) // 2, "LAMP")
        self.put_prop(x1, (y0 + y1) // 2, "LAMP")

        ring = ([(x, y0) for x in range(x0, x1 + 1)]
                + [(x, y1) for x in range(x0, x1 + 1)]
                + [(x0, y) for y in range(y0 + 1, y1)]
                + [(x1, y) for y in range(y0 + 1, y1)])
        for (px, py) in ring:
            if not self.is_empty(px, py):
                continue
            if self.bg_name(px, py) != "SIDEWALK":
                continue
            if self.rng.random() < 0.22:
                self.put_prop(px, py, self.rng.choice(self.PAVEMENT_FURNITURE))

    def _lot(self, x0: int, y0: int, x1: int, y1: int) -> None:
        if x1 - x0 < 1 or y1 - y0 < 1:
            return
        self._stamp_fitting(x0, y0, x1, y1, DOWNTOWN_LOTS)

    def _stamp_fitting(self, x0: int, y0: int, x1: int, y1: int,
                       candidates: list[str]) -> None:
        """Fill a lot with as many stamps as fit side by side.

        Buildings are fixed footprints, so a lot is filled largest first with a
        tile of pavement between them; the remainder stays pavement rather than
        being padded with a kiosk.
        """
        x = x0
        while x <= x1:
            fits = [n for n in candidates
                    if STAMPS[n][0] <= x1 - x + 1
                    and STAMPS[n][1] <= y1 - y0 + 1]
            if not fits:
                return
            widest = max(STAMPS[n][0] for n in fits)
            name = self.rng.choice([n for n in fits
                                    if STAMPS[n][0] >= widest - 1])
            w, _h, _tiles = STAMPS[name]
            self._stamp_named(x, y0, name)
            x += w + 1

    def _stamp_rows(self, x0: int, y0: int, x1: int, y1: int,
                    candidates: list[str]) -> None:
        """Rows of stamps, each row as tall as its tallest building."""
        y = y0
        while y <= y1:
            fits_any = [n for n in candidates if STAMPS[n][1] <= y1 - y + 1]
            if not fits_any:
                return
            row_h = 0
            x = x0
            while x <= x1:
                fits = [n for n in fits_any if STAMPS[n][0] <= x1 - x + 1]
                if not fits:
                    break
                name = self.rng.choice(fits)
                w, h, _tiles = STAMPS[name]
                self._stamp_named(x, y, name)
                row_h = max(row_h, h)
                x += w + 1
            y += row_h + 1

    def _stamp_named(self, x0: int, y0: int, name: str) -> None:
        """Lay a stamp's tiles with their shadows, if the art has any.

        Buildings carry their drop shadow inside the stamp and have none here;
        the truck and the pool do.
        """
        w, h, tiles = STAMPS[name]
        for j in range(h):
            for i in range(w):
                tile = tiles[j * w + i]
                self.put_item(x0 + i, y0 + j, IT[tile])
                self.put_detail(x0 + i, y0 + j, DT.get(tile + "_SH", 0))

    def _stamp_fits(self, x0: int, y0: int, name: str) -> bool:
        w, h, _tiles = STAMPS[name]
        return all(self.is_empty(x0 + i, y0 + j)
                   for j in range(h) for i in range(w))

    # -- the one building you can walk into --------------------------------
    # Whichever 3x2 stamp is put in its place. GLASS_BLUE and HALL_BROWN are
    # the only other buildings with that footprint, and the glass tower is the
    # one that does not read as a second civic building.
    POLICE_REPLACEMENT = "GLASS_BLUE"

    def single_police_station(self) -> None:
        """Collapse the layout's police stations down to exactly one.

        The station is the demo's only interior, and two would be two doors
        into the same room -- a bug the first time a player walks through both.
        A post-pass over the finished layout rather than dropping POLICE from
        DOWNTOWN_LOTS, because of the RNG: the lot packer draws from the same
        stream as every pass after it, so removing one candidate shifts every
        subsequent draw and regenerates the whole city. Rewriting the surplus
        stamps afterwards changes six tiles each and leaves the island byte for
        byte where it was. The one kept is nearest the player's spawn, so the
        interior is somewhere they cross rather than tucked against the coast.
        """
        head = IT["POLICE_0"]
        origins = [(x, y)
                   for y in range(MAP_H) for x in range(MAP_W)
                   if self.items[y][x] == head]
        if not origins:
            raise ValueError("the layout placed no police station")

        sx, sy = self.spawn()
        # Ties broken by position, not by iteration order: the layout must not
        # depend on which way the scan happened to run.
        keep = min(origins, key=lambda p: (abs(p[0] - sx) + abs(p[1] - sy),
                                           p[1], p[0]))
        for origin in origins:
            if origin != keep:
                self._stamp_named(origin[0], origin[1], self.POLICE_REPLACEMENT)
        self.police = keep

    def single_corner_shop(self) -> None:
        """Turn exactly one of the layout's blue shops into one you can enter.

        SHOP_OPEN is SHOP_BLUE with its bottom-left corner cut away, so this is
        a swap of one 2x2 stamp for another of the same footprint on a lot the
        packer already chose: nothing moves, the RNG stream is not touched, and
        -- as with the police station -- the island stays byte for byte.
        Exactly one, for the reason the station is: two doors into the same
        room is a bug the first time a player walks through both.

        The shop kept is the nearest to the spawn whose doorway can be walked
        up to. That second condition is not decoration -- the entrance is
        transparent pixels inside the stamp, so a shop whose front pavement is
        a wall or a hedge is a building the player watches the hint appear on
        and can never enter.
        """
        head = IT["SHOP_BLUE_0"]
        origins = [(x, y)
                   for y in range(MAP_H) for x in range(MAP_W)
                   if self.items[y][x] == head]
        if not origins:
            raise ValueError("the layout placed no blue shop")

        sx, sy = self.spawn()
        # The approach cell, one below the doorway. Checked before the swap
        # because a stamp is not walkable either way: what has to be clear is
        # the pavement the player stands on.
        def approachable(origin: tuple[int, int]) -> bool:
            dx, dy = origin[0], origin[1] + 1
            return self._walkable(dx, dy + 1)

        usable = [o for o in origins if approachable(o)]
        if not usable:
            raise ValueError("no blue shop has a walkable doorway approach")
        # Ties broken by position, not by iteration order: the layout must not
        # depend on which way the scan happened to run.
        keep = min(usable, key=lambda p: (abs(p[0] - sx) + abs(p[1] - sy),
                                          p[1], p[0]))
        self._stamp_named(keep[0], keep[1], "SHOP_OPEN")
        self.shop = keep

    def police_door(self) -> tuple[int, int]:
        """The station's entrance notch, in city tiles.

        The POLICE stamp is 3x2 and draws its U-shaped notch as open ground in
        the bottom-centre cell, so the offset is (+1, +1). Written once here
        rather than at the four call sites that spelled it out.
        """
        return self.police[0] + 1, self.police[1] + 1

    def shop_door(self) -> tuple[int, int]:
        """The shop's open front, in city tiles.

        SHOP_OPEN is 2x2 and its cut-away corner is the bottom-LEFT cell, so
        this offset is not the station's -- which is why both are methods: the
        door is a property of the stamp's art, and a shared constant would be a
        lie about one of them.
        """
        return self.shop[0], self.shop[1] + 1
    # -- park -------------------------------------------------------------
    def _park_block(self, x0: int, y0: int, x1: int, y1: int) -> None:
        self.fill_bg(x0, y0, x1, y1, BG["GRASS"])
        # One guaranteed lake at the heart of the park, plus the odd extra
        # pond. Leaving it to chance alone often produced a park with none.
        centre = (x0 <= PARK_RIGHT // 2 <= x1 and
                  y0 <= (DOWNTOWN_BOTTOM + BELT_BOTTOM) // 2 <= y1)
        if centre or self.rng.random() < 0.12:
            self._pond(x0, y0, x1, y1)
            return
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                roll = self.rng.random()
                if roll < 0.24:
                    self.put_prop(x, y, "TREE")
                elif roll < 0.36:
                    self.put_prop(x, y, "BUSH")
                elif roll < 0.50:
                    self.put_detail(x, y, DT["GRASS_TUFT"])
                elif roll < 0.62:
                    self.put_detail(x, y, DT["FLOWERS"])
        # A two-tile gravel cross through the middle. Random tree cover alone
        # can seal a block off; this guarantees the block stays crossable.
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        self.fill_bg(cx, y0, cx + 1, y1, BG["DIRT"])
        self.fill_bg(x0, cy, x1, cy + 1, BG["DIRT"])
        # Beside the gravel cross, never on it: DIRT counts as a band core, and
        # a bench in the middle of a path is what the "nothing stands in a
        # carriageway" check exists to catch.
        self.put_prop(cx - 1, cy - 1, "BENCH")
        self.put_prop(cx + 2, cy + 2, "BENCH")

    def _pond(self, x0: int, y0: int, x1: int, y1: int) -> None:
        cx, cy = (x0 + x1) / 2.0, (y0 + y1) / 2.0
        rx, ry = (x1 - x0) / 2.0, (y1 - y0) / 2.0
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                d = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
                if d <= 0.62:
                    self.put_bg(x, y, BG["WATER"])
                elif d <= 0.85:
                    self.put_bg(x, y, BG["SAND"])
                elif self.rng.random() < 0.25:
                    self.put_detail(x, y, DT["GRASS_TUFT"])

    # -- marina plaza ------------------------------------------------------
    def _marina_block(self, x0: int, y0: int, x1: int, y1: int) -> None:
        self.fill_bg(x0, y0, x1, y1, BG["PLAZA"])
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
        roll = self.rng.random()
        if roll < 0.4:
            self.put_prop(cx, cy, "FOUNTAIN")
        elif roll < 0.7:
            self._lot(cx - 2, cy - 1, cx + 2, cy + 2)
        for dx in (-1, 1):
            self.put_prop(cx + dx * 4, y0 + 1, "BENCH")
            self.put_prop(cx + dx * 4, y1 - 1, "BENCH")
        for (px, py) in ((x0, y0), (x1, y0), (x0, y1), (x1, y1)):
            self.put_prop(px, py, "LAMP")
        for (px, py) in (((x0 + x1) // 2, y0), ((x0 + x1) // 2, y1)):
            if self.is_empty(px, py):
                self.put_prop(px, py, "SIGN")
        for _ in range(3):
            px = self.rng.randint(x0 + 1, x1 - 1)
            py = self.rng.randint(y0 + 1, y1 - 1)
            if self.is_empty(px, py):
                self.put_prop(px, py, "BUSH")

    # -- suburbs -----------------------------------------------------------
    def _suburb_block(self, x0: int, y0: int, x1: int, y1: int) -> None:
        """Two fenced house lots stacked inside one city block."""
        self.fill_bg(x0, y0, x1, y1, BG["GRASS"])
        mid = (y0 + y1) // 2
        self._house_lot(x0, y0, x1, mid - 1)
        self._house_lot(x0, mid + 1, x1, y1)

    def _house_lot(self, x0: int, y0: int, x1: int, y1: int) -> None:
        if x1 - x0 < 5 or y1 - y0 < 3:
            return
        # Half the gardens are hedged rather than fenced. A suburb where every
        # boundary is the same white railing reads as one repeated stamp.
        h, v = (("HEDGE_H", "HEDGE_V") if self.rng.random() < 0.5
                else ("FENCE_H", "FENCE_V"))
        for x in range(x0, x1 + 1):
            self.put_prop(x, y0, h)
            self.put_prop(x, y1, h)
        for y in range(y0, y1 + 1):
            self.put_prop(x0, y, v)
            self.put_prop(x1, y, v)
        # Gate, so every garden is enterable on foot.
        self.remove_prop((x0 + x1) // 2, y1)

        house = self.rng.choice(SUBURB_HOUSES)
        hw, _hh, _tiles = STAMPS[house]
        self._stamp_named(x0 + 1, y0 + 1, house)
        gx = x0 + hw + 2
        if (gx + 1 < x1 and self.rng.random() < 0.45
                and self._stamp_fits(gx, y0 + 1, "POOL")):
            self._stamp_named(gx, y0 + 1, "POOL")
        elif gx < x1:
            self.put_prop(gx, y0 + 2, "TREE")
        if self.is_empty(x0 + 2, y1 - 1):
            self.put_prop(x0 + 2, y1 - 1, "BUSH")
        for _ in range(4):
            px = self.rng.randint(x0 + 1, x1 - 1)
            py = self.rng.randint(y0 + 1, y1 - 1)
            if self.is_empty(px, py):
                self.put_detail(px, py, DT["GRASS_TUFT"])

    # -- pass 5: parked, driveable vehicles ---------------------------------
    def place_vehicles(self) -> None:
        """Choose where the city's driveable cars are parked.

        Only on a carriageway tile whose neighbour across the kerb is pavement,
        so nothing parks in a lane it does not belong in, and never on a
        crossing or inside a junction box -- those are not named ROAD, so the
        first guard already excludes them. Nor on the centre line, which leaves
        the middle of every band clear.

        A car is an actor, not a tile: it is appended here as a spawn record,
        and the gap between two of them is a hard distance rather than a "leave
        one cell" rule -- twenty-four cars in a 128x128 city should read as a
        parked car here and there, not a rank. No spawn can seal the map: a
        parked car sits in a kerbside lane of a five-tile band, so the centre
        line beside it is free and the pavement it faces uncovered.
        validate()'s reachability check runs on tiles and would not see a car,
        which is why the rule has to hold by construction.
        """
        def kerbside(x: int, y: int) -> str | None:
            if self.bg_name(x, y) != "ROAD" or (x, y) in self.junctions:
                return None
            if not self.is_empty(x, y):
                return None
            if (self.bg_name(x, y - 1) == "SIDEWALK"
                    or self.bg_name(x, y + 1) == "SIDEWALK"):
                return "H"           # the street runs east-west
            if (self.bg_name(x - 1, y) == "SIDEWALK"
                    or self.bg_name(x + 1, y) == "SIDEWALK"):
                return "V"
            return None

        candidates = [(x, y, axis)
                      for y in range(1, MAP_H - 1)
                      for x in range(1, MAP_W - 1)
                      if (axis := kerbside(x, y)) is not None]
        self.rng.shuffle(candidates)

        # Heading: a car parks along its street, pointing either way. The
        # index is into city_art.VEHICLE_HEADINGS -- N, E, S, W.
        headings = {"H": ("E", "W"), "V": ("N", "S")}
        taken: list[tuple[int, int]] = []
        for (x, y, axis) in candidates:
            if len(self.vehicles) >= VEHICLE_COUNT:
                break
            if any(max(abs(x - tx), abs(y - ty)) < VEHICLE_MIN_GAP
                   for (tx, ty) in taken):
                continue
            heading = VEHICLE_HEADINGS.index(self.rng.choice(headings[axis]))
            colour = self.rng.randrange(len(VEHICLE_NAMES))
            self.vehicles.append((x, y, heading, colour))
            taken.append((x, y))

    # -- pass 6: prove the city is still walkable --------------------------
    # Props that may be deleted to reopen a sealed pocket, in the order they
    # are given up. Buildings and fences are absent on purpose: a fence has a
    # deliberate gate and a building is the point of the block. Beach props are
    # last -- a palm boxing in one cell of shore is rare but does happen.
    REMOVABLE = ("BIN", "HYDRANT", "SIGN", "TRAFFIC", "LAMP",
                 "BUSH", "TREE", "PALM", "PARASOL", "ROCK", "LOUNGER",
                 "BEACH_BALL")

    def unseal(self) -> None:
        """Delete whatever props have sealed a walkable pocket off the map.

        A block's pavement ring is one tile wide and already broken up by lamps
        and traffic lights; park a prop against every gap in the kerb and a
        seventeen-tile strip of it becomes an island. Tuning a placement
        probability until that stops happening is not a fix -- it is the same
        bug waiting on a different seed. (Cars are actors now and cannot seal
        anything; lamps, bins and traffic lights still can.) So connectivity is
        restored by construction: flood fill from the spawn, give up the
        cheapest prop bordering anything the fill could not reach, repeat until
        the map is whole. The reachability self-check then finds nothing.
        """
        for _ in range(8):
            reachable = self._reachable_from_spawn()
            stranded = [(x, y)
                        for y in range(MAP_H) for x in range(MAP_W)
                        if self._walkable(x, y) and (x, y) not in reachable]
            if not stranded:
                return
            freed = 0
            for (x, y) in stranded:
                for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if not self.in_bounds(nx, ny):
                        continue
                    if self.items[ny][nx] == 0:
                        continue
                    if self.item_name(nx, ny).startswith(self.REMOVABLE):
                        self.remove_prop(nx, ny)
                        freed += 1
            if freed == 0:
                return          # sealed by something we refuse to remove

    def _walkable(self, x: int, y: int) -> bool:
        return (self.bg_name(x, y) in BG_WALKABLE
                and self.items[y][x] == 0)

    def _reachable_from_spawn(self) -> set[tuple[int, int]]:
        sx, sy = self.spawn()
        seen = {(sx, sy)}
        stack = [(sx, sy)]
        while stack:
            x, y = stack.pop()
            for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (x + dx, y + dy)
                if n in seen or not self.in_bounds(*n):
                    continue
                if self._walkable(*n):
                    seen.add(n)
                    stack.append(n)
        return seen

    # -- pass 4: the shoreline dressing ------------------------------------
    def beach(self) -> None:
        for y in range(MAP_H):
            for x in range(MAP_W):
                if self.bg_name(x, y) != "SAND" or not self.is_empty(x, y):
                    continue
                roll = self.rng.random()
                south = y > BELT_BOTTOM
                if roll < (0.030 if south else 0.022):
                    self.put_prop(x, y, "PALM")
                elif south and roll < 0.055:
                    self.put_prop(x, y, "PARASOL")
                elif roll < (0.075 if south else 0.045):
                    self.put_prop(x, y, "ROCK")
                elif roll < 0.14:
                    self.put_detail(x, y, DT["SHELLS"])
                elif roll < 0.19:
                    self.put_detail(x, y, DT["PEBBLES"])

        # A walkable margin either side of the promenade, so the beach stays
        # reachable from the road whatever the dressing rolled.
        for y in range(PROMENADE_Y - 1, PROMENADE_Y + ROAD_BAND + 2):
            for x in range(MAP_W):
                if self.item_name(x, y) in BEACH_PROPS:
                    self.remove_prop(x, y)

    # -- districts and spawn ------------------------------------------------
    def classify_zones(self) -> None:
        for y in range(MAP_H):
            for x in range(MAP_W):
                if not self.is_land(x, y):
                    self.zone[y][x] = Z_OCEAN
                elif (self.bg_name(x, y) == "SAND"
                      or self.item_name(x, y) in BEACH_PROPS):
                    self.zone[y][x] = Z_BEACH
                else:
                    self.zone[y][x] = zone_at(x, y)

    def walkable(self, x: int, y: int) -> bool:
        if not self.in_bounds(x, y):
            return False
        return self.bg_name(x, y) in BG_WALKABLE and self.items[y][x] == 0

    def spawn(self) -> tuple[int, int]:
        """A walkable sidewalk tile near the middle of downtown.

        Matched by family rather than exact name so the answer survives dress()
        turning a kerb into a SIDEWALK_K variant: the reachability check and
        the emitted spawn have to agree.
        """
        cx, cy = MAP_W // 2, DOWNTOWN_BOTTOM // 2
        for r in range(0, 48):
            for dy in range(-r, r + 1):
                for dx in range(-r, r + 1):
                    if max(abs(dx), abs(dy)) != r:
                        continue
                    x, y = cx + dx, cy + dy
                    if (self.in_bounds(x, y)
                            and self.bg_name(x, y).startswith(("SIDEWALK", "PLAZA"))
                            and self.items[y][x] == 0):
                        return x, y
        return cx, cy
    def zone_chunks(self) -> list[int]:
        """Majority district per ZONE_CHUNK x ZONE_CHUNK block of tiles."""
        out: list[int] = []
        for cy in range(MAP_H // ZONE_CHUNK):
            for cx in range(MAP_W // ZONE_CHUNK):
                counts = [0] * len(ZONE_NAMES)
                for y in range(cy * ZONE_CHUNK, (cy + 1) * ZONE_CHUNK):
                    for x in range(cx * ZONE_CHUNK, (cx + 1) * ZONE_CHUNK):
                        counts[self.zone[y][x]] += 1
                # Ocean only wins a chunk it fully covers, so a coastal chunk
                # announces the land district the player is walking in.
                if counts[Z_OCEAN] == ZONE_CHUNK * ZONE_CHUNK:
                    out.append(Z_OCEAN)
                else:
                    counts[Z_OCEAN] = 0
                    out.append(counts.index(max(counts)))
        return out

    # -- export views -------------------------------------------------------
    def behaviour(self, layer: str) -> list[int]:
        """Dense TileFlags, one byte per cell, as the editor exports."""
        TILE_SOLID = 1
        out: list[int] = []
        if layer == "BACKGROUND":
            for y in range(MAP_H):
                for x in range(MAP_W):
                    solid = self.bg_name(x, y) not in BG_WALKABLE
                    out.append(TILE_SOLID if solid else 0)
        elif layer == "ITEMS":
            for y in range(MAP_H):
                for x in range(MAP_W):
                    out.append(TILE_SOLID if self.items[y][x] != 0 else 0)
        else:
            out = [0] * (MAP_W * MAP_H)
        return out

    def grid(self, layer: str) -> list[list[int]]:
        return {"BACKGROUND": self.bg, "ITEMS": self.items,
                "DETAILS": self.details}[layer]

    def indices(self, layer: str) -> list[int]:
        g = self.grid(layer)
        return [g[y][x] for y in range(MAP_H) for x in range(MAP_W)]

    def palette_indices(self, layer: str) -> list[int]:
        tiles = dict(LAYERS)[layer]
        g = self.grid(layer)
        return [tiles[g[y][x]][2] for y in range(MAP_H) for x in range(MAP_W)]

    def place_weapons(self) -> None:
        """Choose where the city's one free pistol lies.

        Runs after unseal() and classify_zones(), because a pickup has to be on
        a tile the player can walk to and both of those can still move one.
        Deterministic -- same seed, same gun -- so the demo opens the same way
        twice and the reachability check below means something, and it draws no
        randomness of its own, so it can be retuned without moving a building.

        ONE pistol, by design rather than budget. The island used to hold three
        that each returned after a four-block walk, because an emptied gun left
        the player unarmed and the street had to be the way back to a weapon.
        The corner shop is that way back now, and a street handing out free
        guns on a timer would be a shop with nothing to sell: every price in
        game/rules/Economy.h optional, and the courier run a score again.

        What is left is the teaching copy. A first-time player does not know
        the gun exists, so it is placed VISIBLE FROM THE SPAWN -- a few tiles
        out on a fifteen-tile viewport, close enough to be noticed and far
        enough not to be collected by accident. After that, weapons are bought.
        """
        self.weapons = []
        reachable = self._reachable_from_spawn()
        spawn = self.spawn()
        # Both doorways: a pickup in one is walked over on the way in and
        # never seen.
        doors = (self.police_door(), self.shop_door())

        def usable(x: int, y: int) -> bool:
            if (x, y) == spawn or (x, y) in doors:
                return False
            # Not under a parked car: the pickup is drawn on the ground and a
            # car over it would hide it completely.
            return not any(abs(vx - x) <= 1 and abs(vy - y) <= 1
                           for vx, vy, _h, _c in self.vehicles)

        # Downtown, within sight of the spawn: the weapon the player is meant
        # to find first and the only one they will find at all. The loop is
        # kept over a one-entry table rather than flattened, because the
        # far-placement branch below is what a second pickup would need and
        # deleting it would make re-adding one a rewrite.
        for zone, near_spawn, kind in ((Z_DOWNTOWN, True, WEAPON_PISTOL),):
            candidates = [(x, y) for (x, y) in sorted(reachable)
                          if self.zone[y][x] == zone and usable(x, y)]
            if not candidates:
                continue
            if near_spawn:
                # Closest to the target radius, not simply closest: right on
                # top of the player teaches nothing.
                pick = min(candidates,
                           key=lambda p: abs(abs(p[0] - spawn[0])
                                             + abs(p[1] - spawn[1])
                                             - WEAPON_SPAWN_RADIUS_TILES))
            else:
                placed = [(px, py) for px, py, _k in self.weapons] + [spawn]
                pick = max(candidates,
                           key=lambda p: min(abs(px - p[0]) + abs(py - p[1])
                                             for px, py in placed))
            self.weapons.append((pick[0], pick[1], kind))

    def place_missions(self) -> None:
        """Choose the courier's drop points: one per district.

        One per district and not one per block, because the point of a leg is
        the journey: two drops in the same neighbourhood is a mission the
        player finishes by turning round, and the streak stops meaning anything
        the first time that happens.

        Placed at the district's CENTRE OF MASS rather than at random within
        it. A random walkable tile is usually a scrap of pavement behind a
        building, which is a drop the player circles the block looking for; the
        middle of a district is somewhere they were driving through anyway. The
        nearest reachable tile to that centre is taken, so the marker is never
        inside the building that happens to sit on it. Runs after unseal() and
        classify_zones() for the same reason place_weapons() does: both can
        still move a walkable tile.
        """
        self.missions = []
        reachable = self._reachable_from_spawn()
        for zone in (Z_DOWNTOWN, Z_BEACH, Z_PARK, Z_MARINA, Z_SUBURBS):
            cells = [(x, y) for y in range(MAP_H) for x in range(MAP_W)
                     if self.zone[y][x] == zone]
            if not cells:
                continue
            cx = sum(x for x, _y in cells) // len(cells)
            cy = sum(y for _x, y in cells) // len(cells)
            candidates = [(x, y) for (x, y) in sorted(reachable)
                          if self.zone[y][x] == zone
                          and not self._on_carriageway(x, y)]
            if not candidates:
                continue
            self.missions.append(
                min(candidates, key=lambda p: abs(p[0] - cx) + abs(p[1] - cy)))

    def _on_carriageway(self, x: int, y: int) -> bool:
        """Is this tile road, kerb to kerb?

        Drops avoid it. A marker in a lane has to be collected from traffic --
        and since stage 6 the kerb rule will not let the player walk onto it at
        all, so a courier on foot would be refused the last tile.
        """
        for (start, horizontal, first, last) in self.lane_bands:
            across = (y - start) if horizontal else (x - start)
            along = x if horizontal else y
            if LANE_NEAR <= across <= LANE_FAR and first <= along <= last:
                return True
        return False

    def pedestrian_legal(self, x: int, y: int) -> bool:
        """Would the kerb rule let somebody on foot stand here?

        Walkable is the larger question and this the smaller: since stage 6 a
        pedestrian -- the player included -- is refused the carriageway except
        where a zebra is painted across it. Anything placing something the
        player walks TO has to ask this one, or it puts a marker on a tile the
        game will not let them finish the last step onto.
        """
        if not self.walkable(x, y):
            return False
        if self._on_carriageway(x, y):
            return self.bg_name(x, y) in ("CROSSWALK_H", "CROSSWALK_V")
        return True

    def foot_reach_from_spawn(self) -> set[tuple[int, int]]:
        """The pedestrian-legal tiles the player can really walk to.

        A DIFFERENT and strictly smaller set than _reachable_from_spawn(),
        which floods over every walkable tile including the lanes. Kept
        separate and named for it, because "is this tile reachable" has two
        answers here and the wrong one is a marker in the middle of a road.
        """
        start = self.spawn()
        seen = {start}
        stack = [start]
        while stack:
            x, y = stack.pop()
            for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                step = (x + dx, y + dy)
                if step in seen or not self.in_bounds(*step):
                    continue
                if self.pedestrian_legal(*step):
                    seen.add(step)
                    stack.append(step)
        return seen

    def district_at(self, x: int, y: int) -> int:
        """The district a tile is in, as the GAME will answer it.

        Through the coarse chunk grid, not self.zone: zoneAt() in C++ reads
        ZONE_GRID, the majority district of an 8x8 chunk. A rule stated in
        districts ("the car is in another neighbourhood") has to be decided by
        the same grid the running game decides it by, or the self-check passes
        over a map the player experiences differently.
        """
        chunks = self._zone_chunks_cache
        if chunks is None:
            chunks = self._zone_chunks_cache = self.zone_chunks()
        return chunks[(y // ZONE_CHUNK) * (MAP_W // ZONE_CHUNK)
                      + (x // ZONE_CHUNK)]

    def clear_of_doors(self, x: int, y: int) -> bool:
        """Is this tile at least a block from every interior doorway?

        A phone on a doormat is not a place. Chapter 2 needs the walk to the
        station to BE the mission; chapter 3 needs its corner to be street
        rather than shopfront, because the crowd it is about is streamed on to
        pavement. Chapter 1's phone is exempt on purpose -- it sits four tiles
        from the station door because it sends the player the other way, and
        the door is scenery they walk past.
        """
        return all(abs(dx - x) + abs(dy - y) >= BLOCK_PITCH
                   for (dx, dy) in (self.police_door(), self.shop_door()))

    def crowd_room(self, x: int, y: int) -> int:
        """How much pedestrian-legal ground is within half a block of here.

        The crowd is a pool of twelve slots streamed on to walkable pavement
        around the camera, so this is the closest thing the finished city has
        to a population density: a corner with more standing room gets handed
        more people, at a rate no player input can raise. Half a block rather
        than a whole one because a rampage is fought at the corner the phone is
        on -- ground a block away is ground the player has to leave the fight
        to reach.
        """
        reach = BLOCK_PITCH // 2
        return sum(1
                   for dy in range(-reach, reach + 1)
                   for dx in range(-reach + abs(dy), reach - abs(dy) + 1)
                   if self.pedestrian_legal(x + dx, y + dy))

    def place_contract_work(self) -> None:
        """Choose the story payphones, and the job hanging off the first.

        Derived, never drawn: every coordinate below is READ from the finished
        city, with no tile written, no prop placed and, above all, no random
        number drawn -- the stream is shared, and one extra roll here moves
        every building, car and palm tree downstream of it. So this is
        arithmetic over what place_vehicles(), place_weapons() and
        place_missions() decided, and it runs last because it chooses between
        what they placed. Chapter 1's phone, car and drop come first, then
        chapter 2's phone relative to where chapter 1 ends, then chapter 3's.
        Chapter 2's MARK is the one thing not chosen here: it is a cell of the
        station's ASCII plan, which Interior.marked_officer() reads off the
        room as this reads the city.

        THE PHONE. Chapter 1 has to be stumbled into by a player who has never
        seen the demo, so the phone is the pedestrian-legal tile NEAREST THE
        SPAWN, one block clear of every other ring in the game -- the drops and
        the pistol are rings on the ground too, and two on one corner cannot be
        told apart.

        THE CAR. Not the car nearest the phone -- a job you finish by turning
        round is not a job -- but the nearest in a DIFFERENT district: near
        because the player walks there, elsewhere because that is the shortest
        honest description of "somewhere else". The one police cruiser is
        skipped, boosting the city's own patrol car being a joke for the wanted
        system to make later rather than the first lesson, and so is the car
        nearest the spawn, already spoken for as scenery on the way to the
        pistol. THE DROP is then the courier drop FARTHEST from that car and in
        a district of its own again, so the chapter ends in a drive across the
        island rather than a nudge around the corner.

        THE THIRD PHONE. Chapter 3 is a rampage -- a clock and a body count,
        no destination and nothing to drive -- so its corner is chosen last and
        for the only thing it needs, people: the legal site with the most
        pedestrian-legal ground around it. A rampage handed out in a cul-de-sac
        is a chapter the player fails while walking, with a clock and a counter
        on screen that cannot say why.
        """
        self.phones = []
        self.boost_vehicle = -1
        self.boost_drop = -1
        if not self.vehicles or not self.missions:
            return

        spawn = self.spawn()
        foot_reach = self.foot_reach_from_spawn()
        doors = (self.police_door(), self.shop_door())
        # Every other marker drawn as a ring on the ground.
        rings = list(self.missions) + [(x, y) for x, y, _k in self.weapons]

        def far_from_other_rings(x: int, y: int) -> bool:
            # The phones already handed out are rings too: the rule keeping a
            # phone off a courier drop is the rule keeping chapter 2's phone
            # off chapter 1's, written once so the two cannot come apart.
            return all(abs(rx - x) + abs(ry - y) >= BLOCK_PITCH
                       for (rx, ry) in rings + self.phones)

        def under_a_car(x: int, y: int) -> bool:
            # The ring is drawn on the ground; a parked car over it hides it.
            return any(abs(vx - x) <= 1 and abs(vy - y) <= 1
                       for vx, vy, _h, _c in self.vehicles)

        def phone_sites() -> list[tuple[int, int]]:
            """Every tile a payphone may stand on, as the city stands NOW.

            Recomputed per chapter rather than filtered once, because
            far_from_other_rings() reads self.phones: the set shrinks by a
            block with every chapter handed out, which is the point.
            """
            return [(x, y) for (x, y) in sorted(foot_reach)
                    if (x, y) != spawn
                    and (x, y) not in doors
                    and not under_a_car(x, y)
                    and far_from_other_rings(x, y)]

        candidates = phone_sites()
        if not candidates:
            return
        phone = min(candidates,
                    key=lambda p: (abs(p[0] - spawn[0]) + abs(p[1] - spawn[1]),
                                   p))
        self.phones.append(phone)

        def nearest_vehicle(px: int, py: int) -> int:
            return min(range(len(self.vehicles)),
                       key=lambda i: (abs(self.vehicles[i][0] - px)
                                      + abs(self.vehicles[i][1] - py), i))

        def walkable_up_to(x: int, y: int) -> bool:
            # A car parks kerbside, on carriageway, so its own tile is never
            # pedestrian-legal. What has to hold is that the player can get to
            # the door: some tile touching it is foot-reachable pavement.
            return any((x + dx, y + dy) in foot_reach
                       for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)))

        spoken_for = {nearest_vehicle(*phone), nearest_vehicle(*spawn)}
        phone_district = self.district_at(*phone)
        marks = [i for i, (vx, vy, _h, colour) in enumerate(self.vehicles)
                 if i not in spoken_for
                 and VEHICLE_NAMES[colour] != "CAR_POLICE"
                 and self.district_at(vx, vy) != phone_district
                 and walkable_up_to(vx, vy)]
        if not marks:
            return
        self.boost_vehicle = min(
            marks, key=lambda i: (abs(self.vehicles[i][0] - phone[0])
                                  + abs(self.vehicles[i][1] - phone[1]), i))

        bx, by, _h, _c = self.vehicles[self.boost_vehicle]
        car_district = self.district_at(bx, by)
        drops = [i for i, (mx, my) in enumerate(self.missions)
                 if self.district_at(mx, my) != car_district]
        if not drops:
            return
        self.boost_drop = max(
            drops, key=lambda i: (abs(self.missions[i][0] - bx)
                                  + abs(self.missions[i][1] - by), -i))

        # THE SECOND PHONE. Chapter 2 rings where chapter 1 left the player:
        # the nearest legal site to the drop they have just walked away from a
        # car at. The phone could be anywhere, but a call arriving where the
        # last job ended is a city that knows what the player just did, at the
        # cost of one line of arithmetic over a coordinate already decided.
        # Chosen out of the same phone_sites(), which now counts chapter 1's
        # phone among the rings, so it obeys every rule the first obeys -- plus
        # one: a block clear of the station door. Chapter 2 is a walk to the
        # station and back out with the stars on, and a job handed out on the
        # doormat is not a journey; the first phone may sit four tiles from
        # that door precisely because it sends the player the other way.
        dropx, dropy = self.missions[self.boost_drop]
        second = [(x, y) for (x, y) in phone_sites()
                  if self.clear_of_doors(x, y)]
        if not second:
            return
        self.phones.append(min(second,
                               key=lambda p: (abs(p[0] - dropx)
                                              + abs(p[1] - dropy), p)))

        # THE THIRD PHONE. Chapter 3 has no destination to be near and no
        # previous job to follow on from -- the corner IS the mission -- so it
        # is chosen for the one thing a rampage consumes: people. The site with
        # the most pedestrian-legal ground within half a block is the busiest
        # corner the city has, because the crowd is streamed on to exactly that
        # ground and nowhere else. Same doors rule as chapter 2, for a
        # different reason: that one is about the journey, this one about the
        # room -- a rampage started on a doormat is fought from inside a
        # building the crowd never enters.
        third = [(x, y) for (x, y) in phone_sites()
                 if self.clear_of_doors(x, y)]
        if not third:
            return
        self.phones.append(max(third,
                               key=lambda p: (self.crowd_room(*p),
                                              -p[0], -p[1])))

    def generate(self) -> None:
        self.coastline()
        self.inland()
        self.roads()
        self.blocks()
        self.single_police_station()
        self.single_corner_shop()
        self.crosswalks()
        self.beach()
        self.lanes()
        self.place_vehicles()
        self.unseal()
        self.classify_zones()
        self.place_weapons()
        self.place_missions()
        self.place_contract_work()

# --------------------------------------------------------------------------
# Self-checks
# --------------------------------------------------------------------------
# These run on every generation and raise rather than warn. Each one exists
# because the defect it looks for shipped once: they are regression tests for
# the layout, in the only place a layout bug can be caught cheaply.
def validate(city: City, tilesets: dict[str, list[Canvas]],
             night_tilesets: dict[str, list[Canvas]]) -> list[str]:
    report: list[str] = []
    failures: list[str] = []
    mid = ROAD_BAND // 2
    name = city.bg_name

    # 1. Tile index 0 is the renderer's "skip this cell" value, so Background
    #    -- which has nothing beneath it -- must never use it.
    blanks = sum(1 for row in city.bg for t in row if t == 0)
    report.append(f"blank background cells     : {blanks}")
    if blanks:
        failures.append(f"{blanks} background cells use tile index 0")

    # 2. Background tiles are the bottom of the stack and must be fully opaque.
    #    Props must NOT be: an opaque prop is solid across its whole cell in
    #    every collision mode -- the single-layer trap this format avoids.
    for i, canvas in enumerate(tilesets["BACKGROUND"]):
        if i and not canvas.is_opaque_everywhere():
            failures.append(f"background tile {BACKGROUND_TILES[i][0]} "
                            f"has transparent pixels")
    #    Stamps are the exception: a tile from the middle of a roof has
    #    nothing to cut out, and it is solid on purpose.
    stamp_tiles = set(STAMP_OF)
    opaque_props = [ITEMS_TILES[i][0] for i, c in enumerate(tilesets["ITEMS"])
                    if i and c.is_opaque_everywhere()
                    and ITEMS_TILES[i][0] not in stamp_tiles]
    report.append(f"props with no cut-out      : {len(opaque_props)}")
    if opaque_props:
        failures.append(f"prop tiles are fully opaque, so per-pixel collision "
                        f"cannot see past them: {opaque_props[:4]}")

    # 3. No pavement may reach the sea. Streets are clipped to a run that keeps
    #    their full width inland; a regression here puts asphalt on the shore.
    wet = ("WATER",)
    paved = ("ROAD", "ROAD_LINE_H", "ROAD_LINE_V", "CROSSWALK_H",
             "CROSSWALK_V", "SIDEWALK")
    at_water = [
        (x, y) for y in range(MAP_H) for x in range(MAP_W)
        if name(x, y) in paved
        and any(name(x + dx, y + dy) in wet
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
    ]
    report.append(f"paved tiles touching water : {len(at_water)}")
    if at_water:
        failures.append(f"{len(at_water)} paved tiles touch water, "
                        f"e.g. {at_water[:4]}")

    # 4. A junction box is bare carriageway with four kerb corners and no
    #    centre line. Whether a crossing exists is decided from the four
    #    approaches, one tile OUTSIDE the box: the defect hunted here is a
    #    corrupted box, which read from inside would pass as "no crossing".
    crossings = 0
    intruders: list[tuple[int, int, str]] = []
    centre_lines: list[tuple[int, int, str]] = []
    for hy in H_AVENUES + [PROMENADE_Y]:
        for vx in V_STREETS:
            if (vx < 1 or hy < 1
                    or vx + ROAD_BAND >= MAP_W or hy + ROAD_BAND >= MAP_H):
                continue
            h_here = (name(vx - 1, hy + mid) in BAND_CORE
                      and name(vx + ROAD_BAND, hy + mid) in BAND_CORE)
            v_here = (name(vx + mid, hy - 1) in BAND_CORE
                      and name(vx + mid, hy + ROAD_BAND) in BAND_CORE)
            if not (h_here and v_here):
                continue
            crossings += 1
            for dy in range(ROAD_BAND):
                for dx in range(ROAD_BAND):
                    here = name(vx + dx, hy + dy)
                    if dx in (0, ROAD_BAND - 1) and dy in (0, ROAD_BAND - 1):
                        continue                # corners are meant to be kerb
                    if here not in BAND_CORE:
                        intruders.append((vx + dx, hy + dy, here))
                    elif here in ("ROAD_LINE_H", "ROAD_LINE_V"):
                        centre_lines.append((vx + dx, hy + dy, here))
    report.append(f"street crossings           : {crossings}")
    report.append(f"kerb tiles inside crossings: {len(intruders)}")
    report.append(f"centre lines in crossings  : {len(centre_lines)}")
    if intruders:
        failures.append(f"{len(intruders)} kerb/verge tiles sit inside a "
                        f"junction box, e.g. {intruders[:4]}")
    if centre_lines:
        failures.append(f"{len(centre_lines)} centre-line tiles sit inside a "
                        f"junction box, e.g. {centre_lines[:4]}")

    # 5. A street, once painted, must stay a street. Later passes paint over
    #    it -- a block rectangle that overruns its bounds quietly erases part
    #    of a road, and the city looks fine until you walk into the gap.
    holes: list[tuple[int, int, str]] = []
    for (start, horizontal, first, last) in city.bands:
        for i in range(first, last + 1):
            x, y = (i, start + mid) if horizontal else (start + mid, i)
            here = name(x, y)
            if here in BAND_CORE:
                continue
            if here == "SAND" and start != PROMENADE_Y:
                continue      # the beach rule declines to pave open sand
            holes.append((x, y, here))
    report.append(f"holes punched in streets   : {len(holes)}")
    if holes:
        failures.append(f"{len(holes)} street tiles were overwritten after "
                        f"being paved, e.g. {holes[:4]}")

    # 6. Nothing may stand in a carriageway -- possible to get wrong only
    #    because props have their own layer. No exception since cars became
    #    actors: a prop on asphalt is always the defect it was written for, a
    #    bench dropped onto the gravel path it was meant to sit beside.
    on_road = [(x, y, city.item_name(x, y))
               for y in range(MAP_H) for x in range(MAP_W)
               if city.items[y][x] != 0 and name(x, y) in BAND_CORE]
    report.append(f"props standing in a street : {len(on_road)}")
    if on_road:
        failures.append(f"{len(on_road)} props sit on a carriageway, "
                        f"e.g. {on_road[:4]}")

    # 6b. Every vehicle spawn must be a free kerbside carriageway cell, and no
    #     two may share one. A car is an actor that blocks the player, so a
    #     spawn on a prop or on another car is a vehicle welded into scenery on
    #     the first frame, with no way to tell which of the two is at fault.
    bad_spawn = [(x, y) for (x, y, _h, _c) in city.vehicles
                 if name(x, y) != "ROAD" or city.items[y][x] != 0
                 or (x, y) in city.junctions]
    report.append(f"vehicles parked            : {len(city.vehicles)}")
    if bad_spawn:
        failures.append(f"{len(bad_spawn)} vehicle spawns are not on a free "
                        f"carriageway cell, e.g. {bad_spawn[:4]}")
    if len(city.vehicles) != len({(x, y) for (x, y, _h, _c) in city.vehicles}):
        failures.append("two vehicles spawn on the same tile")
    if len(city.vehicles) < VEHICLE_COUNT:
        failures.append(f"only {len(city.vehicles)} of {VEHICLE_COUNT} "
                        f"vehicles found a kerbside slot")

    # 7. Details never collide, so nothing on that layer may carry a flag.
    det_flags = sum(city.behaviour("DETAILS"))
    report.append(f"solid flags on Details     : {det_flags}")
    if det_flags:
        failures.append("the Details layer carries collision flags")

    # 8. Every district has to be walkable to from the spawn, or the city is
    #    not explorable. Tile-level flood fill: cheap, and a false pass needs a
    #    one-tile-wide corridor, which the block treatments never produce.
    sx, sy = city.spawn()
    seen = {(sx, sy)}
    queue = [(sx, sy)]
    while queue:
        x, y = queue.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if (nx, ny) not in seen and city.walkable(nx, ny):
                seen.add((nx, ny))
                queue.append((nx, ny))
    for zid, zname in enumerate(ZONE_NAMES):
        if zid == Z_OCEAN:
            continue
        total = sum(1 for y in range(MAP_H) for x in range(MAP_W)
                    if city.zone[y][x] == zid and city.walkable(x, y))
        reached = sum(1 for (x, y) in seen if city.zone[y][x] == zid)
        report.append(f"  {zname:<9} reachable   : {reached}/{total}")
        if total and not reached:
            failures.append(f"district {zname} is unreachable from the spawn")

    # 9. The interiors. Three ways to break a hand-laid room, all invisible in
    # the source and obvious the moment you walk in: a gap in the wall,
    # furniture across the door, and a spawn cell inside the desk. A room is
    # fifteen cells square, so checking every one of them costs nothing.
    for iv in INTERIORS:
        room = iv.layers()
        ex, ey = iv.exit()
        w, h = iv.width, iv.height

        def room_free(x: int, y: int, room=room, w=w) -> bool:
            i = y * w + x
            return not room["background_flags"][i] and not room["items_flags"][i]

        holes = [(x, y)
                 for y in range(h) for x in range(w)
                 if (x in (0, w - 1) or y in (0, h - 1))
                 and not room["background_flags"][y * w + x]
                 and (x, y) != (ex, ey)]
        if holes:
            failures.append(f"{iv.key}: the wall has {len(holes)} gap(s) in "
                            f"it besides the exit: {holes[:4]}")

        spawn_free = room_free(ex, ey - 1)
        report.append(f"{iv.key} spawn free".ljust(27) + f": {spawn_free}")
        if not spawn_free:
            failures.append(f"{iv.key}: the spawn cell is not walkable")

        # The flood fill from the mat, through Interior's own measure of it:
        # chapter 2 picks its mark by the same distances, and one room cannot
        # have two ideas about what it costs to cross it.
        seen = set(iv.walk_distances())
        # A room may name floor the player is deliberately not meant to reach
        # -- the station's holding cell is behind bars -- so those cells are
        # excluded here. Anything else unreachable is a mistake.
        walkable = sum(1 for y in range(h) for x in range(w)
                       if room_free(x, y)
                       and iv.room[y][x] not in iv.sealed)
        reached = sum(1 for (x, y) in seen
                      if iv.room[y][x] not in iv.sealed)
        report.append(f"{iv.key} reachable".ljust(27) + f": {reached}/{walkable}")
        if reached != walkable:
            failures.append(f"{iv.key}: {walkable - reached} cells are walled "
                            f"off from the exit mat")
        if iv.sealed:
            stuck = [(x, y) for (x, y) in seen if iv.room[y][x] in iv.sealed]
            if stuck:
                failures.append(f"{iv.key}: the sealed area is reachable at "
                                f"{stuck[:4]} -- a player who walks in cannot "
                                f"walk out")

        # An officer posted on furniture, or shut in the holding cell, either
        # cannot move or spends the demo behind bars. Only the station has any;
        # the shop is empty on purpose, which game/rules/Hideout.h prices.
        posts = iv.officers()
        report.append(f"{iv.key} officers".ljust(27) + f": {len(posts)}")
        if iv.staffed and not posts:
            failures.append(f"{iv.key}: nobody is on duty")
        if not iv.staffed and posts:
            failures.append(f"{iv.key}: {len(posts)} officer post(s) in a "
                            f"room that is not staffed")
        walled_in = [p for p in posts if p not in seen]
        if walled_in:
            failures.append(f"{iv.key}: officer posts {walled_in} are not "
                            f"reachable from the exit mat")

        # A till behind the counter it belongs to is a shop that cannot be
        # shopped in, and it looks exactly like a shop that can.
        till = iv.service()
        if till is not None:
            report.append(f"{iv.key} counter".ljust(27) + f": {till}")
            if not room_free(*till):
                failures.append(f"{iv.key}: the counter cell {till} is not "
                                f"walkable")
            elif till not in seen:
                failures.append(f"{iv.key}: the counter cell {till} cannot be "
                                f"reached from the exit mat")

    # 9b. The doorways, from the street side. A door cell is transparent
    # pixels inside a building stamp, so its tile is flagged solid and
    # `_walkable` says no -- right for a car, wrong for the player, who is
    # tested per pixel. What tile granularity CAN check is the pavement in
    # front: a door whose approach is walled off or sealed by a prop is a
    # building the player watches the hint appear on and can never enter.
    reachable = city._reachable_from_spawn()
    for label, (dx, dy) in (("police station", city.police_door()),
                            ("corner shop", city.shop_door())):
        front = (dx, dy + 1)
        report.append(f"{label} approach".ljust(27) + f": {front}")
        if not city._walkable(*front):
            failures.append(f"the {label} door at {(dx, dy)} is approached "
                            f"from {front}, which is not walkable")
        elif front not in reachable:
            failures.append(f"the {label} door at {(dx, dy)} cannot be walked "
                            f"to from the spawn")

        # And the doorway itself, the check the art can fail on its own. The
        # cell is inside a building stamp, so its Items tile is flagged solid
        # and only the per-pixel test finding a hole in the silhouette makes it
        # enterable; if the player's box does not fit that hole, the hint
        # appears on a door that will not open and nothing throws.
        #
        # Which placements count is PlayerActor's arithmetic, not a guess:
        # tileX is (spriteX + 8) / 16 and tileY is (spriteY + 9 + 3) / 16, so
        # the two ranges below are the sprite positions that REPORT this cell.
        # Tile alignment is not required -- the station's notch clears the box
        # one pixel down, and demanding it would fail the doorway the demo has
        # always shipped. Prop erosion is not modelled: it only thins a
        # silhouette, so a doorway that passes without it passes with it.
        bx, by, bw, bh = PLAYER_BOX

        def pixel_solid(px: int, py: int) -> bool:
            tx, ty = px // TILE, py // TILE
            if not city.in_bounds(tx, ty):
                return True
            if city.bg_name(tx, ty) not in BG_WALKABLE:
                return True
            tile = city.items[ty][tx]
            if tile == 0:
                return False
            return tilesets["ITEMS"][tile].rows()[py % TILE][px % TILE] != 0

        def box_free(sprite_x: int, sprite_y: int) -> bool:
            return not any(pixel_solid(sprite_x + bx + i, sprite_y + by + j)
                           for j in range(bh) for i in range(bw))

        stands = [(sx2, sy2)
                  for sy2 in range(dy * TILE - by - bh // 2,
                                   dy * TILE - by - bh // 2 + TILE)
                  for sx2 in range(dx * TILE - TILE // 2,
                                   dx * TILE - TILE // 2 + TILE)
                  if box_free(sx2, sy2)]
        report.append(f"{label} doorway".ljust(27)
                      + f": {len(stands)} of {TILE * TILE} places to stand")
        if not stands:
            failures.append(f"the {label} doorway at {(dx, dy)} is painted "
                            f"shut: the player's {bw}x{bh} box does not fit "
                            f"anywhere the cell would report as this tile")

    # Being caught puts the player back on the station steps -- the tile
    # directly below the entrance notch, which CityConstants.h derives as
    # POLICE_DOOR_TILE_Y + 1. Nothing at runtime can check it: a respawn onto a
    # solid tile wedges the player inside a building, one onto a tile the
    # streets do not connect to ends the run there. Neither throws.
    steps = (city.police_door()[0], city.police_door()[1] + 1)
    report.append(f"station steps              : {steps}")
    if not city._walkable(*steps):
        failures.append(f"the station steps at {steps} are not walkable -- "
                        f"a busted player would respawn inside a wall")
    elif steps not in reachable:
        failures.append(f"the station steps at {steps} cannot be walked to "
                        f"from the spawn")

    # A pistol on a tile the player cannot reach is a weapon that does not
    # exist, and a demo where the gun mechanic is simply never discovered.
    report.append(f"weapon pickups             : {len(city.weapons)}")
    if not city.weapons:
        failures.append("no weapon pickups were placed")
    stranded = [(x, y) for (x, y, _k) in city.weapons
                if (x, y) not in reachable]
    if stranded:
        failures.append(f"weapon pickups {stranded} cannot be walked to")

    # A caught player comes back UNARMED, and the free pistol may have gone
    # hours ago -- losing a fight now costs a walk to the corner shop and a
    # delivery to pay for it. Still reported: until the pistol is taken it is
    # the shortest way back to a weapon and should not be across the island.
    if city.weapons:
        to_gun = min(abs(x - steps[0]) + abs(y - steps[1])
                     for (x, y, _k) in city.weapons)
        report.append(f"steps to nearest gun       : {to_gun} tiles")

    # EXACTLY one, and this check is the economy. Both directions fail silently
    # and neither draws anything wrong. None, and a first-time player never
    # discovers there is shooting in the demo: no weapon, no reason to walk
    # into the shop, no way to learn what the money is for. Two or more, and
    # the shop is optional -- the ladder in game/rules/Economy.h (reload, vest,
    # shotgun) is priced on the counter being the only armoury once the free
    # pistol runs dry, and a second free gun makes every price a suggestion.
    if len(city.weapons) != 1:
        failures.append(f"{len(city.weapons)} weapon pickups on the map: the "
                        f"city hands out exactly one free pistol and sells "
                        f"everything after it -- CityConstants.h asserts the "
                        f"same count on the C++ side")
    # And it has to be the pistol. The shotgun is priced as the top of the
    # ladder; one lying in the street is that ladder skipped.
    pistols = [(x, y) for (x, y, k) in city.weapons if k == WEAPON_PISTOL]
    if not pistols:
        failures.append("the free weapon is not a pistol -- the one gun the "
                        "city gives away is the cheapest line on the "
                        "counter, not one the player would otherwise save for")
    sx, sy = city.spawn()
    if city.weapons:
        first = min(abs(x - sx) + abs(y - sy) for x, y, _k in city.weapons)
        report.append(f"nearest pickup to spawn    : {first} tiles")
        # Half the viewport. Further than this and a first-time player walks
        # the whole demo without ever finding out there is a weapon in it.
        if first > VIEWPORT_TILES // 2:
            failures.append(f"the nearest pistol is {first} tiles from the "
                            f"spawn -- nobody will find it")

        # And no shotgun lies anywhere. It used to be required -- one in the
        # suburbs, the reward for going somewhere -- back when there was no
        # counter and a weapon nobody could reach was one nobody fired. It is
        # the top of the shop's ladder now, at economy::kShotgunPrice, and one
        # in the street hands the player the best gun in the demo before their
        # first delivery, after which the money never means anything again.
        shotguns = [(x, y) for (x, y, k) in city.weapons
                    if k == WEAPON_SHOTGUN]
        report.append(f"shotguns placed            : {len(shotguns)}")
        if shotguns:
            failures.append(f"a shotgun was placed at {shotguns} -- the "
                            f"shotgun is bought, not found; a free one makes "
                            f"the counter's top line unreachable by being "
                            f"pointless")

    # The player palette's GREY is the URBAN palette's ASPHALT to the byte, put
    # there so the old minimap could nearest-match roads into it -- which, see
    # minimap_swatches, it never actually did. The check stays because the
    # coincidence is real whatever put it there: the first version of the
    # weapon drew the pistol in GREY, and a gun lying in the street was exactly
    # the colour of the street -- not subtle, simply impossible to find.
    grey = 1 + [n for n, _r, _g, _b in art_player.PALETTE].index("GREY")
    guns = {"pistol pickup": [art_player.pickup_grid()],
            "shotgun pickup": [art_player.shotgun_pickup_grid()]}
    for facing, cells in art_player.armed_frame_grids().items():
        guns[f"armed {facing}"] = cells
    greyed = sorted(name for name, grids in guns.items()
                    for grid in grids
                    for row in grid if grey in row)
    report.append(f"gun sprites using GREY     : {len(greyed)}")
    if greyed:
        failures.append(f"{greyed} draw a weapon in GREY, which is the "
                        f"asphalt colour -- it will be invisible on the road")

    # ---- The radar -------------------------------------------------------
    # Everything the overlay can get wrong, it gets wrong QUIETLY. It still
    # draws, it still runs, it is simply no longer answering the question. So
    # each way it can stop answering gets its own check.

    # A road family renamed in city_art and not here is the worst of them: the
    # set is matched by name, a name that matches nothing matches silently,
    # and the streets it covered just stop being streets on the map.
    missing = sorted(MINIMAP_ROAD - set(BG))
    if missing:
        failures.append(f"the radar's road set names {missing}, which are "
                        f"not background tiles -- those streets would vanish "
                        f"from the overlay with nothing else noticing")

    bg_ink = minimap_swatches("BACKGROUND")
    inks = set(bg_ink)
    allowed = {MINIMAP_INK_ROAD, MINIMAP_INK_GROUND, MINIMAP_INK_ABSENT}
    report.append(f"minimap inks               : {sorted(inks)}")
    if not inks <= allowed:
        failures.append(f"the radar emitted {sorted(inks - allowed)}, which "
                        f"is neither street, ground nor sea -- it has gone "
                        f"back to being a picture of the island")

    # And the coverage, which is what a rename would actually change. The
    # bounds are wide on purpose: not a target, but the range outside which the
    # overlay has stopped being a street map -- nothing painted, or everything.
    land = [(x, y) for y in range(MAP_H) for x in range(MAP_W)
            if bg_ink[city.bg[y][x]] != MINIMAP_INK_ABSENT]
    street = sum(1 for (x, y) in land
                 if bg_ink[city.bg[y][x]] == MINIMAP_INK_ROAD)
    share = (100 * street // len(land)) if land else 0
    report.append(f"radar street share of land : {share}% "
                  f"({street}/{len(land)})")
    if not 5 <= share <= 60:
        failures.append(f"{share}% of the island draws as street, which is "
                        f"not a street map either way")

    # Whether the two inks read far enough APART is deliberately not checked
    # here: their RGB lives in the engine's PaletteDefs.h, and a copy of that
    # table would be a second place to keep in step -- the same class of
    # mistake that made the radar purple. CityConstants.h asserts the indices
    # against gfx::Color instead; the names are the contract, and the engine
    # owns what they look like.
    if MINIMAP_INK_ROAD == MINIMAP_INK_GROUND:
        failures.append("the radar's street and ground inks are the same "
                        "index, so the overlay is one flat colour")

    # The lane table is what self-driving traffic reads instead of the map,
    # and every way it can be wrong produces a car that keeps driving rather
    # than a crash. So it is checked against the tiles it claims to describe.
    off_road: list[str] = []
    for (start, horizontal, first, last) in city.lane_bands:
        for i in range(first, last + 1):
            for o in (LANE_NEAR, LANE_FAR):
                x, y = (i, start + o) if horizontal else (start + o, i)
                if name(x, y) not in LANE_SURFACE:
                    off_road.append(f"({x},{y})={name(x, y)}")
    report.append(f"lane bands                 : {len(city.lane_bands)}")
    if off_road:
        failures.append(f"{len(off_road)} lane tiles are not carriageway, "
                        f"so traffic would drive over them: {off_road[:4]}")

    # Traffic that can only ever go one way is traffic nobody reads as
    # traffic, and a clipping regression that dropped every band of one
    # orientation would look exactly like a quiet city.
    horizontals = sum(1 for b in city.lane_bands if b[1])
    verticals = len(city.lane_bands) - horizontals
    report.append(f"  east-west / north-south  : {horizontals} / {verticals}")
    if horizontals < 2 or verticals < 2:
        failures.append(f"only {horizontals} east-west and {verticals} "
                        f"north-south lane bands survived clipping")

    # And a grid with no crossings is a set of parallel corridors: cars would
    # drive to the end of a street and never turn anywhere. One junction is
    # not enough to be interesting, but zero is the failure worth catching.
    crossings = sum(
        1
        for (hs, hh, hf, hl) in city.lane_bands if hh
        for (vs, vh, vf, vl) in city.lane_bands if not vh
        if hf <= vs + ROAD_BAND - 1 and vs <= hl
        and vf <= hs + ROAD_BAND - 1 and hs <= vl
    )
    report.append(f"  lane band crossings      : {crossings}")
    if crossings == 0:
        failures.append("no two lane bands cross, so traffic can never turn")

    # The one check that reads the table the way a car does: everything above
    # proves the lanes are asphalt, this proves they go somewhere. A network of
    # dead ends compiles, validates, spawns traffic and then quietly parks
    # every car against a kerb within seconds, which looks like traffic that
    # does not work rather than a map that does not connect. Dead ends are
    # peeled off repeatedly -- a tile with nowhere legal to go is one, and so
    # is a tile whose only exits are dead ends -- leaving exactly the tiles a
    # car can drive from forever.
    exits: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for (start, horizontal, first, last) in city.lane_bands:
        for i in range(first, last + 1):
            for o, d in ((LANE_NEAR, (-1, 0) if horizontal else (0, 1)),
                         (LANE_FAR, (1, 0) if horizontal else (0, -1))):
                tile = (i, start + o) if horizontal else (start + o, i)
                exits.setdefault(tile, []).append(d)
    lane_tiles = set(exits)
    live = dict(exits)
    while True:
        dead = {t for t, ds in live.items()
                if not any((t[0] + dx, t[1] + dy) in live for (dx, dy) in ds)}
        if not dead:
            break
        for t in dead:
            del live[t]
    stranded = len(lane_tiles) - len(live)
    report.append(f"  lane tiles / no way on   : {len(lane_tiles)} / {stranded}")
    # Some are expected and correct: the last few tiles of every clipped band
    # really are a dead end, and a driver turns away from them rather than
    # driving in. What must not happen is most of the network being one.
    if lane_tiles and stranded * 4 > len(lane_tiles):
        failures.append(f"{stranded} of {len(lane_tiles)} lane tiles lead "
                        f"nowhere -- traffic would park itself within seconds")

    # Crossings are the one hole in the kerb rule, so the table had better
    # describe zebra stripes and not asphalt. Every tile of every emitted run
    # is checked against what is painted there, and the total against the tiles
    # that carry the paint -- a run that lost its last tile to a grouping bug
    # would still pass the first test on its own.
    zebra = {"CROSSWALK_H", "CROSSWALK_V"}
    unpainted: list[str] = []
    covered = 0
    for (cx, cy, length, horizontal) in city.crossings:
        if length == 0:
            unpainted.append(f"({cx},{cy}) empty run")
        for i in range(length):
            x = cx + i if horizontal else cx
            y = cy if horizontal else cy + i
            covered += 1
            if name(x, y) not in zebra:
                unpainted.append(f"({x},{y})={name(x, y)}")
    painted_tiles = sum(1 for row in city.bg for t in row
                        if BACKGROUND_TILES[t][0] in zebra)
    report.append(f"crossing runs / tiles      : {len(city.crossings)} "
                  f"/ {covered}")
    if unpainted:
        failures.append(f"{len(unpainted)} crossing tiles are not zebra "
                        f"stripes: {unpainted[:4]}")
    if covered != painted_tiles:
        failures.append(f"the crossing table covers {covered} tiles but "
                        f"{painted_tiles} are painted -- a run was lost")

    # The kerb rule laid over the map the check above proved connected. That
    # proof used WALKABLE tiles, and pedestrians now refuse the carriageway
    # except at a zebra, so the city has to be connected a second time over a
    # strictly smaller set. If it is not, the crowd fragments into blocks that
    # never mix -- invisible until you notice the same four people circling the
    # same pavement all session. City.pedestrian_legal() rather than a copy of
    # the rule: the placement passes ask the same question, and a check that
    # asks it its own way can agree with a map the game disagrees with.
    legal = {(x, y) for y in range(MAP_H) for x in range(MAP_W)
             if city.pedestrian_legal(x, y)}
    # Named rather than another `seen`: the walkable flood forty lines up is
    # a DIFFERENT and strictly larger set, and anything downstream asking
    # "is this tile reachable" has to say which of the two it means.
    foot_reach = {(sx, sy)}
    queue = [(sx, sy)]
    while queue:
        x, y = queue.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            step = (x + dx, y + dy)
            if step not in foot_reach and step in legal:
                foot_reach.add(step)
                queue.append(step)
    stranded_zones = [zname for zid, zname in enumerate(ZONE_NAMES)
                      if zid != Z_OCEAN
                      and any(city.zone[y][x] == zid for (x, y) in legal)
                      and not any(city.zone[y][x] == zid
                                  for (x, y) in foot_reach)]
    report.append(f"pedestrian-legal / reached : {len(legal)} "
                  f"/ {len(foot_reach)}")
    if stranded_zones:
        failures.append(f"the kerb rule cuts {stranded_zones} off from the "
                        f"spawn -- the crowd there would never mix")
    # Pockets are allowed and expected: a courtyard behind a building is one.
    # What must not happen is most of the pavement being one, which is what a
    # missing crossing table would look like.
    if len(foot_reach) * 2 < len(legal):
        failures.append(f"only {len(foot_reach)} of {len(legal)} "
                        f"pedestrian-legal tiles connect to the spawn")

    # A courier drop the player cannot stand on is a run that can never be
    # completed, and the symptom is a timer that always expires -- which reads
    # as the allowance being too tight rather than the marker being in a wall.
    unreachable = [(x, y) for (x, y) in city.missions
                   if (x, y) not in foot_reach]
    report.append(f"mission drops / reachable  : {len(city.missions)} "
                  f"/ {len(city.missions) - len(unreachable)}")
    if unreachable:
        failures.append(f"courier drops the player cannot reach: "
                        f"{unreachable[:4]}")
    if len(city.missions) < 2:
        failures.append(f"only {len(city.missions)} courier drops -- a run "
                        f"needs somewhere else to go")

    # And two drops in the same neighbourhood is a leg the player completes by
    # turning round, which is the streak stopping to mean anything.
    spans = [abs(a[0] - b[0]) + abs(a[1] - b[1])
             for i, a in enumerate(city.missions)
             for b in city.missions[i + 1:]]
    report.append(f"  shortest leg, tiles      : {min(spans, default=0)}")
    # One block, not two: the park, the marina and the suburbs are three strips
    # of the same southern belt with centres of mass genuinely close together,
    # so the promise here is "never the same neighbourhood", not "across the
    # island".
    if spans and min(spans) < BLOCK_PITCH:
        failures.append(f"two courier drops are {min(spans)} tiles apart -- "
                        f"a leg the player finishes by turning round")

    # 10. The main story's payphone, and the chapter 1 job hanging off it.
    #     None of it is drawn: the tables are read off the finished city, so
    #     the only way they can be wrong is by pointing somewhere the player
    #     cannot go. The phone is answered on foot, so it has to be
    #     pedestrian-legal, off the carriageway and in the spawn's own
    #     foot-reachable component; any one of the three missing leaves a
    #     marker on the minimap the player walks at until they give up. All
    #     three phones go through the SAME loop, deliberately: chapters 2 and
    #     3 have to pass everything chapter 1 passes, and one body of rules
    #     with two per-chapter branches hung off it is cheaper than three
    #     lists that agree today.
    if not city.phones:
        failures.append("no contract payphone was placed -- the main story "
                        "has nowhere to start")
    elif len(city.phones) < 3:
        failures.append(f"only {len(city.phones)} contract payphone(s) were "
                        f"placed against 3 chapters -- the story runs out of "
                        f"phones before it runs out of chapters")
    for chapter, (px, py) in enumerate(city.phones, start=1):
        report.append(f"chapter {chapter} payphone".ljust(27)
                      + f": ({px},{py}) in "
                        f"{ZONE_NAMES[city.district_at(px, py)]}")
        if not city.pedestrian_legal(px, py):
            failures.append(f"the payphone at {(px, py)} is not a tile a "
                            f"pedestrian may stand on")
        if city._on_carriageway(px, py):
            failures.append(f"the payphone at {(px, py)} is in the "
                            f"carriageway -- answering it means standing in "
                            f"traffic")
        if (px, py) not in foot_reach:
            failures.append(f"the payphone at {(px, py)} cannot be walked to "
                            f"from the spawn")

        # And it has to be legible. Drops, weapons and phones are all rings on
        # the ground: two on one corner are a blur the player cannot read, and
        # the mission they answer is a coin toss. The OTHER PHONE is in that
        # list and is the worst case -- two payphones a few tiles apart are one
        # marker the player rings twice and gets a different chapter from.
        others = ([(mx, my, "a courier drop") for (mx, my) in city.missions]
                  + [(wx, wy, "the weapon pickup")
                     for (wx, wy, _k) in city.weapons]
                  + [(qx, qy, f"the chapter {other} payphone")
                     for other, (qx, qy) in enumerate(city.phones, start=1)
                     if other != chapter])
        crowded = [(ox, oy, what) for (ox, oy, what) in others
                   if abs(ox - px) + abs(oy - py) < BLOCK_PITCH]
        if crowded:
            ox, oy, what = crowded[0]
            failures.append(f"the payphone at {(px, py)} is "
                            f"{abs(ox - px) + abs(oy - py)} tiles from "
                            f"{what} at {(ox, oy)} -- two rings on one corner "
                            f"cannot be told apart")

        # One rule that is chapter 2's alone. Chapter 1's phone stands four
        # tiles from the station door on purpose, sending the player the other
        # way past scenery. Chapter 2 sends them THROUGH it, and a job handed
        # out on the doormat is not a journey: the walk there, armed, IS it.
        if chapter >= 2:
            doorx, doory = city.police_door()
            walk = abs(doorx - px) + abs(doory - py)
            report.append("  to the station door".ljust(27) + f": {walk}")
            if not city.clear_of_doors(px, py):
                failures.append(f"the chapter {chapter} payphone at "
                                f"{(px, py)} is inside a block of an interior "
                                f"door (the station's is at "
                                f"{(doorx, doory)}, {walk} tiles away) -- a "
                                f"job handed out on a doormat is a job that "
                                f"starts indoors")

        # And one rule that is chapter 3's alone. The rampage is a clock and a
        # body count with nowhere to walk to, so the only thing its corner has
        # to supply is people -- and the crowd streams on to walkable pavement
        # and nothing else. A phone on a quiet corner is a chapter the player
        # fails while looking for somebody to shoot.
        if chapter == 3:
            room = city.crowd_room(px, py)
            report.append("  crowd room, half a block".ljust(27) + f": {room}")
            if room < BLOCK_PITCH:
                failures.append(f"the chapter 3 payphone at {(px, py)} has "
                                f"{room} walkable tiles within half a block "
                                f"-- less standing room than a block is long, "
                                f"which is a rampage with no crowd in it")

    # The mark. Out of range is a read past the end of VEHICLE_SPAWNS; in the
    # phone's own district is a mission the player finishes by turning round,
    # which is the same defect the courier legs are checked for above.
    if city.phones:
        px, py = city.phones[0]
        if not 0 <= city.boost_vehicle < len(city.vehicles):
            failures.append(f"BOOST_VEHICLE_INDEX is {city.boost_vehicle} "
                            f"against {len(city.vehicles)} parked cars -- "
                            f"chapter 1 sends the player to nothing")
        else:
            bx, by, _bh, _bc = city.vehicles[city.boost_vehicle]
            walk = abs(bx - px) + abs(by - py)
            report.append("chapter 1 boost car".ljust(27)
                          + f": #{city.boost_vehicle} ({bx},{by}) in "
                            f"{ZONE_NAMES[city.district_at(bx, by)]}, "
                            f"{walk} tiles from the phone")
            if city.district_at(bx, by) == city.district_at(px, py):
                failures.append(f"the boost car at {(bx, by)} is in the "
                                f"payphone's own district -- a job you finish "
                                f"by turning round")
            # The player walks to it, so somewhere touching it has to be
            # pavement they can reach. A car parks on the carriageway, so the
            # car's OWN tile never is.
            if not any((bx + dx, by + dy) in foot_reach
                       for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                failures.append(f"the boost car at {(bx, by)} cannot be "
                                f"walked up to from the payphone -- no tile "
                                f"beside it is pavement the player can reach")
            # The pistol is already the thing within sight of the spawn. A
            # boost car sitting there too makes the opening one lesson.
            near_spawn = min(range(len(city.vehicles)),
                             key=lambda i: (abs(city.vehicles[i][0] - sx)
                                            + abs(city.vehicles[i][1] - sy), i))
            if city.boost_vehicle == near_spawn:
                failures.append("the boost car is the one parked nearest the "
                                "spawn -- the teaching pistol is already "
                                "there, and chapter 1 would ask for nothing")

            # The drive. Same argument as the car, one leg further on: a drop
            # in the car's own district is a chapter that ends before it
            # starts.
            if not 0 <= city.boost_drop < len(city.missions):
                failures.append(f"BOOST_DROP_INDEX is {city.boost_drop} "
                                f"against {len(city.missions)} courier drops "
                                f"-- the boosted car has nowhere to go")
            else:
                dx_, dy_ = city.missions[city.boost_drop]
                drive = abs(dx_ - bx) + abs(dy_ - by)
                report.append("chapter 1 boost drop".ljust(27)
                              + f": #{city.boost_drop} ({dx_},{dy_}) in "
                                f"{ZONE_NAMES[city.district_at(dx_, dy_)]}, "
                                f"{drive} tiles from the car")
                if city.district_at(dx_, dy_) == city.district_at(bx, by):
                    failures.append(f"the boost drop at {(dx_, dy_)} is in "
                                    f"the boost car's own district -- there "
                                    f"is no drive in the driving mission")

    # 10b. Chapter 2's mark. HIT_OFFICER_INDEX is a subscript the station scene
    #      hands straight to OFFICERS[], so out of range reads past the end of
    #      the room's own table -- a target standing wherever the next struct's
    #      bytes happen to say, which looks like a placement bug for a week.
    station_posts = POLICE_STATION.officers()
    mark = POLICE_STATION.marked_officer()
    station_walk = POLICE_STATION.walk_distances()
    mat = POLICE_STATION.exit()
    if not 0 <= mark < len(station_posts):
        failures.append(f"HIT_OFFICER_INDEX is {mark} against "
                        f"{len(station_posts)} officer post(s) -- chapter 2 "
                        f"sends the player in to shoot nobody")
    else:
        ox, oy = station_posts[mark]
        report.append("chapter 2 mark".ljust(27)
                      + f": #{mark} ({ox},{oy}), "
                        f"{station_walk.get((ox, oy), -1)} steps from the mat")
        # "Walk past the others" is the whole argument for the furthest post.
        # With one officer in the room it is not weaker but false: the mark
        # would be the only body in the station and the chapter a doorway and a
        # trigger pull. The fix is a post in the room plan, not a softer check.
        if len(station_posts) < 2:
            failures.append(f"the station has {len(station_posts)} officer "
                            f"post(s) -- chapter 2 is written around walking "
                            f"PAST the others, and there are no others")
        # The room-wide check above already refuses any post walled off from
        # the mat; this one is narrower. The mark is chosen for being furthest
        # away, so it is the post most likely to end up behind the desk or
        # inside the holding cell, and an unreachable mark cannot be shot.
        if (ox, oy) not in station_walk:
            failures.append(f"the chapter 2 mark at {(ox, oy)} cannot be "
                            f"walked to from the station mat at {mat} -- the "
                            f"hit could never be carried out")
        else:
            # HIT_OFFICER_STEPS_IN comes from this same flood fill and is the
            # second half of chapter 2's clock. A walk is never shorter than
            # the straight line, so this compares the number that ships against
            # the one the scene used to guess -- the gap is why the constant
            # exists. Zero would mean the mark is standing on the mat:
            # shootable from the doorway, and the lobby never happens.
            walk = station_walk[(ox, oy)]
            line = abs(mat[0] - ox) + abs(mat[1] - oy)
            report.append("chapter 2 indoor leg".ljust(27)
                          + f": {walk} steps walked, {line} straight")
            if walk < line:
                failures.append(f"the station flood fill says {walk} steps to "
                                f"the mark and the straight line is {line} -- "
                                f"a walk cannot be shorter than the crow flies")
            if walk == 0:
                failures.append("the chapter 2 mark is standing on the exit "
                                "mat -- the hit can be finished from the "
                                "doorway and the room never happens")

    # 11. After dark. The effect rests on one property nothing else in the
    #     build would notice breaking: the lit palette entry must be drawn by
    #     the after-dark art and by NOTHING in the daylight art. By neither and
    #     the lights come on over nothing; by both and the tint stops darkening
    #     whatever else is painted in it -- a wall that glows at noon.
    for layer, night in night_tilesets.items():
        day = tilesets[layer]
        if len(night) != len(day):
            failures.append(f"{layer}: {len(night)} tiles after dark against "
                            f"{len(day)} by day -- a cell index has to mean "
                            f"the same tile under both")
    day_lit: dict[int, list[str]] = {slot: [] for slot in LIT_ENTRIES}
    night_lit: dict[int, list[str]] = {slot: [] for slot in LIT_ENTRIES}
    for layer, tiles in LAYERS:
        night = night_tilesets.get(layer)
        for i, (tname, _grid, slot) in enumerate(tiles):
            if slot not in LIT_ENTRIES:
                continue
            entry = LIT_ENTRIES[slot][0]
            if any(v == entry for row in tilesets[layer][i].rows() for v in row):
                day_lit[slot].append(tname)
            if night is not None and any(
                    v == entry for row in night[i].rows() for v in row):
                night_lit[slot].append(tname)
    for slot, (entry, cname) in sorted(LIT_ENTRIES.items()):
        report.append(f"lit entry {PALETTE_SLOTS[slot][0]:12}: {cname} "
                      f"(slot {slot}, entry {entry}) on "
                      f"{len(night_lit[slot])} tiles after dark")
        if day_lit[slot]:
            failures.append(f"{cname} is drawn by the DAYLIGHT art on "
                            f"{day_lit[slot][:4]} -- the tint leaves that "
                            f"entry alone, so it would glow at noon")
        if not night_lit[slot]:
            failures.append(f"{cname} (slot {slot}) lights nothing: no "
                            f"after-dark tile draws it")

    # 12. And the lights have to be where the player goes. The tileset can be
    #     perfect and the city still dark, if the layout stopped placing the
    #     props that carry the light.
    lit_tiles = {i for i, canvas in enumerate(night_tilesets["ITEMS"])
                 if canvas.rows() != tilesets["ITEMS"][i].rows()}
    lamp_tiles = {IT[n] for n in NIGHT_STREET_PROPS}
    lamps = sum(1 for row in city.items for t in row if t in lamp_tiles)
    lit_cells = sum(1 for row in city.items for t in row if t in lit_tiles)
    report.append(f"cells that light up        : {lit_cells} "
                  f"({lamps} of them street lamps)")
    if lamps < VEHICLE_COUNT:
        failures.append(f"only {lamps} street lamps in the whole city -- "
                        f"the streets stay dark after dusk")
    if lit_cells - lamps < lamps:
        failures.append(f"only {lit_cells - lamps} lit window cells against "
                        f"{lamps} lamps -- the skyline does not come on")

    if failures:
        raise SystemExit("LAYOUT CHECK FAILED:\n  - " + "\n  - ".join(failures))
    return report


# --------------------------------------------------------------------------
# Emitters -- the Tilemap Editor's scene export format
# --------------------------------------------------------------------------
BANNER = """\
// Generated by tools/generate_city_assets.py -- do not edit by hand.
// Engine: PixelRoot32
//
// Emitted in the PixelRoot32 Tilemap Editor's scene export format (see
// gameplay/room_screen/src/assets/ for an editor-produced example): per-slot
// palettes, one tileset pool and index array per layer, per-cell palette
// slots, and dense TileFlags behaviour layers.
//
// Source: ORIGINAL CC0 pixel art hand-authored as character grids in this
//         demo's tools/art_*.py, laid out procedurally by the generator.
//         Designed from scratch; not derived from any copyrighted artwork.
"""


def rows_of(values: list[int], per_row: int, indent: str = "        ",
            hexa: bool = False) -> list[str]:
    fmt = (lambda v: f"0x{v:02X}") if hexa else str
    out = []
    for i in range(0, len(values), per_row):
        out.append(indent + ", ".join(fmt(v) for v in values[i:i + per_row]) + ",")
    if out:
        out[-1] = out[-1].rstrip(",")
    return out


def minimap_swatches(layer: str) -> list[int]:
    """One radar ink per tile of a layer: street, ground, or nothing.

    This used to be a nearest-colour match of each tile's dominant pixel
    against the player palette: faithful, and unreadable -- the radar came out
    a quilt of eight colours in which the streets were merely one.

    It is a classification now, and it reads the tile's NAME rather than its
    pixels. A swatch derived from art moves silently whenever the art is
    redressed, dirtied or reshaded, and nothing in the build can tell a
    deliberate recolour from a road that has stopped looking like a road; a
    swatch derived from identity cannot drift. The inks below are gfx::Color
    indices resolved against the engine's palette, which is why the player
    palette no longer feeds the radar at all -- the sand, grey, blue, green and
    red it carries but never puts on the character are leftovers from the match
    this replaced.
    """
    tiles = dict(LAYERS)[layer]
    out: list[int] = []
    for name, _grid, _slot in tiles:
        if name.split("_")[0] == MINIMAP_SEA_FAMILY:
            out.append(MINIMAP_INK_ABSENT)
        elif name in MINIMAP_ROAD:
            out.append(MINIMAP_INK_ROAD)
        else:
            out.append(MINIMAP_INK_GROUND)
    return out


def palette_lines(slot: int, slot_name: str,
                  entries: list[tuple[str, int, int, int]]) -> list[str]:
    """One palette slot, as both scene files declare it."""
    L = [f"    // Palette for {slot_name} (slot {slot})",
         f"    static const uint16_t {slot_name}_PALETTE[16] = {{"]
    for i, (cname, r, g, b) in enumerate(entries):
        L.append(f"        0x{rgb565(r, g, b):04X},  // {i:2d}: {cname}"
                 f" - RGB({r}, {g}, {b})")
    L.append("    };")
    L.append("")
    return L


def emit_scene_header(city: City, tilesets: dict[str, list[Canvas]]) -> str:
    L: list[str] = [BANNER, "", "#pragma once", "",
                    "#include <graphics/Renderer.h>",
                    "#include <physics/TileAttributes.h>",
                    "#include <stdint.h>", "",
                    "// LANE_BANDS below is typed by the rule that gives it",
                    "// meaning rather than by a copy of its shape. The header",
                    "// is engine-free and includes nothing but <cstdint>.",
                    '#include "game/rules/Lanes.h"', "",
                    "namespace top_down_city::city_scene {", "",
                    "using namespace pixelroot32::physics;", ""]

    L.append("    // --- Palette slots ---")
    L.append("    // Entry 0 of every slot must stay 0x0000: the 8bpp")
    L.append("    // framebuffer treats a packed zero as transparent, which is")
    L.append("    // what gives Items and Details their cut-out.")
    L.append(f"    // Slot {INTERIOR_SLOT} is the police station's and is declared with it,")
    L.append("    // in tilemaps/police_station.h.")
    for slot, (slot_name, entries) in enumerate(PALETTE_SLOTS):
        if slot == INTERIOR_SLOT:
            continue
        L.extend(palette_lines(slot, slot_name, entries))

    L.append("    // Identity mapping: a 4bpp pixel value indexes its cell's")
    L.append("    // palette slot directly. Every tile in every layer shares it.")
    L.append("    static const pixelroot32::graphics::Color PALETTE_MAPPING[16] = {")
    for row in range(4):
        L.append("        " + " ".join(
            f"(pixelroot32::graphics::Color){row * 4 + col}," for col in range(4)))
    L.append("    };")
    L.append("")

    L.append("    // --- Tile dimensions ---")
    L.append(f"    static const uint8_t TILE_SIZE = {TILE};")
    L.append(f"    static const uint8_t MAP_WIDTH = {MAP_W};")
    L.append(f"    static const uint8_t MAP_HEIGHT = {MAP_H};")
    L.append("")
    L.append("    // --- Street grid pitch ---")
    L.append("    // One city block plus the street band around it. The C++")
    L.append("    // side sizes the map overlay in blocks rather than tiles,")
    L.append("    // so changing BLOCK_PITCH here resizes the overlay too.")
    L.append(f"    static const uint8_t BLOCK_PITCH_TILES = {BLOCK_PITCH};")
    L.append("")
    sx, sy = city.spawn()
    L.append("    // --- Player spawn (tile coords) ---")
    L.append(f"    static const uint16_t SPAWN_TILE_X = {sx};")
    L.append(f"    static const uint16_t SPAWN_TILE_Y = {sy};")
    L.append("")

    L.append("    // --- Layer definitions ---")
    L.append("    // Drawn bottom to top: terrain, then props and buildings,")
    L.append("    // then decoration that never collides.")
    for layer, _tiles in LAYERS:
        L.append(f"    extern pixelroot32::graphics::TileMap4bpp {layer.lower()};")
    L.append("")
    L.append("    /// Binds the palette slots and fills the tilemap")
    L.append("    /// descriptors. Call once from Scene::init(), before the")
    L.append("    /// first draw.")
    L.append("    void init();")
    L.append("")

    L.append("    // --- After dark ---")
    L.append("    // The lit city is a second form of the "
             + " and ".join(l for l, _t in NIGHT_LAYERS) + " tileset,")
    L.append("    // identical in length and order to the daylight one, so a")
    L.append("    // cell keeps its index and only the art behind it changes.")
    L.append("    // The whole swap is one pointer per layer: no RAM, no")
    L.append("    // second index array, and nothing to do per frame.")
    L.append("    //")
    L.append("    // It is not a light source. The lit pixels are painted in a")
    L.append("    // palette entry the daylight art never uses, which")
    L.append("    // NightLights.h then leaves untinted while the rest of the")
    L.append("    // city goes down to a third of daylight.")
    L.append("    void setNightArt(bool lit);")
    L.append("")
    L.append("    // The entry per palette slot that means: this pixel is a")
    L.append("    // light, not a surface. Emitted from the art rather than")
    L.append("    // restated in C++, because a lit entry that has drifted one")
    L.append("    // place is not a compile error -- it is a city that lights")
    L.append("    // its drainpipes. CityConstants.h asserts the table in")
    L.append("    // NightLights.h against this one.")
    lit = sorted(LIT_ENTRIES)
    L.append(f"    static constexpr uint8_t LIT_ENTRY_COUNT = {len(lit)};")
    L.append("    static constexpr uint8_t LIT_ENTRY_SLOT[LIT_ENTRY_COUNT] = {"
             + ", ".join(str(slot) for slot in lit) + "};")
    L.append("    static constexpr uint8_t LIT_ENTRY_INDEX[LIT_ENTRY_COUNT] = {"
             + ", ".join(str(LIT_ENTRIES[slot][0]) for slot in lit) + "};")
    for slot in lit:
        entry, cname = LIT_ENTRIES[slot]
        L.append(f"    // slot {slot} ({PALETTE_SLOTS[slot][0]}) entry {entry}"
                 f" is {cname}")
    L.append("")

    L.append("    // --- Districts, for the on-screen name only ---")
    L.append("    enum ZoneId : uint8_t {")
    for i, zname in enumerate(ZONE_NAMES):
        L.append(f"        kZone{zname.capitalize()} = {i},")
    L.append("    };")
    L.append(f"    static const uint8_t ZONE_CHUNK_TILES = {ZONE_CHUNK};")
    L.append(f"    static const uint8_t ZONE_GRID_WIDTH = {MAP_W // ZONE_CHUNK};")
    L.append(f"    static const uint8_t ZONE_GRID_HEIGHT = {MAP_H // ZONE_CHUNK};")
    L.append("    static const char* const ZONE_LABELS[] = {")
    for i in range(len(ZONE_NAMES)):
        L.append(f'        "{ZONE_LABELS[i]}",')
    L.append("    };")
    chunks = city.zone_chunks()
    L.append(f"    static const uint8_t ZONE_GRID[{len(chunks)}] = {{")
    L.extend(rows_of(chunks, 16))
    L.append("    };")
    L.append("")

    L.append("    // --- Driveable vehicles ---")
    L.append("    // Where the city's cars are parked. Nothing is drawn into a")
    L.append("    // tile layer for them: each record becomes a VehicleActor")
    L.append("    // the player can get into, so the count here is the whole")
    L.append("    // traffic budget -- every one of them is simulated.")
    L.append("    enum VehicleHeading : uint8_t {")
    for i, heading in enumerate(VEHICLE_HEADINGS):
        L.append(f"        kHeading{heading} = {i},")
    L.append("    };")
    L.append("    struct VehicleSpawn {")
    L.append("        uint8_t tileX;")
    L.append("        uint8_t tileY;")
    L.append("        uint8_t heading;   ///< VehicleHeading")
    L.append("        uint8_t color;     ///< index into kVehicleSprites")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_VEHICLES = {len(city.vehicles)};")
    L.append(f"    static const VehicleSpawn VEHICLE_SPAWNS[NUM_VEHICLES] = {{")
    for (x, y, heading, colour) in city.vehicles:
        L.append(f"        {{ {x}, {y}, {heading}, {colour} }},  "
                 f"// {VEHICLE_NAMES[colour]} facing {VEHICLE_HEADINGS[heading]}")
    L.append("    };")
    L.append("")

    L.append("    // --- Lanes, for the traffic that drives itself ---")
    L.append("    // Every street band, clipped to the stretches that are")
    L.append("    // really carriageway: the park's gravel path and the")
    L.append("    // pavement caps at each end are not in here, and a band")
    L.append("    // the park interrupts appears as two records.")
    L.append("    //")
    L.append("    // Fourteen numbers per record and no per-tile map. A")
    L.append("    // direction map over 128x128 tiles would be 16 KB of flash")
    L.append("    // to say what this table already says, and the scan is a")
    L.append("    // handful of comparisons -- see game/rules/Lanes.h, which")
    L.append("    // owns the meaning of the offsets.")
    L.append(f"    static const uint8_t NUM_LANE_BANDS = {len(city.lane_bands)};")
    L.append("    static const lanes::Band LANE_BANDS[NUM_LANE_BANDS] = {")
    for (start, horizontal, first, last) in city.lane_bands:
        axis = "row" if horizontal else "col"
        L.append(f"        {{ {start}, {first}, {last}, "
                 f"{1 if horizontal else 0} }},  "
                 f"// {axis} {start}, {'EW' if horizontal else 'NS'} "
                 f"{first}..{last}")
    L.append("    };")
    L.append("")

    L.append("    // --- Zebra crossings ---")
    L.append("    // Where a pedestrian may step off the kerb, and the only")
    L.append("    // place they will. Grouped into one-tile-thick runs from")
    L.append("    // the tiles crosswalks() really painted -- see")
    L.append("    // game/rules/Lanes.h for what reads them.")
    L.append(f"    static const uint8_t NUM_CROSSINGS = {len(city.crossings)};")
    L.append("    static const lanes::Crossing CROSSINGS[NUM_CROSSINGS] = {")
    for (x, y, length, horizontal) in city.crossings:
        L.append(f"        {{ {x}, {y}, {length}, {horizontal} }},"
                 f"  // {'EW' if horizontal else 'NS'}")
    L.append("    };")
    L.append("")

    L.append("    // --- Courier drop points ---")
    L.append("    // One per district, at the nearest walkable tile to the")
    L.append("    // district's centre of mass -- so a leg is a journey and")
    L.append("    // the marker is somewhere the player was driving through")
    L.append("    // anyway, rather than a scrap of pavement behind a shop.")
    L.append("    struct MissionTarget {")
    L.append("        uint8_t tileX;")
    L.append("        uint8_t tileY;")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_MISSION_TARGETS = "
             f"{len(city.missions)};")
    L.append("    static const MissionTarget "
             "MISSION_TARGETS[NUM_MISSION_TARGETS] = {")
    for (x, y) in city.missions:
        L.append(f"        {{ {x}, {y} }},  // {ZONE_NAMES[city.zone[y][x]]}")
    L.append("    };")
    L.append("")

    L.append("    // --- Contract payphones ---")
    L.append("    // Where the main story is handed out. One row per")
    L.append("    // chapter, and that shape is the whole point: chapters 2")
    L.append("    // and 3 were each written by adding a row here rather than")
    L.append("    // a branch anywhere. Nothing is drawn into a layer for")
    L.append("    // these: a phone is a ring on the ground, like the courier")
    L.append("    // drop, and the player answers it on foot -- so every")
    L.append("    // entry is pedestrian-legal, off the carriageway, walkable")
    L.append("    // to from the spawn and at least one block clear of every")
    L.append("    // other ring INCLUDING THE OTHER PHONES, or two markers on")
    L.append("    // one corner could not be told apart.")
    L.append("    struct ContractPhone {")
    L.append("        uint8_t tileX;")
    L.append("        uint8_t tileY;")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_CONTRACT_PHONES = "
             f"{len(city.phones)};")
    L.append("    static const ContractPhone "
             "CONTRACT_PHONES[NUM_CONTRACT_PHONES] = {")
    for chapter, (x, y) in enumerate(city.phones, start=1):
        L.append(f"        {{ {x}, {y} }},  "
                 f"// chapter {chapter}, {ZONE_NAMES[city.district_at(x, y)]}")
    L.append("    };")
    L.append("")

    L.append("    // --- Chapter 1, \"Boost\" ---")
    L.append("    // The parked car the player is sent to steal, and the drop")
    L.append("    // it has to be brought to. Indices, not coordinates: both")
    L.append("    // tables above are already emitted and a second copy of a")
    L.append("    // tile would be a second thing to keep in step.")
    L.append("    //")
    L.append("    // The car is the nearest one in a district OTHER than the")
    L.append("    // phone's -- a job you finish by turning round is not a")
    L.append("    // job -- and the drop is the courier point farthest from")
    L.append("    // that car, so the chapter ends in a drive across the")
    L.append("    // island rather than a nudge round the corner.")
    if city.phones:
        bx, by, _bh, _bc = city.vehicles[city.boost_vehicle]
        dx_, dy_ = city.missions[city.boost_drop]
        walk = abs(bx - city.phones[0][0]) + abs(by - city.phones[0][1])
        drive = abs(dx_ - bx) + abs(dy_ - by)
        L.append(f"    static const uint8_t BOOST_VEHICLE_INDEX = "
                 f"{city.boost_vehicle};")
        L.append(f"    // ({bx},{by}) in "
                 f"{ZONE_NAMES[city.district_at(bx, by)]}, {walk} tiles' walk "
                 f"from the phone")
        L.append(f"    static const uint8_t BOOST_DROP_INDEX = "
                 f"{city.boost_drop};")
        L.append(f"    // ({dx_},{dy_}) in "
                 f"{ZONE_NAMES[city.district_at(dx_, dy_)]}, {drive} tiles' "
                 f"drive from the car")
    L.append("")

    L.append("    // --- Chapter 2, \"The Hit\" ---")
    L.append("    // CONTRACT_PHONES[1] sends the player into the police")
    L.append("    // station to shoot one particular officer and walk back")
    L.append("    // out with the stars on. WHICH officer is this: a")
    L.append("    // subscript into police_station::OFFICERS, in the same")
    L.append("    // spirit as the two indices above -- the post is already")
    L.append("    // emitted, and a second copy of a coordinate is a second")
    L.append("    // thing to keep in step.")
    L.append("    //")
    L.append("    // It lives in the city's header rather than the")
    L.append("    // station's because it is a fact about the STORY and not")
    L.append("    // about the room: the station is a room full of police")
    L.append("    // whether or not anybody was ever sent to it, and every")
    L.append("    // other chapter constant is here.")
    L.append("    //")
    L.append("    // The mark is the officer FURTHEST FROM THE EXIT MAT,")
    L.append("    // counted in steps through the room's own furniture")
    L.append("    // rather than in a straight line. That is the argument")
    L.append("    // for it: a target by the door can be shot from the")
    L.append("    // doorway and the room never happens. The one at the far")
    L.append("    // end has to be walked to, past every other officer on")
    L.append("    // duty, which is the chapter.")
    mark = POLICE_STATION.marked_officer()
    posts = POLICE_STATION.officers()
    if 0 <= mark < len(posts):
        ox, oy = posts[mark]
        steps = POLICE_STATION.walk_distances().get((ox, oy), -1)
        ex, ey = POLICE_STATION.exit()
        L.append(f"    static const uint8_t HIT_OFFICER_INDEX = {mark};")
        L.append(f"    // post ({ox},{oy}) of {len(posts)}, {steps} steps in "
                 f"from the mat at ({ex},{ey})")
        L.append("")
        L.append("    // And how far in that is, in STEPS, because the scene")
        L.append("    // has to pay for the walk and cannot work it out. The")
        L.append("    // clock chapter 2 starts covers the whole journey --")
        L.append("    // across the island to the door, then across the lobby")
        L.append("    // to this post -- and the second half is behind")
        L.append("    // furniture the city's header has never seen. The room")
        L.append("    // flood-fills its own floor to pick the mark in the")
        L.append("    // first place, so the number already exists here; the")
        L.append("    // scene recomputing it as a straight line would be a")
        L.append("    // second, WORSE answer to a question this file has")
        L.append("    // already answered exactly. The desk is solid, and the")
        L.append("    // post is chosen for being the furthest walk in the")
        L.append("    // room -- so a straight line is not merely approximate")
        L.append("    // here, it is approximate precisely where the mark is.")
        line = abs(ex - ox) + abs(ey - oy)
        L.append(f"    static const uint8_t HIT_OFFICER_STEPS_IN = {steps};")
        L.append(f"    // {steps} walked against {line} in a straight line")
    L.append("")

    L.append("    // --- Weapons lying in the street ---")
    L.append("    // The player starts unarmed. A pistol is an actor, not a")
    L.append("    // tile: nothing is written to a layer for these, they are")
    L.append("    // drawn on the ground and picked up by walking over them.")
    L.append("    // One per district, and the first is placed within sight of")
    L.append("    // the spawn so the mechanic is shown rather than explained.")
    L.append("    struct WeaponPickup {")
    L.append("        uint8_t tileX;")
    L.append("        uint8_t tileY;")
    L.append("        uint8_t weapon;    ///< weapons::WeaponId")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_WEAPON_PICKUPS = {len(city.weapons)};")
    L.append("    static const WeaponPickup WEAPON_PICKUPS[NUM_WEAPON_PICKUPS] = {")
    for (x, y, kind) in city.weapons:
        zname = ZONE_NAMES[city.zone[y][x]]
        L.append(f"        {{ {x}, {y}, {kind} }},  "
                 f"// {WEAPON_ID_NAMES[kind]}, {zname}")
    L.append("    };")
    L.append("")

    L.append("    // --- The buildings you can walk into ---")
    L.append("    // An entrance is drawn as open ground inside a building")
    L.append("    // stamp, so the player could already stand in it; these are")
    L.append("    // those cells. No door tile and no fourth layer were needed")
    L.append("    // -- the art was always walkable, nothing knew it meant")
    L.append("    // anything.")
    L.append("    //")
    L.append("    // The tile itself is still flagged SOLID, because the cell")
    L.append("    // is not empty: what makes it walkable is the per-pixel")
    L.append("    // Items test in CityCollision, which reads the silhouette.")
    L.append("    // A car or a pedestrian, tested whole-tile, cannot follow")
    L.append("    // the player in -- which is the behaviour anyway.")
    dx, dy = city.police_door()
    L.append(f"    static const uint16_t POLICE_DOOR_TILE_X = {dx};")
    L.append(f"    static const uint16_t POLICE_DOOR_TILE_Y = {dy};")
    dx, dy = city.shop_door()
    L.append(f"    static const uint16_t SHOP_DOOR_TILE_X = {dx};")
    L.append(f"    static const uint16_t SHOP_DOOR_TILE_Y = {dy};")
    L.append("")
    L.append("    // What is behind them are scenes of their own; see")
    L.append("    // tilemaps/police_station.h and tilemaps/corner_shop.h.")
    L.append("")

    L.append("    // ====================================================")
    L.append("    // Tile behaviour layers")
    L.append("    // Dense TileFlags, one byte per cell. 0 = TILE_NONE.")
    L.append("    // ====================================================")
    for layer, _tiles in LAYERS:
        flags = city.behaviour(layer)
        solid = sum(1 for f in flags if f)
        L.append(f"    // Behaviour layer: {layer} ({MAP_W}x{MAP_H}) -- "
                 f"{solid} solid cells")
        L.append(f"    static const uint8_t TILE_BEHAVIOR_LAYER_{layer}"
                 f"[{len(flags)}] = {{")
        L.extend(rows_of(flags, 32))
        L.append("    };")
        L.append("")
    L.append("    static const TileBehaviorLayer behavior_layers[] = {")
    for layer, _tiles in LAYERS:
        L.append(f"        {{ TILE_BEHAVIOR_LAYER_{layer}, {MAP_W}, {MAP_H} }},")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_BEHAVIOR_LAYERS = {len(LAYERS)};")
    L.append("")
    for i, (layer, _t) in enumerate(LAYERS):
        L.append(f"    static const uint8_t BEHAVIOR_LAYER_{layer} = {i};")
    L.append("")
    L.append("    /// TileFlags at a cell, or TILE_NONE out of bounds.")
    L.append("    inline uint8_t getTileFlags(uint8_t layer_idx, int x, int y) {")
    L.append("        if (layer_idx >= NUM_BEHAVIOR_LAYERS) {")
    L.append("            return TILE_NONE;")
    L.append("        }")
    L.append("        return pixelroot32::physics::getTileFlags(")
    L.append("            behavior_layers[layer_idx], x, y);")
    L.append("    }")
    L.append("")

    # One layer, not three. The radar asks whether a tile is street, and a
    # street is a background tile -- an Items lookup could only ever answer
    # "there is a building here", which the ground already says. The Details
    # table was emitted and never read at all.
    L.append("    // --- Minimap ink: street / ground / absent, per background tile ---")
    L.append("    // Indices into the ENGINE's built-in palette: the overlay is drawn")
    L.append("    // with primitives, and a primitive never sees this demo's custom")
    L.append("    // sprite palettes. CityConstants.h asserts them against gfx::Color.")
    L.append(f"    static const uint8_t MINIMAP_INK_STREET = {MINIMAP_INK_ROAD};")
    L.append(f"    static const uint8_t MINIMAP_INK_GROUND = {MINIMAP_INK_GROUND};")
    sw = minimap_swatches("BACKGROUND")
    L.append(f"    static const uint8_t MINIMAP_SWATCH_BACKGROUND[{len(sw)}] = {{")
    L.extend(rows_of(sw, 16))
    L.append("    };")
    L.append("")

    L.append("    /// District at a tile, from the coarse chunk grid.")
    L.append("    inline uint8_t zoneAt(int tileX, int tileY) {")
    L.append("        if (tileX < 0 || tileY < 0 ||")
    L.append("            tileX >= MAP_WIDTH || tileY >= MAP_HEIGHT) {")
    L.append("            return kZoneOcean;")
    L.append("        }")
    L.append("        return ZONE_GRID[(tileY / ZONE_CHUNK_TILES) * ZONE_GRID_WIDTH")
    L.append("                         + (tileX / ZONE_CHUNK_TILES)];")
    L.append("    }")
    L.append("")
    L.append("}  // namespace top_down_city::city_scene")
    L.append("")
    return "\n".join(L)


def emit_scene_source(city: City, tilesets: dict[str, list[Canvas]],
                      night_tilesets: dict[str, list[Canvas]]) -> str:
    L: list[str] = [BANNER, "", '#include "city_scene.h"', "",
                    "namespace top_down_city::city_scene {", ""]
    stride = (TILE * TILE) // 2

    for layer, tiles in LAYERS:
        canvases = tilesets[layer]
        L.append(f"    // --- {layer}: {len(canvases)} tiles, "
                 f"{len(canvases) * stride} bytes ---")
        L.append(f"    static const uint8_t {layer}_TILESET_DATA_POOL"
                 f"[{len(canvases) * stride}] = {{")
        for i, canvas in enumerate(canvases):
            body = ", ".join(f"0x{v:02X}" for v in pack4bpp(canvas))
            comma = "" if i == len(canvases) - 1 else ","
            L.append(f"        {body}{comma}  // {i}: {tiles[i][0]}")
        L.append("    };")
        L.append("")
        L.append(f"    static const pixelroot32::graphics::Sprite4bpp "
                 f"{layer}_TILESET_SPRITES[{len(canvases)}] = {{")
        for i in range(len(canvases)):
            L.append(f"        {{ &{layer}_TILESET_DATA_POOL[{i * stride}], "
                     f"PALETTE_MAPPING, {TILE}, {TILE}, 16 }},")
        L.append("    };")
        L.append("")

        night = night_tilesets.get(layer)
        if night is not None:
            changed = [i for i, canvas in enumerate(night)
                       if canvas.rows() != canvases[i].rows()]
            at = {i: k for k, i in enumerate(changed)}
            L.append(f"    // --- {layer} after dark: {len(changed)} of "
                     f"{len(canvases)} tiles differ, "
                     f"{len(changed) * stride} bytes ---")
            L.append("    // Only the tiles that CHANGE are stored. The array")
            L.append("    // below is a full-length second tileset, but every")
            L.append("    // tile that looks the same after dark points back")
            L.append("    // into the daylight pool, so the variant costs its")
            L.append("    // own differences and nothing else.")
            L.append(f"    static const uint8_t {layer}_NIGHT_DATA_POOL"
                     f"[{len(changed) * stride}] = {{")
            for k, i in enumerate(changed):
                body = ", ".join(f"0x{v:02X}" for v in pack4bpp(night[i]))
                comma = "" if k == len(changed) - 1 else ","
                L.append(f"        {body}{comma}  // {i}: {tiles[i][0]}")
            L.append("    };")
            L.append("")
            L.append(f"    static const pixelroot32::graphics::Sprite4bpp "
                     f"{layer}_TILESET_SPRITES_NIGHT[{len(canvases)}] = {{")
            for i in range(len(canvases)):
                if i in at:
                    L.append(f"        {{ &{layer}_NIGHT_DATA_POOL"
                             f"[{at[i] * stride}], PALETTE_MAPPING, "
                             f"{TILE}, {TILE}, 16 }},  // {tiles[i][0]}, lit")
                else:
                    L.append(f"        {{ &{layer}_TILESET_DATA_POOL"
                             f"[{i * stride}], PALETTE_MAPPING, "
                             f"{TILE}, {TILE}, 16 }},")
            L.append("    };")
            L.append("")

        indices = city.indices(layer)
        L.append(f"    static const uint8_t {layer}_INDICES[{len(indices)}] = {{")
        L.extend(rows_of(indices, 32))
        L.append("    };")
        L.append("")
        slots = city.palette_indices(layer)
        L.append(f"    // Per-cell palette slot for {layer}")
        L.append(f"    static const uint8_t {layer}_PALETTE_INDICES"
                 f"[{len(slots)}] = {{")
        L.extend(rows_of(slots, 32))
        L.append("    };")
        L.append("")
        L.append(f"    pixelroot32::graphics::TileMap4bpp {layer.lower()};")
        L.append("")

    L.append("    void init() {")
    L.append("        // Multi-palette mode: one custom palette per slot.")
    L.append("        // Slot " + str(INTERIOR_SLOT) + " is not bound here: it belongs to the")
    L.append("        // police station, which binds it from its own init().")
    L.append("        // The bank is global and both scenes are resident at")
    L.append("        // once, so each one owns the slots it draws with.")
    for slot, (slot_name, _entries) in enumerate(PALETTE_SLOTS):
        if slot == INTERIOR_SLOT:
            continue
        L.append(f"        pixelroot32::graphics::setBackgroundCustomPaletteSlot("
                 f"{slot}, {slot_name}_PALETTE);")
    L.append("")
    for layer, tiles in LAYERS:
        var = layer.lower()
        L.append(f"        // {layer}")
        L.append(f"        {var}.width = MAP_WIDTH;")
        L.append(f"        {var}.height = MAP_HEIGHT;")
        L.append(f"        {var}.tileWidth = TILE_SIZE;")
        L.append(f"        {var}.tileHeight = TILE_SIZE;")
        L.append(f"        {var}.indices = (uint8_t*){layer}_INDICES;")
        L.append(f"        {var}.tiles = {layer}_TILESET_SPRITES;")
        L.append(f"        {var}.tileCount = {len(tiles)};")
        L.append(f"        {var}.runtimeMask = nullptr;")
        L.append(f"        {var}.animManager = nullptr;")
        L.append(f"        {var}.paletteIndices = {layer}_PALETTE_INDICES;")
        L.append("")

    L.append("    }")
    L.append("")
    L.append("    void setNightArt(bool lit) {")
    L.append("        // One pointer per layer. The descriptors are in flash")
    L.append("        // and the maps are the same maps: nothing is copied,")
    L.append("        // nothing is allocated, and the whole city changes at")
    L.append("        // once.")
    for layer, _tiles in NIGHT_LAYERS:
        L.append(f"        {layer.lower()}.tiles = lit")
        L.append(f"            ? {layer}_TILESET_SPRITES_NIGHT")
        L.append(f"            : {layer}_TILESET_SPRITES;")
    L.append("    }")
    L.append("")
    L.append("}  // namespace top_down_city::city_scene")
    L.append("")
    return "\n".join(L)


def emit_player(frames: dict[str, list[Canvas]],
                armed: dict[str, list[Canvas]],
                pickups: dict[int, Canvas]) -> str:
    L = [BANNER, "", "#pragma once", "", "#include <graphics/Renderer.h>",
         "#include <graphics/Color.h>", "#include <stdint.h>", "",
         "namespace top_down_city {", "",
         "using pixelroot32::graphics::Sprite4bpp;",
         "using pixelroot32::graphics::Color;", "",
         f"static constexpr uint8_t kPlayerSpriteSize = {TILE};",
         "static constexpr uint8_t kPlayerWalkFrames = 3;", "",
         "/// The player draws through the SPRITE palette bank, which is",
         "/// separate from the background slots the scene installs. Entry 0",
         "/// must stay 0x0000 so the sprite keeps its cut-out.",
         "inline constexpr uint16_t PLAYER_PALETTE_RGB565[16] = {"]
    for i, (cname, r, g, b) in enumerate(PLAYER_PALETTE):
        L.append(f"    0x{rgb565(r, g, b):04X},  // {i:2d}: {cname}")
    L.append("};")
    L.append("")
    L.append("inline constexpr Color PLAYER_PALETTE_MAPPING[16] = {")
    for row in range(4):
        L.append("    " + " ".join(f"(Color){row * 4 + col}," for col in range(4)))
    L.append("};")
    L.append("")
    # There used to be an `ink::` namespace here -- one named Color handle per
    # entry of this palette, "for the HUD", documented as if the demo replaced
    # the engine palette wholesale. It does not: this palette is bound into
    # sprite palette SLOTS, read by sprite blits only, while every primitive
    # the HUD and the radar are made of resolves against the engine's
    # background palette, which nothing here touches. So `ink::kSand` drew the
    # engine's Purple and the courier's green marker its LightGray, for five
    # stages, because a wrong mapping that lands on plausible colours is not
    # caught by looking. It is deleted rather than re-documented: a primitive
    # must not name it and a sprite does not need it, so it has no correct use.
    # See hud:: in CityConstants.h for what the HUD draws with instead.
    L.append("/// The HUD and the radar do NOT draw with this palette. It is")
    L.append("/// bound into a sprite palette slot and read by sprite blits;")
    L.append("/// primitives resolve against the engine's own palette. See")
    L.append("/// hud:: in CityConstants.h.")
    L.append("")
    for direction, canvases in frames.items():
        for i, canvas in enumerate(canvases):
            data = pack4bpp(canvas)
            L.append(f"inline constexpr uint8_t PLAYER_{direction.upper()}_{i}"
                     f"_4BPP[{len(data)}] = {{")
            L.extend(rows_of(data, 16, indent="    ", hexa=True))
            L.append("};")
            L.append("")
    for direction, canvases in frames.items():
        L.append(f"inline constexpr Sprite4bpp kPlayer{direction.capitalize()}"
                 f"[kPlayerWalkFrames] = {{")
        for i in range(len(canvases)):
            L.append(f"    {{ PLAYER_{direction.upper()}_{i}_4BPP,"
                     f" PLAYER_PALETTE_MAPPING, kPlayerSpriteSize,"
                     f" kPlayerSpriteSize, 16 }},")
        L.append("};")
        L.append("")

    L.append("/// The same nine frames with the pistol stamped in. The body is")
    L.append("/// not authored twice -- tools/art_player.py overlays the gun on")
    L.append("/// the walking frames, so the walk cycle has one source and the")
    L.append("/// armed figure cannot drift out of step with the unarmed one.")
    for direction, canvases in armed.items():
        for i, canvas in enumerate(canvases):
            data = pack4bpp(canvas)
            L.append(f"inline constexpr uint8_t PLAYER_ARMED_{direction.upper()}"
                     f"_{i}_4BPP[{len(data)}] = {{")
            L.extend(rows_of(data, 16, indent="    ", hexa=True))
            L.append("};")
            L.append("")
    for direction, canvases in armed.items():
        L.append(f"inline constexpr Sprite4bpp "
                 f"kPlayerArmed{direction.capitalize()}[kPlayerWalkFrames] = {{")
        for i in range(len(canvases)):
            L.append(f"    {{ PLAYER_ARMED_{direction.upper()}_{i}_4BPP,"
                     f" PLAYER_PALETTE_MAPPING, kPlayerSpriteSize,"
                     f" kPlayerSpriteSize, 16 }},")
        L.append("};")
        L.append("")

    L.append("/// The weapons as they lie in the street, before anybody has")
    L.append("/// picked them up. Drawn side-on and larger than the one in the")
    L.append("/// hand: a two-pixel blob on the pavement is scenery, and these")
    L.append("/// have to read as objects worth walking to -- and as DIFFERENT")
    L.append("/// objects, because a player who has walked past two pistols")
    L.append("/// has to be able to tell at a glance that the third is not.")
    L.append("///")
    L.append("/// Indexed by weapons::WeaponId, so the row the police carry is")
    L.append("/// in here too. Nothing drops one and nothing draws it; the")
    L.append("/// slot exists so the id can be used directly as a subscript")
    L.append("/// instead of through a mapping that could get out of step.")
    for kind in sorted(pickups):
        data = pack4bpp(pickups[kind])
        name = WEAPON_ID_NAMES.get(kind, f"weapon{kind}").upper()
        L.append(f"inline constexpr uint8_t {name}_PICKUP_4BPP"
                 f"[{len(data)}] = {{")
        L.extend(rows_of(data, 16, indent="    ", hexa=True))
        L.append("};")
        L.append("")
    slots = max(pickups) + 1
    L.append(f"inline constexpr Sprite4bpp kWeaponPickups[{slots}] = {{")
    for kind in range(slots):
        source = pickups.get(kind, pickups[min(pickups)])
        name = (WEAPON_ID_NAMES.get(kind) if kind in pickups
                else WEAPON_ID_NAMES[min(pickups)])
        note = ("" if kind in pickups
                else "  // unused: nobody drops a police sidearm")
        L.append(f"    {{ {name.upper()}_PICKUP_4BPP, PLAYER_PALETTE_MAPPING,"
                 f" kPlayerSpriteSize, kPlayerSpriteSize, 16 }},{note}")
    L.append("};")
    L.append("")
    L.append("/// Kept as a name for the one call site that only ever means")
    L.append("/// the pistol.")
    L.append("inline constexpr const Sprite4bpp& kPistolPickup ="
             " kWeaponPickups[0];")
    L.append("")
    L.append("}  // namespace top_down_city")
    L.append("")
    return "\n".join(L)


def vehicle_box(canvas: Canvas) -> tuple[int, int, int, int]:
    """Bounding box of a car's bodywork, excluding its cast shadow.

    The C++ side takes the collision box from this rather than from a
    hand-copied constant, so nudging the drawing inside its cell cannot leave
    the box behind.
    """
    shadow = 1 + [name for name, _r, _g, _b in city_art.VEHICLE_PALETTE[1:]
                  ].index("SHADOW")
    xs, ys = [], []
    for y, row in enumerate(canvas.rows()):
        for x, value in enumerate(row):
            if value != 0 and value != shadow:
                xs.append(x)
                ys.append(y)
    if not xs:
        raise SystemExit("a vehicle frame is empty")
    return min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1


def emit_vehicles(frames: dict[str, list[Canvas]]) -> str:
    """VehicleSprites.h: four headings per car colour, one sprite palette."""
    L = [BANNER, "", "#pragma once", "", "#include <graphics/Renderer.h>",
         "#include <graphics/Color.h>", "#include <stdint.h>", "",
         "namespace top_down_city {", "",
         "using pixelroot32::graphics::Sprite4bpp;",
         "using pixelroot32::graphics::Color;", "",
         f"static constexpr uint8_t kVehicleSpriteSize = {TILE};",
         f"static constexpr uint8_t kVehicleColorCount = {len(frames)};",
         f"static constexpr uint8_t kVehicleHeadingCount = {len(VEHICLE_HEADINGS)};",
         "",
         "/// The CARS scene slot, installed in a SPRITE palette slot instead:",
         "/// the cars left the tilemap when they became actors, but they kept",
         "/// their sixteen colours. Entry 0 stays 0x0000 for the cut-out.",
         "inline constexpr uint16_t VEHICLE_PALETTE_RGB565[16] = {"]
    for i, (cname, r, g, b) in enumerate(city_art.VEHICLE_PALETTE):
        L.append(f"    0x{rgb565(r, g, b):04X},  // {i:2d}: {cname}")
    L.append("};")
    L.append("")
    L.append("inline constexpr Color VEHICLE_PALETTE_MAPPING[16] = {")
    for row in range(4):
        L.append("    " + " ".join(f"(Color){row * 4 + col}," for col in range(4)))
    L.append("};")
    L.append("")
    L.append("/// Collision box per heading, {x, y, width, height} inside the")
    L.append("/// 16x16 cell, measured from the art rather than guessed: the")
    L.append("/// drawing is not centred in its cell (one blank row above the")
    L.append("/// body, two below), so rotating it to face east moves the box")
    L.append("/// by a pixel. The baked cast shadow is excluded -- a shadow is")
    L.append("/// not a bumper.")
    L.append("inline constexpr uint8_t VEHICLE_BOXES[kVehicleHeadingCount][4] = {")
    for index, heading in enumerate(VEHICLE_HEADINGS):
        boxes = {vehicle_box(canvases[index]) for canvases in frames.values()}
        if len(boxes) != 1:
            raise SystemExit(f"vehicle heading {heading}: the colours disagree "
                             f"on the collision box: {sorted(boxes)}")
        x, y, w, h = boxes.pop()
        L.append(f"    {{ {x}, {y}, {w}, {h} }},  // {heading}")
    L.append("};")
    L.append("")
    L.append("/// Heading order matches city_scene::VehicleHeading.")
    for name, canvases in frames.items():
        for heading, canvas in zip(VEHICLE_HEADINGS, canvases):
            data = pack4bpp(canvas)
            L.append(f"inline constexpr uint8_t {name}_{heading}_4BPP[{len(data)}] = {{")
            L.extend(rows_of(data, 16, indent="    ", hexa=True))
            L.append("};")
            L.append("")
    L.append("inline constexpr Sprite4bpp "
             "kVehicleSprites[kVehicleColorCount][kVehicleHeadingCount] = {")
    for name in frames:
        L.append(f"    {{  // {name}")
        for heading in VEHICLE_HEADINGS:
            L.append(f"        {{ {name}_{heading}_4BPP, VEHICLE_PALETTE_MAPPING,"
                     f" kVehicleSpriteSize, kVehicleSpriteSize, 16 }},")
        L.append("    },")
    L.append("};")
    L.append("")
    L.append("}  // namespace top_down_city")
    L.append("")
    return "\n".join(L)


def emit_pedestrians(squashed: Canvas) -> str:
    """PedestrianSprites.h: the recolour palettes and the squashed pose.

    The walk frames are deliberately absent. A pedestrian draws the PLAYER's
    own frames through a different sprite palette slot, so a crowd costs one
    palette each (32 bytes) instead of a sprite sheet each.
    """
    street = city_art.pedestrian_palettes()
    palettes = street + [city_art.police_palette()]
    L = [BANNER, "", "#pragma once", "", '#include "PlayerSprites.h"',
         "#include <stdint.h>", "",
         "namespace top_down_city {", "",
         "/// Recolours the street crowd draws from, at random. The police",
         "/// uniform is NOT one of them -- an officer who turns up on a",
         "/// corner because the dice said so is a police force by accident.",
         f"static constexpr uint8_t kPedestrianTintCount = {len(street)};", "",
         "/// The station officers' recolour, spawned only by the interior.",
         "/// It is a tint like any other and therefore costs one palette",
         "/// slot and no sprite data at all.",
         f"static constexpr uint8_t kPoliceTint = {len(street)};", "",
         f"static constexpr uint8_t kPedestrianPaletteCount = {len(palettes)};", "",
         "/// One palette per person, in the same index order as the",
         "/// player's: the frames ARE the player's frames, so entry 5 has to",
         "/// stay the hair and entry 6 the shirt in every one of them. None",
         "/// of them wears the player's red-over-blue.",
         "inline constexpr uint16_t PEDESTRIAN_PALETTES[kPedestrianPaletteCount][16] = {"]
    for tint_name, entries in palettes:
        L.append(f"    {{  // {tint_name}")
        for i, (cname, r, g, b) in enumerate(entries):
            L.append(f"        0x{rgb565(r, g, b):04X},  // {i:2d}: {cname}")
        L.append("    },")
    L.append("};")
    L.append("")
    data = pack4bpp(squashed)
    L.append("/// What is left of a pedestrian a car drove over. Drawn through")
    L.append("/// that pedestrian's own palette slot, so the body keeps the")
    L.append("/// colours it was wearing.")
    L.append(f"inline constexpr uint8_t PEDESTRIAN_SQUASHED_4BPP[{len(data)}] = {{")
    L.extend(rows_of(data, 16, indent="    ", hexa=True))
    L.append("};")
    L.append("")
    L.append("inline constexpr Sprite4bpp kPedestrianSquashed = {")
    L.append("    PEDESTRIAN_SQUASHED_4BPP, PLAYER_PALETTE_MAPPING,")
    L.append("    kPlayerSpriteSize, kPlayerSpriteSize, 16")
    L.append("};")
    L.append("")
    L.append("}  // namespace top_down_city")
    L.append("")
    return "\n".join(L)


def emit_interior_header(iv: Interior) -> str:
    """One room, declared exactly like the city's own scene.

    Every interior is a scene of its own, with its own tile pools, indices,
    behaviour layers and init(). The only thing the rooms share with the city
    and with each other is a palette SLOT -- the engine's bank of eight is
    global and every scene is resident at once, because entering a room is
    Engine::setScene rather than a push (engine 1.9.0 exposes setScene, which
    re-inits, but not SceneManager::pushScene).
    """
    ex, ey = iv.exit()
    room = iv.layers()
    L: list[str] = [BANNER, "", "#pragma once", "",
                    "#include <graphics/Renderer.h>",
                    "#include <physics/TileAttributes.h>",
                    "#include <cstdint>", "",
                    "/**",
                    f" * {iv.title}: one of the places in this demo that is",
                    " * not the city.",
                    " *"]
    for line in iv.doc:
        L.append(f" * {line}".rstrip())
    L.append(" */")
    L.append(f"namespace top_down_city::{iv.namespace} {{")
    L.append("")
    L.extend(["using pixelroot32::physics::TileBehaviorLayer;",
              "using pixelroot32::physics::TILE_NONE;",
              "using pixelroot32::physics::TILE_SOLID;", ""])

    L.extend(palette_lines(INTERIOR_SLOT, *PALETTE_SLOTS[INTERIOR_SLOT]))

    L.append("    // Identity mapping, as in the city: a 4bpp pixel value")
    L.append("    // indexes its cell's palette slot directly.")
    L.append("    static const pixelroot32::graphics::Color PALETTE_MAPPING[16] = {")
    for row in range(4):
        L.append("        " + " ".join(
            f"(pixelroot32::graphics::Color){row * 4 + col}," for col in range(4)))
    L.append("    };")
    L.append("")

    L.append("    // --- Room dimensions ---")
    L.append("    // Fifteen tiles square is not a round number, it is the")
    L.append("    // viewport: 15 * 16 px is exactly the 240x240 panel, so the")
    L.append("    // camera has nothing to scroll indoors.")
    L.append(f"    static const uint8_t TILE_SIZE = {TILE};")
    L.append(f"    static const uint8_t MAP_WIDTH = {iv.width};")
    L.append(f"    static const uint8_t MAP_HEIGHT = {iv.height};")
    L.append("")

    if iv.staffed:
        officers = iv.officers()
        L.append("    // --- Who is on duty ---")
        L.append("    // Actors, not tiles. Each one is the player's own nine")
        L.append("    // frames drawn through the police palette slot, so the")
        L.append("    // whole station staff costs 32 bytes of colour and no")
        L.append("    // sprite data. They wander from these opening positions.")
        L.append("    struct OfficerPost {")
        L.append("        uint8_t tileX;")
        L.append("        uint8_t tileY;")
        L.append("    };")
        L.append(f"    static const uint8_t NUM_OFFICERS = {len(officers)};")
        L.append("    static const OfficerPost OFFICERS[NUM_OFFICERS] = {")
        for (x, y) in officers:
            L.append(f"        {{ {x}, {y} }},")
        L.append("    };")
        L.append("")

    till = iv.service()
    if till is not None:
        L.append("    // --- The counter ---")
        L.append("    // The cell the player stands in to be served. The")
        L.append("    // counter itself is drawn beside it and is solid, so")
        L.append("    // this is the floor in front of it -- a coordinate,")
        L.append("    // not a tile with a meaning.")
        L.append("    static const bool HAS_COUNTER = true;")
        L.append(f"    static const uint8_t COUNTER_TILE_X = {till[0]};")
        L.append(f"    static const uint8_t COUNTER_TILE_Y = {till[1]};")
        L.append("")

    L.append("    // --- The way out, and where you come in ---")
    L.append(f"    static const uint8_t EXIT_TILE_X = {ex};")
    L.append(f"    static const uint8_t EXIT_TILE_Y = {ey};")
    L.append("    // One cell in from the mat, so walking in does not put the")
    L.append("    // player on the very trigger that sends them back out.")
    L.append(f"    static const uint8_t SPAWN_TILE_X = {ex};")
    L.append(f"    static const uint8_t SPAWN_TILE_Y = {ey - 1};")
    L.append("")

    L.append("    // --- Layer definitions ---")
    L.append("    // Two, not three: a room has no third storey of decoration")
    L.append("    // to put on top of it.")
    for layer, _tiles in INTERIOR_LAYERS:
        L.append(f"    extern pixelroot32::graphics::TileMap4bpp {layer.lower()};")
    L.append("")
    L.append("    /// Binds this scene's palette slot and fills its tilemap")
    L.append("    /// descriptors. Call once from Scene::init(), alongside")
    L.append("    /// city_scene::init() and before the first draw.")
    L.append("    void init();")
    L.append("")

    L.append("    // ====================================================")
    L.append("    // Tile behaviour layers")
    L.append("    // Dense TileFlags, one byte per cell. 0 = TILE_NONE.")
    L.append("    // ====================================================")
    for layer, key in (("BACKGROUND", "background_flags"),
                       ("ITEMS", "items_flags")):
        flags = room[key]
        solid = sum(1 for f in flags if f)
        L.append(f"    // Behaviour layer: {layer} ({iv.width}x{iv.height}) -- "
                 f"{solid} solid cells")
        L.append(f"    static const uint8_t TILE_BEHAVIOR_LAYER_{layer}"
                 f"[{len(flags)}] = {{")
        L.extend(rows_of(flags, iv.width))
        L.append("    };")
        L.append("")
    L.append("    static const TileBehaviorLayer behavior_layers[] = {")
    for layer, _tiles in INTERIOR_LAYERS:
        L.append(f"        {{ TILE_BEHAVIOR_LAYER_{layer}, "
                 f"{iv.width}, {iv.height} }},")
    L.append("    };")
    L.append(f"    static const uint8_t NUM_BEHAVIOR_LAYERS = {len(INTERIOR_LAYERS)};")
    L.append("")
    for i, (layer, _t) in enumerate(INTERIOR_LAYERS):
        L.append(f"    static const uint8_t BEHAVIOR_LAYER_{layer} = {i};")
    L.append("")
    L.append(f"}}  // namespace top_down_city::{iv.namespace}")
    L.append("")
    return "\n".join(L)


def emit_interior_source(iv: Interior,
                         tilesets: dict[str, list[Canvas]]) -> str:
    """The room's tile pools and indices.

    The pools are the WHOLE interior tileset in every room, deliberately: a
    per-room subset would make a tile index mean something different in each
    file and turn a shared legend into a lookup table per room. Every interior
    tile is 128 bytes of flash, the pool is small, and none of it is RAM.
    """
    room = iv.layers()
    L: list[str] = [BANNER, "", f'#include "{iv.key}.h"', "",
                    f"namespace top_down_city::{iv.namespace} {{", ""]
    stride = (TILE * TILE) // 2

    for layer, tiles in INTERIOR_LAYERS:
        canvases = tilesets[layer]
        L.append(f"    // --- {layer}: {len(canvases)} tiles, "
                 f"{len(canvases) * stride} bytes ---")
        L.append(f"    static const uint8_t {layer}_TILESET_DATA_POOL"
                 f"[{len(canvases) * stride}] = {{")
        for i, canvas in enumerate(canvases):
            body = ", ".join(f"0x{v:02X}" for v in pack4bpp(canvas))
            comma = "" if i == len(canvases) - 1 else ","
            L.append(f"        {body}{comma}  // {i}: {tiles[i][0]}")
        L.append("    };")
        L.append("")
        L.append(f"    static const pixelroot32::graphics::Sprite4bpp "
                 f"{layer}_TILESET_SPRITES[{len(canvases)}] = {{")
        for i in range(len(canvases)):
            L.append(f"        {{ &{layer}_TILESET_DATA_POOL[{i * stride}], "
                     f"PALETTE_MAPPING, {TILE}, {TILE}, 16 }},")
        L.append("    };")
        L.append("")

        key = layer.lower()
        L.append(f"    static const uint8_t {layer}_INDICES[{len(room[key])}] = {{")
        L.extend(rows_of(room[key], iv.width))
        L.append("    };")
        L.append("")
        slots = room[key + "_slots"]
        L.append(f"    // Per-cell palette slot for {layer}")
        L.append(f"    static const uint8_t {layer}_PALETTE_INDICES"
                 f"[{len(slots)}] = {{")
        L.extend(rows_of(slots, iv.width))
        L.append("    };")
        L.append("")
        L.append(f"    pixelroot32::graphics::TileMap4bpp {key};")
        L.append("")

    L.append("    void init() {")
    L.append("        // Every interior owns the same one slot of the global")
    L.append("        // background bank; city_scene::init() binds the other")
    L.append("        // seven. Binding it twice is binding it to the same")
    L.append("        // sixteen colours, which is why the rooms can share it.")
    L.append(f"        pixelroot32::graphics::setBackgroundCustomPaletteSlot("
             f"{INTERIOR_SLOT}, {PALETTE_SLOTS[INTERIOR_SLOT][0]}_PALETTE);")
    L.append("")
    for layer, tiles in INTERIOR_LAYERS:
        var = layer.lower()
        L.append(f"        // {layer}")
        L.append(f"        {var}.width = MAP_WIDTH;")
        L.append(f"        {var}.height = MAP_HEIGHT;")
        L.append(f"        {var}.tileWidth = TILE_SIZE;")
        L.append(f"        {var}.tileHeight = TILE_SIZE;")
        L.append(f"        {var}.indices = (uint8_t*){layer}_INDICES;")
        L.append(f"        {var}.tiles = {layer}_TILESET_SPRITES;")
        L.append(f"        {var}.tileCount = {len(tiles)};")
        L.append(f"        {var}.runtimeMask = nullptr;")
        L.append(f"        {var}.animManager = nullptr;")
        L.append(f"        {var}.paletteIndices = {layer}_PALETTE_INDICES;")
        L.append("")
    L.append("    }")
    L.append("")
    L.append(f"}}  // namespace top_down_city::{iv.namespace}")
    L.append("")
    return "\n".join(L)


def write(path: str, content: str) -> None:
    full = os.path.normpath(os.path.join(OUT_DIR, path))
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(content)
    print(f"wrote {full} ({len(content):,} bytes)")


def main() -> None:
    tilesets = {layer: [Canvas(grid) for _n, grid, _s in tiles]
                for layer, tiles in LAYERS}
    night_tilesets = {layer: [Canvas(grid) for _n, grid, _s in tiles]
                      for layer, tiles in NIGHT_LAYERS}
    frames = {
        direction: [player_frame(direction, i) for i in range(3)]
        for direction in ("down", "up", "side")
    }
    armed = {
        direction: [armed_player_frame(direction, i) for i in range(3)]
        for direction in ("down", "up", "side")
    }
    pickups = {
        WEAPON_PISTOL: Canvas(city_art.pistol_pickup()),
        WEAPON_SHOTGUN: Canvas(city_art.shotgun_pickup()),
    }
    vehicles = {name: [Canvas(grid) for grid in grids]
                for name, grids in city_art.vehicle_frames().items()}
    squashed = Canvas(city_art.squashed_frame())
    city = City(SEED)
    city.generate()
    checks = validate(city, tilesets, night_tilesets)
    # Variants, shore edges and kerbs go in AFTER validation: the checks and
    # the layout only ever see canonical names.
    city_art.dress(city)

    # One file pair per scene, one folder per kind of asset. The city and every
    # interior are separate scenes: none includes another, and each carries its
    # own palettes, tilesets, indices, behaviour layers and init().
    interior_tilesets = {layer: [Canvas(grid) for _n, grid, _s in tiles]
                         for layer, tiles in INTERIOR_LAYERS}

    write("tilemaps/city_scene.h", emit_scene_header(city, tilesets))
    write("tilemaps/city_scene.cpp",
          emit_scene_source(city, tilesets, night_tilesets))
    for interior in INTERIORS:
        write(f"tilemaps/{interior.key}.h", emit_interior_header(interior))
        write(f"tilemaps/{interior.key}.cpp",
              emit_interior_source(interior, interior_tilesets))
    write("sprites/PlayerSprites.h", emit_player(frames, armed, pickups))
    write("sprites/VehicleSprites.h", emit_vehicles(vehicles))
    write("sprites/PedestrianSprites.h", emit_pedestrians(squashed))

    cells = MAP_W * MAP_H
    tile_bytes = sum(len(t) for _l, t in LAYERS) * (TILE * TILE // 2)
    map_bytes = len(LAYERS) * cells * 3      # indices + palette slots + flags
    print(textwrap.dedent(f"""
        layers     : {', '.join(l for l, _ in LAYERS)}
        tiles      : {', '.join(f'{l}={len(t)}' for l, t in LAYERS)}
                     = {tile_bytes:,} bytes of tileset
        palettes   : {len(PALETTE_SLOTS)} slots x 16 colours
        map        : {MAP_W}x{MAP_H} tiles = {MAP_W * TILE}x{MAP_H * TILE} px
        map bytes  : {map_bytes:,} = {len(LAYERS)} layers x {cells:,} cells x 3
                     (indices + palette slot + behaviour flags)
        spawn tile : {city.spawn()}
        vehicles   : {len(city.vehicles)} driveable cars,
                     {len(vehicles)} colours x {len(VEHICLE_HEADINGS)} headings
                     = {len(vehicles) * len(VEHICLE_HEADINGS) * (TILE * TILE // 2):,} bytes of sprite
        pedestrians: {len(city_art.PEDESTRIAN_TINT_NAMES)} palettes over the player's frames
    """).strip())
    print("\n".join(checks))


if __name__ == "__main__":
    main()
