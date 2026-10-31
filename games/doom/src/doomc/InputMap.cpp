#include "doomc/InputMap.h"

#include <cstring>

namespace doom {
namespace doomc {

p_ticcmd_t mapButtons(std::uint8_t buttons) {
    p_ticcmd_t cmd;
    std::memset(&cmd, 0, sizeof cmd);

    const bool up = (buttons & kButtonUp) != 0u;
    const bool down = (buttons & kButtonDown) != 0u;
    const bool left = (buttons & kButtonLeft) != 0u;
    const bool right = (buttons & kButtonRight) != 0u;
    const bool attack = (buttons & kButtonA) != 0u;
    const bool run = (buttons & kButtonB) != 0u;

    // Forward and back cancel, as upstream's `forward += ...` / `forward -= ...`
    // pair does when both keys are held.
    if (up != down) {
        cmd.forwardmove = up ? kForwardWalk : static_cast<std::int8_t>(-kForwardWalk);
        // B is the run modifier only in the forward direction: a backpedal has no
        // second speed upstream, and a strafe does not exist yet.
        if (up && run) {
            cmd.forwardmove = kForwardRun;
        }
    }

    // The same cancellation on the turn axis, and upstream's sign: left is
    // positive (counter-clockwise) in DOOM's binary-angle space.
    if (left != right) {
        cmd.angleturn = left ? kTurnStep : static_cast<std::int16_t>(-kTurnStep);
    }

    if (attack) {
        cmd.buttons = static_cast<std::uint8_t>(cmd.buttons | BT_ATTACK);
    }

    // B alone is the use button. With a direction held it was the run modifier
    // (or inert), and P_PlayerThink's usedown latch would otherwise turn every
    // held run into a stream of door opens.
    const bool directionHeld = up || down || left || right;
    if (run && !directionHeld) {
        cmd.buttons = static_cast<std::uint8_t>(cmd.buttons | BT_USE);
    }

    return cmd;
}

}  // namespace doomc
}  // namespace doom
