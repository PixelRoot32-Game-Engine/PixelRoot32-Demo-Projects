#pragma once

#include <cstdint>

#include <platforms/EngineConfig.h>

namespace minesweeper {

/**
 * @file GameConstants.h
 * @brief Display layout and scene tuning for the 240x240 demo.
 *
 * Engine-dependent on purpose: only the presentation layer includes it.
 * The rules core (src/game/) and input (src/input/) stay engine-free.
 */

// --- Layout (240x240 logical display) ----------------------------------------
constexpr int HUD_HEIGHT = 44;
constexpr int BOARD_TOP = 46;
constexpr int BOARD_AREA_HEIGHT = LOGICAL_HEIGHT - BOARD_TOP - 2;
constexpr int HUD_LINE_1_Y = 4;
constexpr int HUD_LINE_2_Y = 18;
constexpr int HUD_HINT_Y = 30;
constexpr int HUD_DIVIDER_Y = 43;

// --- Result banner ------------------------------------------------------------
constexpr int RESULT_WIDTH = 200;
constexpr int RESULT_HEIGHT = 72;

// --- Input behaviour ----------------------------------------------------------
constexpr unsigned long GAME_OVER_LOCK_MS = 600;

/**
 * @brief Largest integer cell size that keeps the board on screen.
 * @param width Board columns.
 * @param height Board rows.
 * @return Cell edge in pixels (EASY 21, MEDIUM 12, EXPERT 8).
 */
constexpr std::uint8_t cellSizeFor(std::uint8_t width, std::uint8_t height) {
    const std::uint8_t fitW = static_cast<std::uint8_t>(LOGICAL_WIDTH / width);
    const std::uint8_t fitH = static_cast<std::uint8_t>(BOARD_AREA_HEIGHT / height);
    return (fitW < fitH) ? fitW : fitH;
}

/**
 * @brief Glyph size that fits inside a cell.
 * @param cellSize Cell edge in pixels.
 * @return 2 for roomy cells, 1 for compact ones (default font is 5x7).
 */
constexpr std::uint8_t cellTextSize(std::uint8_t cellSize) {
    return (cellSize >= 18) ? 2 : 1;
}

}  // namespace minesweeper
