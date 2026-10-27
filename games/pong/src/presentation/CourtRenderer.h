/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include "assets/PongPalette.h"
#include "assets/PongSprites.h"
#include "game/PongRules.h"
#include "presentation/CourtLayout.h"

/**
 * @file CourtRenderer.h
 * @brief Draws the arcade court, its actors, the score band, the overlay and the
 *        title screen.
 *
 * The scene owns the match, the CPU and the selected level; this layer only
 * reads them. Every drawing call is stateless: it derives the viewport offset
 * from the renderer's logical size, paints itself, and leaves no renderer state
 * behind, so the scene can interleave the calls with `Scene::draw()` without an
 * offset leaking into the entity pass.
 *
 * Draw order is the scene's decision and the repo's rule: background first,
 * entities, then the actors this file owns, then the HUD, then the overlay. A
 * full-canvas fill placed after the HUD would erase it, which is why
 * `drawBackground()` paints the score band and the court as two separate
 * rectangles instead of one screen-sized one. The title screen is the one
 * exception to the actor order: it is drawn instead of them, so nothing of the
 * match shows through its panel.
 *
 * This is also the one place the palette lives: `installPalette()` binds the
 * generated 16-slot table, and nothing else in the demo names a colour for the
 * court.
 */
namespace pong::presentation {

// --- Palette slots -----------------------------------------------------------
// A `Color` is a palette index, not an RGB value: every name below is a slot in
// the generated `assets::kPongPalette`, which is why swapping the palette only
// meant regenerating that header.

/// Ink: scores, overlay text, and the ball's specular highlight.
inline constexpr pixelroot32::graphics::Color kInk = pixelroot32::graphics::Color::White;

/// Paper: the score band and the overlay band that carry the ink.
inline constexpr pixelroot32::graphics::Color kPaper = pixelroot32::graphics::Color::Black;

/// Court: the playfield fill behind the actors.
inline constexpr pixelroot32::graphics::Color kCourt = pixelroot32::graphics::Color::Navy;

/// Frame: the border, the dashed centre line and the dim phase word.
inline constexpr pixelroot32::graphics::Color kFrame = pixelroot32::graphics::Color::Gray;

/// The player's tag, and the body ink of its generated paddle.
inline constexpr pixelroot32::graphics::Color kP1Tint = pixelroot32::graphics::Color::Cyan;

/// The CPU tag and the rose paddle.
inline constexpr pixelroot32::graphics::Color kCpuTint = pixelroot32::graphics::Color::LightRed;

/**
 * @brief Binds the generated arcade palette to the renderer.
 *
 * Idempotent by pointer comparison, and called from every drawing entry point
 * rather than once at start-up: the engine stores the pointer it is given and
 * never copies the table, and a scene that is re-initialised (a restart, a
 * transition back) would otherwise silently fall back to the preset the scene's
 * `init()` installed.
 *
 * `setCustomPalette()` assigns both the background and the sprite palette and
 * leaves the engine in single-palette mode, which is deliberate: primitives
 * (the band, the frame, the dashes) and generated sprites must resolve through
 * the same table, or a paddle and the frame behind it would disagree.
 */
void installPalette();

/**
 * @struct CourtRect
 * @brief An axis-aligned rectangle in design-box pixels.
 *
 * Exposed so the effect layer can place a burst on exactly the rectangle the
 * court drew, instead of re-deriving paddle and ball geometry and slowly
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

// --- Sprite contract ---------------------------------------------------------
// A Sprite4bpp cannot be scaled at draw time, so the generated art has to be
// exactly the rectangle the rules hand the renderer. Checked here, where the art
// and the geometry meet, so regenerating a different-sized sprite fails the
// build instead of cropping the actors.
static_assert(assets::kPongBall.width == kBallSize && assets::kPongBall.height == kBallSize,
              "CourtRenderer: the generated ball must be kBallSize square");
static_assert(assets::kPongPaddle.width == kPaddleWidth &&
                  assets::kPongPaddle.height == kPaddleHeight,
              "CourtRenderer: the generated P1 paddle must be kPaddleWidth x kPaddleHeight");
static_assert(assets::kPongPaddleCpu.width == kPaddleWidth &&
                  assets::kPongPaddleCpu.height == kPaddleHeight,
              "CourtRenderer: the generated CPU paddle must be kPaddleWidth x kPaddleHeight");

/**
 * @brief Returns the play area the actors are clipped to.
 * @return Court rectangle of the playfield below the HUD band.
 */
[[nodiscard]] CourtRect playAreaRect();

/**
 * @brief Returns one paddle's rectangle at the current rules position.
 * @param rules Match to read.
 * @param player 0 for the player (left), 1 for the CPU (right); other values
 *        yield an empty rectangle.
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
 * Every rectangle is intersected with the play area before it is drawn, so an
 * actor can never bleed into the score band or past a court edge. With
 * `PIXELROOT32_ENABLE_4BPP_SPRITES` the actors are the generated sprites; a
 * `Sprite4bpp` has no per-draw clip, so an actor whose clip actually bit falls
 * back to a filled rectangle. That is the same rectangle the rules hand the
 * renderer, so the fallback is a colour-only difference, never a position one.
 * Without the flag the whole cast is those filled rectangles.
 */
void drawActors(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules);

/**
 * @brief Paints the score band: both tags with their scores and the phase word.
 * @param renderer Renderer to draw with.
 * @param rules Match to read the scores and the phase from.
 *
 * The band uses the engine's 5x7 default face: "P1" and "CPU" with one digit
 * each at scale 3, the phase word at scale 2. The player's group sits at the
 * left padding, the CPU's is mirrored against the right one so a score grows
 * inwards instead of off the panel, and the phase word is centred on the play
 * area between them. Everything is formatted into a fixed `char[8]`; nothing
 * here allocates.
 *
 * During Countdown the phase word is the number of seconds left (3, 2, 1) read
 * from `Rules::countdownRemainingMs()`, not the static "READY": the serve comes
 * to the tick, and the band is where the player sees it coming.
 */
void drawHud(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules);

/**
 * @brief Paints the centred overlay for the phase that needs one.
 * @param renderer Renderer to draw with.
 * @param rules Match to read the phase and the winner from.
 *
 * MatchOver draws the framed band with the winner on its first line ("P1 WINS"
 * or "CPU WINS", decided by the scores) and "PRESS A" on its second. Countdown,
 * Play and Point draw nothing: their state is already in the HUD phase word and
 * a band over the court during a rally would only hide the ball.
 */
void drawOverlay(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules);

/**
 * @brief Paints the title screen: the panel, the three level rows and the cursor.
 * @param renderer Renderer to draw with.
 * @param selection Index of the highlighted row: 0 for EASY, 1 for MEDIUM, 2 for
 *        HARD.
 *
 * A complete screen pass in the same sense as the other entry points: the caller
 * owns the background, so the court is painted first and this panel stands on it.
 * The panel is filled paper with the court's inset frame, the title and the three
 * level labels are centred in it, the selected row carries a filled frame band
 * with its label in paper, and the hint closes the panel underneath.
 *
 * `selection` is a menu cursor, not a level: any value outside the three rows is
 * clamped to the nearest one and the whole screen is still drawn. Nothing here
 * asserts, because a cursor read from a stale or saved value must not be able to
 * stop the demo before it shows anything.
 */
void drawMenu(pixelroot32::graphics::Renderer& renderer, std::uint8_t selection);

}  // namespace pong::presentation
