#!/usr/bin/env python3
"""Generate the iso_dungeon example's art.

Isometric art is geometry, not draftsmanship: a floor tile is a diamond, and a
wall, an altar and a pillar are the same diamond extruded downward by different
amounts in different colours. Writing that by hand as hex nibbles is both
tedious and unreviewable, so every tile here is produced by two primitives --
``iso_floor`` and ``iso_cube`` -- and only the hero is drawn shape by shape.

    python generate_assets.py                       # locates the demo itself
    python generate_assets.py path/to/iso_dungeon   # or name it explicitly

It rewrites, in place:

- editor/assets/tilesets/dungeon_floors.png and dungeon_walls.png -- the two
  tile sheets the Tilemap Editor project in editor/ paints the rooms with. The
  room tilemap in src/assets/tilemap/ is NOT written here: the editor exports
  it from the project (see the README's "Re-exporting").
- src/assets/DungeonSpritePalette.h -- the sprite palette bank the hero and
  the props draw through.
- src/assets/DungeonProps.h and src/assets/HeroSprites.h -- the sprites.

The PNGs are written with the standard library alone (zlib + struct), so the
tool still needs nothing beyond Python 3.

Two rules the generated art must keep, both learned the hard way:

1. No face may reuse the outline colour index. A cube whose lit face equals its
   outline loses its silhouette and flattens into a hexagon.
2. Adjacent surfaces need contrast against the *backdrop*, not just against
   each other. A single flat floor tone on a same-hue background reads as one
   undifferentiated blob, which is why the floor is a two-tone checkerboard.
"""

import os
import struct
import zlib

# --- Palette -----------------------------------------------------------------
#
# Index order is deliberately the engine's Color enum order, so the 4bpp
# pixel-value -> Color mapping is the identity and SPRITE_PALETTE_DATA[i] can be
# read directly as "the colour of pixel value i". Index 0 is transparent and is
# never emitted into a sprite.
#
# The tile sheets are written as RGBA, not as indices: the Tilemap Editor builds
# the tile layer's own palette from the colours the painted tiles use, so the
# index order above does not survive into the exported tilemap. Only the RGB
# values do, and each one is chosen so the editor's RGB565 conversion lands on
# the same value rgb565() computes here.

PALETTE = [
    ("Black",      (  0,   0,   0), "transparent - never drawn"),
    ("White",      (  0,   0,   0), "void / backdrop"),
    ("Navy",       (  8,  14,  20), "outline"),
    ("Blue",       ( 24,  92,  96), "floor dark teal"),
    ("Cyan",       ( 44, 132, 132), "floor light teal"),
    ("DarkGreen",  (140, 156,  60), "floor accent (ritual square)"),
    ("Green",      ( 14,  52,  58), "stone mortar / shadow"),
    ("LightGreen", ( 26,  90,  96), "stone face, shaded side"),
    ("Yellow",     ( 40, 124, 130), "stone face, lit side"),
    ("Orange",     ( 86, 178, 178), "stone top"),
    ("LightRed",   (248, 200, 152), "skin"),
    ("Red",        (232, 180,  48), "hair / gold"),
    ("DarkRed",    ( 56, 152,  88), "tunic"),
    ("Purple",     (208, 216, 232), "polished stone / metal"),
    ("Magenta",    (240, 140,  40), "torch flame"),
    ("Gray",       (110,  66,  36), "boots"),
]

TRANSPARENT = 0
VOID        = 1
OUTLINE     = 2
FLOOR_DARK  = 3
FLOOR_LIGHT = 4
FLOOR_ACC   = 5
MORTAR      = 6
FACE_DARK   = 7
FACE_LIGHT  = 8
STONE_TOP   = 9
SKIN        = 10
HAIR        = 11
TUNIC       = 12
METAL       = 13
FLAME       = 14
BOOT        = 15


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


# --- Grid primitives ---------------------------------------------------------

def blank(w, h):
    return [[TRANSPARENT] * w for _ in range(h)]


def put(grid, x, y, value):
    if 0 <= y < len(grid) and 0 <= x < len(grid[0]):
        grid[y][x] = value


def diamond_spans(w, h):
    """Column span [c0, c1] of an isometric diamond, per row.

    Rows widen by 2*(w//h) pixels per step, so a 32x16 diamond runs 4, 8, ...,
    32, 32, ..., 8, 4 -- the classic 2:1 tile with two full-width middle rows.
    """
    step = w // h
    spans = []
    for r in range(h):
        t = r if r < h // 2 else (h - 1 - r)
        span_w = (2 * t + 2) * step
        c0 = (w - span_w) // 2
        spans.append((c0, c0 + span_w - 1))
    return spans


def outline_pass(grid, colour=OUTLINE):
    """Surround the silhouette with a 1px border, in place.

    Computed from the finished shape rather than drawn by hand so a cube and a
    pillar cannot disagree about where their edges are.
    """
    h = len(grid)
    w = len(grid[0])
    edges = []
    for y in range(h):
        for x in range(w):
            if grid[y][x] != TRANSPARENT:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < w and 0 <= ny < h and grid[ny][nx] not in (TRANSPARENT, colour):
                    edges.append((x, y))
                    break
    for x, y in edges:
        grid[y][x] = colour


def iso_floor(fill, edge, w=32, h=16):
    """A flat diamond tile: solid fill with a 1px rim.

    The rim is what makes the grid legible -- without it a checkerboard of two
    close tones still reads as a gradient rather than as discrete cells.
    """
    grid = blank(w, h)
    spans = diamond_spans(w, h)
    for r, (c0, c1) in enumerate(spans):
        for x in range(c0, c1 + 1):
            grid[r][x] = fill
    for r, (c0, c1) in enumerate(spans):
        for x in (c0, c0 + 1, c1 - 1, c1):
            put(grid, x, r, edge)
    for x in range(spans[0][0], spans[0][1] + 1):
        grid[0][x] = edge
    for x in range(spans[h - 1][0], spans[h - 1][1] + 1):
        grid[h - 1][x] = edge
    return grid


def iso_cube(height, top, left, right, mortar=None, dia_w=32, dia_h=16,
             arch=None, brick=7):
    """A diamond extruded downward by `height` pixels.

    The canvas is dia_h + height tall. The TOP diamond sits at rows 0..dia_h-1;
    the BASE diamond -- the part that must align with the floor tile underneath
    -- is the same diamond translated down by `height`, so it occupies the last
    dia_h rows. `foot_y` (returned by the caller via FOOT_Y below) is therefore
    the base diamond's centre row.

    `left` is the face pointing down-left and `right` the face pointing
    down-right. Making `right` the lighter of the two puts the light source at
    the upper right, which is why one sprite serves both back walls: the wall
    along +x shows its left face to the room and the wall along +y shows its
    right face, and each is lit correctly without a second bitmap.

    `arch` is None, 'left' or 'right' and punches a doorway into that face.
    """
    w = dia_w
    h = dia_h + height
    grid = blank(w, h)
    spans = diamond_spans(dia_w, dia_h)

    for r, (c0, c1) in enumerate(spans):
        for x in range(c0, c1 + 1):
            grid[r][x] = top

    # Lowest row of the top diamond per column: the face starts one row below.
    face_top = [None] * w
    for r, (c0, c1) in enumerate(spans):
        for x in range(c0, c1 + 1):
            face_top[x] = r

    half = w // 2
    for x in range(w):
        if face_top[x] is None:
            continue
        colour = left if x < half else right
        for r in range(face_top[x] + 1, face_top[x] + 1 + height):
            put(grid, x, r, colour)

    if mortar is not None:
        # Horizontal courses only. Each one follows face_top, so it runs
        # parallel to the tile's top edge and reads as a mortar line in
        # perspective. Vertical joints were tried and removed: because
        # face_top steps every second column, a joint drawn straight down
        # crosses several courses at a slight angle and the whole face turns
        # into diagonal noise at this scale.
        for x in range(w):
            if face_top[x] is None:
                continue
            for k in range(1, height // brick + 1):
                put(grid, x, face_top[x] + k * brick, mortar)

    # The front corner: the vertical edge below the diamond's bottom vertex,
    # where the two faces meet. Without it the two faces merge into one plane.
    for r in range(dia_h - 1, dia_h - 1 + height):
        put(grid, half - 1, r, mortar if mortar is not None else left)
        put(grid, half, r, mortar if mortar is not None else left)

    if arch is not None:
        _punch_arch(grid, face_top, height, dia_h, arch)

    outline_pass(grid)
    return grid


def _punch_arch(grid, face_top, height, dia_h, side):
    """Carve a doorway into one face of a cube, following the face's slope."""
    w = len(grid[0])
    half = w // 2
    cols = range(4, half - 2) if side == "left" else range(half + 2, w - 4)
    cols = list(cols)
    if not cols:
        return
    centre = (cols[0] + cols[-1]) / 2.0
    span = (cols[-1] - cols[0]) / 2.0 + 0.5
    for x in cols:
        if face_top[x] is None:
            continue
        # Rounded top: the arch is shallower toward the jambs.
        rel = abs(x - centre) / span
        crown = int(round(3 * (1.0 - rel * rel)))
        top_r = face_top[x] + 2 + (3 - crown)
        for r in range(top_r, face_top[x] + height + 1):
            put(grid, x, r, OUTLINE)


# --- Hero --------------------------------------------------------------------

def hero(facing, frame):
    """A 16x24 hero, built from blocks rather than drawn pixel by pixel.

    `facing` is 'S' (toward the camera) or 'N' (away). East/west come from
    mirroring at draw time, which is why only two bitmaps per frame exist: the
    four isometric facings are two poses and a horizontal flip.
    """
    w, h = 16, 24
    g = blank(w, h)

    def rect(x0, y0, x1, y1, colour):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                put(g, x, y, colour)

    # Head: hair cap over a skin face.
    rect(4, 2, 11, 9, SKIN)
    rect(4, 2, 11, 4, HAIR)
    rect(3, 3, 3, 6, HAIR)
    rect(12, 3, 12, 6, HAIR)
    if facing == "S":
        put(g, 6, 6, OUTLINE)
        put(g, 9, 6, OUTLINE)
    else:
        # Back of the head: hair all the way down, no face.
        rect(4, 2, 11, 9, HAIR)
        rect(3, 3, 3, 7, HAIR)
        rect(12, 3, 12, 7, HAIR)

    # Torso and belt.
    rect(4, 10, 11, 16, TUNIC)
    rect(4, 16, 11, 16, METAL)

    # Arms swing opposite to the legs, one pixel of travel each way. At 16px
    # wide that single pixel is the whole walk cycle -- more would read as a
    # limp, not a stride.
    swing = 1 if frame else -1
    rect(2, 11 + swing, 3, 14 + swing, TUNIC)
    rect(2, 15 + swing, 3, 16 + swing, SKIN)
    rect(12, 11 - swing, 13, 14 - swing, TUNIC)
    rect(12, 15 - swing, 13, 16 - swing, SKIN)

    # Legs, then boots on top of them so the boot always caps the leg.
    #
    # The two legs are set flush with the torso's edges and left 2px apart
    # rather than butted together: the outline pass then runs down the gap and
    # separates them. Adjacent columns produce one solid block that reads as a
    # skirt, and no amount of walk-frame offset rescues it.
    lead = 1 if frame else 0
    trail = 0 if frame else 1
    rect(4, 17, 6, 21 - lead, TUNIC)
    rect(9, 17, 11, 21 - trail, TUNIC)
    rect(4, 22 - lead, 6, 23 - lead, BOOT)
    rect(9, 22 - trail, 11, 23 - trail, BOOT)

    outline_pass(g)
    return g


# --- Emitters ----------------------------------------------------------------

def pack4(grid):
    """Nibble-pack a grid into uint16 words, 4 pixels per word, LSB first.

    Matches the layout Sprite4bpp expects once reinterpret_cast to uint8_t on a
    little-endian target: byte n holds pixel 2n in its low nibble.
    """
    words = []
    for row in grid:
        padded = list(row)
        while len(padded) % 4:
            padded.append(TRANSPARENT)
        for i in range(0, len(padded), 4):
            p0, p1, p2, p3 = padded[i:i + 4]
            words.append(p0 | (p1 << 4) | (p2 << 8) | (p3 << 12))
    return words


def fmt_array(name, grid):
    words = pack4(grid)
    lines = [f"    static const uint16_t {name}_4BPP[] = {{"]
    for i in range(0, len(words), 8):
        chunk = ", ".join(f"0x{v:04X}" for v in words[i:i + 8])
        lines.append(f"        {chunk},")
    lines.append("    };")
    return "\n".join(lines)


def fmt_descriptor(name, grid, foot_y):
    h = len(grid)
    w = len(grid[0])
    return (
        f"    static const uint8_t {name}_WIDTH  = {w};\n"
        f"    static const uint8_t {name}_HEIGHT = {h};\n"
        f"    /// Sprite row that lands on the cell's diamond centre.\n"
        f"    static const int {name}_FOOT_Y = {foot_y};\n"
        f"    static const pixelroot32::graphics::Sprite4bpp {name}_SPRITE = {{\n"
        f"        reinterpret_cast<const uint8_t*>({name}_4BPP), SPRITE_PALETTE_MAPPING,\n"
        f"        {name}_WIDTH, {name}_HEIGHT, 16\n"
        f"    }};"
    )


def png_rgba(path, grid):
    """Write `grid` (palette indices) as an 8-bit RGBA PNG.

    Index 0 becomes fully transparent; every other index its PALETTE colour,
    opaque. No interlace, filter 0 on every row and a fixed zlib level, so the
    same art always produces the same bytes and a re-run leaves git clean.
    """
    h = len(grid)
    w = len(grid[0])
    raw = bytearray()
    for row in grid:
        raw.append(0)  # filter type 0 (None)
        for v in row:
            if v == TRANSPARENT:
                raw += b"\x00\x00\x00\x00"
            else:
                r, g, b = PALETTE[v][1]
                raw += bytes((r, g, b, 255))

    def chunk(kind, data):
        body = kind + data
        return (struct.pack(">I", len(data)) + body +
                struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def side_by_side(grids):
    """One sheet row of equally sized tiles, left to right."""
    h = len(grids[0])
    return [sum((g[y] for g in grids), []) for y in range(h)]


def locate_demo():
    """Return the demo root: the parent of this tools/ directory."""
    here = os.path.dirname(os.path.abspath(__file__))
    demo = os.path.dirname(here)
    if os.path.isdir(os.path.join(demo, "src", "assets")):
        return demo
    raise SystemExit(
        demo + " does not look like the iso_dungeon demo (no src/assets under "
        "it). This script expects to live in <demo>/tools/. Pass the demo path "
        "explicitly:\n    python generate_assets.py path/to/iso_dungeon")


def write_sprite_palette(assets):
    rows = []
    for i, (name, rgb, note) in enumerate(PALETTE):
        rows.append(f"        0x{rgb565(rgb):04X}, // {i:2d} {name:<10} - {note}")
    body = "\n".join(rows)

    mapping = "\n".join(
        f"        pixelroot32::graphics::Color::{name},"
        f"{' ' * max(1, 12 - len(name))}// {i:2d} {note}"
        for i, (name, _rgb, note) in enumerate(PALETTE)
    )

    text = f'''// GENERATED by tools/generate_assets.py - do not edit by hand.
#ifndef ISO_DUNGEON_SPRITE_PALETTE_H
#define ISO_DUNGEON_SPRITE_PALETTE_H

#include "graphics/Renderer.h"
#include <stdint.h>

/**
 * @file DungeonSpritePalette.h
 * @brief The 16-colour RGB565 palette the hero and the props draw through.
 *
 * Installed on the SPRITE bank (IsoDungeonScene::init). The room tilemap does
 * not use it: the Tilemap Editor export carries its own palette, built from the
 * colours the tile sheets paint with, and its init() installs that one on the
 * BACKGROUND bank. drawSprite() resolves through getSpritePaletteSlot() and
 * drawTileMap() through getBackgroundPaletteSlot(), which are separate arrays,
 * so the two palettes never meet -- the stone colours below are the same RGB
 * values the tile sheets use, which is all the props need to match the walls.
 *
 * CAUTION: under a custom palette a graphics::Color is a palette INDEX and its
 * name says nothing about what it renders as. Color::White is index 1, which
 * this palette defines as pure black (the void around the room). Reach for the
 * kVoidColor constant in IsoDungeonConstants.h rather than naming a Color
 * directly, or you will paint the backdrop teal.
 *
 * The pixel-value -> Color mapping is deliberately the identity, so
 * SPRITE_PALETTE_DATA[v] IS the colour of 4bpp pixel value v. Value 0 maps
 * to Color::Black, which the renderer treats as transparent.
 */

namespace iso_dungeon {{

    // --- RGB565 by engine Color slot ---
    static const uint16_t SPRITE_PALETTE_DATA[16] = {{
{body}
    }};

    // --- 4bpp pixel value to engine Color slot (identity) ---
    static const pixelroot32::graphics::Color SPRITE_PALETTE_MAPPING[16] = {{
{mapping}
    }};

}} // namespace iso_dungeon

#endif // ISO_DUNGEON_SPRITE_PALETTE_H
'''
    with open(os.path.join(assets, "DungeonSpritePalette.h"), "w", newline="\n") as f:
        f.write(text)


def write_tilesheets(tilesets):
    """The two sheets the editor project paints the rooms with.

    One sheet per tile height, because a sheet has one tile size and the
    editor derives each sheet's foot row from it (tileHeight - cellHeight / 2):
    8 for the 32x16 floors, 32 for the 32x40 walls -- the rows the room
    tilemap's TILESET_FOOT_Y ends up holding. Sheet order and tile order are
    what the editor's tile indices refer to, so changing either means
    repainting the scene.
    """
    floors = [iso_floor(FLOOR_DARK, MORTAR),     # FLOOR_A
              iso_floor(FLOOR_LIGHT, MORTAR),    # FLOOR_B
              iso_floor(FLOOR_ACC, MORTAR)]      # FLOOR_ACCENT
    walls = [iso_cube(24, STONE_TOP, FACE_DARK, FACE_LIGHT, MORTAR),                 # WALL
             iso_cube(24, STONE_TOP, FACE_DARK, FACE_LIGHT, MORTAR, arch="left"),    # DOOR_NE
             iso_cube(24, STONE_TOP, FACE_DARK, FACE_LIGHT, MORTAR, arch="right")]   # DOOR_NW
    os.makedirs(tilesets, exist_ok=True)
    png_rgba(os.path.join(tilesets, "dungeon_floors.png"), side_by_side(floors))
    png_rgba(os.path.join(tilesets, "dungeon_walls.png"), side_by_side(walls))


def write_props(assets):
    altar = iso_cube(14, METAL, FACE_LIGHT, STONE_TOP, FACE_DARK, brick=5)

    # Narrower base diamond than a wall (16x8, half a tile) so the column reads
    # as standing ON its tile rather than filling it, and short enough that two
    # of them cannot span the room vertically and merge into one apparent shaft.
    pillar = iso_cube(26, STONE_TOP, FACE_DARK, FACE_LIGHT, MORTAR,
                      dia_w=16, dia_h=8, brick=6)
    props = [("ALTAR", altar, len(altar) - 8), ("PILLAR", pillar, len(pillar) - 4)]

    arrays = "\n\n".join(fmt_array(n, g) for n, g, _ in props)
    descs = "\n\n".join(fmt_descriptor(n, g, f) for n, g, f in props)

    text = f'''// GENERATED by tools/generate_assets.py - do not edit by hand.
#ifndef ISO_DUNGEON_PROPS_H
#define ISO_DUNGEON_PROPS_H

#include "platforms/PlatformDefaults.h"

#if PIXELROOT32_ENABLE_4BPP_SPRITES

#include "graphics/Renderer.h"
#include "assets/DungeonSpritePalette.h"
#include <stdint.h>

/**
 * @file DungeonProps.h
 * @brief Free-standing props: the altar and the pillar.
 *
 * These are NOT tiles, and that distinction is the reason this file exists
 * apart from the tile sheets. A tile belongs to the tile layer: it occupies a
 * cell, it is addressed by an index into a tileset, and the Tilemap Editor
 * emits it. A prop is placed at a cell but drawn as an ordinary depth-sorted
 * sprite on layer 1 by PropEntity, because the hero passes both behind and in
 * front of it -- see RoomCatalog.h's kProps* tables for where they stand.
 *
 * Keeping them in the tileset made the tileset look eight tiles wide when the
 * tile layer only ever had six, which is exactly the confusion the exported
 * tileset must not carry.
 *
 * Anchoring is unchanged and identical to the tiles': each sprite carries a
 * FOOT_Y, the bitmap row that must land on the target cell's diamond CENTRE.
 *
 *     renderer.drawSprite(S, centreX - S.width / 2, centreY - S_FOOT_Y);
 *
 * Both props are the same floor diamond extruded upward: the altar 32x16 by
 * 14 px (ALTAR_HEIGHT 30), the pillar a narrower 16x8 by 26 (PILLAR_HEIGHT
 * 34). That shared geometry is why they stand against the walls of the tile
 * sheets (editor/assets/tilesets/) without a seam.
 *
 * The extrusion figures are height minus the diamond's own height, and they
 * are stated here rather than left implicit because the tileset header this
 * art came from had the pillar's wrong -- it said 40, which is the WALL's
 * total height, not the pillar's extrusion.
 */

namespace iso_dungeon {{

{arrays}

{descs}
}} // namespace iso_dungeon

#endif // PIXELROOT32_ENABLE_4BPP_SPRITES

#endif // ISO_DUNGEON_PROPS_H
'''
    with open(os.path.join(assets, "DungeonProps.h"), "w", newline="\n") as f:
        f.write(text)


def write_hero(assets):
    frames = [
        ("HERO_S_0", hero("S", 0)),
        ("HERO_S_1", hero("S", 1)),
        ("HERO_N_0", hero("N", 0)),
        ("HERO_N_1", hero("N", 1)),
    ]
    arrays = "\n\n".join(fmt_array(n, g) for n, g in frames)
    entries = "\n".join(
        f"        {{ reinterpret_cast<const uint8_t*>({n}_4BPP), SPRITE_PALETTE_MAPPING,"
        f" HERO_WIDTH, HERO_HEIGHT, 16 }},"
        for n, _ in frames
    )

    text = f'''// GENERATED by tools/generate_assets.py - do not edit by hand.
#ifndef ISO_DUNGEON_HERO_SPRITES_H
#define ISO_DUNGEON_HERO_SPRITES_H

#include "platforms/PlatformDefaults.h"

#if PIXELROOT32_ENABLE_4BPP_SPRITES

#include "graphics/Renderer.h"
#include "assets/DungeonSpritePalette.h"
#include <stdint.h>

/**
 * @file HeroSprites.h
 * @brief Two poses, two walk frames; the other two facings are mirrors.
 *
 * The four isometric facings are not four drawings. Screen-wise the grid axes
 * point down-right, down-left, up-right and up-left, which is two poses (toward
 * the camera and away from it) plus a horizontal flip. Halving the bitmap count
 * this way is the same trade legend_of_clone makes for its side-on walk frames.
 *
 * HERO_FOOT_Y is the sprite's full height: the hero's feet sit ON the cell
 * centre, unlike a block whose base diamond covers the whole tile footprint.
 */

namespace iso_dungeon {{

    static const uint8_t HERO_WIDTH  = 16;
    static const uint8_t HERO_HEIGHT = 24;
    /// Feet on the cell centre - see the file comment.
    static const int HERO_FOOT_Y = 24;

{arrays}

    static const pixelroot32::graphics::Sprite4bpp HERO_FRAMES[] = {{
{entries}
    }};

    enum HeroFrame : uint8_t {{
        HERO_TOWARD_0 = 0,  ///< Facing the camera, walk frame 0.
        HERO_TOWARD_1 = 1,  ///< Facing the camera, walk frame 1.
        HERO_AWAY_0   = 2,  ///< Facing away, walk frame 0.
        HERO_AWAY_1   = 3,  ///< Facing away, walk frame 1.
    }};

}} // namespace iso_dungeon

#endif // PIXELROOT32_ENABLE_4BPP_SPRITES

#endif // ISO_DUNGEON_HERO_SPRITES_H
'''
    with open(os.path.join(assets, "HeroSprites.h"), "w", newline="\n") as f:
        f.write(text)


if __name__ == "__main__":
    import sys

    DEMO = sys.argv[1] if len(sys.argv) > 1 else locate_demo()
    ASSETS = os.path.join(DEMO, "src", "assets")
    TILESETS = os.path.join(DEMO, "editor", "assets", "tilesets")
    write_tilesheets(TILESETS)
    write_sprite_palette(ASSETS)
    write_props(ASSETS)
    write_hero(ASSETS)
    print("wrote dungeon_floors.png, dungeon_walls.png to " + TILESETS)
    print("wrote DungeonSpritePalette.h, DungeonProps.h, HeroSprites.h to " + ASSETS)
