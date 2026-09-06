#!/usr/bin/env python3
"""Render the whole generated island to a PNG.

The city is 2048x2048 pixels and the panel is 240x240, so nothing in the game
ever shows more than about a seventieth of it at once. This renders the map the
way the engine would draw it -- the three tile layers composited in order,
every cell through its own palette slot -- so the README can show the island
the player only ever sees a corner of.

It is a viewer, not part of the build: it writes a PNG and never touches
src/generated/. It runs the same generator the build runs, in the same order,
so what comes out is the city that shipped rather than a fresh roll.

    python tools/render_map.py screenshots/city_map.png [scale]

``scale`` divides the output: 1 is the full 2048x2048, 2 is half.
"""

from __future__ import annotations

import sys

from PIL import Image

import city_art
import generate_city_assets as gen

LAYER_TILES = {
    "BACKGROUND": gen.BACKGROUND_TILES,
    "ITEMS": gen.ITEMS_TILES,
    "DETAILS": gen.DETAILS_TILES,
}


def build_city() -> gen.City:
    """The shipped island: the same calls main() makes, in the same order.

    The generator draws from one shared RNG stream, so an extra or missing
    draw here would re-roll the whole map. Mirror main() rather than
    shortcutting it.
    """
    tilesets = {layer: [gen.Canvas(grid) for _n, grid, _s in tiles]
                for layer, tiles in gen.LAYERS}
    night = {layer: [gen.Canvas(grid) for _n, grid, _s in tiles]
             for layer, tiles in gen.NIGHT_LAYERS}
    city = gen.City(gen.SEED)
    city.generate()
    gen.validate(city, tilesets, night)
    city_art.dress(city)
    return city


def render(city: gen.City) -> Image.Image:
    tile = gen.TILE
    im = Image.new("RGB", (gen.MAP_W * tile, gen.MAP_H * tile), (0, 0, 0))
    px = im.load()

    for name, grid in (("BACKGROUND", city.bg),
                       ("ITEMS", city.items),
                       ("DETAILS", city.details)):
        tiles = LAYER_TILES[name]
        for ty in range(gen.MAP_H):
            row = grid[ty]
            for tx in range(gen.MAP_W):
                index = row[tx]
                # Index 0 is the reserved blank; the renderer skips it too.
                if index == 0 and name != "BACKGROUND":
                    continue
                _tname, cells, slot = tiles[index]
                palette = gen.PALETTE_SLOTS[slot][1]
                ox, oy = tx * tile, ty * tile
                for y in range(tile):
                    line = cells[y]
                    for x in range(tile):
                        value = line[x]
                        if value == 0:      # entry 0 is transparent
                            continue
                        _cname, r, g, b = palette[value]
                        px[ox + x, oy + y] = (r, g, b)
    return im


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "city_map.png"
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    city = build_city()
    im = render(city)
    if scale > 1:
        im = im.resize((im.width // scale, im.height // scale), Image.NEAREST)
    im.save(path)
    print(f"{path}: {im.width}x{im.height}, spawn tile {city.spawn()}")


if __name__ == "__main__":
    main()
