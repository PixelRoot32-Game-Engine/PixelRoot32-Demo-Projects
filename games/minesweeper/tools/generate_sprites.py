#!/usr/bin/env python3
"""Generate the 4bpp sprite headers for the Minesweeper Game Boy reskin.

The art lives in ``tools/art_source.py`` as character grids; this script packs
it and writes ``src/assets/GraphicsPalette.h``, ``TileSprites.h`` and
``HudSprites.h``. Standard library only - no Pillow, no PlatformIO - so the
headers can be regenerated anywhere Python runs.

Usage (from ``games/minesweeper``)::

    python tools/generate_sprites.py                       # write the headers
    python tools/generate_sprites.py --check               # verify, write nothing
    python tools/generate_sprites.py --preview contact.png # 4x PNG contact sheet

Packing follows ``Sprite4bpp`` (``include/graphics/Renderer.h``)::

    row stride       = ceil(width / 2) bytes
    byte low nibble  = LEFT pixel, high nibble = right pixel
    nibble value 0   = transparent, never written to the framebuffer

The two things that decide whether this art survives the ESP32 are handled
here rather than in the game code:

* ``Color::Black`` resolves to RGB565 ``0x0000``, which packs to 0 in the 8bpp
  framebuffer and disappears. No nibble is bound to ``Color::Black``: the
  darkest ink goes to ``Color::Navy`` instead, and ``GraphicsPalette.h``
  assigns every one of the 16 slots so primitives stay readable.
* ``Sprite4bpp`` has no draw-time scaling, so the board tiles are emitted at
  16, 12 and 8 px and the layout picks one per difficulty.
"""

import argparse
import difflib
import os
import struct
import sys
import zlib

import art_source as art

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS_DIR = os.path.join(ROOT, "src", "assets")

# Contact-sheet magnification. 4x keeps the 8 px glyphs reviewable by eye.
PREVIEW_SCALE = 4

# Background the 8x8 opened cells are composed on for the EXPERT board.
MID_INK = art.INKS["m"]
DARK_INK = art.INKS["D"]

# Extra slot colour: a bright highlight that no sprite uses but the palette
# needs for primitives drawn over the dark band.
HIGHLIGHT_RGB = (0xE8, 0xEF, 0xD0)


# --------------------------------------------------------------------------- art


def parse_grid(name, rows, expect):
    """Validate one character grid and return it as nibble values.

    Refuses a grid that is not exactly ``expect`` pixels or that uses a
    character outside the ink set: a typo in the art has to be a failed run,
    not a header full of zeroes.
    """
    height = len(rows)
    width = len(rows[0]) if rows else 0
    if (width, height) != expect:
        raise SystemExit(f"{name}: expected {expect[0]}x{expect[1]}, got {width}x{height}")
    grid = []
    for y, row in enumerate(rows):
        if len(row) != width:
            raise SystemExit(f"{name}: row {y} is {len(row)} wide, expected {width}")
        line = []
        for ch in row:
            if ch == art.TRANSPARENT:
                line.append(0)
            elif ch in art.INKS:
                line.append(art.INKS[ch])
            else:
                raise SystemExit(f"{name}: unknown ink character {ch!r} at row {y}")
        grid.append(line)
    return grid


def resample(grid, size):
    """Nearest-neighbour downscale with endpoint alignment.

    ``src = round(dst * (srcSize - 1) / (dstSize - 1))`` maps the last source
    pixel onto the last destination pixel, so a 16 px tile keeps its exact
    corner pixels at 12 and 8 px instead of losing the outer edge to a simple
    block average. Integer arithmetic keeps the result byte-identical across
    platforms (no banker's rounding differences).
    """
    src_h, src_w = len(grid), len(grid[0])
    if size == 1:
        return [[grid[0][0]]]
    if size == src_w and size == src_h:
        return [row[:] for row in grid]

    def pick(dst, src_size):
        return (2 * dst * (src_size - 1) + (size - 1)) // (2 * (size - 1))

    return [
        [grid[pick(y, src_h)][pick(x, src_w)] for x in range(size)]
        for y in range(size)
    ]


def opened_8_base():
    """The 8x8 opened cell for the EXPERT board.

    It is a plain mid fill on purpose. The 8x8 digits are drawn to fill
    a whole cell, so a resampled 1 px dark seam would eat their first row and
    column - a 5 would lose its top bar outright. Covered tiles keep their
    raised edge at every size, so an opened cell still reads as opened.
    """
    return [[MID_INK] * art.GLYPH_SIZE for _ in range(art.GLYPH_SIZE)]


def compose_number(base, glyph):
    """Paint an 8x8 glyph on an opened cell, recoloured to dark ink.

    Dark ink on mid ink keeps the digit readable at 8 px without drawing the
    glyph in black, which would out-shout the cell it sits on.
    """
    out = [row[:] for row in base]
    for y in range(len(glyph)):
        for x in range(len(glyph[0])):
            if glyph[y][x]:
                out[y][x] = DARK_INK
    return out


def build_board_variants():
    """Every board tile at every emitted size, in header order."""
    variants = []
    for key, rows in art.BOARD_TILES.items():
        source = parse_grid(key, rows, (16, 16))
        for size in art.BOARD_TILE_SIZES:
            if size == 16:
                grid = [row[:] for row in source]
            elif size == 12:
                grid = resample(source, 12)
            elif key.startswith("OPEN_"):
                # EXPERT keeps its digits legible by composing the 8x8
                # glyphs; a downsample of the 16 px digit tile would be mush.
                number = int(key[len("OPEN_"):])
                grid = opened_8_base()
                if number:
                    digit = f"DIGIT_{number}"
                    grid = compose_number(
                        grid, parse_grid(digit, art.DIGITS[digit], (8, 8))
                    )
            else:
                grid = resample(source, 8)
            variants.append((f"{key}_{size}", size, grid))
    return variants


def inset_cursor_frame(source, inset, size):
    """Move a dash frame onto the inner rectangle of a cell.

    The authored frame is drawn flush with the cell border, which is exactly
    where a covered tile's light top/left bevel lives: the two inks are the
    same, so those two sides of the selector disappear. The design notes call
    shows what it is meant to look like -- the dashes sit a couple of pixels
    inside the cell, on the mid body, clear of the bevel.

    At 16 px the 2-on/2-off rhythm survives at the smaller perimeter
    and is reproduced exactly (corner phases come from the frame). At 12 and
    8 px the perimeter only holds two or three dashes per side, which reads as
    scattered pixels rather than a frame, so those sizes use a solid 1 px
    outline instead. Both phases are identical there, which costs the blink on
    the two small boards and buys a selector you can actually find.
    """
    inner = size - 2 * inset
    out = [[0] * size for _ in range(size)]
    dashed = size >= 16

    phase_top = 0 if source[0][0] else 2
    phase_bottom = 0 if source[-1][0] else 2
    phase_left = 0 if source[0][0] else 2
    phase_right = 0 if source[0][-1] else 2

    for i in range(inner):
        top = not dashed or (i + phase_top) % 4 < 2
        bottom = not dashed or (i + phase_bottom) % 4 < 2
        left = not dashed or (i + phase_left) % 4 < 2
        right = not dashed or (i + phase_right) % 4 < 2
        if top:
            out[inset][inset + i] = art.INKS["L"]
        if bottom:
            out[size - 1 - inset][inset + i] = art.INKS["L"]
        if left:
            out[inset + i][inset] = art.INKS["L"]
        if right:
            out[inset + i][size - 1 - inset] = art.INKS["L"]
    return out


def build_cursor_variants():
    """Selection frames at every emitted size, inset off the cell bevel.

    Inset is one covered-tile bevel: 2 px at 16 and 12 px, 1 px at 8 px, which
    is where the design notes draw the dashes. Keeping the frame inside
    the cell also means it never bleeds onto a neighbouring tile.
    """
    variants = []
    for key, rows in art.CURSOR_FRAMES.items():
        source = parse_grid(key, rows, (16, 16))
        for size in art.BOARD_TILE_SIZES:
            inset = 1 if size <= 8 else 2
            variants.append((f"{key}_{size}", size, inset_cursor_frame(source, inset, size)))
    return variants


def build_hud_variants():
    variants = []
    for key, rows in art.SMILEYS.items():
        variants.append((f"{key}_16", 16, parse_grid(key, rows, (16, 16))))
    for key, rows in art.DIGITS.items():
        variants.append((f"{key}_8", 8, parse_grid(key, rows, (8, 8))))
    for key, rows in art.LETTERS.items():
        variants.append((f"{key}_8", 8, parse_grid(key, rows, (8, 8))))
    for key, rows in art.PANEL_PIECES.items():
        variants.append((f"{key}_8", 8, parse_grid(key, rows, (8, 8))))
    return variants


# ---------------------------------------------------------------------- packing


def pack_4bpp(grid):
    """Pack a nibble grid into 4bpp bytes, low nibble = left pixel."""
    height, width = len(grid), len(grid[0])
    stride = (width + 1) // 2
    data = bytearray(height * stride)
    for y in range(height):
        base = y * stride
        for x in range(0, width, 2):
            low = grid[y][x] & 0x0F
            high = (grid[y][x + 1] & 0x0F) if x + 1 < width else 0
            data[base + x // 2] = low | (high << 4)
    return bytes(data)


def hex_rows(data, per_line=14):
    lines = []
    for start in range(0, len(data), per_line):
        chunk = data[start:start + per_line]
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    return "\n".join(lines)


def symbol_stem(name):
    """COVERED_FLAG_12 -> coveredFlag12, for the data array identifier."""
    parts = name.lower().split("_")
    return parts[0] + "".join(p.capitalize() for p in parts[1:])


# ---------------------------------------------------------------------- palette


def rgb565(rgb):
    """Truncating RGB565 conversion, matching the engine's own tables."""
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def palette_slots():
    """The 16 slots with one comment each, in slot order.

    Slot 0 and 2 (``Color::Black`` / ``Color::Navy``) both carry the black ink:
    nothing is bound to ``Color::Black`` in a sprite palette, but primitives
    and ``Color::Navy``-bound ink must still draw a real dark pixel instead of
    the framebuffer's transparent 0.
    """
    light, mid, dark, black = (
        art.INK_RGB["L"],
        art.INK_RGB["m"],
        art.INK_RGB["D"],
        art.INK_RGB["K"],
    )
    return [
        (black, "#1F1F1F black ink; Color::Black index, kept off every sprite"),
        (light, "#C6D1A3 light ink; Color::White index"),
        (black, "#1F1F1F black ink; Color::Navy index, the darkest drawn ink"),
        (dark, "#4D533C dark ink; shadow partner"),
        ((0xA8, 0xB1, 0x87), "#A8B187 soft green; spare primitive shade"),
        (dark, "#4D533C dark ink; Color::DarkGreen index"),
        (mid, "#8D976D mid ink; Color::Green index"),
        ((0xB4, 0xBD, 0x93), "#B4BD93 light green; spare primitive shade"),
        (HIGHLIGHT_RGB, "#E8EFD0 bright highlight for text over the dark band"),
        ((0x5F, 0x66, 0x47), "#5F6647 deep green; pressed/dim state"),
        ((0xD5, 0xDD, 0xB5), "#D5DDB5 pale green; secondary highlight"),
        ((0x2A, 0x2D, 0x22), "#2A2D22 near-black green; deepest shadow"),
        ((0x3A, 0x3F, 0x2D), "#3A3F2D very dark green; inset edge"),
        ((0x6E, 0x76, 0x52), "#6E7652 muted green; disabled state"),
        ((0x9D, 0xA7, 0x7B), "#9DA77B mid-light green; spare shade"),
        (mid, "#8D976D mid ink; Color::Gray index"),
    ]


# ---------------------------------------------------------------------- headers

BANNER = """// Generated by tools/generate_sprites.py - do not edit by hand.
// Source: tools/art_source.py; change the art there, then re-run the generator.
// Regenerate: python tools/generate_sprites.py
// Verify:     python tools/generate_sprites.py --check"""


def render_palette_header():
    slots = palette_slots()
    if len(slots) != 16:
        raise SystemExit("palette_slots() must return exactly 16 entries")
    lines = [
        BANNER,
        "",
        "#pragma once",
        "",
        "#include <graphics/Color.h>",
        "",
        "#include <cstdint>",
        "",
        "namespace minesweeper {",
        "",
        "using pixelroot32::graphics::Color;",
        "",
        "/**",
        " * @file GraphicsPalette.h",
        " * @brief Custom 16-slot RGB565 palette for the Game Boy Minesweeper reskin.",
        " *",
        " * Every slot is assigned. The renderer resolves a Color through this table",
        " * for primitives as well as for sprites, so a hole would turn an existing",
        " * drawText() or drawFilledRectangle() into whatever the tail of the table",
        " * held. The four sprite inks are duplicated into the slots that",
        " * Color::Navy / DarkGreen / Gray / White resolve to, which is what lets",
        " * kTilePalette below name them by engine enum instead of by raw index.",
        " *",
        " * The engine stores the pointer and never copies it, so this array has",
        " * static storage duration: it must outlive every frame drawn with it.",
        " */",
        "static const uint16_t kMinesweeperPalette[16] = {",
    ]
    for index, (rgb, comment) in enumerate(slots):
        lines.append(f"    0x{rgb565(rgb):04X},  // {index:>2}  {comment}")
    lines += [
        "};",
        "",
        "/**",
        " * @file GraphicsPalette.h",
        " * @brief Nibble value to Color: the palette every generated sprite carries.",
        " *",
        " * Index 0 is the transparent sentinel. The blitter skips nibble 0 before it",
        " * resolves a colour, so its entry is never drawn - it exists only because",
        " * paletteSize is 5 and the array must be well formed.",
        " *",
        " * Index 1 is deliberately Color::Navy. In the engine's own palette",
        " * Color::Black is RGB565 0x0000, and that value packs to 0 in the 8bpp",
        " * framebuffer and disappears; the black ink has to ride a slot that",
        " * survives that pack.",
        " */",
        "static const Color kTilePalette[5] = {",
        "    Color::Black,      // 0 transparent sentinel, never drawn",
        "    Color::Navy,       // 1 black ink   #1F1F1F (palette slot 2)",
        "    Color::DarkGreen,  // 2 dark ink    #4D533C (palette slot 5)",
        "    Color::Gray,       // 3 mid ink     #8D976D (palette slot 15)",
        "    Color::White,      // 4 light ink   #C6D1A3 (palette slot 1)",
        "};",
        "",
        "}  // namespace minesweeper",
        "",
    ]
    return "\n".join(lines)


def render_sprite_block(name, grid):
    """One data array plus its descriptor, for a single emitted sprite."""
    height, width = len(grid), len(grid[0])
    data = pack_4bpp(grid)
    expected = height * ((width + 1) // 2)
    if len(data) != expected:
        raise SystemExit(f"{name}: packed {len(data)} bytes, expected {expected}")
    stem = symbol_stem(name)
    array = f"k{stem[0].upper()}{stem[1:]}Data"
    return "\n".join(
        [
            f"// {name}: {width}x{height}, {len(data)} bytes",
            f"static const uint8_t {array}[{len(data)}] = {{",
            hex_rows(data),
            "};",
            f"/** {name.replace('_', ' ').title()} sprite at {width} px. */",
            f"static const Sprite4bpp {name} = {{"
            f"{array}, kTilePalette, {width}, {height}, 5}};",
        ]
    )


TILE_HEADER = """#pragma once

#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

#include "assets/GraphicsPalette.h"

namespace minesweeper {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Sprite4bpp;

/**
 * @file TileSprites.h
 * @brief Board cells and cursor frames at every difficulty's cell size.
 *
 * A Sprite4bpp cannot be scaled at draw time, so the same art is emitted at
 * 16, 12 and 8 px: EASY uses 16 px, MEDIUM 12 px and EXPERT 8 px. Every sprite
 * resolves against kTilePalette, where nibbles 1..4 are the four inks and
 * nibble 0 is transparent.
 *
 * The 8 px opened cells are composed from the 8x8 digit glyphs instead
 * of a downsample of the 16 px digit tiles - see tools/generate_sprites.py.
 */"""

HUD_HEADER = """#pragma once

#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

#include "assets/GraphicsPalette.h"

namespace minesweeper {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Sprite4bpp;

/**
 * @file HudSprites.h
 * @brief Header band art: smileys, digits, letters and the 9-slice frame.
 *
 * The glyphs are light ink on a transparent background: the header band is
 * drawn as a black rectangle, so baking a black matte into each
 * sprite would only make the sprites unusable over panels.
 *
 * The panel pieces are an 8x8 9-slice. Draw PANEL_TL / PANEL_TR / PANEL_BL /
 * PANEL_BR at the corners, tile PANEL_T / PANEL_B / PANEL_L / PANEL_R along the
 * edges, and fill the interior with the mid-ink colour - there is no centre
 * sprite because it would be a single solid colour.
 */"""


def render_tile_header():
    parts = [BANNER, "", TILE_HEADER]
    board = build_board_variants()
    parts.append("")
    parts.append("// --- Board cells: 16 px (EASY), 12 px (MEDIUM), 8 px (EXPERT) ----------")
    for name, _size, grid in board:
        parts.append("")
        parts.append(render_sprite_block(name, grid))
    parts.append("")
    parts.append("// --- Selection cursor: dashed frame inset onto the cell body ----------")
    parts.append("// The dashes sit two pixels (one at 8 px) inside the cell, clear of the")
    parts.append("// covered tile's light bevel, which uses the same ink as the frame.")
    for name, _size, grid in build_cursor_variants():
        parts.append("")
        parts.append(render_sprite_block(name, grid))
    parts.append("")
    parts.append("}  // namespace minesweeper")
    parts.append("")
    return "\n".join(parts)


def render_hud_header():
    parts = [BANNER, "", HUD_HEADER]
    variants = build_hud_variants()
    smileys = [v for v in variants if v[0].startswith("SMILEY_")]
    digits = [v for v in variants if v[0].startswith("DIGIT_")]
    letters = [v for v in variants if v[0].startswith("LETTER_")]
    panels = [v for v in variants if v[0].startswith("PANEL_")]
    for title, block in (
        ("--- Smileys: 16x16 state faces ----------------------------------------", smileys),
        ("--- Digits: 8x8, light ink on transparent ------------------------------", digits),
        ("--- Letters: 8x8, light ink on transparent -----------------------------", letters),
        ("--- 9-slice panel pieces: 8x8 ------------------------------------------", panels),
    ):
        parts.append("")
        parts.append(f"// {title}")
        for name, _size, grid in block:
            parts.append("")
            parts.append(render_sprite_block(name, grid))
    parts.append("")
    parts.append("}  // namespace minesweeper")
    parts.append("")
    return "\n".join(parts)


def rendered_files():
    return {
        os.path.join(ASSETS_DIR, "GraphicsPalette.h"): render_palette_header(),
        os.path.join(ASSETS_DIR, "TileSprites.h"): render_tile_header(),
        os.path.join(ASSETS_DIR, "HudSprites.h"): render_hud_header(),
    }


# ---------------------------------------------------------------------- preview


def write_png(path, width, height, rows):
    """Minimal RGB PNG writer (colour type 2, 8 bit, no interlace)."""
    raw = b"".join(b"\x00" + bytes(v for px in row for v in px) for row in rows)

    def chunk(tag, payload):
        body = tag + payload
        return (
            struct.pack(">I", len(payload))
            + body
            + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        )

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 9))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(png)


def preview_rows():
    """Sprites to draw in the contact sheet, grouped so sizes are comparable."""
    rows = []
    board = build_board_variants()
    cursors = build_cursor_variants()
    hud = build_hud_variants()
    for size in art.BOARD_TILE_SIZES:
        rows.append([(name, grid) for name, s, grid in board if s == size])
    for size in art.BOARD_TILE_SIZES:
        rows.append([(name, grid) for name, s, grid in cursors if s == size])
    rows.append([(name, grid) for name, _s, grid in hud if name.startswith("SMILEY_")])
    rows.append([(name, grid) for name, _s, grid in hud if name.startswith("DIGIT_")])
    rows.append([(name, grid) for name, _s, grid in hud if name.startswith("LETTER_")])
    rows.append([(name, grid) for name, _s, grid in hud if name.startswith("PANEL_")])
    return rows


def write_preview(path, scale=PREVIEW_SCALE):
    """Render every sprite at ``scale`` into one contact sheet.

    Transparent pixels are magenta so a hole in the art cannot hide against a
    dark background. The layout is printed to stdout so a viewer can map a
    block back to its symbol without labels burnt into the PNG.
    """
    gutter = 2
    groups = preview_rows()
    background = (0x14, 0x12, 0xA7)
    missing = (0xFF, 0x00, 0xFF)
    ink = {
        1: art.INK_RGB["K"],
        2: art.INK_RGB["D"],
        3: art.INK_RGB["m"],
        4: art.INK_RGB["L"],
    }

    spacing = gutter * scale
    # Lay the sheet out first so the canvas is exactly the size it needs: a
    # one-row mismatch here would index past the bottom of the image.
    layout = []
    width = 0
    height = spacing
    for group in groups:
        row_h = max((len(grid) * scale for _name, grid in group), default=0)
        row_w = spacing + sum(
            len(grid[0]) * scale + spacing for _name, grid in group
        )
        width = max(width, row_w)
        layout.append((group, height, row_h))
        height += row_h + spacing
    width += spacing

    canvas = [[background] * width for _ in range(height)]
    for group, y, row_h in layout:
        print(f"-- row {y:>4}px, {row_h:>3}px tall:")
        x = spacing
        for name, grid in group:
            print(f"   x={x:>4}  {name} ({len(grid[0])}x{len(grid)})")
            for gy, grid_row in enumerate(grid):
                for gx, value in enumerate(grid_row):
                    colour = ink.get(value, missing)
                    for dy in range(scale):
                        for dx in range(scale):
                            canvas[y + gy * scale + dy][x + gx * scale + dx] = colour
            x += len(grid[0]) * scale + spacing
        y += row_h + spacing

    write_png(path, width, height, canvas)
    return width, height


# ------------------------------------------------------------------------- main


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--check",
        action="store_true",
        help="re-render in memory and exit non-zero if a header on disk differs",
    )
    parser.add_argument(
        "--preview",
        metavar="PATH",
        help="write a 4x PNG contact sheet instead of the headers",
    )
    args = parser.parse_args(argv)

    if args.preview:
        width, height = write_preview(args.preview)
        print(f"wrote {args.preview} ({width}x{height}, {PREVIEW_SCALE}x, magenta=transparent)")
        return 0

    files = rendered_files()

    if args.check:
        stale = False
        for path, content in files.items():
            relative = os.path.relpath(path, ROOT)
            if not os.path.exists(path):
                print(f"missing: {relative}")
                stale = True
                continue
            with open(path, "r", encoding="utf-8") as handle:
                disk = handle.read()
            if disk == content:
                print(f"ok: {relative}")
                continue
            stale = True
            print(f"stale: {relative} - run `python tools/generate_sprites.py`")
            diff = difflib.unified_diff(
                disk.splitlines(keepends=True),
                content.splitlines(keepends=True),
                fromfile=f"{relative} (on disk)",
                tofile=f"{relative} (generated)",
            )
            sys.stdout.writelines(diff)
        return 1 if stale else 0

    for path, content in files.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        # Platform-default newlines on purpose: this repo checks out with
        # core.autocrlf=true, so a header written with hard-coded LF would look
        # stale to --check the moment a fresh Windows checkout re-created it.
        # --check reads with universal newlines, so it compares text, not bytes.
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)
        print(f"wrote {os.path.relpath(path, ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
