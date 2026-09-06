/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/Renderer.h>

/**
 * @file PlayerSprite.h
 * @brief Two hand-drawn walk frames for the 16x16 explorer, in the engine's
 *        1bpp sprite format.
 *
 * ## The whole format, in four lines
 *
 * `pixelroot32::graphics::Sprite` is three fields:
 *
 * ```cpp
 * struct Sprite {
 *     const uint16_t* data;   // one entry per row, `height` entries long
 *     uint8_t         width;  // pixels, at most 16 (one uint16_t per row)
 *     uint8_t         height; // pixels
 * };
 * ```
 *
 * One `uint16_t` is one row. One bit is one pixel: 1 draws in the colour you
 * pass to `Renderer::drawSprite`, 0 draws nothing at all (it is transparent,
 * not black — whatever was already on screen shows through).
 *
 * ## Which bit is the leftmost pixel
 *
 * Bit `(width - 1)` is the LEFTMOST pixel and bit 0 is the rightmost. For a
 * 16-wide sprite that means bit 15 on the left, so a row written in binary
 * reads left to right exactly like the picture:
 *
 *     0x0F80  ==  0b0000111110000000  ==  "....XXXXX......."
 *
 * That is why every row below carries its ASCII picture in a comment: the two
 * are the same thing written twice, and if you edit one you must edit both.
 *
 * @warning The Doxygen comment on `struct Sprite` in the engine's
 *          `graphics/Renderer.h` claims "Bit 0 represents the leftmost pixel".
 *          That comment is stale. `Renderer::drawSprite` builds its column
 *          mask as `1 << (width - 1)` and walks it right, so bit `(width - 1)`
 *          is on the left. The code is the contract; verify with an
 *          asymmetric shape if you ever doubt it.
 *
 * ## Why `constexpr` and not a local array
 *
 * `Sprite` stores a `const uint16_t*`. `Renderer::drawSprite` takes the sprite
 * by const reference and reads straight through that pointer while it blits —
 * it never copies your rows anywhere. So the row data has to stay alive and at
 * a fixed address for as long as anything can draw it. `inline constexpr` at
 * namespace scope gives exactly that: the arrays live in flash/rodata for the
 * whole run of the program, cost zero RAM, and have one address across every
 * translation unit that includes this header.
 *
 * Build a `uint16_t rows[16]` inside `init()` instead and the sprite will point
 * at a dead stack frame the first time `draw()` runs. That failure looks like
 * garbage pixels, not like a crash, which is why it is worth spelling out.
 *
 * ## The two frames
 *
 * Rows 0-12 (head, torso, arm) are identical in both frames. Only the legs,
 * rows 13-15, change. That is the cheapest possible walk cycle and it is enough
 * for the eye to read "walking". Both frames face RIGHT; the scene mirrors them
 * with `Renderer::drawSprite(..., flipX = true)` instead of storing a second
 * copy, which is the normal way to halve sprite flash on a microcontroller.
 */

namespace first_sprite::assets {

/// Sprite size in pixels. 16 wide is the maximum a `uint16_t` row can hold.
inline constexpr uint8_t kPlayerWidth  = 16;
inline constexpr uint8_t kPlayerHeight = 16;

/// Walk frame 0: legs apart (the stride). Faces right.
inline constexpr uint16_t kPlayerWalkFrame0Bits[kPlayerHeight] = {
    0x0000,  // ................
    0x0F80,  // ....XXXXX.......
    0x1FC0,  // ...XXXXXXX......
    0x1BE0,  // ...XX.XXXXX.....   <- eye gap, and the visor lip pointing right
    0x1FC0,  // ...XXXXXXX......
    0x0F80,  // ....XXXXX.......
    0x0700,  // .....XXX........   <- neck
    0x1FC0,  // ...XXXXXXX......
    0x3FF0,  // ..XXXXXXXXXX....   <- arm reaching right; this row makes the
    0x27C0,  // ..X..XXXXX......      flip obvious on screen
    0x07C0,  // .....XXXXX......
    0x07C0,  // .....XXXXX......
    0x07C0,  // .....XXXXX......
    0x0C60,  // ....XX...XX.....
    0x1830,  // ...XX.....XX....
    0x3838   // ..XXX.....XXX...
};

/// Walk frame 1: legs together (the pass). Rows 0-12 match frame 0 exactly.
inline constexpr uint16_t kPlayerWalkFrame1Bits[kPlayerHeight] = {
    0x0000,  // ................
    0x0F80,  // ....XXXXX.......
    0x1FC0,  // ...XXXXXXX......
    0x1BE0,  // ...XX.XXXXX.....
    0x1FC0,  // ...XXXXXXX......
    0x0F80,  // ....XXXXX.......
    0x0700,  // .....XXX........
    0x1FC0,  // ...XXXXXXX......
    0x3FF0,  // ..XXXXXXXXXX....
    0x27C0,  // ..X..XXXXX......
    0x07C0,  // .....XXXXX......
    0x07C0,  // .....XXXXX......
    0x07C0,  // .....XXXXX......
    0x07C0,  // .....XXXXX......
    0x06C0,  // .....XX.XX......
    0x0EE0   // ....XXX.XXX.....
};

/// Number of frames in the walk cycle. The scene alternates between them.
inline constexpr uint8_t kPlayerFrameCount = 2;

/**
 * The descriptors the renderer actually takes. Each one just wraps a row array
 * plus its dimensions — the pixels stay where they were declared above.
 */
inline constexpr pixelroot32::graphics::Sprite kPlayerWalkFrames[kPlayerFrameCount] = {
    { kPlayerWalkFrame0Bits, kPlayerWidth, kPlayerHeight },
    { kPlayerWalkFrame1Bits, kPlayerWidth, kPlayerHeight }
};

} // namespace first_sprite::assets
