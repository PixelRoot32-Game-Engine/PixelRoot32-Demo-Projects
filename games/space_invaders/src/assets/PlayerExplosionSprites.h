#pragma once
#include <cstdint>
#include "graphics/Renderer.h"

namespace spaceinvaders {

/**
 * @file PlayerExplosionSprites.h
 * @brief Bitmap sprite data for the 13x8 explosion burst.
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * The burst covers the cannon exactly, so the death frame swaps the ship for a
 * same-size mask and nothing around it moves. The arcade uses one shared burst
 * for an alien kill and for the player death, hence a single mask here and the
 * one-entry frame table below: it keeps the existing death animation wiring
 * working while making the "one burst" decision explicit.
 */
static const uint16_t EXPLOSION_BITS[] = {
    0x0110, // ....X...X....
    0x08A2, // .X...X.X...X.
    0x0404, // ..X.......X..
    0x0208, // ...X.....X...
    0x1803, // XX.........XX
    0x0208, // ...X.....X...
    0x04A4, // ..X..X.X..X..
    0x0912  // .X..X...X..X.
};

static const pixelroot32::graphics::Sprite EXPLOSION_SPRITE = { EXPLOSION_BITS, 13, 8 };

static const pixelroot32::graphics::SpriteAnimationFrame PLAYER_EXPLOSION_FRAMES[] = {
    { &EXPLOSION_SPRITE, nullptr }
};

}
