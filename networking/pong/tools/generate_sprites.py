#!/usr/bin/env python3
"""Generate the 4bpp sprite and palette headers for the networking Pong demo.

The art lives in ``tools/art_source.py`` as character grids; this script packs
it and writes ``src/assets/PongPalette.h`` and ``src/assets/PongSprites.h``.
Standard library only - no Pillow, no PlatformIO - so the headers can be
regenerated anywhere Python runs.

Usage (from ``networking/pong``)::

    python tools/generate_sprites.py                       # write the headers
    python tools/generate_sprites.py --check               # verify, write nothing
    python tools/generate_sprites.py --preview             # 6x PNG contact sheet
    python tools/generate_sprites.py --preview out.png     # ... somewhere else

Packing follows ``Sprite4bpp`` (``include/graphics/Renderer.h``)::

    row stride      = ceil(width / 2) bytes
    byte low nibble = LEFT pixel, high nibble = right pixel
    nibble value 0  = transparent, never written to the framebuffer

The two things that decide whether this art survives the ESP32 are handled here
rather than in the game code:

* A nibble is not a palette slot. The sprite carries a ``const Color*`` table
  that maps its nibbles to engine colour enumerators, and the installed
  ``kPongPalette`` maps those enumerators to RGB565. ``PongPalette.h`` assigns
  all 16 slots, and slot 0 must not be 0x0000: ``Color::Black`` is index 0 and
  that value packs to 0 in the 8bpp framebuffer, which would make every
  ``Color::Black`` primitive invisible.
* ``Sprite4bpp`` has no draw-time scaling, so every sprite is emitted at the
  exact rectangle the court draws it into. ``CourtRenderer.h`` asserts those
  sizes against the rules geometry, so art and collision cannot drift apart.
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

# Contact-sheet magnification. 6x makes the 6 px ball reviewable and keeps the
# 44 px paddles from turning the sheet into one tall column.
PREVIEW_SCALE = 6

# Where `--preview` writes when it is given no path. Excluded by .gitignore.
DEFAULT_PREVIEW = os.path.join(ROOT, "tools", "sprite_preview.png")


# --------------------------------------------------------------------------- art


def parse_grid(sprite):
    """Validate one character grid and return it as nibble values.

    Refuses a grid that is not exactly the size the sprite declares or that
    uses a character outside its ink set: a typo in the art has to be a failed
    run, not a header full of zeroes.
    """
    name = sprite["name"]
    rows = sprite["rows"]
    expect_w, expect_h = sprite["size"]
    height = len(rows)
    width = len(rows[0]) if rows else 0
    if (width, height) != (expect_w, expect_h):
        raise SystemExit(
            f"{name}: expected {expect_w}x{expect_h}, got {width}x{height}"
        )

    inks = sprite["inks"]
    grid = []
    for y, row in enumerate(rows):
        if len(row) != width:
            raise SystemExit(f"{name}: row {y} is {len(row)} wide, expected {width}")
        line = []
        for ch in row:
            if ch == art.TRANSPARENT:
                line.append(0)
            elif ch in inks:
                line.append(inks[ch])
            else:
                raise SystemExit(
                    f"{name}: unknown ink character {ch!r} at row {y}, col {len(line)}"
                )
        grid.append(line)
    return grid


def check_palette_tables(sprite):
    """Refuses a nibble table that is not a full 4bpp palette.

    The blitter reads up to ``paletteSize`` entries; a nibble used by the art
    but missing from the table would draw paletteLUT[0] (black) and the shape
    would lose its interior instead of failing the run.
    """
    colors = sprite["colors"]
    highest = max(sprite["inks"].values(), default=0)
    if len(colors) != highest + 1:
        raise SystemExit(
            f"{sprite['name']}: {len(colors)} colour entries for nibbles 0..{highest}"
        )
    for index, (name, _comment) in enumerate(colors):
        if name not in art.COLOR_INDEX:
            raise SystemExit(f"{sprite['name']}: unknown engine colour {name!r}")
        if index == 0 and name != "Black":
            raise SystemExit(
                f"{sprite['name']}: nibble 0 is the transparent sentinel and must "
                "stay Color::Black"
            )


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


# ---------------------------------------------------------------------- palette


def rgb565(rgb):
    """Truncating RGB565 conversion, matching the engine's own tables."""
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def palette_slots():
    """The 16 slots with one comment each, in slot order."""
    if len(art.PALETTE) != 16:
        raise SystemExit("art.PALETTE must hold exactly 16 entries")
    return art.PALETTE


# ---------------------------------------------------------------------- headers

BANNER = """// Generated by tools/generate_sprites.py - do not edit by hand.
// Source: tools/art_source.py; change the art there, then re-run the generator.
// Regenerate: python tools/generate_sprites.py
// Verify:     python tools/generate_sprites.py --check"""


def render_palette_header():
    slots = palette_slots()
    lines = [
        BANNER,
        "",
        "#pragma once",
        "",
        "#include <graphics/Color.h>",
        "",
        "#include <cstdint>",
        "",
        "namespace netpong::assets {",
        "",
        "using pixelroot32::graphics::Color;",
        "",
        "/**",
        " * @file PongPalette.h",
        " * @brief The 16-slot RGB565 arcade palette and the nibble tables that bind it.",
        " *",
        " * Every slot is assigned, in the order the court paints: slot 0 is the court",
        " * ink, then the inks the actors and the score band draw with, then the HUD's",
        " * spare shades. The renderer resolves primitives and sprites through this one",
        " * table, so a hole would not be a harmless gap - it would turn a named Color",
        " * into whatever the tail of the table held.",
        " *",
        " * Slot 0 is the trap this file exists to disarm. `Color::Black` is index 0 and",
        " * the engine's own palette gives it RGB565 0x0000 - the value the ESP32 8bpp",
        " * framebuffer reads as transparent. A 0x0000 in slot 0 makes every",
        " * `Color::Black` primitive silently disappear, so the court ink is a dark blue",
        " * that packs to a non-zero word instead. The static_assert below is the",
        " * tripwire for that regression.",
        " *",
        " * The engine stores the pointer it is handed and never copies it, so this",
        " * array has static storage duration: it outlives every frame drawn with it.",
        " */",
        "inline constexpr uint16_t kPongPalette[16] = {",
    ]
    for index, (rgb, comment) in enumerate(slots):
        red, green, blue = rgb
        lines.append(
            f"    0x{rgb565(rgb):04X},"
            f"  // {index:>2}  #{red:02X}{green:02X}{blue:02X}  {comment}"
        )
    lines += [
        "};",
        "",
        "static_assert(kPongPalette[0] != 0,",
        '              "PongPalette: slot 0 is Color::Black; 0x0000 would erase every "',
        '              "Color::Black primitive on ESP32");',
        "",
        "/**",
        " * @file PongPalette.h",
        " * @brief Nibble value to Color: the tables the generated sprites carry.",
        " *",
        " * Index 0 of every table is the transparent sentinel. The blitter skips",
        " * nibble 0 before it resolves a colour, so the entry is never drawn - it",
        " * exists only so `paletteSize` and the array agree.",
        " *",
        " * The sprite inks are the palette's own inks, named here by the engine",
        " * enumerator each one rides, so the court, the HUD and the art all resolve",
        " * through the same slot table.",
        " */",
    ]
    for sprite in art.SPRITES:
        name = sprite["palette"]
        entries = sprite["colors"]
        lines += [
            f"inline constexpr Color {name}[{len(entries)}] = {{",
        ]
        for index, (color, comment) in enumerate(entries):
            lines.append(f"    Color::{color},{' ' * max(1, 12 - len(color))}// {comment}")
        lines.append("};")
    lines += [
        "",
        "}  // namespace netpong::assets",
        "",
    ]
    return "\n".join(lines)


def render_sprite_block(sprite):
    """One data array plus its constexpr descriptor, for a single sprite."""
    name = sprite["name"]
    grid = parse_grid(sprite)
    height, width = len(grid), len(grid[0])
    data = pack_4bpp(grid)
    expected = height * ((width + 1) // 2)
    if len(data) != expected:
        raise SystemExit(f"{name}: packed {len(data)} bytes, expected {expected}")

    doc = sprite["doc"].splitlines()
    doc_lines = ["/**", " * @brief " + doc[0]]
    doc_lines += doc[1:]
    doc_lines.append(" */")

    return "\n".join(
        [f"// {name}: {width}x{height}, {len(data)} bytes",
         f"inline constexpr uint8_t {name}Data[{len(data)}] = {{",
         hex_rows(data),
         "};"]
        + doc_lines
        + [f"inline constexpr Sprite4bpp {name} = "
           f"{{{name}Data, {sprite['palette']}, {width}, {height}, "
           f"{len(sprite['colors'])}}};"]
    )


SPRITES_HEADER = """#pragma once

#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

#include "assets/PongPalette.h"

namespace netpong::assets {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Sprite4bpp;

/**
 * @file PongSprites.h
 * @brief The cast of the arcade court, packed at the size the court draws it.
 *
 * Three sprites are all this demo needs: a ball, and one paddle per player.
 * A `Sprite4bpp` cannot be scaled at draw time, so each one is emitted at the
 * exact rectangle `CourtRenderer` clips it into - 6x6 for the ball, 6x44 for a
 * paddle - and the court's static_asserts compare these sizes against the rules
 * geometry so the art can never quietly disagree with the collision.
 *
 * The two paddles are the same bevelled grid in two ink sets. Player two's is
 * drawn mirrored, which is what puts its light leading edge on the side that
 * faces the centre of the court.
 */"""


def render_sprites_header():
    parts = [BANNER, "", SPRITES_HEADER]
    for sprite in art.SPRITES:
        parts.append("")
        parts.append(render_sprite_block(sprite))

    parts.append("")
    parts.append("// --- Contract checks --------------------------------------------------------")
    parts.append("// The renderer asserts these against the rules geometry as well; keeping a")
    parts.append("// copy here means a hand-edited grid fails in the header that owns it.")
    for sprite in art.SPRITES:
        width, height = sprite["size"]
        parts.append(
            f"static_assert({sprite['name']}.width == {width} && "
            f"{sprite['name']}.height == {height},"
        )
        parts.append(
            f'              "PongSprites: {sprite["name"]} must stay {width}x{height}");'
        )
    parts.append("static_assert(kPongBall.paletteSize == 4 && "
                 "kPongPaddle.paletteSize == 5 &&")
    parts.append("                  kPongPaddleP2.paletteSize == 5,")
    parts.append('              "PongSprites: every sprite must declare its full nibble table");')
    parts.append("")
    parts.append("}  // namespace netpong::assets")
    parts.append("")
    return "\n".join(parts)


def rendered_files():
    for sprite in art.SPRITES:
        check_palette_tables(sprite)
    return {
        os.path.join(ASSETS_DIR, "PongPalette.h"): render_palette_header(),
        os.path.join(ASSETS_DIR, "PongSprites.h"): render_sprites_header(),
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


def write_preview(path, scale=PREVIEW_SCALE):
    """Render every sprite at ``scale`` into one contact sheet.

    Transparent pixels are magenta so a hole in the art cannot hide against a
    dark background, and the cell is painted on the court ink first: the point
    of the sheet is to judge the actors against the colour they will ship over.
    The layout is printed to stdout so a viewer can map a block back to its
    symbol without labels burnt into the PNG.
    """
    gutter = 2
    slots = palette_slots()

    def rgb_of(color_name):
        return slots[art.COLOR_INDEX[color_name]][0]

    court = rgb_of("Black")
    background = (0x14, 0x12, 0xA7)
    missing = (0xFF, 0x00, 0xFF)

    sprites = [(s["name"], parse_grid(s), s["colors"]) for s in art.SPRITES]

    spacing = gutter * scale
    row_w = spacing + sum(len(grid[0]) * scale + spacing for _n, grid, _c in sprites)
    row_h = max(len(grid) * scale for _n, grid, _c in sprites)
    width = row_w
    height = row_h + 2 * spacing

    canvas = [[background] * width for _ in range(height)]
    x = spacing
    for name, grid, colors in sprites:
        ink = {index: rgb_of(color) for index, (color, _c) in enumerate(colors)}
        print(f"   x={x:>4}  {name} ({len(grid[0])}x{len(grid)})")
        for gy, grid_row in enumerate(grid):
            for gx, value in enumerate(grid_row):
                colour = court if value == 0 else ink.get(value, missing)
                for dy in range(scale):
                    for dx in range(scale):
                        canvas[spacing + gy * scale + dy][x + gx * scale + dx] = colour
        x += len(grid[0]) * scale + spacing

    directory = os.path.dirname(os.path.abspath(path))
    if directory:
        os.makedirs(directory, exist_ok=True)
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
        nargs="?",
        const=DEFAULT_PREVIEW,
        metavar="PATH",
        help=f"write a {PREVIEW_SCALE}x PNG contact sheet instead of the headers "
             "(default: tools/sprite_preview.png)",
    )
    args = parser.parse_args(argv)

    if args.preview:
        width, height = write_preview(args.preview)
        shown = os.path.relpath(os.path.abspath(args.preview), ROOT)
        print(
            f"wrote {shown} ({width}x{height}, {PREVIEW_SCALE}x, magenta=transparent)"
        )
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
