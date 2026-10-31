#pragma once

#include <cstdint>

#include "p_user.h"

namespace doom {
namespace doomc {

/*
 * The engine's six button bits, in the engine's `InputConfig` order.
 *
 * The order is the whole contract and it is stated once, here: index 0 is up
 * and index 5 is B. `DoomScene` samples the input manager in this order, packs
 * the bits in this order, paints the on-screen slots in this order and prints
 * them in this order, so there is no table that can fall out of step.
 */
constexpr std::uint8_t kButtonUp = 1u << 0;
constexpr std::uint8_t kButtonDown = 1u << 1;
constexpr std::uint8_t kButtonLeft = 1u << 2;
constexpr std::uint8_t kButtonRight = 1u << 3;
constexpr std::uint8_t kButtonA = 1u << 4;
constexpr std::uint8_t kButtonB = 1u << 5;

/// G_BuildTiccmd's `forwardmove[]`: the walk and the run speed, signed 8-bit.
constexpr std::int8_t kForwardWalk = 0x19;
constexpr std::int8_t kForwardRun = 0x32;

/// G_BuildTiccmd's `angleturn[0]`: upstream's slowest turn step, in 1/65536 turn.
/// Positive turns left (counter-clockwise), which is upstream's `key_left` sign.
constexpr std::int16_t kTurnStep = 640;

/**
 * @brief One frame's held buttons as the world's tic command (D4.2f).
 *
 * Pure: no state, no clock, no engine, no allocation. The same bits always
 * produce the same command, which is what lets a host test pin the table below
 * and the panel trust it.
 *
 * THE TABLE, and where each row comes from
 *
 *   up held                     forwardmove = 0x19          G_BuildTiccmd's walk
 *   up and B held               forwardmove = 0x32          the run modifier
 *   down held                   forwardmove = -0x19         backpedal
 *   left held                   angleturn  = +kTurnStep     slow turn, upstream's sign
 *   right held                  angleturn  = -kTurnStep
 *   A held                      buttons   |= BT_ATTACK      D4.2d consumes it
 *   B held alone                buttons   |= BT_USE         P_PlayerThink's usedown
 *                                                           latch makes it edge-triggered
 *
 * THE CASES THE TABLE DOES NOT NAME, decided here so none is undefined
 *
 *   - both directions held on one axis cancel: up+down is forwardmove 0, and
 *     left+right is angleturn 0. Upstream adds then subtracts, which is the same
 *     number.
 *   - B is contextual, exactly as upstream's `key_speed`: with up it is the run
 *     modifier, with any other direction it is inert, and only with no direction
 *     at all is it BT_USE. That is why "B alone" is the use rule rather than
 *     "B is always use" -- a run would otherwise open every door it ran past.
 *   - A+B therefore sets both bits: A is not a direction, so B still means use.
 *   - A sets BT_ATTACK whether or not a direction is held, because upstream's
 *     fire button is independent of movement.
 *
 * WHAT IS NOT HERE, ON PURPOSE
 *
 * Strafing (sidemove), the strafe modifier, the full contextual matrix upstream
 * builds from autorun, always-run and the mouse, and BT_CHANGE are D4.3's: they
 * need the weapon and options layers that do not exist yet. `sidemove` is
 * therefore always 0, which is a value, not a missing field.
 *
 * @param buttons The engine's six bits, packed as `INPUT,buttons` prints them.
 * @return The tic command those buttons produce.
 */
p_ticcmd_t mapButtons(std::uint8_t buttons);

}  // namespace doomc
}  // namespace doom
