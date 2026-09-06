"""Pixel-art DSL shared by the ``tools/art_*.py`` modules.

Every tile in the demo is authored as rows of characters, one character per
pixel, so a 16x16 tile is sixteen strings of sixteen characters. ``.`` is the
transparent cut-out (palette value 0); every other character is looked up in
the module's ``INK`` table, which maps it to a palette entry NAME, and the
assembler (``city_art.py``) resolves that name to the entry's index inside the
module's palette slot.

An art module exports:

    SLOT     -- the palette slot name, e.g. "VEGETATION"
    PALETTE  -- up to fifteen (name, r, g, b) entries; entry 0 (transparent)
                is implicit and added by the assembler
    INK      -- {char: palette entry name}; "." is reserved for transparent
    TILES    -- {name: rows} single 16x16 tiles for the Background or Items
                layer
    STAMPS   -- {name: (w_tiles, h_tiles, rows)} multi-tile objects; rows is
                16*h strings of 16*w characters, cut row-major into tiles
    SHADOWS  -- {name: rows} cast shadows drawn on the Details layer for the
                prop or stamp of the same name; "<name>_SPILL" continues a
                shadow into the cell to the right
    DETAILS  -- {name: rows} pure decoration tiles (no collision, Details layer)
    FAMILY   -- {name: "grass" | "pave" | "sand" | "road" | "any"} the ground a
                prop variant is drawn for (its shadow colour matches it)
    NIGHT    -- {tileset name: rows} the after-dark variant of a tile the
                module owns, keyed by the name the tile carries in the
                TILESET rather than the name it has here: "LAMP_P" for a
                prop, "LAMP_P_SH" for its shadow. Optional, and only for
                tiles whose after-dark form is a different DRAWING.
    WINDOWS  -- {object name: (ink chars that are glass, share of panes lit)}
                the rule-driven half of the same thing: city_art finds the
                panes, lights a deterministic share of them and cuts the
                result. Optional.
    LIT      -- the palette entry name a lit pixel is painted in. Required by
                NIGHT and WINDOWS, and it must be an entry the module's
                daylight art never draws -- the day/night tint leaves it
                alone everywhere, so a shared entry is a wall that glows at
                noon. The generator refuses to emit a scene that breaks it.

Helpers below validate the grids, cut stamps into tiles, mirror and rotate,
and render a module to PNG for review.
"""
from __future__ import annotations

import os
from typing import Sequence

TILE = 16
TRANSPARENT = "."


def parse(rows: Sequence[str], ink: dict[str, str], palette_index: dict[str, int],
          width: int = TILE, height: int = TILE, label: str = "?") -> list[list[int]]:
    """Rows of characters -> rows of palette indices (0 = transparent)."""
    if len(rows) != height:
        raise ValueError(f"{label}: expected {height} rows, got {len(rows)}")
    out: list[list[int]] = []
    for y, row in enumerate(rows):
        if len(row) != width:
            raise ValueError(f"{label}: row {y} is {len(row)} wide, expected {width}")
        line: list[int] = []
        for x, ch in enumerate(row):
            if ch == TRANSPARENT:
                line.append(0)
                continue
            if ch not in ink:
                raise ValueError(f"{label}: unknown ink {ch!r} at ({x}, {y})")
            name = ink[ch]
            if name not in palette_index:
                raise ValueError(f"{label}: ink {ch!r} -> {name} is not in the palette")
            line.append(palette_index[name])
        out.append(line)
    return out


def palette_lookup(palette: Sequence[tuple]) -> dict[str, int]:
    """Entry name -> 1-based index (0 is the implicit transparent entry)."""
    if len(palette) > 15:
        raise ValueError(f"palette has {len(palette)} entries, the slot holds 15")
    names = [entry[0] for entry in palette]
    if len(set(names)) != len(names):
        raise ValueError("duplicate palette entry names")
    return {name: i + 1 for i, name in enumerate(names)}


def cut(grid: list[list[int]], w_tiles: int, h_tiles: int) -> list[list[list[int]]]:
    """Cut a (16*h) x (16*w) grid into row-major 16x16 tiles."""
    tiles = []
    for ty in range(h_tiles):
        for tx in range(w_tiles):
            tiles.append([[grid[ty * TILE + y][tx * TILE + x] for x in range(TILE)]
                          for y in range(TILE)])
    return tiles


def hflip(rows: Sequence[str]) -> list[str]:
    return [row[::-1] for row in rows]


def vflip(rows: Sequence[str]) -> list[str]:
    return list(rows)[::-1]


def rot90(rows: Sequence[str]) -> list[str]:
    """Rotate clockwise; a vertical car becomes a horizontal one facing right."""
    h = len(rows)
    w = len(rows[0])
    return ["".join(rows[h - 1 - y][x] for y in range(h)) for x in range(w)]


def is_opaque(grid: list[list[int]]) -> bool:
    return all(v != 0 for row in grid for v in row)


def regions(grid: list[list[int]], values: set[int]) -> list[list[tuple[int, int]]]:
    """The 4-connected runs of ``values`` in ``grid``, as lists of (x, y).

    Used to find window panes, which is why connectivity is 4 and not 8: two
    panes touching only at a corner are two panes, and a mullion one pixel
    thick separates them. The order is deterministic -- scan order of each
    region's first pixel -- because a caller that seeds a per-region roll needs
    the same regions in the same order on every generation.
    """
    height = len(grid)
    width = len(grid[0]) if height else 0
    seen = [[False] * width for _ in range(height)]
    found: list[list[tuple[int, int]]] = []
    for y0 in range(height):
        for x0 in range(width):
            if seen[y0][x0] or grid[y0][x0] not in values:
                continue
            stack = [(x0, y0)]
            seen[y0][x0] = True
            region: list[tuple[int, int]] = []
            while stack:
                x, y = stack.pop()
                region.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if (0 <= nx < width and 0 <= ny < height
                            and not seen[ny][nx] and grid[ny][nx] in values):
                        seen[ny][nx] = True
                        stack.append((nx, ny))
            found.append(sorted(region))
    return found


def module_grids(module) -> dict[str, dict[str, object]]:
    """Resolve every grid an art module declares.

    Returns {"tiles": {name: grid}, "stamps": {name: (w, h, [grids])},
             "stamps_uncut": {name: (w, h, grid)},
             "shadows": {name: grid}, "details": {name: grid},
             "night": {tileset name: grid}}.

    ``stamps_uncut`` is the stamp before it is cut into tiles, kept because a
    pass that has to see a whole OBJECT cannot work on the cut ones: a window
    pane eleven pixels wide straddles a sixteen-pixel tile boundary, and
    lighting it in one tile and not the other puts a seam down its middle.
    """
    lookup = palette_lookup(module.PALETTE)
    ink = module.INK
    tiles = {n: parse(r, ink, lookup, label=n) for n, r in getattr(module, "TILES", {}).items()}
    stamps = {}
    stamps_uncut = {}
    for name, (w, h, rows) in getattr(module, "STAMPS", {}).items():
        grid = parse(rows, ink, lookup, TILE * w, TILE * h, label=name)
        stamps_uncut[name] = (w, h, grid)
        stamps[name] = (w, h, cut(grid, w, h))
    shadows = {n: parse(r, ink, lookup, label=n) for n, r in getattr(module, "SHADOWS", {}).items()}
    details = {n: parse(r, ink, lookup, label=n) for n, r in getattr(module, "DETAILS", {}).items()}
    night = {n: parse(r, ink, lookup, label=n) for n, r in getattr(module, "NIGHT", {}).items()}
    return {"tiles": tiles, "stamps": stamps, "stamps_uncut": stamps_uncut,
            "shadows": shadows, "details": details, "night": night}


def preview(module, path: str, scale: int = 4, background=(255, 0, 255)) -> None:
    """Render a module's tiles and stamps to one PNG sheet, for review."""
    from PIL import Image  # review-only dependency

    rgb = [background] + [(r, g, b) for _n, r, g, b in module.PALETTE]
    grids = module_grids(module)
    items: list[tuple[str, int, int, list[list[list[int]]]]] = []
    for name, grid in grids["tiles"].items():
        items.append((name, 1, 1, [grid]))
    for name, (w, h, tiles) in grids["stamps"].items():
        items.append((name, w, h, tiles))
    for name, grid in grids["shadows"].items():
        items.append((name + "~SH", 1, 1, [grid]))
    for name, grid in grids["details"].items():
        items.append((name + "~DT", 1, 1, [grid]))
    if not items:
        print(f"{module.__name__}: nothing to preview (no tiles, stamps, shadows or details)")
        return
    gap = 2
    row_w = 0
    rows: list[list[tuple]] = [[]]
    for item in items:
        w = item[1] * TILE + gap
        if row_w + w > 40 * TILE and rows[-1]:
            rows.append([])
            row_w = 0
        rows[-1].append(item)
        row_w += w
    height = sum(max(i[2] for i in r) * TILE + gap for r in rows)
    width = max(sum(i[1] * TILE + gap for i in r) for r in rows)
    im = Image.new("RGB", (width, height), background)
    px = im.load()
    y0 = 0
    for r in rows:
        x0 = 0
        for name, w, h, tiles in r:
            for ty in range(h):
                for tx in range(w):
                    grid = tiles[ty * w + tx]
                    for y in range(TILE):
                        for x in range(TILE):
                            v = grid[y][x]
                            if v:
                                px[x0 + tx * TILE + x, y0 + ty * TILE + y] = rgb[v]
            x0 += w * TILE + gap
        y0 += max(i[2] for i in r) * TILE + gap
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    im.save(path)
    print(f"{module.__name__}: {len(grids['tiles'])} tiles, {len(grids['stamps'])} stamps, "
          f"{len(grids['shadows'])} shadows, {len(grids['details'])} details -> {path}")
