#pragma once
#include "graphics/Color.h"

namespace spaceinvaders {

/**
 * @file BandColor.h
 * @brief Colour of the cabinet overlay for a screen row (S3).
 *
 * The real cabinet prints a coloured strip over the tube, so a single white
 * mask reads red at the top, white across the middle and green at the bottom.
 * Tinting the masks while drawing reproduces that overlay from one white
 * bitmap per sprite instead of shipping three per-band recoloured copies.
 */

    /**
     * @brief First scanline that is not inside the red band.
     *
     * The upper strip covers the score row and the saucer row, ending just
     * above the rack, so this edge sits one row past the saucer's bottom.
     */
    constexpr int BAND_RED_END_Y = 32;

    /**
     * @brief First scanline inside the green band.
     *
     * It coincides with the shield row (S1): the lower strip starts where the
     * bunkers start, which is why the shields read green at their top edge.
     */
    constexpr int BAND_GREEN_START_Y = 184;

    /**
     * @brief Resolves the overlay tint for a gameplay mask.
     *
     * Pass the top row of the drawn mask, not its centre or bottom. The band is
     * decided by where the mask enters the tube, so a mask crossing an edge
     * keeps a single colour instead of splitting or flipping late.
     *
     * The HUD text and the PRESS FIRE / game-over messages are printed on the
     * tube rather than under the strip, so they stay white and must not use
     * this helper.
     *
     * @param topY Screen y of the mask's first drawn row.
     * @return Red above the red edge, green from the green edge down, white between.
     */
    inline pixelroot32::graphics::Color bandColorFor(int topY) {
        if (topY < BAND_RED_END_Y) {
            return pixelroot32::graphics::Color::Red;
        }
        if (topY >= BAND_GREEN_START_Y) {
            return pixelroot32::graphics::Color::Green;
        }
        return pixelroot32::graphics::Color::White;
    }

}
