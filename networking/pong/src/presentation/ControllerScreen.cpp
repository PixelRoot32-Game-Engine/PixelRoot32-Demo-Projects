/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "presentation/ControllerScreen.h"

#include <cstdio>
#include <cstring>

namespace netpong {

namespace {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Renderer;

/// Built-in font cell at scale 1. The panel layout below is expressed in
/// cells, so a font change moves every band together instead of overlapping.
constexpr int kGlyphWidth = 6;
constexpr int kGlyphHeight = 8;

/// Top line: tag on the left, link/version on the right.
constexpr int kHeaderY = 0;

/// Big score. Size 3 gives 18x24 per glyph; "N  -  M" is seven glyphs, 126 px,
/// which is why the scores are clamped to one digit below: the single set ends
/// at five, and a corrupt peer value must never widen the line past the panel.
constexpr int kScoreY = kHeaderY + kGlyphHeight + 2;
constexpr std::uint8_t kScoreScale = 3;
constexpr std::uint8_t kMaxDisplayedScore = 9;

/// Phase word under the score.
constexpr int kPhaseY = kScoreY + kGlyphHeight * kScoreScale + 2;

/// Ball marker: a 3x3 block on a line of its own, clear of the phase word.
constexpr int kBallY = kPhaseY + kGlyphHeight + 1;
constexpr int kBallMarker = 3;

/// Paddle track: a framed horizontal run representing the paddle's vertical
/// travel, with a filled marker for where this board predicts it is.
constexpr int kTrackY = kBallY + kBallMarker + 2;
constexpr int kTrackHeight = 8;
constexpr int kMarkerWidth = 8;

/// Side margin shared by every band.
constexpr int kMarginX = 2;

/// Paddle-centre limits of the court's play area, in whole pixels. These mirror
/// game::kHudHeight / kCourtHeight rather than re-deriving them from the panel,
/// because the marker must report the real paddle position, not a stretched one.
constexpr int kPaddleCentreMin = game::kHudHeight + game::kPaddleHeight / 2;
constexpr int kPaddleCentreMax = game::kCourtHeight - game::kPaddleHeight / 2;

static_assert(kPaddleCentreMax > kPaddleCentreMin,
              "the paddle track needs a non-empty play area");

/// @brief Clamps an integer into [low, high], with low winning when inverted.
int clampInt(int value, int low, int high) {
    if (low > high) {
        return low;
    }
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/// @brief Maps the match phase to the four-to-five letter word the panel shows.
const char* phaseLabel(std::uint8_t phase) {
    switch (phase) {
        case net::kPhaseWaitingForPlayers:
            return "WAIT";
        case net::kPhaseCountdown:
            return "READY";
        case net::kPhasePlay:
            return "PLAY";
        case net::kPhasePoint:
            return "POINT";
        case net::kPhaseMatchOver:
            return "MATCH";
        case net::kPhasePaused:
            return "PAUSE";
        default:
            return "?";
    }
}

}  // namespace

bool ScreenKey::operator==(const ScreenKey& other) const {
    return score1 == other.score1 && score2 == other.score2 &&
           phase == other.phase && hostOnline == other.hostOnline &&
           myPaddleY4 == other.myPaddleY4 && ballX8 == other.ballX8 &&
           versionOk == other.versionOk;
}

ScreenKey makeKey(const ControllerView& view) {
    ScreenKey key;
    key.score1 = view.score1;
    key.score2 = view.score2;
    key.phase = view.phase;
    key.hostOnline = view.hostOnline;
    key.myPaddleY4 = static_cast<std::int16_t>(view.myPaddleY / 4);
    key.ballX8 = static_cast<std::int16_t>(view.ballX / 8);
    key.versionOk = view.versionOk;
    return key;
}

void drawControllerScreen(Renderer& renderer, const ControllerView& view,
                          std::uint8_t playerIndex) {
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();
    if (width <= 2 * kMarginX || height <= 0) {
        return;
    }

    // A 1-bit panel: U8G2_Drawer collapses every other colour, so the scene
    // never asks for more than ink.
    const Color ink = Color::White;

    // Top line: who this board is, and whether the host is reachable with a
    // compatible protocol version. Both live on one line because a player with
    // a dead controller needs both answers at a glance.
    const char* tag = (playerIndex == 1) ? "P2" : "P1";
    renderer.drawText(tag, static_cast<std::int16_t>(kMarginX),
                      static_cast<std::int16_t>(kHeaderY), ink, 1);

    char status[16];
    std::snprintf(status, sizeof(status), "%s %s",
                  view.hostOnline ? "LINK" : "NO LNK",
                  view.versionOk ? "V1" : "V?");
    // Right-align: "NO LNK V?" is nine cells wide, and a fixed column would
    // either clip it or float mid-row on a narrower canvas.
    const int statusWidth =
        static_cast<int>(std::strlen(status)) * kGlyphWidth;
    const int statusX = width - kMarginX - statusWidth;
    renderer.drawText(status, static_cast<std::int16_t>(clampInt(statusX, kMarginX, width)),
                      static_cast<std::int16_t>(kHeaderY), ink, 1);

    // Score. The two spaces around the dash are what make the size-3 line
    // readable from a metre away; the clamp keeps it inside the panel.
    if (height >= kScoreY + kGlyphHeight * kScoreScale) {
        const std::uint8_t shown1 = (view.score1 > kMaxDisplayedScore)
                                        ? kMaxDisplayedScore
                                        : view.score1;
        const std::uint8_t shown2 = (view.score2 > kMaxDisplayedScore)
                                        ? kMaxDisplayedScore
                                        : view.score2;
        char score[16];
        std::snprintf(score, sizeof(score), "%u  -  %u",
                      static_cast<unsigned>(shown1),
                      static_cast<unsigned>(shown2));
        renderer.drawTextCentered(score, static_cast<std::int16_t>(kScoreY), ink,
                                  kScoreScale);
    }

    if (height >= kPhaseY + kGlyphHeight) {
        renderer.drawTextCentered(phaseLabel(view.phase),
                                  static_cast<std::int16_t>(kPhaseY), ink, 1);
    }

    // Ball marker: horizontal court position only. The panel has no room for a
    // court, so the ball is an indicator of where the rally is across the
    // table, not a second miniature game.
    if (height >= kBallY + kBallMarker) {
        const int courtX = clampInt(static_cast<int>(view.ballX), 0,
                                    game::kCourtWidth);
        const int usable = width - 2 * kMarginX - kBallMarker;
        const int ballX = kMarginX + (courtX * usable) / game::kCourtWidth;
        renderer.drawFilledRectangle(ballX, kBallY, kBallMarker, kBallMarker,
                                     ink);
    }

    // Paddle track. The frame is drawn even before the first STATE, so a board
    // waiting for the host shows where its paddle will appear instead of an
    // empty panel that reads as a hung program.
    const int trackWidth = width - 2 * kMarginX;
    if (trackWidth >= 2 * (kMarkerWidth + 2) &&
        height >= kTrackY + kTrackHeight) {
        renderer.drawRectangle(kMarginX, kTrackY, trackWidth, kTrackHeight,
                               ink);

        const int innerLeft = kMarginX + 2;
        const int innerRight = width - kMarginX - 2;
        const int span = innerRight - innerLeft - kMarkerWidth;
        const int centre = clampInt(static_cast<int>(view.myPaddleY),
                                    kPaddleCentreMin, kPaddleCentreMax);
        const int markerX =
            innerLeft + ((centre - kPaddleCentreMin) * span) /
                            (kPaddleCentreMax - kPaddleCentreMin);
        renderer.drawFilledRectangle(markerX, kTrackY + 2, kMarkerWidth,
                                     kTrackHeight - 4, ink);
    }
}

}  // namespace netpong
