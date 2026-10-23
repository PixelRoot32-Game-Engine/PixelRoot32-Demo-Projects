#pragma once

#include <cstdint>

namespace spaceinvaders::rules {

/**
 * @file FireTiming.h
 * @brief Enemy reload ladder indexed by the score high byte (S9).
 */

/**
 * @brief Reload budget, in enemy-shot steps, for the current score.
 *
 * The ROM table is 48, 16, 11, 8, 7 selected at the high-byte thresholds
 * 0x02, 0x10, 0x20 and 0x30: a run starts slow and tightens as the score
 * grows. The unit is a shot step, not a tick, because the caller owns the
 * three-tick cadence of each step.
 *
 * @param scoreHighByte High byte of the current score.
 * @return Delay in shot steps.
 */
uint8_t enemyReloadTicks(uint16_t scoreHighByte);

}  // namespace spaceinvaders::rules
