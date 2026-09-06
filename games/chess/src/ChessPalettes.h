/*
 * ChessPalettes.h - Which engine palettes this demo loads, and where each one
 * applies.
 *
 * The demo used to configure nothing and ride the engine defaults: single
 * palette mode, PALETTE_PR32 for everything. That works, but it hides the
 * mechanism, and this is a demo. So the palettes are now selected explicitly,
 * background and sprite separately, and both are switchable from
 * lib/platformio.ini without touching a line of code.
 *
 * Two things are worth understanding before changing anything here.
 *
 * A Color is an INDEX, not a colour. Color::Magenta is #CECECE under PR32 and
 * something else entirely under GB or a custom table. Every colour choice in
 * ChessConstants.h and ChessPalette.h is therefore a choice about PR32, and
 * swapping palettes can quietly make the pieces unreadable. The generator's
 * check_contrast() walks every palette it knows about for exactly that reason -
 * add yours to PALETTES there and it gets checked too.
 *
 * Dual palette mode only splits what is drawn through a render context.
 * Sprite4bpp always resolves through the sprite palette slot, no matter what
 * the context says. Rectangles, lines, circles and text follow the context, and
 * default to Sprite when none is set - so the board squares only reach the
 * background palette because drawBoard() opens a PaletteScope. Without that
 * scope the background palette would be configured and never used.
 */
#pragma once

#include <graphics/Color.h>
#include <graphics/Renderer.h>

// --- Configuration -----------------------------------------------------------
//
// Override either of these from lib/platformio.ini, e.g.
//
//     -D CHESS_SPRITE_PALETTE=GB
//
// Valid names are the PaletteType members: PR32, NES, GB, GBC, PICO8.

#ifndef CHESS_BACKGROUND_PALETTE
#define CHESS_BACKGROUND_PALETTE PR32
#endif

#ifndef CHESS_SPRITE_PALETTE
#define CHESS_SPRITE_PALETTE PR32
#endif

/**
 * Set to 1 to draw the board through the generated 16-slot table in
 * src/assets/ChessCustomPalette.h instead of a named PaletteType. The pieces
 * keep CHESS_SPRITE_PALETTE, which is the point: two palettes, one board.
 */
#ifndef CHESS_CUSTOM_BACKGROUND
#define CHESS_CUSTOM_BACKGROUND 0
#endif

namespace chessdemo {

/**
 * @brief Load this demo's palettes into the engine.
 *
 * Call once from Scene::init(). Palette state is global to the engine rather
 * than owned by the renderer, so this is a one-shot, not a per-frame call.
 */
void applyPalettes();

/**
 * @class PaletteScope
 * @brief Selects the background or sprite palette for the drawing it encloses.
 *
 * The renderer holds a POINTER to a PaletteContext, not a copy, so the value
 * has to outlive the drawing that reads it. This owns that lifetime and puts
 * the previous context back on the way out, which also makes nesting safe.
 *
 * Only primitives - rectangles, lines, circles, text - consult the context.
 * Sprites resolve through the sprite palette slot regardless.
 */
class PaletteScope {
public:
    /**
     * @brief Switch the renderer to a palette context.
     * @param renderer Renderer whose context is being set.
     * @param context Which palette the enclosed primitives resolve through.
     */
    PaletteScope(pixelroot32::graphics::Renderer& renderer,
                 pixelroot32::graphics::PaletteContext context);

    /** @brief Restore whatever context was in force before. */
    ~PaletteScope();

    PaletteScope(const PaletteScope&) = delete;
    PaletteScope& operator=(const PaletteScope&) = delete;

private:
    pixelroot32::graphics::Renderer& renderer_;
    pixelroot32::graphics::PaletteContext* previous_;
};

}  // namespace chessdemo
