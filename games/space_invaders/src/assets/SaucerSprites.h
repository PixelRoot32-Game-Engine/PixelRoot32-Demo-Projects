#pragma once
#include <cstdint>
#include "graphics/Renderer.h"

namespace spaceinvaders {

/**
 * @file SaucerSprites.h
 * @brief Bitmap sprite data for the mystery saucer (16x7).
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * The saucer has a single frame: it never animates in the arcade, and the only
 * thing that changes while it crosses the field is which edge it entered from.
 * The fourth row is a window strip rather than a solid body, which is what makes
 * the silhouette read as a saucer and not as a flat disc.
 */
static const uint16_t SAUCER_BITS[] = {
    0x07E0, // .....XXXXXX.....
    0x1FF8, // ...XXXXXXXXXX...
    0x3FFC, // ..XXXXXXXXXXXX..
    0x6DB6, // .XX.XX.XX.XX.XX.
    0xFFFF, // XXXXXXXXXXXXXXXX
    0x399C, // ..XXX..XX..XXX..
    0x1008  // ...X........X...
};

static const pixelroot32::graphics::Sprite SAUCER_SPRITE = { SAUCER_BITS, 16, 7 };

}
