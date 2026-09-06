/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/Renderer.h>

/**
 * @file HudSprites.h
 * @brief The three heart fill states drawn by the HUD's UISpriteRow.
 *
 * Format is the engine's 1bpp `Sprite`: one `uint16_t` per row, one bit per
 * pixel, **bit (width - 1) is the leftmost pixel** — so for these 8-wide icons
 * the leftmost pixel is bit 7 and the row reads like a binary literal. A 1
 * draws in the colour passed to `Renderer::drawSprite`; a 0 is transparent.
 *
 * That bit order is worth stating because the engine's own header documents the
 * opposite. `Renderer.h:35` and `:1054` both say "bit 0 is the leftmost pixel",
 * while `Renderer.cpp:465` builds `firstColMask = 1u << (width - 1)` and shifts
 * it right per column, with an inline comment at `:464` saying so. **The
 * implementation is what renders**; the header comment is stale. Art whose rows
 * are bit-palindromic (`0x66`, `0x99`, `0x81`, `0xFF`) looks the same either
 * way, which is how the wrong claim survives — asymmetric art is where it bites.
 *
 * 1bpp is the right format here because every icon is a single-colour shape:
 * `UISpriteRow::setStateSprite(int, const Sprite&, Color)` takes the tint per
 * state, so an empty heart can be grey and a full one red without a palette.
 * A 2bpp or 4bpp icon would need `PIXELROOT32_ENABLE_2BPP_SPRITES` /
 * `_4BPP_SPRITES` in the build and buy nothing this HUD uses.
 *
 * These are `inline constexpr` — flash-resident, with a stable address for the
 * whole run. `UISpriteRow` stores a **non-owning** pointer to whatever you hand
 * `setStateSprite()`, so a sprite built on the stack inside `init()` would leave
 * the row pointing at a dead object the first time it drew.
 */

namespace hud_widgets::assets {

/// Hollow heart: the outline of the full shape. Fill level 0 (empty).
inline constexpr uint16_t kHeartEmptyBits[] = {
    0x66,  // .XX..XX.
    0x99,  // X..XX..X
    0x81,  // X......X
    0x81,  // X......X
    0x42,  // .X....X.
    0x24,  // ..X..X..
    0x18,  // ...XX...
    0x00   // ........
};

/// Left half solid, right half hollow. Fill level 1 of 2 (half).
inline constexpr uint16_t kHeartHalfBits[] = {
    0x66,  // .XX..XX.
    0xF9,  // XXXXX..X
    0xF1,  // XXXX...X
    0xF1,  // XXXX...X
    0x72,  // .XXX..X.
    0x34,  // ..XX.X..
    0x18,  // ...XX...
    0x00   // ........
};

/// Solid heart. Fill level 2 of 2 (full).
inline constexpr uint16_t kHeartFullBits[] = {
    0x66,  // .XX..XX.
    0xFF,  // XXXXXXXX
    0xFF,  // XXXXXXXX
    0xFF,  // XXXXXXXX
    0x7E,  // .XXXXXX.
    0x3C,  // ..XXXX..
    0x18,  // ...XX...
    0x00   // ........
};

/// Icon edge in pixels. The row's cell size is the widest/tallest state, and
/// all three states are the same 8x8, so this is also the cell.
inline constexpr uint8_t kHeartSize = 8;

inline constexpr pixelroot32::graphics::Sprite kHeartEmpty{kHeartEmptyBits, kHeartSize, kHeartSize};
inline constexpr pixelroot32::graphics::Sprite kHeartHalf{kHeartHalfBits, kHeartSize, kHeartSize};
inline constexpr pixelroot32::graphics::Sprite kHeartFull{kHeartFullBits, kHeartSize, kHeartSize};

}  // namespace hud_widgets::assets
