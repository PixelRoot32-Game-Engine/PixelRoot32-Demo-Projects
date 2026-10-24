#pragma once
#include <cstdint>
#include "graphics/Renderer.h"

namespace spaceinvaders {

/**
 * @file ShotSprites.h
 * @brief Bitmap sprite data for the two shot types.
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * The player shot is the 1x4 bar of the arcade missile: one pixel wide is the
 * whole point, because three of them can climb the same lane without hiding the
 * aliens behind them. The enemy shot is a 3x7 zigzag whose two frames are mirror
 * images, so alternating them reads as the bolt wriggling down the lane.
 */

/** Player shot (1x4) — spawns at the cannon centre */
static const uint16_t PLAYER_SHOT_BITS[] = {
    0x0001, // X
    0x0001, // X
    0x0001, // X
    0x0001  // X
};

/** Enemy shot (3x7) — frame 1, bolt leaning left */
static const uint16_t ENEMY_SHOT_F1_BITS[] = {
    0x0002, // .X.
    0x0004, // X..
    0x0002, // .X.
    0x0001, // ..X
    0x0002, // .X.
    0x0004, // X..
    0x0002  // .X.
};

/** Enemy shot (3x7) — frame 2, the mirror of frame 1 */
static const uint16_t ENEMY_SHOT_F2_BITS[] = {
    0x0002, // .X.
    0x0001, // ..X
    0x0002, // .X.
    0x0004, // X..
    0x0002, // .X.
    0x0001, // ..X
    0x0002  // .X.
};

static const pixelroot32::graphics::Sprite PLAYER_SHOT_SPRITE = { PLAYER_SHOT_BITS, 1, 4 };

static const pixelroot32::graphics::Sprite ENEMY_SHOT_F1 = { ENEMY_SHOT_F1_BITS, 3, 7 };
static const pixelroot32::graphics::Sprite ENEMY_SHOT_F2 = { ENEMY_SHOT_F2_BITS, 3, 7 };

}
