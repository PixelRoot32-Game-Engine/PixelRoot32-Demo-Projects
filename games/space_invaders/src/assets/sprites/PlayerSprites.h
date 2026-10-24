#pragma once
#include <cstdint>
#include "graphics/Renderer.h"
#include "GameConstants.h"

namespace spaceinvaders {

/**
 * @file PlayerSprites.h
 * @brief Bitmap sprite data for the player cannon (13x8).
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * The cannon is a 3 px dome on an 11 px bar that becomes the full 13 px width on
 * its last four rows, matching the reference sheet: the wide bar is what the eye
 * reads as the base, and the narrow dome is what keeps the ship from looking
 * heavier than the aliens above it.
 */
static const uint16_t PLAYER_SHIP_BITS[] = {
    0x0040, // ......X......
    0x00E0, // .....XXX.....
    0x00E0, // .....XXX.....
    0x0FFE, // .XXXXXXXXXXX.
    0x1FFF, // XXXXXXXXXXXXX
    0x1FFF, // XXXXXXXXXXXXX
    0x1FFF, // XXXXXXXXXXXXX
    0x1FFF  // XXXXXXXXXXXXX
};

static const pixelroot32::graphics::Sprite PLAYER_SHIP_SPRITE = { PLAYER_SHIP_BITS, PLAYER_SPRITE_W, PLAYER_SPRITE_H };

}
