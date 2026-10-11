#pragma once

#include <cstdint>

#include <platforms/EngineConfig.h>

namespace tetris {

/**
 * @file TetrisConstants.h
 * @brief Display layout and scene tuning for the 240x240 demo.
 *
 * Engine-dependent on purpose: only the presentation layer includes it.
 * The rules core (src/game/) and input (src/input/) stay engine-free.
 */

// --- Layout (240x240 logical display) ----------------------------------------
constexpr int HUD_HEIGHT = 52;
constexpr int BOARD_TOP = HUD_HEIGHT + 2;
constexpr int BOARD_AREA_HEIGHT = LOGICAL_HEIGHT - BOARD_TOP - 2;
constexpr int SIDE_PANEL_X = 0;  // computed at runtime from board geometry
constexpr int HUD_LINE_1_Y = 4;
constexpr int HUD_LINE_2_Y = 18;
constexpr int HUD_LINE_3_Y = 32;
constexpr int HUD_DIVIDER_Y = HUD_HEIGHT - 2;

// --- Result / pause banners ---------------------------------------------------
constexpr int BANNER_WIDTH = 200;
constexpr int BANNER_HEIGHT = 84;

// --- Input behaviour ----------------------------------------------------------
constexpr unsigned long GAME_OVER_LOCK_MS = 600;

/** Idle time on the title screen before the AI attract-mode demo starts. */
constexpr unsigned long TITLE_ATTRACT_DELAY_MS = 15000;

/** Interval between two AI actions (rotate / step / drop) during the demo. */
constexpr unsigned long AI_STEP_MS = 150;

/**
 * @brief Largest integer cell size that keeps the well plus side panel on screen.
 * @param boardPixelW Board width in cells (10).
 * @param boardPixelH Board height in cells (20).
 * @param availW Pixels available for board + panel.
 * @param availH Pixels available vertically.
 * @return Cell edge in pixels.
 */
constexpr int cellSizeFor(int boardCellsW, int boardCellsH, int availW, int availH) {
    const int fitW = availW / boardCellsW;
    const int fitH = availH / boardCellsH;
    const int fit = (fitW < fitH) ? fitW : fitH;
    return (fit < 1) ? 1 : fit;
}

}  // namespace tetris
