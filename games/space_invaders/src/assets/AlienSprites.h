#pragma once
#include <cstdint>
#include "graphics/Renderer.h"
#include "../GameConstants.h"

namespace spaceinvaders {

/**
 * @file AlienSprites.h
 * @brief Bitmap sprite data for the three alien types (Squid, Crab, Octopus).
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent.
 * Bit 0 is the leftmost pixel, so the ASCII picture beside each row reads in
 * the same direction as the bits. Each type has two frames; the formation
 * advances them in lockstep with its movement steps, which is what makes the
 * classic "march" read as one animation instead of eight.
 *
 * The Crab additionally ships a `MultiSprite`: the same body bitmap plus a
 * second, mostly empty layer drawn in another colour. That is the engine's
 * way of getting a two-colour sprite out of two 1bpp layers.
 */

/** Squid (8x8) — top row, 30 points */
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

/** Crab (11x8) — middle rows, 20 points */
static const uint16_t CRAB_F1_BITS[] = {
    0x0104, // ..X.....X..
    0x0088, // ...X...X...
    0x01FC, // ..XXXXXXX..
    0x0376, // .XX.XXX.XX.
    0x07FF, // XXXXXXXXXXX
    0x01DD, // X.XXX.XXX..
    0x0208, // ...X.....X.
    0x0000  // ...........
};

static const uint16_t CRAB_F2_BITS[] = {
    0x0104, // ..X.....X..
    0x03F8, // ...XXXXXXX.
    0x01FC, // ..XXXXXXX..
    0x0376, // .XX.XXX.XX.
    0x07FF, // XXXXXXXXXXX
    0x02AA, // .X.X.X.X.X.
    0x0404, // ..X.......X
    0x0000  // ...........
};

/** Octopus (12x8) — bottom rows, 10 points */
static const uint16_t OCTOPUS_F1_BITS[] = {
    0x0000, // ............
    0x01E0, // .....XXXX...
    0x07F8, // ...XXXXXXXX.
    0x09F2, // .X..XXXXX..X
    0x0FFE, // .XXXXXXXXXXX
    0x05A0, // .....X.XX.X.
    0x0A50, // ....X.X..X.X
    0x0210  // ....X....X..
};

static const uint16_t OCTOPUS_F2_BITS[] = {
    0x0000, // ............
    0x01E0, // .....XXXX...
    0x07F8, // ...XXXXXXXX.
    0x09F2, // .X..XXXXX..X
    0x0FFE, // .XXXXXXXXXXX
    0x02D0, // ....X.XX.X..
    0x0528, // ...X.X..X.X.
    0x0000  // ............
};

// Sprite descriptors, shared by every actor of the matching type.
static const pixelroot32::graphics::Sprite SQUID_F1 = { SQUID_F1_BITS, ALIEN_SQUID_SPRITE_W, ALIEN_SQUID_SPRITE_H };
static const pixelroot32::graphics::Sprite SQUID_F2 = { SQUID_F2_BITS, ALIEN_SQUID_SPRITE_W, ALIEN_SQUID_SPRITE_H };

static const pixelroot32::graphics::Sprite CRAB_F1 = { CRAB_F1_BITS, ALIEN_CRAB_SPRITE_W, ALIEN_CRAB_SPRITE_H };
static const pixelroot32::graphics::Sprite CRAB_F2 = { CRAB_F2_BITS, ALIEN_CRAB_SPRITE_W, ALIEN_CRAB_SPRITE_H };

static const pixelroot32::graphics::Sprite OCTOPUS_F1 = { OCTOPUS_F1_BITS, ALIEN_OCTOPUS_SPRITE_W, ALIEN_OCTOPUS_SPRITE_H };
static const pixelroot32::graphics::Sprite OCTOPUS_F2 = { OCTOPUS_F2_BITS, ALIEN_OCTOPUS_SPRITE_W, ALIEN_OCTOPUS_SPRITE_H };

// Layered sprite: the crab body plus an eye highlight drawn in a second
// colour. Layers share the sprite's dimensions and are drawn in array order.
static const uint16_t CRAB_EYES_BITS[] = {
    0x0000, // ...........
    0x0000, // ...........
    0x0000, // ...........
    0x0000, // ...........
    0x0000, // ...........
    0x0240, // ......X..X.
    0x0000, // ...........
    0x0000  // ...........
};

static const pixelroot32::graphics::SpriteLayer CRAB_LAYERS_F1[] = {
    { CRAB_F1_BITS,   pixelroot32::graphics::Color::Orange },
    { CRAB_EYES_BITS, pixelroot32::graphics::Color::White }
};

static const pixelroot32::graphics::MultiSprite CRAB_F1_MULTI = {
    ALIEN_CRAB_SPRITE_W,
    ALIEN_CRAB_SPRITE_H,
    CRAB_LAYERS_F1,
    2
};

}
