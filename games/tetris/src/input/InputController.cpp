#include "input/InputController.h"

namespace tetris {

InputController::InputController() {
    reset();
}

void InputController::reset() {
    prevHeld_ = 0;
    chordConsumed_ = false;
    repeatAt_[0] = 0;
    repeatAt_[1] = 0;
}

std::uint16_t InputController::poll(std::uint16_t heldMask, unsigned long nowMs) {
    const std::uint16_t pressed = static_cast<std::uint16_t>(heldMask & ~prevHeld_);
    prevHeld_ = heldMask;

    const std::uint16_t bothActions =
        static_cast<std::uint16_t>((1U << BTN_A) | (1U << BTN_B));
    const bool chordHeld = (heldMask & bothActions) == bothActions;

    std::uint16_t out = 0;

    // A+B chord means Pause. It fires once per combined press and swallows
    // the individual edges, so pausing can never rotate by accident.
    if (chordHeld) {
        if (!chordConsumed_ && (pressed & bothActions) != 0) {
            out |= commandBit(GameCommand::Pause);
            chordConsumed_ = true;
        }
    } else {
        chordConsumed_ = false;
        if ((pressed & (1U << BTN_A)) != 0) {
            out |= commandBit(GameCommand::RotateCw) | commandBit(GameCommand::Confirm);
        }
        if ((pressed & (1U << BTN_B)) != 0) {
            out |= commandBit(GameCommand::RotateCcw);
        }
        if ((pressed & (1U << BTN_UP)) != 0) {
            out |= commandBit(GameCommand::HardDrop);
        }
    }

    const bool leftHeld = (heldMask & (1U << BTN_LEFT)) != 0;
    const bool rightHeld = (heldMask & (1U << BTN_RIGHT)) != 0;
    const bool leftEdge = (pressed & (1U << BTN_LEFT)) != 0;
    const bool rightEdge = (pressed & (1U << BTN_RIGHT)) != 0;
    // Opposite directions cancel: holding both holds the piece still.
    if (!(leftHeld && rightHeld)) {
        out |= pollDirection(0, leftHeld, leftEdge, nowMs);
        out |= pollDirection(1, rightHeld, rightEdge, nowMs);
    } else {
        repeatAt_[0] = 0;
        repeatAt_[1] = 0;
    }
    return out;
}

std::uint16_t InputController::pollDirection(std::uint8_t dir, bool held, bool pressed,
                                             unsigned long nowMs) {
    static const GameCommand commands[2] = {GameCommand::Left, GameCommand::Right};
    if (!held) {
        return 0;
    }
    if (pressed) {
        // Fresh press steps once and arms the DAS timer.
        repeatAt_[dir] = nowMs + DAS_DELAY_MS;
        return commandBit(commands[dir]);
    }
    if (nowMs >= repeatAt_[dir]) {
        repeatAt_[dir] = nowMs + DAS_INTERVAL_MS;
        return commandBit(commands[dir]);
    }
    return 0;
}

}  // namespace tetris
