/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include "game/PongRules.h"

/**
 * @file CourtLayout.h
 * @brief The design contract of the arcade court.
 *
 * The presentation is authored on one design box whose width is the rules court
 * width, so every coordinate in the court, the HUD and the overlays is written
 * once and no drawing code has to ask which panel it is running on. There is one
 * canvas shape per build: `PONG_COURT_WIDTH` picks 240 (the square ST7789 panel
 * and the native window that mirrors it) or 320 (the ILI9341 panel after its
 * quarter turn), and `game::kCourtWidth` is the same macro, so the design box and
 * the simulation can never disagree. `viewOffset()` is the single translation
 * from design pixels to renderer pixels.
 *
 * The header is deliberately engine-free (it only pulls the rules core) so the
 * geometry can be asserted without a renderer, and so the numbers the effect
 * layer places particles on are the same numbers the court draws with.
 *
 * Everything here is in design-box pixels, with the origin at the top-left of
 * the design box. `game::Fixed` positions are converted to whole pixels by the
 * renderer, never here.
 */
namespace pong::presentation {

// --- Design box --------------------------------------------------------------

/// Width of the design canvas in pixels: the rules court width of this build.
inline constexpr int kDesignWidth = game::kCourtWidth;

/// Height of the design canvas in pixels: both panels are 240 tall.
inline constexpr int kDesignHeight = game::kCourtHeight;

/// Height of the HUD score band; the play area starts below it.
inline constexpr int kHudHeight = game::kHudHeight;

// --- Play area ---------------------------------------------------------------
// The court is everything below the score band. The band is not part of the
// play area on purpose: the ball and the paddles are clipped to this rectangle,
// so a stray value can never be drawn over the scores.

/// Left edge of the play area (the court has no side margin).
inline constexpr int kPlayX = 0;

/// Top edge of the play area, immediately under the score band.
inline constexpr int kPlayY = kHudHeight;

/// Width of the play area.
inline constexpr int kPlayWidth = kDesignWidth;

/// Height of the play area.
inline constexpr int kPlayHeight = kDesignHeight - kHudHeight;

/// Bottom edge of the play area, exclusive.
inline constexpr int kPlayBottom = kPlayY + kPlayHeight;

/// Horizontal centre of the court: the dashed line and the serve both use it.
inline constexpr int kCourtCentreX = kDesignWidth / 2;

// --- Court furniture ---------------------------------------------------------

/// Thickness of the border frame drawn just inside the play area edges.
inline constexpr int kFrameThickness = 2;

/// Width of the dashed centre line.
inline constexpr int kCentreLineWidth = 2;

/// Length of one dash of the centre line.
inline constexpr int kCentreDashLength = 6;

/// Gap between two dashes of the centre line.
inline constexpr int kCentreDashGap = 6;

/// Distance the centre line keeps from the top and bottom edges.
inline constexpr int kCentreLineInset = 6;

// --- Actor geometry ----------------------------------------------------------
// Aliases, not copies: the rules own the simulation geometry, and a renderer
// that re-derived a paddle width could disagree with the collision that uses it.

/// Paddle width in pixels.
inline constexpr int kPaddleWidth = game::kPaddleWidth;

/// Paddle height in pixels.
inline constexpr int kPaddleHeight = game::kPaddleHeight;

/// Left edge of the player's paddle.
inline constexpr int kPaddleLeftX = game::kPaddleLeftX;

/// Left edge of the CPU's paddle.
inline constexpr int kPaddleRightX = game::kPaddleRightX;

/// Ball collision radius in pixels.
inline constexpr int kBallRadius = game::kBallRadius;

/// Ball side in pixels; the ball is drawn as a square of this size.
inline constexpr int kBallSize = 2 * kBallRadius;

// --- Text metrics ------------------------------------------------------------
// The HUD is authored against the engine's default 5x7 face (five pixel glyphs,
// one pixel of spacing). Horizontal measurement in the renderer goes through
// FontManager so the drawn width and the measured width cannot drift; these
// three numbers only place the bands vertically and prove the groups do not
// collide.

/// Glyph width of the engine's default font.
inline constexpr int kGlyphWidth = 5;

/// Glyph height of the engine's default font.
inline constexpr int kGlyphHeight = 7;

/// Spacing between two glyphs of the engine's default font.
inline constexpr int kGlyphSpacing = 1;

/// Advance of one character cell, in pixels at scale 1.
inline constexpr int kGlyphAdvance = kGlyphWidth + kGlyphSpacing;

// --- HUD score band ----------------------------------------------------------

/// Distance from each band edge to its score group.
inline constexpr int kHudPadX = 8;

/// Gap between a side's tag and its score.
inline constexpr int kHudGroupGap = 8;

/// Text size of the tags and the scores; 3 x 7 = 21 px inside a 24 px band.
inline constexpr int kHudScoreScale = 3;

/// Text size of the phase word.
inline constexpr int kHudPhaseScale = 2;

/// Vertical offset of the score row inside the band: (24 - 21) / 2, so the
/// 21 px face sits optically centred with one pixel above and two below.
inline constexpr int kHudScoreY = 1;

/// Vertical offset of the phase word inside the band: (24 - 14) / 2.
inline constexpr int kHudPhaseY = 5;

/// Characters in the player's tag ("P1").
inline constexpr int kHudLeftTagCells = 2;

/// Characters in the CPU's tag ("CPU").
inline constexpr int kHudRightTagCells = 3;

/// Characters reserved for one score; kWinScore is a single digit.
inline constexpr int kHudScoreCells = 1;

/// Drawn width of the player's tag.
inline constexpr int kHudLeftTagWidth =
    kHudLeftTagCells * kGlyphAdvance * kHudScoreScale - kGlyphSpacing * kHudScoreScale;

/// Drawn width of the CPU's tag.
inline constexpr int kHudRightTagWidth =
    kHudRightTagCells * kGlyphAdvance * kHudScoreScale - kGlyphSpacing * kHudScoreScale;

/// Drawn width of a score.
inline constexpr int kHudScoreWidth =
    kHudScoreCells * kGlyphAdvance * kHudScoreScale - kGlyphSpacing * kHudScoreScale;

/// Drawn width of the left tag-plus-score group.
inline constexpr int kHudLeftGroupWidth =
    kHudLeftTagWidth + kHudGroupGap + kHudScoreWidth;

/// Drawn width of the right tag-plus-score group.
inline constexpr int kHudRightGroupWidth =
    kHudRightTagWidth + kHudGroupGap + kHudScoreWidth;

/// Characters in the longest phase word ("POINT", "READY").
inline constexpr int kHudPhaseCells = 5;

/// Drawn width of the longest phase word.
inline constexpr int kHudPhaseWidth =
    kHudPhaseCells * kGlyphAdvance * kHudPhaseScale - kGlyphSpacing * kHudPhaseScale;

// --- Centred overlay ---------------------------------------------------------
// One band serves the match-over screen: the winner on its first line and the
// restart hint on its second. It is sized for the longest line, "CPU WINS", at
// scale 2.

/// Characters in the longest overlay line ("CPU WINS").
inline constexpr int kOverlayLongestChars = 8;

/// Width of the overlay band.
inline constexpr int kOverlayWidth = 224;

/// Height of the overlay band.
inline constexpr int kOverlayHeight = 44;

/// Text size inside the overlay band.
inline constexpr int kOverlayScale = 2;

/// Left edge of the overlay band, centred on the court.
inline constexpr int kOverlayX = (kDesignWidth - kOverlayWidth) / 2;

/// Top edge of the overlay band, centred in the play area.
inline constexpr int kOverlayY = kPlayY + (kPlayHeight - kOverlayHeight) / 2;

/// Top edge of the first line inside the band.
inline constexpr int kOverlayLine1Y = kOverlayY + 8;

/// Top edge of the second line: one line height plus a four pixel gap below the
/// first, so the pair sits inside the band with the frame still clear.
inline constexpr int kOverlayLine2Y =
    kOverlayLine1Y + kGlyphHeight * kOverlayScale + 4;

// --- Contract checks ---------------------------------------------------------
// A composition that no longer fits would draw off-panel or over the scores
// instead of failing loudly, so the geometry is checked against the box here,
// for both canvases a build can select.

static_assert(kDesignWidth == game::kCourtWidth && kDesignHeight == game::kCourtHeight,
              "CourtLayout: the design box must stay equal to the rules court");
static_assert(kHudHeight == game::kHudHeight,
              "CourtLayout: the HUD band must stay equal to the rules play-area inset");
static_assert(kHudHeight > 0 && kHudHeight < kDesignHeight,
              "CourtLayout: the HUD band must leave a play area");
static_assert(kPlayHeight == game::kCourtHeight - game::kHudHeight,
              "CourtLayout: the play area is the court minus the HUD band");
static_assert(kCourtCentreX * 2 == kDesignWidth,
              "CourtLayout: the centre line and the serve assume an even court width");

// The two paddles and the ball are the whole moving cast, so the design box has
// to hold them: x 8 and width - 14, 6 x 44 paddles, and a ball of radius 3.
static_assert(kPaddleLeftX >= kPlayX && kPaddleLeftX + kPaddleWidth <= kPlayX + kPlayWidth,
              "CourtLayout: the left paddle must fit inside the play area");
static_assert(kPaddleRightX >= kPlayX &&
                  kPaddleRightX + kPaddleWidth <= kPlayX + kPlayWidth,
              "CourtLayout: the right paddle must fit inside the play area");
static_assert(kPaddleLeftX + kPaddleWidth < kPaddleRightX,
              "CourtLayout: the two paddles must not meet");
static_assert(kPaddleHeight <= kPlayHeight,
              "CourtLayout: a 44 px paddle must fit the play area height");
static_assert(kBallSize > 0 && kBallSize <= kPlayWidth && kBallSize <= kPlayHeight,
              "CourtLayout: a radius-3 ball must fit the play area");

// The parked ball sits at the serve position (court centre, 132); a clip that
// cut it at rest would be wrong before the first rally starts.
static_assert(game::kBallCenterX / game::kOne >= kPlayX + kBallRadius &&
                  game::kBallCenterX / game::kOne <= kPlayX + kPlayWidth - 1 - kBallRadius,
              "CourtLayout: the serve position must be inside the play area horizontally");
static_assert(game::kBallCenterY / game::kOne >= kPlayY + kBallRadius &&
                  game::kBallCenterY / game::kOne <= kPlayBottom - 1 - kBallRadius,
              "CourtLayout: the serve position must be inside the play area vertically");

// Furniture: a frame thicker than the court, or a dash that cannot repeat,
// would silently draw nothing.
static_assert(2 * kFrameThickness < kPlayWidth && 2 * kFrameThickness < kPlayHeight,
              "CourtLayout: the border frame must leave an interior");
static_assert(kCentreLineInset + kCentreDashLength <= kPlayHeight / 2,
              "CourtLayout: the dashed centre line needs room for one dash per half");

// HUD: two tag-plus-score groups and the phase word share one 24 px strip, so
// the measured widths must leave the centre free for the longest phase word.
static_assert(kHudPadX + kHudLeftGroupWidth < kCourtCentreX - kHudPhaseWidth / 2,
              "CourtLayout: the left score group must clear the phase word");
static_assert(kDesignWidth - kHudPadX - kHudRightGroupWidth >
                  kCourtCentreX + kHudPhaseWidth / 2,
              "CourtLayout: the right score group must clear the phase word");
static_assert(game::kWinScore < 10,
              "CourtLayout: the HUD reserves one digit per score, so kWinScore must stay a single digit");

// Overlay: the band is centred in the play area and must hold its longest line
// on both axes, with the second line still inside the frame.
static_assert(kOverlayX >= kPlayX && kOverlayX + kOverlayWidth <= kPlayX + kPlayWidth,
              "CourtLayout: the overlay band must fit the court width");
static_assert(kOverlayY >= kPlayY && kOverlayY + kOverlayHeight <= kPlayBottom,
              "CourtLayout: the overlay band must fit the play area height");
static_assert(kOverlayWidth >=
                  kOverlayLongestChars * kGlyphAdvance * kOverlayScale -
                      kGlyphSpacing * kOverlayScale,
              "CourtLayout: the longest overlay line must fit its band");
static_assert(kOverlayLine2Y + kGlyphHeight * kOverlayScale <= kOverlayY + kOverlayHeight,
              "CourtLayout: the second overlay line must stay inside the band");

/**
 * @struct ViewOffset
 * @brief Translation from design-box pixels to renderer pixels.
 *
 * Signed on purpose: a canvas narrower than the design box yields a negative
 * offset, which centres the box and lets the renderer clip its overflow instead
 * of pretending the composition fits.
 */
struct ViewOffset {
    int x = 0;  ///< Pixels added to every design-box X.
    int y = 0;  ///< Pixels added to every design-box Y.
};

/**
 * @brief Centres the design box on a renderer's logical canvas.
 * @param logicalWidth Width reported by `Renderer::getLogicalWidth()`.
 * @param logicalHeight Height reported by `Renderer::getLogicalHeight()`.
 * @return Offset to add to every design-box coordinate.
 *
 * Both device canvases agree with the design box -- the square panel is 240x240
 * and the rotated panel is 320x240, exactly the court width each build asks for
 * -- so this returns (0, 0). The arithmetic is still real so a future canvas
 * does not need every coordinate edited.
 */
[[nodiscard]] constexpr ViewOffset viewOffset(int logicalWidth, int logicalHeight) {
    return ViewOffset{(logicalWidth - kDesignWidth) / 2, (logicalHeight - kDesignHeight) / 2};
}

}  // namespace pong::presentation
