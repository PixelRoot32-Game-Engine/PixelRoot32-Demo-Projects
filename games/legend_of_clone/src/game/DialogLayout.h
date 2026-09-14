#pragma once

#include "platforms/PlatformDefaults.h"
#include "graphics/DialogBox.h"

#include <cstdint>

namespace legend_of_clone {

/// Vertical slot the dialog box occupies on screen.
struct DialogBoxPlacement {
    int16_t y;
    int16_t h;
};

/**
 * @brief Where a box of `contentHeightPx` goes (R12).
 *
 * A box that fits the 64 px status bar takes the whole bar, so the playfield
 * stays fully visible while the player talks. A taller one is anchored to the
 * bottom of the screen and overlays the bottom of the playfield.
 */
DialogBoxPlacement placeDialogBox(int16_t contentHeightPx);

/**
 * @brief The box's width, font and spacing, before it is placed.
 *
 * 200 px wide with a 1 px border and 1 px padding leaves 196 px of text, 32
 * glyphs of the 5x7 font. The width is what makes the old man's speech wrap
 * to five lines and page (R3); the padding is what keeps the shop menu's
 * speaker, prompt and four rows inside the status bar.
 */
pixelroot32::graphics::DialogBoxStyle dialogBoxBaseStyle();

/// measureHeightPx() of the tallest of the four scripts at the base style.
int16_t tallestDialogHeightPx();

/// The base style, placed by placeDialogBox(tallestDialogHeightPx()).
pixelroot32::graphics::DialogBoxStyle dialogBoxStyle();

} // namespace legend_of_clone
