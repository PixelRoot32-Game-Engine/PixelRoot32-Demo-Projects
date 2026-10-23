#pragma once

#include <cstdint>

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

/**
 * @class Saucer
 * @brief Mystery-ship timer, entry side and the 15-value score table (S11).
 *
 * The saucer is not on the field most of the time: a 1536-tick period has to
 * expire while at least eight aliens are still alive. Its side is decided by
 * the parity of the shot counter, and its score comes from a table that wraps
 * rather than from a random draw, so a fixed input script reproduces it (S20).
 */
class Saucer {
public:
    /**
     * @brief Returns the timer and the score table to their start-of-run state.
     */
    void reset();

    /**
     * @brief Advances the spawn period by one 60 Hz tick.
     */
    void tick();

    /**
     * @brief Reports whether a saucer may enter now.
     * @param aliveCount Live aliens in the rack.
     * @return true when the period expired and at least SaucerMinAliens are alive.
     */
    bool spawnDue(uint8_t aliveCount) const;

    /**
     * @brief Restarts the spawn period after a saucer has entered.
     */
    void onSpawned();

    /**
     * @brief Entry side chosen from the shot counter.
     * @param shotsFired Player shots fired so far this run.
     * @return +1 from the left, -1 from the right (even count enters left).
     */
    int8_t side(uint32_t shotsFired) const;

    /**
     * @brief Next score this saucer is worth, without consuming it.
     * @return Current table entry.
     */
    uint16_t currentScore() const;

    /**
     * @brief Returns the current score and advances the table.
     *
     * The table wraps after SaucerScoreCount entries, so a long run keeps
     * cycling the same fifteen values instead of running off the end.
     *
     * @return Score awarded for the hit that just landed.
     */
    uint16_t takeScore();

    /**
     * @brief Ticks elapsed since the last spawn or reset.
     * @return Timer value in 60 Hz ticks.
     */
    uint16_t timer() const;

private:
    uint16_t timerTicks = 0;
    uint8_t scoreIndex = 0;
};

}  // namespace spaceinvaders::rules
