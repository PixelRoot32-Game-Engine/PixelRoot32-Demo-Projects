#include "input/InputController.h"

namespace minesweeper {

InputController::InputController() {
    reset();
}

void InputController::reset() {
    prevHeld_ = 0;
    chordConsumed_ = false;
    for (std::uint8_t i = 0; i < 4; ++i) {
        repeatAt_[i] = 0;
        repeating_[i] = false;
    }
}

std::uint16_t InputController::poll(std::uint16_t heldMask, unsigned long nowMs) {
    const std::uint16_t pressed = static_cast<std::uint16_t>(heldMask & ~prevHeld_);
    prevHeld_ = heldMask;

    const std::uint16_t bothActions =
        static_cast<std::uint16_t>((1U << BTN_A) | (1U << BTN_B));
    const bool chordHeld = (heldMask & bothActions) == bothActions;

    std::uint16_t out = 0;

    // A+B chord means Cancel (back to menu). It fires once per combined
    // press and swallows the individual edges, so asking for the menu can
    // never reveal a cell or plant a flag by accident.
    if (chordHeld) {
        if (!chordConsumed_ && (pressed & bothActions) != 0) {
            out |= commandBit(GameCommand::Cancel);
            chordConsumed_ = true;
        }
    } else {
        chordConsumed_ = false;
        if ((pressed & (1U << BTN_A)) != 0) {
            out |= commandBit(GameCommand::Reveal);
        }
        if ((pressed & (1U << BTN_B)) != 0) {
            out |= commandBit(GameCommand::ToggleFlag);
        }
    }

    for (std::uint8_t dir = 0; dir < 4; ++dir) {
        const bool held = (heldMask & (1U << dir)) != 0;
        const bool edge = (pressed & (1U << dir)) != 0;
        out |= pollDirection(dir, held, edge, nowMs);
    }
    return out;
}

std::uint16_t InputController::pollDirection(std::uint8_t dir, bool held, bool pressed,
                                             unsigned long nowMs) {
    static const GameCommand commands[4] = {
        GameCommand::Up, GameCommand::Down, GameCommand::Left, GameCommand::Right};
    if (!held) {
        repeating_[dir] = false;
        return 0;
    }
    if (pressed) {
        // Fresh press moves one cell and arms the auto-repeat timer.
        repeating_[dir] = false;
        repeatAt_[dir] = nowMs + REPEAT_DELAY_MS;
        return commandBit(commands[dir]);
    }
    if (nowMs >= repeatAt_[dir]) {
        // Held past the delay: step again and re-arm the interval.
        repeating_[dir] = true;
        repeatAt_[dir] = nowMs + REPEAT_INTERVAL_MS;
        return commandBit(commands[dir]);
    }
    return 0;
}

}  // namespace minesweeper
