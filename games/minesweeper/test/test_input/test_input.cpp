/*
 * Unit tests for InputController: edge-only actions (spec AC-09),
 * cursor auto-repeat and the A+B menu chord.
 */
#include <unity.h>

#include "input/InputBindings.h"
#include "input/InputController.h"

using namespace minesweeper;

static constexpr std::uint16_t HELD_A = static_cast<std::uint16_t>(1U << BTN_A);
static constexpr std::uint16_t HELD_B = static_cast<std::uint16_t>(1U << BTN_B);
static constexpr std::uint16_t HELD_UP = static_cast<std::uint16_t>(1U << BTN_UP);

void test_action_fires_once_while_held(void) {
    InputController input;
    // AC-09: holding Reveal or Flag never repeats the dangerous action.
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_A, 100), GameCommand::Reveal));
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(HELD_A, 200));
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(HELD_A, 10000));
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(0, 10100));
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_A, 10200), GameCommand::Reveal));

    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_B, 10300), GameCommand::ToggleFlag));
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(HELD_B, 10400));
}

void test_direction_repeats_after_delay(void) {
    InputController input;
    const unsigned long t0 = 1000;
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_UP, t0), GameCommand::Up));
    // Inside the initial delay: silence.
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(HELD_UP, t0 + REPEAT_DELAY_MS - 1));
    // Past the delay: first auto-repeat, then steady interval steps.
    TEST_ASSERT_TRUE(
        hasCommand(input.poll(HELD_UP, t0 + REPEAT_DELAY_MS), GameCommand::Up));
    TEST_ASSERT_EQUAL_UINT16(
        0, input.poll(HELD_UP, t0 + REPEAT_DELAY_MS + REPEAT_INTERVAL_MS - 1));
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_UP, t0 + REPEAT_DELAY_MS + REPEAT_INTERVAL_MS),
                                GameCommand::Up));
}

void test_release_rearms_repeat(void) {
    InputController input;
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_UP, 0), GameCommand::Up));
    TEST_ASSERT_TRUE(
        hasCommand(input.poll(HELD_UP, REPEAT_DELAY_MS), GameCommand::Up));
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(0, REPEAT_DELAY_MS + 10));
    // A fresh press moves immediately instead of waiting out the old timer.
    TEST_ASSERT_TRUE(
        hasCommand(input.poll(HELD_UP, REPEAT_DELAY_MS + 20), GameCommand::Up));
}

void test_chord_emits_cancel_not_actions(void) {
    InputController input;
    const std::uint16_t both = static_cast<std::uint16_t>(HELD_A | HELD_B);
    const std::uint16_t mask = input.poll(both, 500);
    TEST_ASSERT_TRUE(hasCommand(mask, GameCommand::Cancel));
    TEST_ASSERT_FALSE(hasCommand(mask, GameCommand::Reveal));
    TEST_ASSERT_FALSE(hasCommand(mask, GameCommand::ToggleFlag));
    // Still held: no repeat while the chord stays down.
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(both, 600));
    // Released and pressed again: Cancel fires once more.
    TEST_ASSERT_EQUAL_UINT16(0, input.poll(0, 700));
    TEST_ASSERT_TRUE(hasCommand(input.poll(both, 800), GameCommand::Cancel));
}

void test_second_button_while_held_becomes_cancel(void) {
    InputController input;
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_A, 100), GameCommand::Reveal));
    // B joins while A is held: the frame means "menu", not "flag".
    const std::uint16_t mask =
        input.poll(static_cast<std::uint16_t>(HELD_A | HELD_B), 200);
    TEST_ASSERT_TRUE(hasCommand(mask, GameCommand::Cancel));
    TEST_ASSERT_FALSE(hasCommand(mask, GameCommand::ToggleFlag));
}

void test_reset_clears_stuck_state(void) {
    InputController input;
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_A, 100), GameCommand::Reveal));
    input.reset();
    // After a transition the same held level counts as a fresh press.
    TEST_ASSERT_TRUE(hasCommand(input.poll(HELD_A, 200), GameCommand::Reveal));
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_action_fires_once_while_held);
    RUN_TEST(test_direction_repeats_after_delay);
    RUN_TEST(test_release_rearms_repeat);
    RUN_TEST(test_chord_emits_cancel_not_actions);
    RUN_TEST(test_second_button_while_held_becomes_cancel);
    RUN_TEST(test_reset_clears_stuck_state);

    return UNITY_END();
}
