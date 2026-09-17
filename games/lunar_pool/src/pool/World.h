/*
 * World.h - Ball simulation state and the move/capture/friction substeps.
 *
 * This slice implements only pipeline steps 1 (move), 2 (capture) and 5
 * (friction/rest); ball-ball and cushion resolution (steps 3-4) are added by
 * later slices, so substep() runs straight from capture to friction for now.
 * Each step is also exposed directly so it can be tested in isolation (e.g.
 * a drift test needs friction out of the way).
 */
#pragma once

#include <cstdint>

#include "pool/Fixed.h"
#include "pool/Table.h"

namespace pool {

/** One simulated ball. Default-constructed is inactive (never placed). */
struct Ball {
    int32_t x = 0;        ///< Position, raw units (1/256 px).
    int32_t y = 0;
    int32_t vx = 0;        ///< Velocity, raw units per second.
    int32_t vy = 0;
    int16_t remX = 0;      ///< Sub-unit position carry so move() never drifts.
    int16_t remY = 0;
    uint8_t number = 0;    ///< 0 = cue ball.
    bool active = false;   ///< False once captured, or never placed.
};

/** Balls captured during the current shot, in fall order. */
struct PocketLog {
    uint8_t ball[kMaxBalls]{};  ///< Ball indices, in capture order.
    uint8_t count = 0;
};

/** Ball simulation state for one table. See file header for pipeline scope. */
class World {
public:
    /** @brief Sets one ball's position/velocity directly; number = index (see TableDef.h's index-order convention). */
    void placeBall(uint8_t index, int32_t x, int32_t y, int32_t vx, int32_t vy);

    /** @brief Places every ball from a loaded table's starting positions, all at rest, and clears the pocket log. */
    void placeFromTable(const Table& table);

    /** @brief Marks a ball captured directly: inactive, zero velocity/carry, appended to the log. Also used by captureNear(). */
    void capture(uint8_t index);

    /** @brief Clears the pocket log, e.g. when a new shot begins. */
    void clearLog();

    /** @brief Pipeline step 1: drift-free integration for every active ball. */
    void move();

    /** @brief Pipeline step 2: captures balls index-ascending whose center lies strictly inside a pocket's radius, so the log ends up ball-number ordered (TableDef.h's index-order convention) with no extra sort. */
    void captureNear(const Table& table);

    /** @brief Pipeline step 5: reduces every active ball's speed by kFrictionPerSubstepRaw along its direction of motion, zeroing it at or below that amount. */
    void friction();

    /** @brief Advances one 1/240 s substep: move(), captureNear(), friction() only (see file header). */
    void substep(const Table& table);

    [[nodiscard]] const Ball& ball(uint8_t index) const { return balls_[index]; }
    [[nodiscard]] uint8_t ballCount() const { return ballCount_; }
    [[nodiscard]] const PocketLog& pocketLog() const { return log_; }

private:
    Ball balls_[kMaxBalls]{};
    uint8_t ballCount_ = 0;
    PocketLog log_{};
};

}  // namespace pool
