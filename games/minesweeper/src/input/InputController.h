#pragma once

#include <cstdint>

#include "input/InputBindings.h"

namespace minesweeper {

/**
 * @class InputController
 * @brief Turns a raw held-button mask into debounced game commands.
 *
 * Directions auto-repeat after REPEAT_DELAY_MS every REPEAT_INTERVAL_MS so a
 * held D-pad crosses the board; Reveal/Flag/Cancel fire only on fresh
 * presses so a held action button can never open or flag twice (spec
 * FR-10). The controller never asks whether a cell holds a mine: it only
 * shapes input.
 */
class InputController {
public:
    InputController();

    /** @brief Clears edges and repeat state; call on scene transitions. */
    void reset();

    /**
     * @brief Samples held buttons and emits this frame's commands.
     * @param heldMask Bit i set while physical button i is down.
     * @param nowMs Monotonic clock in milliseconds.
     * @return Bitmask of commandBit() values fired this frame.
     */
    [[nodiscard]] std::uint16_t poll(std::uint16_t heldMask, unsigned long nowMs);

private:
    std::uint16_t prevHeld_;
    unsigned long repeatAt_[4];
    bool repeating_[4];
    bool chordConsumed_;

    [[nodiscard]] std::uint16_t pollDirection(std::uint8_t dir, bool held, bool pressed,
                                             unsigned long nowMs);
};

}  // namespace minesweeper
