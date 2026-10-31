/*
 * Behaviour tests for the button -> ticcmd mapping (D4.2f).
 *
 * The world's only input is a tic command. This mapping is the one place the
 * engine's six button bits become `p_ticcmd_t`, and it is a pure function, so
 * the table is pinned here instead of being inferred from a panel run:
 *
 *   up                        forwardmove = 0x19     (walk)
 *   up + B                    forwardmove = 0x32     (run)
 *   down                      forwardmove = -0x19    (backpedal)
 *   left / right              angleturn = +640 / -640
 *   A                         BT_ATTACK
 *   B alone                   BT_USE
 *
 * The cases the brief left open are asserted too, because a value nobody chose
 * is a behaviour the panel would have to discover: both directions on one axis
 * cancel, B is a run modifier only forward, B with any direction is not BT_USE,
 * and A+B sets both bits.
 *
 * Every case checks all four fields, so a mapping that sets the right movement
 * and a stray button fails just as loudly as an inverted sign. `sidemove` is 0
 * in every case: strafing is D4.3's and the field's presence is the contract.
 */
#include <cstdint>

#include <unity.h>

// `p_user.h` is a C header. Its `static inline p_player_wire()` uses upstream's
// `= {0}`, which C accepts and C++ -Wextra reports as a missing-initializer
// warning. The test is the only C++ translation unit that includes it, so the
// diagnostic is silenced here instead of touching lib/doom for a test build.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "doomc/InputMap.h"
#include "p_user.h"
#pragma GCC diagnostic pop

#include "../../test_config.h"

using doom::doomc::kButtonA;
using doom::doomc::kButtonB;
using doom::doomc::kButtonDown;
using doom::doomc::kButtonLeft;
using doom::doomc::kButtonRight;
using doom::doomc::kButtonUp;
using doom::doomc::kForwardRun;
using doom::doomc::kForwardWalk;
using doom::doomc::kTurnStep;
using doom::doomc::mapButtons;

void setUp(void) {
    test_setup();
}

void tearDown() {
    test_teardown();
}

namespace {

/// Asserts the whole command, field by field: a rule is all four fields.
void assertCommand(std::uint8_t buttons, std::int8_t forwardmove, std::int8_t sidemove,
                   std::int16_t angleturn, std::uint8_t expectedButtons) {
    const p_ticcmd_t cmd = mapButtons(buttons);

    TEST_ASSERT_EQUAL_INT8(forwardmove, cmd.forwardmove);
    TEST_ASSERT_EQUAL_INT8(sidemove, cmd.sidemove);
    TEST_ASSERT_EQUAL_INT16(angleturn, cmd.angleturn);
    TEST_ASSERT_EQUAL_UINT8(expectedButtons, cmd.buttons);
}

}  // namespace

// The bits and the speeds are the engine's order and upstream's literals. If a
// constant drifts, every case below still passes but the world walks at the
// wrong speed, so the numbers are asserted once on their own.
void test_the_constants_are_upstreams_numbers(void) {
    TEST_ASSERT_EQUAL_UINT8(0x01u, kButtonUp);
    TEST_ASSERT_EQUAL_UINT8(0x02u, kButtonDown);
    TEST_ASSERT_EQUAL_UINT8(0x04u, kButtonLeft);
    TEST_ASSERT_EQUAL_UINT8(0x08u, kButtonRight);
    TEST_ASSERT_EQUAL_UINT8(0x10u, kButtonA);
    TEST_ASSERT_EQUAL_UINT8(0x20u, kButtonB);

    TEST_ASSERT_EQUAL_INT8(25, kForwardWalk);
    TEST_ASSERT_EQUAL_INT8(50, kForwardRun);
    TEST_ASSERT_EQUAL_INT16(640, kTurnStep);
    TEST_ASSERT_EQUAL_UINT8(1u, BT_ATTACK);
    TEST_ASSERT_EQUAL_UINT8(2u, BT_USE);
}

// No input is a command of zeroes: one tic of friction, no button.
void test_no_buttons_produce_an_empty_command(void) {
    assertCommand(0x00u, 0, 0, 0, 0u);
}

// Up is the walk speed, and that is the whole command.
void test_up_holds_the_walk_speed(void) {
    assertCommand(kButtonUp, kForwardWalk, 0, 0, 0u);
}

// Up with B is the run speed. B is the speed modifier, not a second action, so
// the button word stays empty.
void test_up_and_b_run(void) {
    assertCommand(static_cast<std::uint8_t>(kButtonUp | kButtonB), kForwardRun, 0, 0, 0u);
}

// Down backpedals at the walk speed, and B does not make it a run: upstream has
// no second reverse speed.
void test_down_backpedals_and_b_does_not_run_backwards(void) {
    assertCommand(kButtonDown, static_cast<std::int8_t>(-kForwardWalk), 0, 0, 0u);
    assertCommand(static_cast<std::uint8_t>(kButtonDown | kButtonB),
                  static_cast<std::int8_t>(-kForwardWalk), 0, 0, 0u);
}

// Left and right are upstream's slow turn step, with upstream's signs: left is
// positive (counter-clockwise) in DOOM's binary-angle space.
void test_left_and_right_turn_in_opposite_directions(void) {
    assertCommand(kButtonLeft, 0, 0, kTurnStep, 0u);
    assertCommand(kButtonRight, 0, 0, static_cast<std::int16_t>(-kTurnStep), 0u);
}

// Both keys on one axis cancel, exactly as upstream's add-then-subtract does.
void test_opposite_directions_cancel(void) {
    assertCommand(static_cast<std::uint8_t>(kButtonUp | kButtonDown), 0, 0, 0, 0u);
    assertCommand(static_cast<std::uint8_t>(kButtonLeft | kButtonRight), 0, 0, 0, 0u);
}

// A is BT_ATTACK, alone or while walking: D4.2d consumes it, and until then
// P_PlayerThink counts the press instead of dropping it.
void test_a_is_attack(void) {
    assertCommand(kButtonA, 0, 0, 0, BT_ATTACK);
    assertCommand(static_cast<std::uint8_t>(kButtonA | kButtonUp), kForwardWalk, 0, 0,
                  BT_ATTACK);
}

// B alone is BT_USE. The hold is not edge-triggered here: P_PlayerThink's
// usedown latch is what turns a held button into one press.
void test_b_alone_is_use(void) {
    assertCommand(kButtonB, 0, 0, 0, BT_USE);
}

// B while turning is inert: it is neither use nor a run, because there is no
// forward movement to speed up. A run past every door is the bug this prevents.
void test_b_with_a_turn_is_neither_use_nor_run(void) {
    assertCommand(static_cast<std::uint8_t>(kButtonB | kButtonLeft), 0, 0, kTurnStep, 0u);
}

// A+B sets both: B still means use (no direction is held) and A still means fire.
void test_a_and_b_sets_attack_and_use(void) {
    assertCommand(static_cast<std::uint8_t>(kButtonA | kButtonB), 0, 0, 0,
                  static_cast<std::uint8_t>(BT_ATTACK | BT_USE));
}

// A walking turn is the chord the panel proof uses: forward and turn at once both
// reach the command, so the avatar can steer without stopping.
void test_walking_and_turning_are_independent(void) {
    assertCommand(static_cast<std::uint8_t>(kButtonUp | kButtonRight), kForwardWalk, 0,
                  static_cast<std::int16_t>(-kTurnStep), 0u);
}

// Only the six documented bits are read: a bit above B cannot invent a button.
void test_undocumented_bits_are_ignored(void) {
    assertCommand(0xC0u, 0, 0, 0, 0u);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_the_constants_are_upstreams_numbers);
    RUN_TEST(test_no_buttons_produce_an_empty_command);
    RUN_TEST(test_up_holds_the_walk_speed);
    RUN_TEST(test_up_and_b_run);
    RUN_TEST(test_down_backpedals_and_b_does_not_run_backwards);
    RUN_TEST(test_left_and_right_turn_in_opposite_directions);
    RUN_TEST(test_opposite_directions_cancel);
    RUN_TEST(test_a_is_attack);
    RUN_TEST(test_b_alone_is_use);
    RUN_TEST(test_b_with_a_turn_is_neither_use_nor_run);
    RUN_TEST(test_a_and_b_sets_attack_and_use);
    RUN_TEST(test_walking_and_turning_are_independent);
    RUN_TEST(test_undocumented_bits_are_ignored);

    return UNITY_END();
}
