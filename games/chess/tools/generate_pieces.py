#!/usr/bin/env python3
"""Generate the chess piece assets from the ASCII art below.

Writes two headers into src/assets/:

    ChessPalette.h  - the Color mapping every piece sprite resolves against
    ChessPieces.h   - Sprite4bpp tables at board size (28x28) and tray size (14x14)

The art is original work for this demo, drawn from scratch against the 16-colour
PR32 palette. It is not derived from any third-party pack, so the generated
headers are free to ship with the rest of the repository.

Each piece is one 14x14 grid, one character per palette index:

    '.'  index 0  transparent - never drawn (the 4bpp blitter skips nibble 0)
    'o'  index 1  outline
    'b'  index 2  body
    's'  index 3  accent

The grids are emitted twice: once at 1:1 for the captured-piece tray, and once
nearest-neighbour doubled to 28x28 for the board. The doubling happens here
because the renderer has no scaled draw for Sprite4bpp - only the 1bpp Sprite
and MultiSprite paths take a scale factor - so board pieces have to be stored at
their final size.

Usage:
    python tools/generate_pieces.py
"""

from pathlib import Path

SIZE = 14
BOARD_SCALE = 2

# Palette index per art character. Index 0 is the transparent sentinel.
INDEX_FOR = {".": 0, "o": 1, "b": 2, "s": 3}
PALETTE_SIZE = 4

# --- Colour ------------------------------------------------------------------
#
# This block is the ONLY place the piece colours are defined. The header emitter
# below and tools/preview_board.py both read it, so the preview cannot drift out
# of sync with what the firmware draws - which is exactly how a piece colour once
# ended up identical to a board square without anyone noticing.

# PR32 slot -> RGB888, matching include/graphics/PaletteDefs.h (PALETTE_PR32).
PR32 = {
    0:  (0x00, 0x00, 0x00),  1:  (0xFF, 0xFF, 0xFF),
    2:  (0x1B, 0x1F, 0x3B),  3:  (0x00, 0x47, 0xFF),
    4:  (0x00, 0xC2, 0xFF),  5:  (0x0E, 0x7A, 0x0D),
    6:  (0x2E, 0xCC, 0x40),  7:  (0xA8, 0xFF, 0x9E),
    8:  (0xFF, 0xD5, 0x00),  9:  (0xFF, 0x9F, 0x1C),
    10: (0xC7, 0x7D, 0xFF),  11: (0xC1, 0x12, 0x1F),
    12: (0x6A, 0x04, 0x0F),  13: (0x7B, 0x2C, 0xBF),
    14: (0xCE, 0xCE, 0xCE),  15: (0x8D, 0x8D, 0x8D),
}

# The engine's own PALETTE_PR32, copied verbatim from
# include/graphics/PaletteDefs.h. Only check_packing() reads it - it exists so
# rgb565() can be held to the engine's rounding instead of guessing at it.
PR32_RGB565 = [
    0x0000, 0xFFFF, 0x1907, 0x025F, 0x061F, 0x13C2, 0x3648, 0xA7F3,
    0xFEA0, 0xFCE3, 0xC3FF, 0xB884, 0x6822, 0x7977, 0xCE79, 0x8C71,
]

# Enum spelling for each slot, so the emitted header reads as Color::Name.
PR32_NAMES = {
    0: "Black",  1: "White",      2: "Navy",    3: "Blue",
    4: "Cyan",   5: "DarkGreen",  6: "Green",   7: "LightGreen",
    8: "Yellow", 9: "Orange",     10: "LightRed", 11: "Red",
    12: "DarkRed", 13: "Purple",  14: "Magenta", 15: "Gray",
}

# A worked custom palette, shipped so the demo has something to switch to.
#
# It is PR32 with exactly two slots replaced: the two the board squares use. The
# pieces keep their PR32 slots untouched, which is the whole point - a palette is
# a lookup table, so re-pointing two entries reskins the board without a single
# line of drawing code changing.
#
# Both replacements go through check_contrast() below like any other palette.
WOOD = dict(PR32)
WOOD[14] = (0xD9, 0xB3, 0x82)   # light square: pale oak
WOOD[5] = (0x8B, 0x5A, 0x2B)    # dark square: walnut

# Every palette the pieces may be drawn against. check_contrast() walks all of
# them, so shipping a new one here is what proves it is safe to ship.
PALETTES = {"PR32": PR32, "Wood": WOOD}

# The entry emitted to src/assets/ChessCustomPalette.h for the demo to select.
CUSTOM_PALETTE = "Wood"

# The squares the pieces sit on. Must match kLightSquare / kDarkSquare in
# src/ChessConstants.h - the contrast guard below is what ties the two together.
LIGHT_SQUARE = 14   # Magenta, which is #CECECE in this palette - a light grey
DARK_SQUARE = 5     # DarkGreen #0E7A0D

# Piece palettes, indexed by the nibble stored in the sprite data.
#
#   0  transparent sentinel, never drawn - its value only has to be something
#   1  outline
#   2  body
#   3  accent
#
# The two sides share every bitmap and differ only by which of these is passed
# at draw time.
WHITE_PALETTE = {0: 0, 1: 2, 2: 1, 3: 15}   # -, Navy,   White, Gray
BLACK_PALETTE = {0: 0, 1: 9, 2: 2, 3: 15}   # -, Orange, Navy,  Gray

PALETTE_ROLES = {1: "outline", 2: "body", 3: "accent"}

# Smallest RGB distance any two colours that touch may have. 60 is chosen to sit
# below the weakest pair the art actually relies on - White body against the
# #CECECE light square, which measures about 85 and reads because the Navy
# outline frames it - and far above zero, which is what a straight collision
# scores.
MIN_COLOUR_DISTANCE = 60

PIECES = {
    "Pawn": [
        "..............",
        "..............",
        ".....oooo.....",
        "....obbbbo....",
        "....obbbbo....",
        ".....obbo.....",
        ".....obbo.....",
        "....obbbbo....",
        "...obbbbbbo...",
        "...obbbbbbo...",
        "..oooooooooo..",
        "..obbbbbbbbo..",
        "..obbssssbbo..",
        "..oooooooooo..",
    ],
    # Head in profile facing right: ears top left, muzzle bulging right, a
    # single accent pixel for the eye.
    "Knight": [
        "..............",
        "....oo........",
        "...obbo.......",
        "...obbboo.....",
        "..obbbbbbo....",
        "..obsbbbbbo...",
        "..oobbbbbbo...",
        "....obbbbbo...",
        "...obbbbbbo...",
        "..obbbbbbbo...",
        "..obbbbbbbbo..",
        "..oooooooooo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
    ],
    "Bishop": [
        "......oo......",
        ".....obbo.....",
        ".....obbo.....",
        "....obbbbo....",
        "....obsbbo....",
        "....obbbbo....",
        ".....obbo.....",
        ".....obbo.....",
        "....obbbbo....",
        "...obbbbbbo...",
        "..obbbbbbbbo..",
        "..oooooooooo..",
        "..obbssssbbo..",
        "..oooooooooo..",
    ],
    "Rook": [
        "..............",
        "..oo.oo.oo.o..",
        "..obbobbobbo..",
        "..obbbbbbbbo..",
        "..oobbbbbboo..",
        "...obbbbbbo...",
        "...obbssbbo...",
        "...obbbbbbo...",
        "...obbbbbbo...",
        "..obbbbbbbbo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
    ],
    "Queen": [
        "..o..o..o..o..",
        "..oo.oo.oo.o..",
        "..obbobbobbo..",
        "...obbbbbbo...",
        "...obssssbo...",
        "....obbbbo....",
        "....obbbbo....",
        "...obbbbbbo...",
        "...obbbbbbo...",
        "..obbbbbbbbo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
    ],
    "King": [
        "......oo......",
        "......oo......",
        "...oooooooo...",
        "......oo......",
        "...oooooooo...",
        "..obbbbbbbbo..",
        "..obsobbosbo..",
        "..obbbbbbbbo..",
        "...obbbbbbo...",
        "...obbbbbbo...",
        "..obbbbbbbbo..",
        "..oooooooooo..",
        "..obbbbbbbbo..",
        "..oooooooooo..",
    ],
}

# Order must match chess::PieceType with PieceType::None dropped.
ORDER = ["Pawn", "Knight", "Bishop", "Rook", "Queen", "King"]

PALETTE_HEADER = """// Generated by tools/generate_pieces.py
// Engine: PixelRoot32
// Mode: 4bpp
// Source: ORIGINAL artwork for this demo (grids live in the generator).
//         Not derived from any third-party pack; free to redistribute.

#pragma once

#include <graphics/Color.h>

#include <cstdint>

namespace chessdemo {

using pixelroot32::graphics::Color;

/**
 * Colours every piece sprite resolves against, indexed by the nibble stored in
 * the sprite data. Index 0 is the transparent sentinel and is never drawn, so
 * its value only has to be *something*.
 *
 * These are checked against the board squares when they are generated: every
 * drawn colour has to stand at least a set distance from both kLightSquare and
 * kDarkSquare, and from the other colours in its own palette. Nothing in C++
 * notices a collision - the header compiles and the sprite draws, the outline
 * is just invisible on half the board - so the check lives in the generator and
 * fails the run instead. Change a colour there, not here.
 *
 * Two PR32 slots are deliberately avoided for piece pixels:
 *
 *   Color::Black resolves to RGB565 0x0000, and packRgb565ToTftSprite8(0x0000)
 *   is 0 - the value the 8bpp framebuffer treats as transparent. A black pixel
 *   therefore disappears on the ESP32 while still drawing on the SDL2 build.
 *   Color::Navy (#1B1F3B) is the darkest slot that survives the pack, so it
 *   plays the part of black here.
 *
 *   Color::Magenta is not magenta in this palette: it is #CECECE, a light grey,
 *   and it is the light board square. It was the rim on the dark pieces until
 *   that turned out to mean the rim and the square were the same colour.
 */
static constexpr uint8_t kPiecePaletteSize = %d;

%s

%s

}  // namespace chessdemo
"""

CUSTOM_PALETTE_HEADER = """// Generated by tools/generate_pieces.py
// Engine: PixelRoot32
// Source: the %s entry in tools/generate_pieces.py PALETTES.

#pragma once

#include <cstdint>

namespace chessdemo {

/**
 * A full 16-slot RGB565 palette, in the layout the engine expects.
 *
 * This is PR32 with exactly two slots re-pointed - the two the board squares
 * use - which is the whole demonstration: a palette is a lookup table, so
 * changing two entries reskins the board without one line of drawing code
 * changing. The piece slots are untouched.
 *
 * It is generated rather than hand-written so check_contrast() can validate it
 * against the piece colours, exactly as it validates PR32. A hand-written table
 * here would be a palette nothing verifies.
 *
 * The engine stores the POINTER and never copies, so this must keep static
 * storage duration for as long as the palette is selected.
 */
static const uint16_t kChessCustomPalette[16] = {
%s
};

}  // namespace chessdemo
"""

PIECES_HEADER = """// Generated by tools/generate_pieces.py
// Engine: PixelRoot32
// Mode: 4bpp
// Source: ORIGINAL artwork for this demo (grids live in the generator).
//         Not derived from any third-party pack; free to redistribute.
//
// Packing matches Sprite4bpp (include/graphics/Renderer.h):
//   row stride = (width * 4 + 7) / 8 bytes
//   byte low nibble  = left pixel, high nibble = right pixel
//   nibble value 0   = transparent, never written to the framebuffer

#pragma once

#include <graphics/Renderer.h>

#include <cstdint>

#include "assets/ChessPalette.h"
#include "chess/ChessRules.h"

namespace chessdemo {

using pixelroot32::graphics::Sprite4bpp;

/** Board pieces, drawn 1:1 into a %d px square inside each %d px cell. */
static constexpr uint8_t kPieceSize = %d;

/** Captured-piece icons for the HUD tray. */
static constexpr uint8_t kPieceIconSize = %d;

"""


def validate():
    for name, rows in PIECES.items():
        if len(rows) != SIZE:
            raise SystemExit(f"{name}: expected {SIZE} rows, got {len(rows)}")
        for index, row in enumerate(rows):
            if len(row) != SIZE:
                raise SystemExit(
                    f"{name} row {index}: expected {SIZE} columns, got {len(row)}"
                )
            unknown = set(row) - set(INDEX_FOR)
            if unknown:
                raise SystemExit(f"{name} row {index}: unexpected characters {unknown}")


def distance(a, b):
    """Plain RGB distance. Crude next to a real colour space, but it only has
    to answer one question: could a player tell these two apart?"""
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def rgb565(rgb):
    """Pack RGB888 into the RGB565 the engine palettes are made of.

    Rounds rather than truncates. That is not a detail: >> 3 and >> 2 reproduce
    only about half of PALETTE_PR32, and a custom palette built by truncation
    would shift every slot it was meant to leave alone. PR32_RGB565 below is the
    engine's own table, and check_packing() holds this function to it.
    """
    r, g, b = rgb
    return (((r * 31 + 127) // 255) << 11
            | ((g * 63 + 127) // 255) << 5
            | ((b * 31 + 127) // 255))


def check_packing():
    """Prove rgb565() agrees with the engine's own PALETTE_PR32, slot for slot.

    Everything downstream trusts that a generated palette differs from PR32 only
    where it was meant to. This is what makes that true rather than hoped for.
    """
    wrong = [
        f"slot {slot} {PR32_NAMES[slot]}: packed 0x{rgb565(PR32[slot]):04X}, "
        f"engine has 0x{PR32_RGB565[slot]:04X}"
        for slot in range(16)
        if rgb565(PR32[slot]) != PR32_RGB565[slot]
    ]
    if wrong:
        raise SystemExit("rgb565() disagrees with PALETTE_PR32:\n  " + "\n  ".join(wrong))


def check_contrast():
    """Refuse to emit art a player cannot read.

    The piece palettes and the board squares are chosen independently, and
    nothing in C++ notices when they collide - the header compiles, the sprite
    draws, and the outline is simply invisible against half the board. That is
    what happened when the dark pieces were rimmed in Color::Magenta, which is
    #CECECE, the very colour of the light square.

    So every drawn colour is checked against both squares and against the other
    colours in its own palette, and a collision fails the run. The check runs
    once per entry in PALETTES: a colour is chosen as a slot *number*, and what
    that number looks like is whatever palette is loaded, so a piece that reads
    under PR32 can still vanish under a custom one.
    """
    problems = []

    for palette_name, table in PALETTES.items():
        squares = {"light square": LIGHT_SQUARE, "dark square": DARK_SQUARE}

        for side, palette in (("white", WHITE_PALETTE), ("black", BLACK_PALETTE)):
            drawn = {index: slot for index, slot in palette.items() if index != 0}

            for index, slot in drawn.items():
                role = PALETTE_ROLES[index]

                for label, square in squares.items():
                    gap = distance(table[slot], table[square])
                    if gap < MIN_COLOUR_DISTANCE:
                        problems.append(
                            f"[{palette_name}] {side} {role} Color::{PR32_NAMES[slot]} "
                            f"vs the {label} Color::{PR32_NAMES[square]}: "
                            f"distance {gap:.0f}"
                        )

                for other, otherSlot in drawn.items():
                    if other <= index:
                        continue
                    gap = distance(table[slot], table[otherSlot])
                    if gap < MIN_COLOUR_DISTANCE:
                        problems.append(
                            f"[{palette_name}] {side} {role} "
                            f"Color::{PR32_NAMES[slot]} vs its own "
                            f"{PALETTE_ROLES[other]} Color::{PR32_NAMES[otherSlot]}: "
                            f"distance {gap:.0f}"
                        )

    if problems:
        raise SystemExit(
            "colour collision (minimum distance is "
            f"{MIN_COLOUR_DISTANCE}):\n  " + "\n  ".join(problems)
        )


def palette_block(symbol, palette):
    """Emit one Color[] table, widest comment aligned."""
    lines = [f"static const Color {symbol}[kPiecePaletteSize] = {{"]
    entries = []
    for index in range(PALETTE_SIZE):
        slot = palette[index]
        role = ("transparent sentinel, never drawn" if index == 0
                else f"{PALETTE_ROLES[index]} - #%02X%02X%02X" % PR32[slot])
        entries.append((f"Color::{PR32_NAMES[slot]}", f"{index}: {role}"))

    width = max(len(name) for name, _ in entries)
    for position, (name, comment) in enumerate(entries):
        comma = "" if position == len(entries) - 1 else ","
        lines.append(f"    {name}{comma}".ljust(width + 5) + f" // {comment}")
    lines.append("};")
    return "\n".join(lines)


def scaled(rows, factor):
    """Nearest-neighbour upscale, so the pixel art stays exact at any factor."""
    out = []
    for row in rows:
        widened = "".join(cell * factor for cell in row)
        out.extend([widened] * factor)
    return out


def pack_4bpp(rows):
    """Pack a grid into 4bpp bytes: low nibble left pixel, high nibble right."""
    width = len(rows[0])
    stride = (width * 4 + 7) // 8

    data = []
    for row in rows:
        line = [0] * stride
        for x, cell in enumerate(row):
            value = INDEX_FOR[cell]
            if x % 2 == 0:
                line[x // 2] |= value
            else:
                line[x // 2] |= value << 4
        data.extend(line)
    return data, stride


def emit_bytes(symbol, data, stride):
    lines = [f"static const uint8_t {symbol}[{len(data)}] = {{"]
    for offset in range(0, len(data), stride):
        row = ", ".join(f"0x{byte:02X}" for byte in data[offset:offset + stride])
        lines.append(f"    {row},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    validate()
    check_packing()
    check_contrast()

    board_size = SIZE * BOARD_SCALE
    out = [PIECES_HEADER % (board_size, 30, board_size, SIZE)]

    out.append("// --- Bitmaps ---------------------------------------------"
               "--------------------\n\n")
    for name in ORDER:
        rows = PIECES[name]

        data, stride = pack_4bpp(scaled(rows, BOARD_SCALE))
        out.append(f"// {name}, {board_size}x{board_size}\n")
        out.append(emit_bytes(f"CHESS_{name.upper()}_4BPP", data, stride))
        out.append("\n")

        data, stride = pack_4bpp(rows)
        out.append(f"// {name}, {SIZE}x{SIZE}\n")
        out.append(emit_bytes(f"CHESS_{name.upper()}_ICON_4BPP", data, stride))
        out.append("\n")

    out.append("// --- Sprite tables, indexed by chess::PieceType - 1 -------"
               "--------------------\n//\n"
               "// The bitmap is shared between the two sides; only the palette"
               " differs.\n\n")

    for side, palette in (("White", "kWhitePiecePalette"), ("Black", "kBlackPiecePalette")):
        out.append(f"static const Sprite4bpp k{side}Pieces[6] = {{\n")
        for name in ORDER:
            out.append(
                f"    {{ CHESS_{name.upper()}_4BPP, {palette}, "
                f"kPieceSize, kPieceSize, kPiecePaletteSize }},  // {name}\n"
            )
        out.append("};\n\n")

        out.append(f"static const Sprite4bpp k{side}PieceIcons[6] = {{\n")
        for name in ORDER:
            out.append(
                f"    {{ CHESS_{name.upper()}_ICON_4BPP, {palette}, "
                f"kPieceIconSize, kPieceIconSize, kPiecePaletteSize }},  // {name}\n"
            )
        out.append("};\n\n")

    out.append(
        """/**
 * Board sprite for a piece, or nullptr for an empty square.
 *
 * PieceType::None is 0, so the tables above are indexed by the enum value
 * minus one.
 */
inline const Sprite4bpp* spriteFor(chess::Piece piece) {
    if (chess::isEmpty(piece)) return nullptr;

    const uint8_t index = static_cast<uint8_t>(chess::typeOf(piece)) - 1;
    if (index >= 6) return nullptr;

    return (chess::sideOf(piece) == chess::Side::White) ? &kWhitePieces[index]
                                                        : &kBlackPieces[index];
}

/** Tray icon for a bare piece type of the given side. */
inline const Sprite4bpp* iconFor(chess::PieceType type, chess::Side side) {
    if (type == chess::PieceType::None) return nullptr;

    const uint8_t index = static_cast<uint8_t>(type) - 1;
    if (index >= 6) return nullptr;

    return (side == chess::Side::White) ? &kWhitePieceIcons[index]
                                        : &kBlackPieceIcons[index];
}

}  // namespace chessdemo
"""
    )

    assets = Path(__file__).resolve().parent.parent / "src" / "assets"
    assets.mkdir(parents=True, exist_ok=True)

    (assets / "ChessPalette.h").write_text(
        PALETTE_HEADER % (
            PALETTE_SIZE,
            palette_block("kWhitePiecePalette", WHITE_PALETTE),
            palette_block("kBlackPiecePalette", BLACK_PALETTE),
        ),
        encoding="utf-8",
        newline="\n",
    )
    (assets / "ChessPieces.h").write_text("".join(out), encoding="utf-8", newline="\n")

    rows = []
    for slot in range(16):
        colour = PALETTES[CUSTOM_PALETTE][slot]
        rows.append("    0x%04X,  // %-2d %-10s #%02X%02X%02X"
                    % ((rgb565(colour), slot, PR32_NAMES[slot]) + colour))
    rows[-1] = rows[-1].replace(",", " ", 1)

    (assets / "ChessCustomPalette.h").write_text(
        CUSTOM_PALETTE_HEADER % (CUSTOM_PALETTE, "\n".join(rows)),
        encoding="utf-8",
        newline="\n",
    )

    print(f"wrote {assets / 'ChessPalette.h'}")
    print(f"wrote {assets / 'ChessPieces.h'}")
    print(f"wrote {assets / 'ChessCustomPalette.h'}")


if __name__ == "__main__":
    main()
