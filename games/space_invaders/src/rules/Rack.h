#pragma once

#include <cstdint>

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

/**
 * @struct RackCell
 * @brief One alien slot: its current top-left position, liveness and variant.
 *
 * Dead cells keep their last position so a caller can draw the explosion where
 * the alien died without a second lookup table.
 */
struct RackCell {
    int16_t x = 0;                    ///< Top-left x in playfield pixels.
    int16_t y = 0;                    ///< Top-left y in playfield pixels.
    bool alive = false;               ///< False once the alien is destroyed.
    AlienType type = AlienType::Squid;  ///< Variant, fixed for the run.
};

/**
 * @struct RackTick
 * @brief What one 60 Hz tick did to the formation.
 *
 * The flags are separate because the caller reacts differently: a completed
 * sweep may play a step sound, an edge turn drops the rack, and the frame flip
 * is a draw concern only.
 */
struct RackTick {
    bool movedAlien = false;      ///< Exactly one live cell shifted this tick.
    bool sweepCompleted = false;  ///< The cursor visited the last live cell.
    bool turnedAtEdge = false;    ///< Direction flipped and the rack dropped.
    bool frameFlipped = false;    ///< The two-frame sprite animation advanced.
};

/**
 * @brief Spawn origin y for a wave (S12).
 *
 * Wave 1 starts at the reference origin and every later wave starts one edge
 * drop lower, which is the same 8 px the rack takes when it turns at an edge
 * (S5): the reference does not distinguish a wave offset from an edge drop.
 *
 * The drop saturates at the deepest formation that still fits inside the field.
 * A wave starting any lower is one no run can reach, because the invasion check
 * (S13) ends it on its first tick, so the function returns a real formation
 * position instead of accumulating off the panel.
 *
 * @param wave Wave number, 1-based; any other value is treated as wave 1.
 * @return Formation top in playfield pixels, RackOriginY..RackMaxOriginY.
 */
int16_t rackOriginYForWave(uint16_t wave);

/**
 * @class Rack
 * @brief The 5 x 11 formation and its one-alien-per-tick sweep (S4-S6).
 *
 * The reference CPU does not move the formation on a timer. It moves one live
 * alien per interrupt and treats the visit to the last live cell as the end of
 * the sweep, so the march interval equals the live alien count and accelerates
 * by itself as aliens die. This class reproduces that clock and nothing else:
 * no rendering, no audio, no allocation.
 */
class Rack {
public:
    /**
     * @brief Builds the full formation and resets the sweep state.
     *
     * Every cell is alive, typed by row, and placed on the 16 px pitch grid.
     * Direction, animation frame, cursor and hold are returned to their
     * start-of-wave values so a new wave can reuse the same object.
     *
     * @param originX Playfield x of the top-left cell.
     * @param originY Playfield y of the top-left cell.
     */
    void spawn(int16_t originX, int16_t originY);

    /**
     * @brief Advances the sweep by one live alien.
     *
     * While a hold is counting down the tick only decrements it. Otherwise the
     * scan resumes at the cursor, moves the next live cell by
     * `direction * (aliveCount()==1 ? RackLastStepX : RackStepX)` and, when
     * that cell was the last live one in row-major order, closes the sweep:
     * the frame flips, the cursor resets, and the rack reverses and drops
     * `RackDropY` if the next step would leave `[RackMinLeft, RackMaxRight]`.
     *
     * @return Flags describing what the tick did.
     */
    RackTick tick();

    /**
     * @brief Freezes the rack for a number of ticks.
     *
     * Used while an alien explosion is on screen (S5); the sweep resumes
     * exactly where it stopped.
     *
     * @param ticks Number of ticks to hold, consumed one per tick().
     */
    void hold(uint16_t ticks);

    /**
     * @brief Destroys one alien.
     * @param row Rack row, 0-based from the top.
     * @param col Rack column, 0-based from the left.
     */
    void kill(int row, int col);

    /**
     * @brief Reports whether any alien is still alive.
     * @return true when at least one cell is alive.
     */
    bool anyAlive() const;

    /**
     * @brief Counts the live aliens.
     *
     * This is also the sweep length, so callers must not cache it across kills.
     *
     * @return Number of live cells, 0..55.
     */
    int aliveCount() const;

    /**
     * @brief Current two-frame animation index.
     * @return 0 or 1, flipped once per completed sweep.
     */
    int animationFrame() const;

    /**
     * @brief Accesses one cell.
     * @param row Rack row, 0-based from the top.
     * @param col Rack column, 0-based from the left.
     * @return Mutable reference to the cell.
     */
    RackCell& cell(int row, int col);

    /**
     * @brief Accesses one cell without allowing mutation.
     * @param row Rack row, 0-based from the top.
     * @param col Rack column, 0-based from the left.
     * @return Const reference to the cell.
     */
    const RackCell& cell(int row, int col) const;

    /**
     * @brief Bottom edge of the lowest live alien.
     *
     * The invasion check (S13) compares this against the cannon baseline, so
     * the sprite height of each variant participates.
     *
     * @return Largest `y + alienHeight(type)` over live cells, 0 when empty.
     */
    int16_t lowestAlienBottom() const;

    /**
     * @brief Moves the whole formation down by a fixed delta.
     *
     * Wave advance restarts the rack eight pixels lower (S12); this keeps the
     * offset out of spawn() so the caller owns the accumulated drop.
     *
     * @param dy Vertical delta in pixels, positive downwards.
     */
    void shiftDown(int16_t dy);

    /**
     * @brief Current horizontal march direction.
     * @return +1 to the right, -1 to the left.
     */
    int direction() const;

private:
    RackCell cells[RackRows][RackCols];
    uint8_t animFrame = 0;
    uint8_t cursor = 0;
    uint16_t holdTicks = 0;
    int8_t marchDirection = 1;
};

}  // namespace spaceinvaders::rules
