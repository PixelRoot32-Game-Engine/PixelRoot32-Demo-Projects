#pragma once
#include <cstdint>
#include "graphics/Renderer.h"

namespace spaceinvaders {

/**
 * @file PlayerExplosionSprites.h
 * @brief Bitmap sprite data for the 13x8 explosion burst, in two frames.
 *
 * One `uint16_t` per row, one bit per pixel: 1 draws, 0 is transparent. Rows are
 * MSB-first, so bit (width-1) is the leftmost pixel and the ASCII picture beside
 * each row reads in the same direction as `drawSprite` walks the bits.
 *
 * The burst covers the cannon exactly, so the death frame swaps the ship for a
 * same-size mask and nothing around it moves. The reference flips two images
 * for the whole death pause, so there are two frames here and the scene's
 * death hold alternates them once per rules tick (S14).
 *
 * Both frames are drawn by hand in this repository. They are not traced or
 * sampled from the arcade artwork: the README's CC0 position covers masks that
 * were redrawn here, so a later edit must keep drawing them rather than
 * copying the reference sheet.
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

/**
 * @brief Second death frame: the same burst with its centre blown open.
 *
 * The first frame keeps every fragment on four diagonals through the middle.
 * This one pushes those fragments outward, keeps only their outermost pixels
 * and leaves the centre rows nearly empty, so alternating the two reads as the
 * burst tearing apart instead of as a single mask flickering.
 */
static const uint16_t EXPLOSION_BITS_ALT[] = {
    0x0408, // ..X......X...
    0x1122, // X...X..X...X.
    0x0889, // .X...X...X..X
    0x0244, // ...X..X...X..
    0x1405, // X.X.......X.X
    0x0921, // .X..X..X....X
    0x1089, // X....X...X..X
    0x0444  // ..X...X...X..
};

static const pixelroot32::graphics::Sprite EXPLOSION_SPRITE = { EXPLOSION_BITS, 13, 8 };
static const pixelroot32::graphics::Sprite EXPLOSION_SPRITE_ALT = { EXPLOSION_BITS_ALT, 13, 8 };

/**
 * @brief The death frames, frame 0 first.
 *
 * Frame 0 stays the first entry because the hold starts on the tick the cannon
 * dies, and that is the tick the counter's low bit reads as zero.
 */
static const pixelroot32::graphics::SpriteAnimationFrame PLAYER_EXPLOSION_FRAMES[] = {
    { &EXPLOSION_SPRITE, nullptr },
    { &EXPLOSION_SPRITE_ALT, nullptr }
};

/// The hold picks a frame with `counter & 1`, so the table must hold exactly two.
static_assert(sizeof(PLAYER_EXPLOSION_FRAMES) / sizeof(PLAYER_EXPLOSION_FRAMES[0]) == 2,
              "the death hold assumes a two-frame table");

}
