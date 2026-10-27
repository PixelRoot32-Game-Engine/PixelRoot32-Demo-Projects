/*
 * Contract tests for the games/pong court rules.
 *
 * The behaviour suite in test_pong_rules proves the match a player sees; these
 * cases pin the edges of the public contract that a scene can lean on and that
 * a future refactor could quietly break: a zero-length step, an absurdly long
 * one, a restart, an out-of-range slot and the CPU constants the header
 * promises. They are small on purpose; each one names a rule, not a scenario.
 */
#include <cstdint>

#include <unity.h>

#include "game/PongRules.h"

#include "../../test_config.h"

using namespace pong::game;

void setUp(void) {
    test_setup();
}

void tearDown(void) {
    test_teardown();
}

namespace {

/// Steps `count` nominal 60 Hz frames.
void stepN(Rules& rules, int count) {
    for (int i = 0; i < count; ++i) {
        rules.step(kStepDtUs);
    }
}

/// Compares every exported field of two matches.
void assertSameState(const Rules& expected, const Rules& actual) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.phase()), static_cast<int>(actual.phase()));
    TEST_ASSERT_EQUAL_UINT32(expected.countdownRemainingMs(), actual.countdownRemainingMs());
    TEST_ASSERT_EQUAL_UINT8(expected.score(0), actual.score(0));
    TEST_ASSERT_EQUAL_UINT8(expected.score(1), actual.score(1));
    TEST_ASSERT_EQUAL_INT32(expected.ballX(), actual.ballX());
    TEST_ASSERT_EQUAL_INT32(expected.ballY(), actual.ballY());
    TEST_ASSERT_EQUAL_INT32(expected.paddleY(0), actual.paddleY(0));
    TEST_ASSERT_EQUAL_INT32(expected.paddleY(1), actual.paddleY(1));
}

}  // namespace

// -----------------------------------------------------------------------------
// A zero-length step is inert, not a phase transition.
// -----------------------------------------------------------------------------
void test_zero_delta_step_is_inert(void) {
    Rules rules(0);
    stepN(rules, 72);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Play), static_cast<int>(rules.phase()));

    const Fixed x = rules.ballX();
    const Fixed y = rules.ballY();
    rules.step(0);
    TEST_ASSERT_EQUAL_INT32(x, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(y, rules.ballY());
}

// -----------------------------------------------------------------------------
// A one-second step is clamped to 100 ms, so a stalled frame cannot teleport
// the ball across the court.
// -----------------------------------------------------------------------------
void test_a_one_second_step_is_clamped_to_a_tenth(void) {
    Rules rules(0);
    stepN(rules, 72);

    const Fixed x = rules.ballX();
    rules.step(1000000U);
    TEST_ASSERT_EQUAL_INT32(18 * kOne, rules.ballX() - x);
}

// -----------------------------------------------------------------------------
// A restart returns to a 0-0 countdown, and two instances still agree step by
// step across the boundary.
// -----------------------------------------------------------------------------
void test_restart_replays_the_same_set(void) {
    Rules left(0, false);
    Rules right(0, false);
    left.setPlayerInput(0, kButtonRight);
    right.setPlayerInput(0, kButtonRight);
    left.setPlayerInput(1, kButtonRight);
    right.setPlayerInput(1, kButtonRight);

    int steps = 0;
    while (left.phase() != Phase::MatchOver && steps < 4000) {
        left.step(kStepDtUs);
        right.step(kStepDtUs);
        assertSameState(left, right);
        ++steps;
    }
    TEST_ASSERT_LESS_THAN(4000, steps);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::MatchOver),
                          static_cast<int>(left.phase()));
    TEST_ASSERT_TRUE(left.score(0) == kWinScore || left.score(1) == kWinScore);

    left.restart();
    right.restart();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Phase::Countdown),
                          static_cast<int>(left.phase()));
    TEST_ASSERT_EQUAL_UINT8(0, left.score(0));
    TEST_ASSERT_EQUAL_UINT8(0, left.score(1));

    for (int i = 0; i < 300; ++i) {
        left.step(kStepDtUs);
        right.step(kStepDtUs);
        assertSameState(left, right);
    }
}

// -----------------------------------------------------------------------------
// Slots outside 0..1 read as zero and never take input.
// -----------------------------------------------------------------------------
void test_out_of_range_slot_is_ignored(void) {
    Rules rules(0);
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(2));
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(255));
    TEST_ASSERT_EQUAL_INT32(0, rules.paddleY(2));
    TEST_ASSERT_EQUAL_INT32(0, rules.paddleY(255));

    rules.setPlayerInput(2, kButtonLeft);
    rules.setPlayerInput(255, kButtonRight);
    stepN(rules, 72);

    // The centred paddle is still where centreActors() left it.
    TEST_ASSERT_EQUAL_INT32(110 * kOne, rules.paddleY(0));
}

// -----------------------------------------------------------------------------
// The CPU constants are the promised ratios, not incidental numbers.
// -----------------------------------------------------------------------------
void test_cpu_cap_is_sixty_percent_of_the_player_paddle(void) {
    TEST_ASSERT_EQUAL_INT32(kPaddleSpeed * 60 / 100, kCpuMaxSpeed);
    TEST_ASSERT_EQUAL_INT32(114 * kOne, kCpuMaxSpeed);
    TEST_ASSERT_TRUE(kCpuMaxSpeed < kPaddleSpeed);
    TEST_ASSERT_EQUAL_INT32(3 * kOne, kCpuNoiseRadius);
    TEST_ASSERT_EQUAL_INT32(kOne / 2, kCpuDeadZone);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_zero_delta_step_is_inert);
    RUN_TEST(test_a_one_second_step_is_clamped_to_a_tenth);
    RUN_TEST(test_restart_replays_the_same_set);
    RUN_TEST(test_out_of_range_slot_is_ignored);
    RUN_TEST(test_cpu_cap_is_sixty_percent_of_the_player_paddle);

    return UNITY_END();
}
