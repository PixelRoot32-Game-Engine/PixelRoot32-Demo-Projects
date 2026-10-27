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
 * @brief Draws the arcade court, its actors, the score band and the overlays.
 *
 * The host scene owns the match and the network; this layer only reads it. All
 * four drawing calls are stateless: they derive the viewport offset from the
 * renderer's logical size, paint themselves, and leave no renderer state behind,
 * so the scene can interleave them with `Scene::draw()` without an offset
 * leaking into the entity pass.
 *
 * Draw order is the scene's decision and the repo's rule: background first,
 * entities, then the actors this file owns, then the HUD, then the overlay. A
 * full-canvas fill placed after the HUD would erase it, which is why
 * `drawBackground()` paints the score band and the court as two separate
 * rectangles instead of one screen-sized one.
 *
 * This is also the one place the palette lives (S11): `installPalette()` binds
 * the generated 16-slot table, and nothing else in the demo names a colour for
 * the court.
 */
namespace netpong::presentation {

/// Blue/gold arcade look that a `PaletteType` cannot express; the table itself
/// is generated into `assets::kPongPalette`.

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

/// Player one's tag, and the body ink of its generated paddle.
inline constexpr pixelroot32::graphics::Color kP1Tint = pixelroot32::graphics::Color::Cyan;

/// Player two's tag, and the body ink of its generated paddle.
inline constexpr pixelroot32::graphics::Color kP2Tint = pixelroot32::graphics::Color::LightRed;

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
 * @struct NetStatus
 * @brief The link facts the diagnostics panel prints.
 *
 * Filled by the scene from `PongHost`'s public getters and handed to
 * `drawOverlay()`, so the renderer stays engine-free and cannot reach into a
 * session itself. Every field is a plain value; the struct allocates nothing.
 */
struct NetStatus {
    /// Wi-Fi channel every node must share.
    std::uint8_t channel = 0;
    /// One token per player slot: bound, online AND speaking this build's
    /// protocol version. False renders as "--".
    bool slotOk[2] = {false, false};
    /// Newest reliable round trip in milliseconds, 0 before the first ACK.
    std::uint16_t lastRttMs = 0;
    /// Frames the host session had to drop; the demo's contract is zero.
    std::uint32_t rxDropped = 0;
    /// Reliable frames still waiting for an ACK.
    std::uint8_t pendingCount = 0;
};

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

// --- Sprite contract ---------------------------------------------------------
// A Sprite4bpp cannot be scaled at draw time, so the generated art has to be
// exactly the rectangle the rules hand the renderer. Checked here, where the
// art and the geometry meet, so regenerating a different-sized sprite fails the
// build instead of cropping the actors.
static_assert(assets::kPongBall.width == kBallSize && assets::kPongBall.height == kBallSize,
              "CourtRenderer: the generated ball must be kBallSize square");
static_assert(assets::kPongPaddle.width == kPaddleWidth &&
                  assets::kPongPaddle.height == kPaddleHeight,
              "CourtRenderer: the generated P1 paddle must be kPaddleWidth x kPaddleHeight");
static_assert(assets::kPongPaddleP2.width == kPaddleWidth &&
                  assets::kPongPaddleP2.height == kPaddleHeight,
              "CourtRenderer: the generated P2 paddle must be kPaddleWidth x kPaddleHeight");

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
 *
 * During Countdown the phase word is the number of seconds left (3, 2, 1) read
 * from `Rules::countdownRemainingMs()`, not the static "READY": the host screen
 * is the only place a player can see the serve coming to the tick, while the
 * controllers keep their READY word (S10, no wire change).
 */
void drawHud(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules,
             std::uint8_t level);

/**
 * @brief Paints the centred overlay for the phases that need a word.
 * @param renderer Renderer to draw with.
 * @param rules Match to read the phase and the winner from.
 * @param net Link facts for the diagnostics panel.
 *
 * WaitingForPlayers and Paused show the diagnostics panel: the channel, one
 * token per controller slot, the last RTT, the session's drop counter and its
 * reliable queue depth. Those are the two phases where nothing is moving and a
 * player needs to know why. MatchOver shows "P1 WINS" or "P2 WINS" by score.
 * Countdown, Play and Point draw nothing: their state is already in the HUD
 * phase word and a band over the court during a rally would only hide the ball.
 */
void drawOverlay(pixelroot32::graphics::Renderer& renderer, const game::Rules& rules,
                 const NetStatus& net);

}  // namespace netpong::presentation
