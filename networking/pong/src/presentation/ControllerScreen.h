/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <graphics/Renderer.h>

#include "net/PongController.h"

/**
 * @file ControllerScreen.h
 * @brief The 128x64 controller scoreboard: redraw key and drawing.
 *
 * S10 asks for score, phase, player label and a paddle bar, and it asks for the
 * panel to be repainted only when something visible changed, at most ten times
 * a second. A full 128x64 frame over I2C blocks for about 23 ms, so an
 * unconditional redraw would eat nearly a quarter of the frame budget.
 *
 * This file therefore splits the panel in two:
 *
 *  - `ScreenKey` is a plain value, quantised to what the panel can actually
 *    show (a quarter of a pixel of paddle travel, eight pixels of ball travel).
 *    The scene compares it once per frame and skips the whole draw when it did
 *    not move, which is the same pattern the ESP-NOW dice demo uses.
 *  - `drawControllerScreen()` paints the whole 128x64 canvas from the view. It
 *    is deliberately stateless: a skipped frame leaves the previous pixels on
 *    the panel, and the next accepted frame repaints everything.
 *
 * The key carries no Renderer, no session and no engine type, so the visible
 * contract stays testable and the drawing is the only engine dependency.
 */
namespace netpong {

/**
 * @struct ScreenKey
 * @brief Quantised picture of everything the controller panel draws.
 *
 * The quantisation is the point: the paddle marker is an 8-pixel block on a
 * 112-pixel track, so a 4-pixel change of the underlying paddle is the
 * smallest move that can alter a pixel; the ball marker is 3 pixels wide on a
 * 121-pixel run, so 8 court pixels is the smallest meaningful step. Coarser
 * keys mean fewer I2C frames for movement the panel cannot show anyway.
 */
struct ScreenKey {
    std::uint8_t score1 = 0;  ///< Player one's points.
    std::uint8_t score2 = 0;  ///< Player two's points.
    std::uint8_t phase = 0;   ///< One kPhase* value.
    bool hostOnline = false;  ///< LINK / NO LNK.
    std::int16_t myPaddleY4 = 0;  ///< Local paddle centre in quarter pixels.
    std::int16_t ballX8 = 0;      ///< Ball centre X in eight-pixel steps.
    bool versionOk = false;   ///< V1 / V?.

    /**
     * @brief Compares every visible field.
     * @param other Key to compare against.
     * @return true when the two keys would produce identical pixels.
     *
     * Field-by-field instead of memcmp: the struct has padding, and padding
     * bytes are not initialised, so a byte comparison could force a redraw for
     * a picture that did not change.
     */
    bool operator==(const ScreenKey& other) const;

    /**
     * @brief Compares every visible field.
     * @param other Key to compare against.
     * @return true when at least one visible field differs.
     */
    bool operator!=(const ScreenKey& other) const { return !(*this == other); }
};

/**
 * @brief Samples the visible fields of a controller view.
 * @param view View to quantise.
 * @return Key that changes exactly when the panel would.
 */
[[nodiscard]] ScreenKey makeKey(const ControllerView& view);

/**
 * @brief Paints the whole controller scoreboard.
 * @param renderer Renderer whose logical canvas is the panel (128x64 on the C3).
 * @param view Board picture to draw.
 * @param playerIndex 0 for P1, 1 for P2; picks the top-left tag.
 *
 * The layout is derived from `getLogicalWidth()`/`getLogicalHeight()`, and each
 * band is skipped when the canvas is too short to hold it, so a differently
 * sized simulator window never paints outside the framebuffer.
 */
void drawControllerScreen(pixelroot32::graphics::Renderer& renderer,
                          const ControllerView& view, std::uint8_t playerIndex);

}  // namespace netpong
