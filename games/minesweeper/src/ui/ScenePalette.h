#pragma once

namespace minesweeper {

/**
 * @file ScenePalette.h
 * @brief Binds the generated Game Boy palette to the running scene.
 *
 * Engine-dependent on purpose (it pulls in the renderer's palette state), so
 * only the presentation layer includes it; the rules core stays engine-free.
 */

/**
 * @brief Installs the generated 16-slot palette for all later draws.
 *
 * Called once when the scene starts, not per frame: the engine stores the
 * pointer it is given and never copies the table, so a per-frame call would
 * only churn renderer state. The generated kMinesweeperPalette array has
 * static storage duration, which is exactly the lifetime the engine requires.
 */
void applyScenePalette();

}  // namespace minesweeper
