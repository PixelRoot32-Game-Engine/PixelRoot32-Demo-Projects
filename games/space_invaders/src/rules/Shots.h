#pragma once

#include <cstdint>

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

/**
 * @file Shots.h
 * @brief Projectile motion and the enemy-shot speed switch (S8, S9).
 */

/**
 * @brief Advances the player shot one tick.
 *
 * The player shot is the only projectile that always moves: four pixels per
 * 60 Hz tick, which crosses the playfield in about a second (F10).
 *
 * @param y Current top edge of the shot.
 * @return New top edge, four pixels higher.
 */
int16_t advancePlayerShot(int16_t y);

/**
 * @brief Reports whether enemy fire has switched to its fast step.
 *
 * The reference tightens the enemy shot once fewer than nine aliens remain.
 *
 * @param aliveCount Live aliens in the rack.
 * @return true when the fast step applies.
 */
bool enemyShotIsFast(uint8_t aliveCount);

/**
 * @class EnemyShotMotion
 * @brief Enemy shot descent on the three-tick step budget (S9).
 *
 * An enemy shot does not move every tick: it falls four pixels (five once
 * fewer than nine aliens remain) on every third advance() call and reports an
 * unchanged position on the other two. Keeping the phase inside the object is
 * what lets a test drive the cadence without a timer.
 */
class EnemyShotMotion {
public:
    /**
     * @brief Places the shot and restarts its three-tick phase.
     * @param y Initial top edge.
     */
    void reset(int16_t y);

    /**
     * @brief Advances the three-tick phase once.
     * @param fast true to use the five-pixel step.
     * @return Current top edge after the call.
     */
    int16_t advance(bool fast);

    /**
     * @brief Lands one whole shot step immediately.
     *
     * The scene owns the three-tick clock now (S9), so it hands the motion one
     * call per shot step instead of one per tick. The step size stays in this
     * class, and the phase restarts at zero so a later advance() caller still
     * sees the same cadence it always had.
     *
     * @param fast true to use the five-pixel step.
     * @return New top edge after the step.
     */
    int16_t step(bool fast);

    /**
     * @brief Current top edge.
     * @return Top edge in playfield pixels.
     */
    int16_t y() const;

private:
    int16_t position = 0;
    uint8_t tickPhase = 0;
};

}  // namespace spaceinvaders::rules
