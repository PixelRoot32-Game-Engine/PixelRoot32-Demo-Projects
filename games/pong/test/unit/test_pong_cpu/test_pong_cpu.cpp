/*
 * Behaviour tests for the games/pong CPU paddle.
 *
 * The right paddle of the single-player match is a proportional controller that
 * reads the ball and writes its own top edge. Its contract is narrow and worth
 * pinning down, because it is the whole opponent:
 *  - it chases the ball: a ball below the paddle drags it down, a ball above
 *    drags it up;
 *  - it ignores errors inside the dead zone, so a settled ball does not make the
 *    paddle buzz;
 *  - it never leaves the play area;
 *  - it is slower than the human paddle, by the same 60 % ratio the retired
 *    physics demo used, so a rally is winnable;
 *  - its imperfection is bounded: the pseudo-random offset stays inside the
 *    +-3 px box of the demo's AI_TARGET_OFFSET, so a parked ball leaves the
 *    paddle resting near the centre;
 *  - it is deterministic: the same script replays the same Q8.8 positions, and
 *    `reset()` puts the whole controller -- accumulator and noise stream -- back
 *    to where it started.
 *
 * The tests drive `CpuPaddle::step()` directly: it is a pure function of the
 * ball, the paddle edge and the elapsed microseconds, so no match, no scene and
 * no clock is involved. The court primitives it reads (`Fixed`, `kOne`, the
 * paddle geometry) come from "game/PongRules.h", which must be included first.
 */
#include <cstdint>

#include <unity.h>

#include "game/PongRules.h"
#include "game/PongCpu.h"

#include "../../test_config.h"

using namespace pong::game;

void setUp(void) {
    test_setup();
}

void tearDown(void) {
    test_teardown();
}

namespace {

/// Paddle top edge when a paddle is centred in the play area (110 px).
constexpr Fixed kPaddleCentreTop =
    (kHudHeight + (kCourtHeight - kHudHeight - kPaddleHeight) / 2) * kOne;

/// Centre of the paddle whose top edge is `top`, in Q8.8.
constexpr Fixed paddleCentre(Fixed top) {
    return top + (kPaddleHeight / 2) * kOne;
}

/// Top edge of the play area, in Q8.8.
constexpr Fixed kPlayTop = kHudHeight * kOne;

/// Lowest legal paddle top edge, in Q8.8.
constexpr Fixed kPaddleFloor = (kCourtHeight - kPaddleHeight) * kOne;

Fixed absFixed(Fixed value) {
    return value < 0 ? -value : value;
}

/// A ball path that sweeps the whole court, so a scripted controller has to
/// work for its position instead of settling on a constant.
constexpr Fixed scriptedBallY(int step) {
    return (kHudHeight + (step * 37) % (kCourtHeight - kHudHeight)) * kOne;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. A ball below the paddle drags it down and the gap closes.
// -----------------------------------------------------------------------------
void test_cpu_moves_down_towards_a_ball_below_it(void) {
    CpuPaddle cpu(0);  // no imperfection: this test is about the gain

    Fixed paddle = kPaddleCentreTop;
    const Fixed ball = paddleCentre(paddle) + 60 * kOne;
    const Fixed gapBefore = ball - paddleCentre(paddle);
    TEST_ASSERT_EQUAL_INT32(60 * kOne, gapBefore);

    // 60 px of error at gain 3/2 is 90 px/s; one 60 Hz step is 1.5 px of
    // travel, so exactly one whole pixel leaves the accumulator.
    paddle = cpu.step(ball, paddle, kStepDtUs);
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop + kOne, paddle);

    // Half a second of chasing shrinks the gap without overshooting.
    for (int i = 0; i < 30; ++i) {
        paddle = cpu.step(ball, paddle, kStepDtUs);
    }
    const Fixed gapAfter = ball - paddleCentre(paddle);
    TEST_ASSERT_TRUE(gapAfter < gapBefore);
    TEST_ASSERT_TRUE(gapAfter > 0);
    TEST_ASSERT_TRUE(gapAfter < 40 * kOne);
}

// -----------------------------------------------------------------------------
// 2. A ball above the paddle drags it up.
// -----------------------------------------------------------------------------
void test_cpu_moves_up_towards_a_ball_above_it(void) {
    CpuPaddle cpu(0);

    Fixed paddle = kPaddleCentreTop;
    const Fixed ball = paddleCentre(paddle) - 60 * kOne;
    const Fixed gapBefore = paddleCentre(paddle) - ball;

    paddle = cpu.step(ball, paddle, kStepDtUs);
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop - kOne, paddle);

    for (int i = 0; i < 30; ++i) {
        paddle = cpu.step(ball, paddle, kStepDtUs);
    }
    const Fixed gapAfter = paddleCentre(paddle) - ball;
    TEST_ASSERT_TRUE(gapAfter < gapBefore);
    TEST_ASSERT_TRUE(gapAfter > 0);
    TEST_ASSERT_TRUE(gapAfter < 40 * kOne);
}

// -----------------------------------------------------------------------------
// 3. An error inside the dead zone does not move the paddle at all.
// -----------------------------------------------------------------------------
void test_cpu_ignores_an_error_inside_the_dead_zone(void) {
    // Exactly the threshold: still inside, because the rule is |error| > 0.5.
    CpuPaddle below(0);
    Fixed lowered = kPaddleCentreTop;
    const Fixed ballBelow = paddleCentre(lowered) + kCpuDeadZone;
    for (int i = 0; i < 600; ++i) {
        lowered = below.step(ballBelow, lowered, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, lowered);

    CpuPaddle above(0);
    Fixed raised = kPaddleCentreTop;
    const Fixed ballAbove = paddleCentre(raised) - kCpuDeadZone;
    for (int i = 0; i < 600; ++i) {
        raised = above.step(ballAbove, raised, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, raised);

    // Outside the dead zone it does move, and it stops before the ball: the
    // controller chases the centre, never overshoots it.
    CpuPaddle chasing(0);
    Fixed chasing1 = kPaddleCentreTop;
    const Fixed ballNear = paddleCentre(chasing1) + 2 * kOne;
    for (int i = 0; i < 60; ++i) {
        chasing1 = chasing.step(ballNear, chasing1, kStepDtUs);
    }
    TEST_ASSERT_TRUE(chasing1 >= kPaddleCentreTop + kOne);
    TEST_ASSERT_TRUE(chasing1 <= kPaddleCentreTop + 2 * kOne);
}

// -----------------------------------------------------------------------------
// 4. The paddle clamps at the top and at the bottom of the play area.
// -----------------------------------------------------------------------------
void test_cpu_clamps_inside_the_play_area(void) {
    // A ball at the top of the court pins the paddle against the HUD band.
    CpuPaddle rising(0);
    Fixed high = kPaddleCentreTop;
    const Fixed ballAtTop = kPlayTop;
    for (int i = 0; i < 100; ++i) {
        high = rising.step(ballAtTop, high, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPlayTop, high);
    for (int i = 0; i < 10; ++i) {
        high = rising.step(ballAtTop, high, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPlayTop, high);

    // A ball at the bottom edge pins it at the last legal top edge.
    CpuPaddle falling(0);
    Fixed low = kPaddleCentreTop;
    const Fixed ballAtBottom = kCourtHeight * kOne;
    for (int i = 0; i < 100; ++i) {
        low = falling.step(ballAtBottom, low, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPaddleFloor, low);
    for (int i = 0; i < 10; ++i) {
        low = falling.step(ballAtBottom, low, kStepDtUs);
    }
    TEST_ASSERT_EQUAL_INT32(kPaddleFloor, low);
}

// -----------------------------------------------------------------------------
// 5. The cap: one second of a 200 px error travels kCpuMaxSpeed, not 200 px.
// -----------------------------------------------------------------------------
void test_cpu_speed_is_capped_below_the_player_paddle(void) {
    // The court of this environment is 240 tall, so the widest reachable error
    // is 194 px: a paddle at the top edge and a ball on the bottom edge.
    CpuPaddle cpu(0);
    const Fixed start = kPlayTop;
    Fixed paddle = start;
    const Fixed ball = kCourtHeight * kOne;
    const Fixed error = ball - paddleCentre(paddle);
    TEST_ASSERT_TRUE(error > 190 * kOne);

    for (int i = 0; i < 60; ++i) {
        paddle = cpu.step(ball, paddle, kStepDtUs);
    }
    const Fixed travelled = paddle - start;

    // 60 % of kPaddleSpeed, and the sub-pixel remainder cannot add a whole
    // pixel inside one second.
    TEST_ASSERT_TRUE(travelled <= kCpuMaxSpeed + kOne);
    TEST_ASSERT_TRUE(travelled >= kCpuMaxSpeed - 2 * kOne);
    // The cap is what it costs the opponent: in the second it had, it could not
    // cover the 194 px of error in front of it.
    TEST_ASSERT_TRUE(travelled < error);
}

// -----------------------------------------------------------------------------
// 6. Two controllers with the same seed replay the same script; other seeds
//    drive a different imperfection.
// -----------------------------------------------------------------------------
void test_cpu_replays_the_same_script_for_the_same_seed(void) {
    CpuPaddle left;
    CpuPaddle right;
    Fixed leftY = kPaddleCentreTop;
    Fixed rightY = kPaddleCentreTop;

    for (int i = 0; i < 900; ++i) {
        const Fixed ball = scriptedBallY(i);
        leftY = left.step(ball, leftY, kStepDtUs);
        rightY = right.step(ball, rightY, kStepDtUs);
        TEST_ASSERT_EQUAL_INT32(leftY, rightY);
    }
    // The script actually exercised the paddle.
    TEST_ASSERT_TRUE(leftY != kPaddleCentreTop);

    // A different seed is a different imperfection: the same ball script cannot
    // end on the same Q8.8 value by accident.
    CpuPaddle other(kCpuNoiseRadius, 0x12345678u);
    CpuPaddle seeded(kCpuNoiseRadius, 0x12345679u);
    Fixed otherY = kPaddleCentreTop;
    Fixed seededY = kPaddleCentreTop;
    bool diverged = false;
    for (int i = 0; i < 900; ++i) {
        const Fixed ball = scriptedBallY(i);
        otherY = other.step(ball, otherY, kStepDtUs);
        seededY = seeded.step(ball, seededY, kStepDtUs);
        if (otherY != seededY) {
            diverged = true;
        }
    }
    TEST_ASSERT_TRUE(diverged);
}

// -----------------------------------------------------------------------------
// 7. The imperfection is bounded: a parked ball leaves the paddle near centre.
// -----------------------------------------------------------------------------
void test_cpu_rests_close_to_a_parked_ball(void) {
    CpuPaddle cpu;  // the production imperfection, +-3 px

    Fixed paddle = kPaddleCentreTop;
    const Fixed ball = paddleCentre(paddle);
    Fixed worst = 0;
    for (int i = 0; i < 600; ++i) {
        paddle = cpu.step(ball, paddle, kStepDtUs);
        const Fixed error = absFixed(ball - paddleCentre(paddle));
        if (error > worst) {
            worst = error;
        }
    }

    // The pseudo-random target offset is what moves the paddle at all, so the
    // resting error may reach the box plus the one pixel the accumulator hands
    // over at a time; it can never wander away from the ball.
    TEST_ASSERT_TRUE(worst <= kCpuNoiseRadius + kOne);
    TEST_ASSERT_TRUE(worst > 0);
}

// -----------------------------------------------------------------------------
// 8. reset() returns the accumulator and the noise stream to the start.
// -----------------------------------------------------------------------------
void test_cpu_reset_restores_the_initial_state(void) {
    constexpr std::uint32_t kSeed = 0x12345678u;

    CpuPaddle scripted(kCpuNoiseRadius, kSeed);
    Fixed ran = kPaddleCentreTop;
    bool moved = false;
    for (int i = 0; i < 240; ++i) {
        ran = scripted.step(scriptedBallY(i), ran, kStepDtUs);
        if (ran != kPaddleCentreTop) {
            moved = true;
        }
    }
    // The script actually exercised the controller, so "reset clears it" is not
    // a statement about two untouched objects.
    TEST_ASSERT_TRUE(moved);
    scripted.reset();

    // A controller that was run and reset must agree, step by step, with one
    // that was never run: both the sub-pixel remainder and the noise stream are
    // part of the state `reset()` clears.
    CpuPaddle fresh(kCpuNoiseRadius, kSeed);
    Fixed resetY = kPaddleCentreTop;
    Fixed freshY = kPaddleCentreTop;
    for (int i = 0; i < 240; ++i) {
        const Fixed ball = scriptedBallY(i);
        resetY = scripted.step(ball, resetY, kStepDtUs);
        freshY = fresh.step(ball, freshY, kStepDtUs);
        TEST_ASSERT_EQUAL_INT32(freshY, resetY);
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_cpu_moves_down_towards_a_ball_below_it);
    RUN_TEST(test_cpu_moves_up_towards_a_ball_above_it);
    RUN_TEST(test_cpu_ignores_an_error_inside_the_dead_zone);
    RUN_TEST(test_cpu_clamps_inside_the_play_area);
    RUN_TEST(test_cpu_speed_is_capped_below_the_player_paddle);
    RUN_TEST(test_cpu_replays_the_same_script_for_the_same_seed);
    RUN_TEST(test_cpu_rests_close_to_a_parked_ball);
    RUN_TEST(test_cpu_reset_restores_the_initial_state);

    return UNITY_END();
}
