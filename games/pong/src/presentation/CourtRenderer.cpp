/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "presentation/CourtRenderer.h"

#include <algorithm>
#include <cstdio>
#include <string_view>

#include <graphics/FontManager.h>

#include "presentation/MenuLayout.h"

namespace pong::presentation {
namespace {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::FontManager;
using pixelroot32::graphics::Renderer;
#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES
using pixelroot32::graphics::Sprite4bpp;
#endif

/// Converts a Q8.8 rules value to whole pixels.
///
/// The shift is the one the rules layer uses for its own geometry, so a rounding
/// difference of one pixel between a collision and the rectangle drawn over it
/// cannot appear.
int toPixel(game::Fixed value) { return static_cast<int>(value >> game::kFixedShift); }

/**
 * @brief Intersects a rectangle with the play area.
 * @param rect Rectangle in design-box pixels.
 * @return The visible part, or an empty rectangle when they do not overlap.
 */
CourtRect clipToPlay(const CourtRect& rect) {
    const int left = std::max(rect.x, kPlayX);
    const int top = std::max(rect.y, kPlayY);
    const int right = std::min(rect.x + rect.width, kPlayX + kPlayWidth);
    const int bottom = std::min(rect.y + rect.height, kPlayBottom);
    if (right <= left || bottom <= top) {
        return CourtRect{};
    }
    return CourtRect{left, top, right - left, bottom - top};
}

#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES
/**
 * @brief Blits one actor sprite when the play-area clip left it whole.
 * @param renderer Renderer to draw with.
 * @param sprite Generated sprite for the actor.
 * @param rect Clipped rectangle the rules and the court agree on.
 * @param origin Viewport offset of the design box.
 * @param flipX True to mirror; the CPU's paddle uses it so its light leading
 *        edge faces the centre of the court.
 * @return true when the sprite was drawn, false when the caller must fall back.
 *
 * `Renderer::drawSprite()` clips to the canvas, not to the play area, so blitting
 * a clipped actor would paint over the score band. The clip only bites on a
 * pathological rules value, which is why the rectangle fallback is enough: it is
 * the same rectangle, drawn in one ink instead of four.
 */
bool drawWholeSprite(Renderer& renderer, const Sprite4bpp& sprite, const CourtRect& rect,
                     const ViewOffset& origin, bool flipX) {
    if (rect.width != sprite.width || rect.height != sprite.height) {
        return false;
    }
    renderer.drawSprite(sprite, origin.x + rect.x, origin.y + rect.y, 0, flipX);
    return true;
}
#endif

/**
 * @brief Measures text in the engine's default face.
 * @param text NUL-terminated string to measure.
 * @param scale Text size multiplier.
 * @return Drawn width in pixels, trailing spacing excluded.
 */
int textWidth(const char* text, int scale) {
    return static_cast<int>(
        FontManager::textWidth(nullptr, std::string_view(text), static_cast<std::uint8_t>(scale)));
}

/**
 * @brief Draws one line centred inside a design-box column.
 * @param renderer Renderer to draw with.
 * @param origin Viewport offset of the design box.
 * @param text NUL-terminated string.
 * @param y Top edge of the line, in design-box pixels.
 * @param color Ink for the line.
 * @param scale Text size multiplier.
 * @param boxX Left edge of the column to centre in, in design-box pixels.
 * @param boxWidth Width of that column.
 *
 * `Renderer::drawTextCentered()` centres on the logical canvas and knows nothing
 * about the design box, so it is not usable here: a wider canvas would move the
 * court's centre away from the canvas centre.
 */
void drawCentered(Renderer& renderer, const ViewOffset& origin, const char* text, int y,
                  Color color, int scale, int boxX, int boxWidth) {
    const int width = textWidth(text, scale);
    renderer.drawText(text, static_cast<int16_t>(origin.x + boxX + (boxWidth - width) / 2),
                      static_cast<int16_t>(origin.y + y), color,
                      static_cast<std::uint8_t>(scale));
}

/**
 * @brief Returns the word the score band shows for a phase.
 * @param phase Current match phase.
 * @return Upper-case word for the 5x7 face; never nullptr.
 *
 * Countdown is the one phase whose word is a number, so `drawHud()` overrides it
 * from the rules' live timer instead of returning a word here.
 */
const char* phaseWord(game::Phase phase) {
    switch (phase) {
        case game::Phase::Countdown:
            return "READY";
        case game::Phase::Play:
            return "GO";
        case game::Phase::Point:
            return "POINT";
        case game::Phase::MatchOver:
            return "OVER";
    }
    // Unreachable for a well-formed Phase; an empty word is better than a
    // stale one if a future enumerator is added without a case here.
    return "";
}

/**
 * @brief Draws the framed paper band the match-over lines sit in.
 * @param renderer Renderer to draw with.
 * @param origin Viewport offset of the design box.
 * @param line1 Winner line, centred on the band's first row.
 * @param line2 Continue hint, centred on the band's second row.
 */
void drawOverlayBand(Renderer& renderer, const ViewOffset& origin, const char* line1,
                     const char* line2) {
    // Paper first, then an inset frame: the band has to read even when the
    // court behind it resolves to the same palette slot.
    renderer.drawFilledRectangle(origin.x + kOverlayX, origin.y + kOverlayY, kOverlayWidth,
                                 kOverlayHeight, kPaper);
    for (int inset = 0; inset < kFrameThickness; ++inset) {
        renderer.drawRectangle(origin.x + kOverlayX + inset, origin.y + kOverlayY + inset,
                               kOverlayWidth - 2 * inset, kOverlayHeight - 2 * inset, kFrame);
    }

    drawCentered(renderer, origin, line1, kOverlayLine1Y, kInk, kOverlayScale, kOverlayX,
                 kOverlayWidth);
    drawCentered(renderer, origin, line2, kOverlayLine2Y, kInk, kOverlayScale, kOverlayX,
                 kOverlayWidth);
}

// The title screen's labels, in the menu's row order: EASY, MEDIUM, HARD, which
// is the order the level enum declares so a row index is the level index.
const char* const kMenuRowLabels[] = {"EASY", "MEDIUM", "HARD"};

static_assert(static_cast<int>(sizeof(kMenuRowLabels) / sizeof(kMenuRowLabels[0])) ==
                  kMenuRowCount,
              "CourtRenderer: the title screen needs one label per level row");

/// Milliseconds each countdown digit is shown. kCountdownMs splits evenly into
/// three, so the HUD reads 3, 2, 1 and never shows a stale 0.
constexpr std::uint32_t kCountdownDigitMs = game::kCountdownMs / 3;

static_assert(game::kCountdownMs % 3 == 0,
              "CourtRenderer: the countdown must split into three whole digits");

/**
 * @brief Formats the countdown digit for the score band.
 * @param rules Match whose countdown timer is read.
 * @param buffer Destination, at least 2 bytes.
 * @return `buffer`, holding "3", "2" or "1".
 */
const char* countdownWord(const game::Rules& rules, char* buffer) {
    const std::uint32_t remainingMs = rules.countdownRemainingMs();
    // Ceiling division: the first moments of the countdown still read 3.
    std::uint32_t digit = (remainingMs + kCountdownDigitMs - 1) / kCountdownDigitMs;
    if (digit < 1) {
        digit = 1;
    } else if (digit > 3) {
        digit = 3;
    }
    std::snprintf(buffer, 2, "%u", static_cast<unsigned>(digit));
    return buffer;
}

}  // namespace

void installPalette() {
    // Both banks are compared, not just the sprite one: setCustomPalette()
    // assigns them together, so a call that put either half back on a preset
    // (a scene re-init, a sibling demo's palette install) has to be repaired
    // before the next primitive resolves. Two pointer reads per entry point is
    // cheaper than a per-frame table copy, and the engine never copies it.
    if (pixelroot32::graphics::getBackgroundPaletteSlot(0) == assets::kPongPalette &&
        pixelroot32::graphics::getSpritePaletteSlot(0) == assets::kPongPalette) {
        return;
    }
    pixelroot32::graphics::setCustomPalette(assets::kPongPalette);
}

CourtRect playAreaRect() { return CourtRect{kPlayX, kPlayY, kPlayWidth, kPlayHeight}; }

CourtRect paddleRect(const game::Rules& rules, std::uint8_t player) {
    if (player > 1) {
        return CourtRect{};
    }
    const int x = (player == 0) ? kPaddleLeftX : kPaddleRightX;
    return clipToPlay(CourtRect{x, toPixel(rules.paddleY(player)), kPaddleWidth, kPaddleHeight});
}

CourtRect ballRect(const game::Rules& rules) {
    return clipToPlay(CourtRect{toPixel(rules.ballX()) - kBallRadius,
                                toPixel(rules.ballY()) - kBallRadius, kBallSize, kBallSize});
}

void drawBackground(Renderer& renderer) {
    // First entry point of a frame, and the one caller that paints the band and
    // the court: binding the palette here keeps the install ahead of every
    // primitive in the pass (see installPalette()).
    installPalette();

    const ViewOffset origin = viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    // Two fills, never one screen-sized one: the score band is painted as its
    // own strip and the playfield starts below it, so a background refresh can
    // never erase the HUD drawn later in the frame.
    renderer.drawFilledRectangle(origin.x, origin.y, kDesignWidth, kHudHeight, kPaper);
    renderer.drawFilledRectangle(origin.x + kPlayX, origin.y + kPlayY, kPlayWidth, kPlayHeight,
                                 kCourt);

    // The border is drawn from the play area inwards, one outline per pixel of
    // thickness, so it frames the court without ever touching the score band.
    for (int inset = 0; inset < kFrameThickness; ++inset) {
        renderer.drawRectangle(origin.x + kPlayX + inset, origin.y + kPlayY + inset,
                               kPlayWidth - 2 * inset, kPlayHeight - 2 * inset, kFrame);
    }

    // Dashed centre line: a fixed 6 on / 6 off walk that stops before the last
    // incomplete dash instead of drawing a ragged stub at the bottom edge.
    const int lineX = origin.x + kCourtCentreX - kCentreLineWidth / 2;
    const int lastY = kPlayBottom - kCentreLineInset;
    for (int y = kPlayY + kCentreLineInset; y + kCentreDashLength <= lastY;
         y += kCentreDashLength + kCentreDashGap) {
        renderer.drawFilledRectangle(lineX, origin.y + y, kCentreLineWidth, kCentreDashLength,
                                     kFrame);
    }
}

void drawActors(Renderer& renderer, const game::Rules& rules) {
    installPalette();

    const ViewOffset origin = viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    for (std::uint8_t player = 0; player < 2; ++player) {
        const CourtRect paddle = paddleRect(rules, player);
        if (paddle.empty()) {
            continue;
        }
#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES
        // Same bevelled bar, different ink set; the CPU's paddle is mirrored at
        // draw time so its light leading edge faces the centre of the court.
        const Sprite4bpp& sprite =
            (player == 0) ? assets::kPongPaddle : assets::kPongPaddleCpu;
        if (drawWholeSprite(renderer, sprite, paddle, origin, player != 0)) {
            continue;
        }
#endif
        renderer.drawFilledRectangle(origin.x + paddle.x, origin.y + paddle.y, paddle.width,
                                     paddle.height, kInk);
    }

    const CourtRect ball = ballRect(rules);
    if (ball.empty()) {
        return;
    }
#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES
    if (drawWholeSprite(renderer, assets::kPongBall, ball, origin, false)) {
        return;
    }
#endif
    renderer.drawFilledRectangle(origin.x + ball.x, origin.y + ball.y, ball.width, ball.height,
                                 kInk);
}

void drawHud(Renderer& renderer, const game::Rules& rules) {
    installPalette();

    const ViewOffset origin = viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());
    const auto scoreScale = static_cast<std::uint8_t>(kHudScoreScale);

    char score[8];
    char countdown[2];

    // The player: tag at the padding, score immediately to its right.
    std::snprintf(score, sizeof(score), "%u", static_cast<unsigned>(rules.score(0)));
    renderer.drawText("P1", static_cast<int16_t>(origin.x + kHudPadX),
                      static_cast<int16_t>(origin.y + kHudScoreY), kP1Tint, scoreScale);
    renderer.drawText(score,
                      static_cast<int16_t>(origin.x + kHudPadX + kHudLeftTagWidth + kHudGroupGap),
                      static_cast<int16_t>(origin.y + kHudScoreY), kInk, scoreScale);

    // The CPU is mirrored against the right edge: the score keeps the padding
    // and the tag follows it leftwards, so a wider score grows inwards instead
    // of off the panel.
    std::snprintf(score, sizeof(score), "%u", static_cast<unsigned>(rules.score(1)));
    const int cpuScoreX = origin.x + kDesignWidth - kHudPadX - textWidth(score, kHudScoreScale);
    const int cpuTagX = cpuScoreX - kHudGroupGap - kHudRightTagWidth;
    renderer.drawText("CPU", static_cast<int16_t>(cpuTagX),
                      static_cast<int16_t>(origin.y + kHudScoreY), kCpuTint, scoreScale);
    renderer.drawText(score, static_cast<int16_t>(cpuScoreX),
                      static_cast<int16_t>(origin.y + kHudScoreY), kInk, scoreScale);

    const char* word = phaseWord(rules.phase());
    if (rules.phase() == game::Phase::Countdown) {
        // The band counts the serve in, so it reads the live timer rather than
        // the static word: the number is derived locally, on every frame.
        word = countdownWord(rules, countdown);
    }
    drawCentered(renderer, origin, word, kHudPhaseY, kInk, kHudPhaseScale, kPlayX, kPlayWidth);
}

void drawOverlay(Renderer& renderer, const game::Rules& rules) {
    installPalette();

    if (rules.phase() != game::Phase::MatchOver) {
        // No band over a live court: the phase word in the HUD already says what
        // is happening, and a band would cover the ball.
        return;
    }

    const ViewOffset origin = viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    // A match ends when a side reaches kWinScore, so the scores can never tie
    // here; comparing them is still the truthful source.
    drawOverlayBand(renderer, origin, (rules.score(0) > rules.score(1)) ? "P1 WINS" : "CPU WINS",
                    "PRESS A");
}

void drawMenu(Renderer& renderer, std::uint8_t selection) {
    installPalette();

    const ViewOffset origin = viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    // Paper panel first, then the same inset frame the match-over band wears:
    // the panel has to read as its own surface even where the court behind it
    // resolves to the same palette slot.
    renderer.drawFilledRectangle(origin.x + kMenuPanelX, origin.y + kMenuPanelY, kMenuPanelWidth,
                                 kMenuPanelHeight, kPaper);
    for (int inset = 0; inset < kMenuBorder; ++inset) {
        renderer.drawRectangle(origin.x + kMenuPanelX + inset, origin.y + kMenuPanelY + inset,
                               kMenuPanelWidth - 2 * inset, kMenuPanelHeight - 2 * inset, kFrame);
    }

    // The title sits in the same column as the rows, so the whole panel has one
    // left and right margin instead of three.
    drawCentered(renderer, origin, "SELECT LEVEL", kMenuTitleY, kInk, kMenuTitleScale, kMenuRowX,
                 kMenuRowWidth);

    // A cursor past the last row is pulled back to it instead of being rejected:
    // the screen has to be whole even if the index arrived from somewhere else.
    const int selected = std::clamp(static_cast<int>(selection), 0, kMenuRowCount - 1);

    for (int row = 0; row < kMenuRowCount; ++row) {
        const int rowY = kMenuFirstRowY + row * kMenuRowPitch;
        const bool isSelected = row == selected;

        // The band goes under the label: frame ink behind paper text, so the
        // selected row inverts against the paper panel around it.
        if (isSelected) {
            renderer.drawFilledRectangle(origin.x + kMenuRowX,
                                         origin.y + rowY + kMenuHighlightYOffset, kMenuRowWidth,
                                         kMenuHighlightHeight, kFrame);
        }
        drawCentered(renderer, origin, kMenuRowLabels[row], rowY, isSelected ? kPaper : kInk,
                     kMenuRowScale, kMenuRowX, kMenuRowWidth);
    }

    drawCentered(renderer, origin, "UP DOWN A", kMenuHintY, kFrame, kMenuHintScale, kMenuPanelX,
                 kMenuPanelWidth);
}

}  // namespace pong::presentation
