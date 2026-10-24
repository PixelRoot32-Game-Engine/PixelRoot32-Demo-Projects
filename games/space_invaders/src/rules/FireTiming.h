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

/**
 * @class EnemyFireTimer
 * @brief Reload countdown for enemy fire, measured in shot steps (S9).
 *
 * The ROM does not fire on a tick count. It charges a budget of enemy-shot
 * steps and spends one step every time the shots take a step, so the reload
 * survives a change of cadence: the caller owns the three-tick clock and this
 * object only counts whole steps. Keeping the unit in shot steps is what makes
 * "48" mean the same thing at 60 Hz as it did on the arcade board.
 */
class EnemyFireTimer {
public:
    /**
     * @brief Loads the budget of a score band and latches the byte.
     *
     * The latched band is what the next reloads read, so the caller re-arms
     * this when the score climbs into a tighter band.
     *
     * @param scoreHighByte High byte of the current score.
     */
    void reset(uint16_t scoreHighByte);

    /**
     * @brief Spends one enemy-shot step of the budget.
     *
     * Call once per shot step whether or not a slot is free: the reference
     * keeps charging while all three shots are in flight, so a full rack of
     * shots must not pause the countdown. The step that reaches zero is the
     * shot, and it reloads on the spot so the next gap is a whole budget.
     *
     * @return true on the step a shot may be fired.
     */
    bool step();

    /**
     * @brief Steps left before the next shot.
     * @return Remaining budget in shot steps; 0 only on the firing step.
     */
    uint8_t remaining() const { return budget; }

private:
    uint16_t band = 0;   ///< Score high byte the budget was loaded for.
    uint8_t budget = 0;  ///< Shot steps left before the next shot.
};

}  // namespace spaceinvaders::rules
