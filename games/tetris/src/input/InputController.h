#pragma once

#include <cstdint>

#include "input/InputBindings.h"

namespace tetris {

/**
 * @class InputController
 * @brief Turns a raw held-button mask into debounced game commands.
 *
 * Left/Right auto-repeat with DAS timing so a held direction walks the
 * piece across the well; Rotate/HardDrop/Confirm/Pause fire on fresh edges
 * only so holding a button never double-rotates or double-drops. The A+B
 * chord means Pause and swallows the individual A/B edges, so pausing can
 * never rotate the piece by accident. Soft drop is a level read straight
 * from the held mask (softDropHeld()), not a pulsed command.
 */
class InputController {
public:
    InputController();

    /** @brief Clears edges and repeat state; call on state transitions. */
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
    unsigned long repeatAt_[2];
    bool chordConsumed_;

    [[nodiscard]] std::uint16_t pollDirection(std::uint8_t dir, bool held, bool pressed,
                                             unsigned long nowMs);
};

}  // namespace tetris
