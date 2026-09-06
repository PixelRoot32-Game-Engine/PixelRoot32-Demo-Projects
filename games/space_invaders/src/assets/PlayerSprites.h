#pragma once
#include <cstdint>
#include "graphics/Renderer.h"
#include "../GameConstants.h"

namespace spaceinvaders {

/**
 * @file PlayerSprites.h
 * @brief Bitmap sprite data for the player ship (11x8).
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent.
 * Bit 0 is the leftmost pixel, so the ASCII picture beside each row reads in
 * the same direction as the bits.
 */
static const uint16_t PLAYER_SHIP_BITS[] = {
    0x0020, // .....X.....
    0x0070, // ....XXX....
    0x00F8, // ...XXXXX...
    0x01FC, // ..XXXXXXX..
    0x03DE, // .XXXX.XXXX.
    0x03FE, // .XXXXXXXXX.
    0x0124, // ..X..X..X..
    0x0124  // ..X..X..X..
};

static const pixelroot32::graphics::Sprite PLAYER_SHIP_SPRITE = { PLAYER_SHIP_BITS, PLAYER_SPRITE_W, PLAYER_SPRITE_H };

}
