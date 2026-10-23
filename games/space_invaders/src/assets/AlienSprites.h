#pragma once
#include <cstdint>
#include "graphics/Renderer.h"
#include "../GameConstants.h"

namespace spaceinvaders {

/**
 * @file AlienSprites.h
 * @brief Bitmap sprite data for the three alien types (Squid, Crab, Octopus).
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * Each type has two frames; the formation advances them in lockstep with its
 * movement steps, which is what makes the classic "march" read as one animation
 * instead of eight. Every frame is a plain `Sprite`: the masks are drawn in a
 * single band colour, so a second layer (MultiSprite) would add state without
 * adding a pixel.
 */

/** Squid (8x8) — top row, 30 points, frame 1 */
static const uint16_t SQUID_F1_BITS[] = {
    0x0018, // ...XX...
    0x003C, // ..XXXX..
    0x007E, // .XXXXXX.
    0x00DB, // XX.XX.XX
    0x00FF, // XXXXXXXX
    0x0024, // ..X..X..
    0x005A, // .X.XX.X.
    0x00A5  // X.X..X.X
};

/** Squid (8x8) — top row, 30 points, frame 2 */
static const uint16_t SQUID_F2_BITS[] = {
    0x0018, // ...XX...
    0x003C, // ..XXXX..
    0x007E, // .XXXXXX.
    0x00DB, // XX.XX.XX
    0x00FF, // XXXXXXXX
    0x005A, // .X.XX.X.
    0x0081, // X......X
    0x0042  // .X....X.
};

/** Crab (11x8) — middle rows, 20 points, frame 1 */
static const uint16_t CRAB_F1_BITS[] = {
    0x0104, // ..X.....X..
    0x0489, // X..X...X..X
    0x05FD, // X.XXXXXXX.X
    0x0777, // XXX.XXX.XXX
    0x07FF, // XXXXXXXXXXX
    0x03FE, // .XXXXXXXXX.
    0x0104, // ..X.....X..
    0x0202  // .X.......X.
};

/** Crab (11x8) — middle rows, 20 points, frame 2 */
static const uint16_t CRAB_F2_BITS[] = {
    0x0104, // ..X.....X..
    0x0088, // ...X...X...
    0x01FC, // ..XXXXXXX..
    0x0376, // .XX.XXX.XX.
    0x07FF, // XXXXXXXXXXX
    0x05FD, // X.XXXXXXX.X
    0x0505, // X.X.....X.X
    0x00D8  // ...XX.XX...
};

/** Octopus (12x8) — bottom rows, 10 points, frame 1 */
static const uint16_t OCTOPUS_F1_BITS[] = {
    0x00F0, // ....XXXX....
    0x07FE, // .XXXXXXXXXX.
    0x0FFF, // XXXXXXXXXXXX
    0x0E67, // XXX..XX..XXX
    0x0FFF, // XXXXXXXXXXXX
    0x0198, // ...XX..XX...
    0x036C, // ..XX.XX.XX..
    0x0C03  // XX........XX
};

/** Octopus (12x8) — bottom rows, 10 points, frame 2 */
static const uint16_t OCTOPUS_F2_BITS[] = {
    0x00F0, // ....XXXX....
    0x07FE, // .XXXXXXXXXX.
    0x0FFF, // XXXXXXXXXXXX
    0x0E67, // XXX..XX..XXX
    0x0FFF, // XXXXXXXXXXXX
    0x039C, // ..XXX..XXX..
    0x0666, // .XX..XX..XX.
    0x030C  // ..XX....XX..
};

// Sprite descriptors, shared by every actor of the matching type.
static const pixelroot32::graphics::Sprite SQUID_F1 = { SQUID_F1_BITS, ALIEN_SQUID_SPRITE_W, ALIEN_SQUID_SPRITE_H };
static const pixelroot32::graphics::Sprite SQUID_F2 = { SQUID_F2_BITS, ALIEN_SQUID_SPRITE_W, ALIEN_SQUID_SPRITE_H };

static const pixelroot32::graphics::Sprite CRAB_F1 = { CRAB_F1_BITS, ALIEN_CRAB_SPRITE_W, ALIEN_CRAB_SPRITE_H };
static const pixelroot32::graphics::Sprite CRAB_F2 = { CRAB_F2_BITS, ALIEN_CRAB_SPRITE_W, ALIEN_CRAB_SPRITE_H };

static const pixelroot32::graphics::Sprite OCTOPUS_F1 = { OCTOPUS_F1_BITS, ALIEN_OCTOPUS_SPRITE_W, ALIEN_OCTOPUS_SPRITE_H };
static const pixelroot32::graphics::Sprite OCTOPUS_F2 = { OCTOPUS_F2_BITS, ALIEN_OCTOPUS_SPRITE_W, ALIEN_OCTOPUS_SPRITE_H };

}
