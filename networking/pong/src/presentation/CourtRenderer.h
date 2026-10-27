/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include "game/PongRules.h"
#include "presentation/CourtLayout.h"

/**
 * @file CourtRenderer.h
 * @brief Draws the arcade court, its actors, the score band and the overlays.
 *
 * The host scene owns the match and the network; this layer only reads it. All
 * four calls are stateless: they derive the viewport offset from the renderer's
 * logical size, paint themselves, and leave no renderer state behind, so the
 * scene can interleave them with `Scene::draw()` without an offset leaking into
 * the entity pass.
 *
 * Draw order is the scene's decision and the repo's rule: background first,
 * entities, then the actors this file owns, then the HUD, then the overlay. A
 * full-canvas fill placed after the HUD would erase it, which is why
 * `drawBackground()` paints the score band and the court as two separate
 * rectangles instead of one screen-sized one.
 *
 * This is also the one place the palette lives (S11). T7 replaces the
 * `PaletteType` and the slot constants below with the generated custom palette;
 * nothing else in the demo names a colour for the court.
 */
namespace netpong::presentation {

/// Palette the court is authored against.
///
/// PR32 is the engine default and the palette the `Color` enumerators are
/// indexed for, so the slots below read as their names until the generated
/// arcade palette of T7 replaces this constant.
inline constexpr pixelroot32::graphics::PaletteType kCourtPalette =
    pixelroot32::graphics::PaletteType::PR32;

// --- Palette slots -----------------------------------------------------------
// A `Color` is a palette index, not an RGB value: every name below is a slot in
// whatever palette is active, which is what makes the T7 swap a one-file change.

/// Ink: scores, paddles, ball, overlay text.
inline constexpr pixelroot32::graphics::Color kInk = pixelroot32::graphics::Color::White;

/// Paper: the score band and the overlay band that carry the ink.
inline constexpr pixelroot32::graphics::Color kPaper = pixelroot32::graphics::Color::Black;

/// Court: the playfield fill behind the actors.
inline constexpr pixelroot32::graphics::Color kCourt = pixelroot32::graphics::Color::Navy;

/// Frame: the border, the dashed centre line and the dim phase word.
inline constexpr pixelroot32::graphics::Color kFrame = pixelroot32::graphics::Color::Gray;

/// Player one's tag.
inline constexpr pixelroot32::graphics::Color kP1Tint = pixelroot32::graphics::Color::Cyan;

/// Player two's tag.
inline constexpr pixelroot32::graphics::Color kP2Tint = pixelroot32::graphics::Color::LightRed;

/**
 * @struct CourtRect
 * @brief An axis-aligned rectangle in design-box pixels.
 *
 * Exposed so the effect layer of T6 can place a burst on exactly the rectangle
 * the court drew, instead of re-deriving paddle and ball geometry and slowly
 * drifting away from it.
 */
struct CourtRect {
    int x = 0;  ///< Left edge in design-box pixels.
    int y = 0;  ///< Top edge in design-box pixels.
    int width = 0;  ///< Width in pixels.
    int height = 0;  ///< Height in pixels.

    /**
     * @brief Reports whether the rectangle covers no pixel.
     * @return true when either dimension is zero or negative.
     */
    [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
};

/**
 * @brief Returns the play area the actors are clipped to.
 * @return Court rectangle of the playfield below the HUD band.
 */
[[nodiscard]] CourtRect playAreaRect();

/**
 * @brief Returns one paddle's rectangle at the current rules position.
 * @param rules Match to read.
 * @param player 0 for P1 (left), 1 for P2 (right); other values yield an empty
 *        rectangle.
 * @return The paddle rectangle in design-box pixels, already clipped to the
 *         play area.
 *
 * The rules clamp both paddles to the play area, so the clip only bites on a
 * pathological value; keeping it here means the drawn paddle can never leave the
 * court even if that clamp changes.
 */
[[nodiscard]] CourtRect paddleRect(const game::Rules& rules, std::uint8_t player);

/**
 * @brief Returns the ball's rectangle at the current rules position.
 * @param rules Match to read.
 * @return The 6x6 ball rectangle in design-box pixels, already clipped to the
 *         play area.
 */
[[nodiscard]] CourtRect ballRect(const game::Rules& rules);

/**
 * @brief Paints the court: score band, playfield, border frame, dashed centre.
 * @param renderer Renderer to draw with.
 *
 * Deliberately two fills instead of one screen-sized one: the HUD band is
 * painted first as its own strip, so a later background refresh can never wipe
 * the scores, and the playfield fill starts below it.
 */
void drawBackground(pixelroot32::graphics::Renderer& renderer);

/**
 * @brief Paints both paddles and the ball from the rules positions.
 * @param renderer Renderer to draw with.
 * @param rules Match to read.
 *
 * Every rectangle is intersected with the play area before it is filled, so an
 * actor can never bleed into the score band or past a court edge.
 */
void drawActors(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules);

/**
 * @brief Paints the score band: tags, scores and the phase word.
 * @param renderer Renderer to draw with.
 * @param rules Match to read the scores and the phase from.
 * @param level How many controllers the host counts as ready (0..2). While
 *        waiting, a short count dims the phase word: it is the only place the
 *        band can say "still waiting for a board" without adding a second line
 *        that T8 would have to move.
 *
 * The band uses the engine's 5x7 default face: the tags and scores at scale 3,
 * the phase word at scale 2. Both scores and the phase word are formatted into
 * a fixed `char[8]`; nothing here allocates.
 */
void drawHud(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules,
             std::uint8_t level);

/**
 * @brief Paints the centred overlay for the phases that need a word.
 * @param renderer Renderer to draw with.
 * @param rules Match to read the phase and the winner from.
 *
 * WaitingForPlayers shows "WAITING FOR PLAYERS", Paused shows "PAUSED" and
 * MatchOver shows "P1 WINS" or "P2 WINS" by score. Countdown, Play and Point
 * draw nothing: their state is already in the HUD phase word and a band over
 * the court during a rally would only hide the ball.
 */
void drawOverlay(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules);

}  // namespace netpong::presentation
