"""Assemble the hand-authored art modules into what the generator consumes.

The art lives in ``tools/art_*.py`` as character grids (see ``art_dsl.py``
for the module contract). This module is the only place that knows which
modules exist, which palette slot each one occupies and how their tiles,
stamps, shadows and details are laid out in the three tilesets. The layout
code in ``generate_city_assets.py`` asks for canonical prop names (TREE,
LAMP, CAR_V ...) and never imports an art module directly.

Everything here is resolved once at import time:

    PALETTE_SLOTS     8 x (slot name, 16 x (name, r, g, b))
    BACKGROUND_TILES  [(name, grid, slot)]   opaque terrain, index 0 = BLANK
    ITEMS_TILES       [(name, grid, slot)]   props and stamp tiles
    DETAILS_TILES     [(name, grid, slot)]   cast shadows and decoration
    STAMPS            {name: (w, h, [tile names row-major])}
    PROP_FAMILY       {name: ground} from every module
    DOWNTOWN_LOTS     stamps drawn for pavement, SUBURB_HOUSES for grass
    PLAYER_PALETTE    16 entries for the sprite bank; player_frame(dir, i)
    pedestrian_palettes() / squashed_frame()  the pooled pedestrians
    VEHICLE_NAMES / vehicle_frames()          the driveable cars
    BG_WALKABLE       background names the player may stand on
    dress(city)       the post-validation terrain pass

A grid is a plain 16x16 list of palette indices; the generator wraps it in
its ``Canvas`` for packing.
"""
from __future__ import annotations

import random

import art_dsl
import art_nature
import art_urban
import art_vegetation
import art_furniture
import art_cars
import art_buildings_a
import art_buildings_b
import art_buildings_c
import art_interior
import art_player

TILE = art_dsl.TILE

# --------------------------------------------------------------------------
# Palette slots
# --------------------------------------------------------------------------
# One slot per module, in this fixed order. The engine keeps a bank of eight
# background palette slots (`paletteIndices` bits 0-2, see the engine's
# graphics/Renderer.h), and the art uses all eight: three for the buildings
# alone, because a downtown of tan, navy, red and glass could not be dithered
# out of one fifteen-colour ramp.
#
# Slot 4 is the interior's. It used to be the CARS palette, from when parked
# cars were tiles; stage 2 made them actors and left the slot bound to a
# palette no cell referenced. art_cars is still imported -- it owns the vehicle
# SPRITE frames and their palette, which belong to the sprite bank, not here.
_MODULES = [art_nature, art_urban, art_vegetation, art_furniture, art_interior,
            art_buildings_a, art_buildings_b, art_buildings_c]
_BUILDING_MODULES = [art_buildings_a, art_buildings_b, art_buildings_c]

SLOT_INDEX = {module.SLOT: i for i, module in enumerate(_MODULES)}

TRANSPARENT_ENTRY = ("TRANSPARENT", 0, 0, 0)


def _slot_palette(module) -> list[tuple[str, int, int, int]]:
    """Entry 0 is the transparency sentinel; pad short palettes to sixteen."""
    entries = [TRANSPARENT_ENTRY] + [tuple(e) for e in module.PALETTE]
    if len(entries) > 16:
        raise ValueError(f"{module.SLOT}: {len(entries) - 1} colours, the slot holds 15")
    while len(entries) < 16:
        entries.append((f"UNUSED_{len(entries)}", 0, 0, 0))
    return entries


PALETTE_SLOTS: list[tuple[str, list[tuple[str, int, int, int]]]] = [
    (module.SLOT, _slot_palette(module)) for module in _MODULES
]

# --------------------------------------------------------------------------
# Tilesets
# --------------------------------------------------------------------------
_GRIDS = {module.SLOT: art_dsl.module_grids(module) for module in _MODULES}


def blank_grid() -> list[list[int]]:
    return [[0] * TILE for _ in range(TILE)]


def _tiles_of(module) -> list[tuple[str, list[list[int]], int]]:
    slot = SLOT_INDEX[module.SLOT]
    return [(name, grid, slot) for name, grid in _GRIDS[module.SLOT]["tiles"].items()]


def _layer_tiles_of(module, background: bool) -> list[tuple[str, list[list[int]], int]]:
    """One half of a module that draws both its ground and what stands on it.

    Every other art module belongs to a single layer. The interior is the
    exception: it draws a room rather than dressing the city's ground, so it
    supplies its own floor, naming those tiles in BACKGROUND_TILE_NAMES.
    Everything else it declares is an Items prop.
    """
    names = set(getattr(module, "BACKGROUND_TILE_NAMES", ()))
    return [entry for entry in _tiles_of(module)
            if (entry[0] in names) == background]


# Background: the two terrain modules, every tile fully opaque. The blank at
# index 0 is the renderer's "skip this cell" value and is never referenced.
BACKGROUND_TILES: list[tuple[str, list[list[int]], int]] = [("BLANK", blank_grid(), 0)]
BACKGROUND_TILES += _tiles_of(art_nature) + _tiles_of(art_urban)
for _name, _grid, _slot in BACKGROUND_TILES[1:]:
    if not art_dsl.is_opaque(_grid):
        raise ValueError(f"background tile {_name} has transparent pixels")

# Items: single-tile props first, then every stamp cut into row-major tiles
# named "<STAMP>_<i>". A stamp's tiles are consecutive, which is convenient
# but nothing depends on it -- placement goes through STAMPS by name.
ITEMS_TILES: list[tuple[str, list[list[int]], int]] = [("BLANK", blank_grid(), 0)]
ITEMS_TILES += _tiles_of(art_vegetation) + _tiles_of(art_furniture)

STAMPS: dict[str, tuple[int, int, list[str]]] = {}
for _module in _MODULES:
    _slot = SLOT_INDEX[_module.SLOT]
    for _name, (_w, _h, _grids) in _GRIDS[_module.SLOT]["stamps"].items():
        if _name in STAMPS:
            raise ValueError(f"stamp {_name} is defined twice")
        _names = [f"{_name}_{i}" for i in range(_w * _h)]
        ITEMS_TILES += [(n, g, _slot) for n, g in zip(_names, _grids)]
        STAMPS[_name] = (_w, _h, _names)

# Details: every cast shadow as "<name>_SH" (so a prop finds its shadow by
# appending the suffix, and "<name>_SPILL_SH" is the cell to its right), then
# the pure decoration tiles under their own names.
DETAILS_TILES: list[tuple[str, list[list[int]], int]] = [("BLANK", blank_grid(), 0)]
for _module in _MODULES:
    _slot = SLOT_INDEX[_module.SLOT]
    DETAILS_TILES += [(f"{n}_SH", g, _slot)
                      for n, g in _GRIDS[_module.SLOT]["shadows"].items()]
for _module in _MODULES:
    _slot = SLOT_INDEX[_module.SLOT]
    DETAILS_TILES += [(n, g, _slot) for n, g in _GRIDS[_module.SLOT]["details"].items()]

# The interior is a scene of its own and gets pools of its own, exactly like
# the city's: index 0 is the renderer's "skip this cell" blank and is never
# referenced. Sharing the city's pools is fewer bytes on paper only -- the ten
# interior tiles would sit in the city's tilesets where no city cell indexes
# them, and the two scenes could no longer be read, regenerated or reused
# apart.
INTERIOR_BACKGROUND_TILES: list[tuple[str, list[list[int]], int]] = [
    ("BLANK", blank_grid(), 0)]
INTERIOR_BACKGROUND_TILES += _layer_tiles_of(art_interior, background=True)
for _name, _grid, _slot in INTERIOR_BACKGROUND_TILES[1:]:
    if not art_dsl.is_opaque(_grid):
        raise ValueError(f"interior background tile {_name} has transparent pixels")

INTERIOR_ITEMS_TILES: list[tuple[str, list[list[int]], int]] = [
    ("BLANK", blank_grid(), 0)]
INTERIOR_ITEMS_TILES += _layer_tiles_of(art_interior, background=False)

for _layer_name, _tiles in (("BACKGROUND", BACKGROUND_TILES), ("ITEMS", ITEMS_TILES),
                            ("DETAILS", DETAILS_TILES),
                            ("INTERIOR_BACKGROUND", INTERIOR_BACKGROUND_TILES),
                            ("INTERIOR_ITEMS", INTERIOR_ITEMS_TILES)):
    _seen = [n for n, _g, _s in _tiles]
    if len(set(_seen)) != len(_seen):
        raise ValueError(f"{_layer_name}: duplicate tile names")
    if len(_tiles) > 256:
        raise ValueError(f"{_layer_name}: {len(_tiles)} tiles, the index byte holds 256")

INTERIOR_BG_INDEX = {name: i
                     for i, (name, _g, _s) in enumerate(INTERIOR_BACKGROUND_TILES)}
INTERIOR_IT_INDEX = {name: i
                     for i, (name, _g, _s) in enumerate(INTERIOR_ITEMS_TILES)}

BG_INDEX = {name: i for i, (name, _g, _s) in enumerate(BACKGROUND_TILES)}
IT_INDEX = {name: i for i, (name, _g, _s) in enumerate(ITEMS_TILES)}
DT_INDEX = {name: i for i, (name, _g, _s) in enumerate(DETAILS_TILES)}

# --------------------------------------------------------------------------
# After dark
# --------------------------------------------------------------------------
# A second form of the Items and Details tilesets, swapped in by the clock.
# Not a light source and no lighting pass: the after-dark tiles are the same
# drawings with their windows and lamp lenses repainted in a palette entry the
# daylight art never uses, and NightLights.h leaves that entry alone while the
# day/night tint takes a third off everything around it.
#
# Two kinds of tile change, declared separately because they fail differently:
#
#   NIGHT   a tile whose after-dark form is a different DRAWING. Only the
#           street lamp: it lights its lens and stops casting a shadow, which
#           no rule could have derived.
#   WINDOWS a tile whose after-dark form is the same drawing with some panes
#           lit. A rule does derive that, so the artist supplies only which
#           ink is glass and how much of it is lit.
#
# The lit pattern belongs to the TILESET, so every OFFICE_TAN in the city is
# lit the same way. That is the price of a tileset variant over a light source
# and the reason the effect costs no RAM; at a fifteen-tile viewport the repeat
# is not visible, two copies of one building rarely being on screen together.
NIGHT_SEED = 20260830


def _lit_grid(module, lookup, grid: list[list[int]], chars: str,
              share: float, key: str) -> list[list[int]]:
    """``grid`` with a deterministic share of its window panes lit."""
    glass = {lookup[module.INK[ch]] for ch in chars}
    lit = lookup[module.LIT]
    panes = art_dsl.regions(grid, glass)
    if not panes:
        raise ValueError(f"{key}: declared windows in {chars!r}, found no glass")
    order = list(range(len(panes)))
    # Seeded by the object's own name, so adding a building does not relight
    # every other one -- a diff of the generated tileset should be about the
    # thing that changed.
    random.Random(f"{NIGHT_SEED}:{key}").shuffle(order)
    out = [list(row) for row in grid]
    for i in order[:round(len(panes) * share)]:
        for x, y in panes[i]:
            out[y][x] = lit
    return out


def _night_tiles(day: list[tuple[str, list[list[int]], int]]
                 ) -> list[tuple[str, list[list[int]], int]]:
    """One layer's after-dark tileset: the day list with the tiles that change
    replaced, in the same order, so a cell's index means the same thing under
    either variant. That is what lets the swap be one pointer."""
    night = {n: g for n, g, _s in day}
    for module in _MODULES:
        lookup = art_dsl.palette_lookup(module.PALETTE)
        # A module's NIGHT table names tiles across both layers -- the lamp
        # prop is on Items, the light it throws on Details -- so each layer
        # takes its own names. That every name lands on SOME layer is checked
        # once, below, rather than twice here.
        for name, grid in _GRIDS[module.SLOT]["night"].items():
            if name in night:
                night[name] = grid
        for stamp, (chars, share) in getattr(module, "WINDOWS", {}).items():
            if f"{stamp}_0" not in night:
                continue
            w, h, grid = _GRIDS[module.SLOT]["stamps_uncut"][stamp]
            for i, tile in enumerate(art_dsl.cut(
                    _lit_grid(module, lookup, grid, chars, share, stamp), w, h)):
                night[f"{stamp}_{i}"] = tile
    return [(n, night[n], s) for n, _g, s in day]


NIGHT_ITEMS_TILES = _night_tiles(ITEMS_TILES)
NIGHT_DETAILS_TILES = _night_tiles(DETAILS_TILES)

# Every name the art declares an after-dark form for has to be a tile in one
# of the two layers. A typo here is silent otherwise: the tile is simply never
# lit, and nothing in the build says so.
_NIGHT_NAMED = {n for m in _MODULES for n in _GRIDS[m.SLOT]["night"]}
_NIGHT_NAMED |= {f"{stamp}_0" for m in _MODULES
                 for stamp in getattr(m, "WINDOWS", {})}
_NIGHT_KNOWN = ({n for n, _g, _s in ITEMS_TILES}
                | {n for n, _g, _s in DETAILS_TILES})
if _NIGHT_NAMED - _NIGHT_KNOWN:
    raise ValueError(f"NIGHT/WINDOWS name tiles that do not exist: "
                     f"{sorted(_NIGHT_NAMED - _NIGHT_KNOWN)}")

# Which palette entry each slot lights, for the C++ side to leave untinted.
# Derived from the art rather than restated in C++: a lit entry that has
# drifted one place is not a compile error, it is a city lighting its
# drainpipes.
LIT_ENTRIES: dict[int, tuple[int, str]] = {}
for _module in _MODULES:
    _lit_name = getattr(_module, "LIT", None)
    if _lit_name is None:
        continue
    LIT_ENTRIES[SLOT_INDEX[_module.SLOT]] = (
        art_dsl.palette_lookup(_module.PALETTE)[_lit_name], _lit_name)

PROP_FAMILY: dict[str, str] = {}
for _module in _MODULES:
    PROP_FAMILY.update(getattr(_module, "FAMILY", {}))

# Which stamps the layout may drop on a pavement lot and which on a lawn.
# Derived from the artists' FAMILY tables rather than listed here, so a new
# building stamp joins the right pool by declaring its ground.
# Stamps the layout must never DRAW. They exist to be swapped in afterwards
# over a building the packer already placed; putting one in a lot pool would
# place several of them and -- the pool being drawn from the same RNG stream as
# every pass after it -- shift every subsequent draw and regenerate the whole
# island. See City.single_corner_shop().
SWAP_ONLY = {"SHOP_OPEN"}

DOWNTOWN_LOTS = [name for m in _BUILDING_MODULES for name in m.STAMPS
                 if m.FAMILY[name] == "pave" and name not in SWAP_ONLY]
SUBURB_HOUSES = [name for m in _BUILDING_MODULES for name in m.STAMPS
                 if m.FAMILY[name] == "grass"]

# Terrain the player can walk on. Every variant of a walkable family is
# walkable too, which is what keeps dress() from changing the collision map.
_WALKABLE_FAMILIES = ("SAND", "GRASS", "DIRT", "ROAD", "CROSSWALK", "SIDEWALK",
                      "PLAZA", "PARKING")
BG_WALKABLE = {name for name in BG_INDEX
               if name.split("_")[0] in _WALKABLE_FAMILIES}

# --------------------------------------------------------------------------
# Player
# --------------------------------------------------------------------------
PLAYER_PALETTE: list[tuple[str, int, int, int]] = (
    [TRANSPARENT_ENTRY] + [tuple(e) for e in art_player.PALETTE])
if len(PLAYER_PALETTE) != 16:
    raise ValueError(f"player palette has {len(PLAYER_PALETTE)} entries, expected 16")

_PLAYER_LOOKUP = art_dsl.palette_lookup(art_player.PALETTE)


def player_frame(direction: str, frame: int) -> list[list[int]]:
    """16x16 walk frame grid. ``direction`` is 'down', 'up' or 'side'."""
    rows = art_player.FRAMES[direction][frame % len(art_player.FRAMES[direction])]
    return art_dsl.parse(rows, art_player.INK, _PLAYER_LOOKUP,
                         label=f"player {direction}[{frame}]")


def armed_player_frame(direction: str, frame: int) -> list[list[int]]:
    """16x16 walk frame with the pistol stamped in. Same body, one source."""
    frames = art_player.ARMED_FRAMES[direction]
    return art_dsl.parse(frames[frame % len(frames)], art_player.INK,
                         _PLAYER_LOOKUP, label=f"armed {direction}[{frame}]")


def shotgun_pickup() -> list[list[int]]:
    """The shotgun on the ground, through the player palette."""
    return art_dsl.parse(art_player.SHOTGUN_PICKUP, art_player.INK,
                         _PLAYER_LOOKUP, label="shotgun pickup")


def pistol_pickup() -> list[list[int]]:
    """16x16 grid of the pistol lying in the street."""
    return art_dsl.parse(art_player.PICKUP, art_player.INK, _PLAYER_LOOKUP,
                         label="pistol pickup")


def squashed_frame() -> list[list[int]]:
    """16x16 grid of a pedestrian that has been run over."""
    return art_dsl.parse(art_player.SQUASHED, art_player.INK, _PLAYER_LOOKUP,
                         label="squashed")


PEDESTRIAN_TINT_NAMES = [name for name, _o in art_player.PEDESTRIAN_TINTS]


def pedestrian_palettes() -> list[tuple[str, list[tuple[str, int, int, int]]]]:
    """One 16-entry palette per pedestrian recolour.

    Built from PLAYER_PALETTE by replacing named entries, so an index means
    the same thing in every one of them -- which is what lets a pedestrian
    draw the player's own frames through its own palette slot.
    """
    return [_tinted_palette(name, overrides)
            for name, overrides in art_player.PEDESTRIAN_TINTS]


def _tinted_palette(tint_name: str, overrides: dict[str, tuple[int, int, int]]
                    ) -> tuple[str, list[tuple[str, int, int, int]]]:
    unknown = set(overrides) - {e[0] for e in PLAYER_PALETTE}
    if unknown:
        raise ValueError(f"tint {tint_name}: unknown entries {unknown}")
    entries = [(name, *overrides[name]) if name in overrides else (name, r, g, b)
               for name, r, g, b in PLAYER_PALETTE]
    return (tint_name, entries)


def police_palette() -> tuple[str, list[tuple[str, int, int, int]]]:
    """The station officers' recolour.

    Built exactly like a pedestrian tint and kept out of the pedestrian list
    on purpose: the crowd picks its recolour at random, and a police force
    that exists because the dice said so is not a police force.
    """
    return _tinted_palette(*art_player.POLICE_TINT)


# --------------------------------------------------------------------------
# Vehicles
# --------------------------------------------------------------------------
# Cars are actors, not tiles, so their art is resolved here as sprite grids
# against art_cars' own palette. That palette is no longer one of the eight
# background slots -- stage 2 took cars out of the tilemap, stage 4 gave the
# slot to the interior -- so it is built here rather than read back out of
# PALETTE_SLOTS. The C++ side installs it in a SPRITE palette slot at runtime.
VEHICLE_PALETTE: list[tuple[str, int, int, int]] = _slot_palette(art_cars)
VEHICLE_NAMES = list(art_cars.SPRITES)          # index = the exported colour id
VEHICLE_HEADINGS = art_cars.HEADINGS            # N, E, S, W

_CARS_LOOKUP = art_dsl.palette_lookup(art_cars.PALETTE)


def vehicle_frames() -> dict[str, list[list[list[int]]]]:
    """{car name: [grid per heading]} in VEHICLE_HEADINGS order."""
    return {name: [art_dsl.parse(headings[h], art_cars.INK, _CARS_LOOKUP,
                                 label=f"{name}_{h}")
                   for h in VEHICLE_HEADINGS]
            for name, headings in art_cars.SPRITES.items()}


# --------------------------------------------------------------------------
# Dressing: terrain variants and transition edges
# --------------------------------------------------------------------------
# Neighbour order matches the mask convention documented in art_nature.py
# and art_urban.py: bit 1 = N, 2 = E, 4 = S, 8 = W.
_SIDES = ((0, -1), (1, 0), (0, 1), (-1, 0))

# Anything a kerb is drawn against. Sidewalk beside grass, sand or plaza
# stays plain.
CARRIAGEWAY = {"ROAD", "ROAD_LINE_H", "ROAD_LINE_V", "CROSSWALK_H",
               "CROSSWALK_V", "PARKING", "ROAD_MANHOLE"}

MANHOLE_CHANCE = 0.02


def dress(city) -> None:
    """Post-validation pass: terrain variants, shore edges and kerbs.

    Runs after validate() on purpose: the layout and every self-check work on
    canonical names (WATER, SAND, GRASS, SIDEWALK ...) and this is the one
    place variants are introduced, so nothing upstream has to know they exist.
    Every decision reads a snapshot of those names, so a cell already rewritten
    never changes what its neighbour sees.
    """
    rng: random.Random = city.rng
    height = len(city.bg)
    width = len(city.bg[0])
    names = [[city.bg_name(x, y) for x in range(width)] for y in range(height)]

    def at(x: int, y: int) -> str:
        if 0 <= x < width and 0 <= y < height:
            return names[y][x]
        return "WATER"

    def mask(x: int, y: int, foreign) -> int:
        m = 0
        for bit, (dx, dy) in enumerate(_SIDES):
            if foreign(at(x + dx, y + dy)):
                m |= 1 << bit
        return m

    for y in range(height):
        for x in range(width):
            name = names[y][x]
            if name == "WATER":
                m = mask(x, y, lambda n: n != "WATER")
                if m:
                    tile = f"WATER_E{m}"
                else:
                    tile = "WATER_B" if rng.random() < 0.5 else "WATER"
            elif name == "SAND":
                m = mask(x, y, lambda n: n == "WATER")
                if m:
                    tile = f"SAND_E{m}"
                else:
                    tile = "SAND_B" if rng.random() < 0.5 else "SAND"
            elif name == "GRASS":
                m = mask(x, y, lambda n: n == "SAND")
                if m:
                    tile = f"GRASS_S{m}"
                else:
                    tile = "GRASS_B" if rng.random() < 0.5 else "GRASS"
            elif name == "SIDEWALK":
                m = mask(x, y, lambda n: n in CARRIAGEWAY)
                tile = f"SIDEWALK_K{m}" if m else "SIDEWALK"
            elif (name == "ROAD" and city.items[y][x] == 0
                    and (x, y) not in city.junctions
                    and rng.random() < MANHOLE_CHANCE):
                tile = "ROAD_MANHOLE"
            else:
                continue
            city.bg[y][x] = BG_INDEX[tile]
