/*
 * Difficulty tests for the games/pong CPU opponent.
 *
 * The menu offers three levels (Easy, Medium, Hard) and they must be nothing
 * more than a tuning of the same proportional controller: the court, the ball,
 * the serve and the player paddle do not change. These cases pin that promise:
 *  - the three caps are 40 / 60 / 80 % of the player's paddle, ordered;
 *  - the imperfection shrinks as the level rises, and so does the dead zone;
 *  - Medium is bit-for-bit the controller the demo shipped with, so the default
 *    match did not change when the level was introduced;
 *  - a harder level really does track a static ball more closely;
 *  - `Rules` owns the level: it round-trips and survives `resetMatch()`;
 *  - the level is part of the deterministic state: the same script at Hard
 *    replays the same match, field by field.
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

/// Paddle top edge when a paddle is centred in the play area (110 px).
constexpr Fixed kPaddleCentreTop =
    (kHudHeight + (kCourtHeight - kHudHeight - kPaddleHeight) / 2) * kOne;

/// Centre of the paddle whose top edge is `top`, in Q8.8.
constexpr Fixed paddleCentre(Fixed top) {
    return top + (kPaddleHeight / 2) * kOne;
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
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected.difficulty()),
                          static_cast<int>(actual.difficulty()));
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. The speed caps are 76 / 114 / 152 px/s, all below the player paddle.
// -----------------------------------------------------------------------------
void test_speed_caps_are_ordered_fractions_of_the_player_paddle(void) {
    const CpuTuning easy = cpuTuning(Difficulty::Easy);
    const CpuTuning medium = cpuTuning(Difficulty::Medium);
    const CpuTuning hard = cpuTuning(Difficulty::Hard);

    // kPaddleSpeed is 190 px/s: 40 %, 60 % and 80 % of it.
    TEST_ASSERT_EQUAL_INT32(76 * kOne, easy.maxSpeed);
    TEST_ASSERT_EQUAL_INT32(114 * kOne, medium.maxSpeed);
    TEST_ASSERT_EQUAL_INT32(152 * kOne, hard.maxSpeed);

    TEST_ASSERT_TRUE(easy.maxSpeed < kPaddleSpeed);
    TEST_ASSERT_TRUE(medium.maxSpeed < kPaddleSpeed);
    TEST_ASSERT_TRUE(hard.maxSpeed < kPaddleSpeed);
    TEST_ASSERT_TRUE(easy.maxSpeed < medium.maxSpeed);
    TEST_ASSERT_TRUE(medium.maxSpeed < hard.maxSpeed);
}

// -----------------------------------------------------------------------------
// 2. The imperfection shrinks as the level rises; the dead zone does too.
// -----------------------------------------------------------------------------
void test_imperfection_and_dead_zone_shrink_with_level(void) {
    const CpuTuning easy = cpuTuning(Difficulty::Easy);
    const CpuTuning medium = cpuTuning(Difficulty::Medium);
    const CpuTuning hard = cpuTuning(Difficulty::Hard);

    // The imperfection radii are 6, 3 and 1 px.
    TEST_ASSERT_EQUAL_INT32(6 * kOne, easy.noiseRadius);
    TEST_ASSERT_EQUAL_INT32(3 * kOne, medium.noiseRadius);
    TEST_ASSERT_EQUAL_INT32(kOne, hard.noiseRadius);
    TEST_ASSERT_TRUE(easy.noiseRadius > medium.noiseRadius);
    TEST_ASSERT_TRUE(medium.noiseRadius > hard.noiseRadius);

    // The dead zones are 2, 0.5 and 0.125 px.
    TEST_ASSERT_EQUAL_INT32(2 * kOne, easy.deadZone);
    TEST_ASSERT_EQUAL_INT32(kOne / 2, medium.deadZone);
    TEST_ASSERT_EQUAL_INT32(kOne / 8, hard.deadZone);
    TEST_ASSERT_TRUE(easy.deadZone > medium.deadZone);
    TEST_ASSERT_TRUE(medium.deadZone > hard.deadZone);
}

// -----------------------------------------------------------------------------
// 3. Medium is the legacy controller: the default match did not change.
// -----------------------------------------------------------------------------
void test_medium_matches_the_legacy_constants(void) {
    const CpuTuning medium = cpuTuning(Difficulty::Medium);

    TEST_ASSERT_EQUAL_INT32(kCpuGainNum, medium.gainNum);
    TEST_ASSERT_EQUAL_INT32(kCpuGainDen, medium.gainDen);
    TEST_ASSERT_EQUAL_INT32(kCpuMaxSpeed, medium.maxSpeed);
    TEST_ASSERT_EQUAL_INT32(kCpuNoiseRadius, medium.noiseRadius);
    TEST_ASSERT_EQUAL_INT32(kCpuDeadZone, medium.deadZone);

    // The gain ratios are 1/1, 3/2 and 5/2, exactly as the tuning table says.
    TEST_ASSERT_EQUAL_INT32(1, cpuTuning(Difficulty::Easy).gainNum);
    TEST_ASSERT_EQUAL_INT32(1, cpuTuning(Difficulty::Easy).gainDen);
    TEST_ASSERT_EQUAL_INT32(3, medium.gainNum);
    TEST_ASSERT_EQUAL_INT32(2, medium.gainDen);
    TEST_ASSERT_EQUAL_INT32(5, cpuTuning(Difficulty::Hard).gainNum);
    TEST_ASSERT_EQUAL_INT32(2, cpuTuning(Difficulty::Hard).gainDen);
}

// -----------------------------------------------------------------------------
// 4. A harder level leaves a smaller tracking error on the same script.
// -----------------------------------------------------------------------------
void test_a_harder_level_tracks_a_static_ball_more_closely(void) {
    CpuPaddle easy(cpuTuning(Difficulty::Easy));
    CpuPaddle medium(cpuTuning(Difficulty::Medium));
    CpuPaddle hard(cpuTuning(Difficulty::Hard));

    Fixed easyY = kPaddleCentreTop;
    Fixed mediumY = kPaddleCentreTop;
    Fixed hardY = kPaddleCentreTop;
    // The same ball 40 px below the paddle centre for every level.
    const Fixed ball = paddleCentre(kPaddleCentreTop) + 40 * kOne;

    // Three seconds is long enough for every cap to close the 40 px gap and
    // settle into its imperfection instead of still travelling.
    for (int i = 0; i < 180; ++i) {
        easyY = easy.step(ball, easyY, kStepDtUs);
        mediumY = medium.step(ball, mediumY, kStepDtUs);
        hardY = hard.step(ball, hardY, kStepDtUs);
    }

    const Fixed easyGap = ball - paddleCentre(easyY);
    const Fixed mediumGap = ball - paddleCentre(mediumY);
    const Fixed hardGap = ball - paddleCentre(hardY);

    TEST_ASSERT_TRUE(hardGap <= mediumGap);
    TEST_ASSERT_TRUE(mediumGap <= easyGap);
    TEST_ASSERT_TRUE(hardGap < easyGap);
}

// -----------------------------------------------------------------------------
// 5. Rules owns the level, and resetMatch() preserves it.
// -----------------------------------------------------------------------------
void test_rules_round_trips_and_preserves_the_level(void) {
    Rules rules(0, true, Difficulty::Hard);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Difficulty::Hard),
                          static_cast<int>(rules.difficulty()));

    rules.setDifficulty(Difficulty::Easy);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Difficulty::Easy),
                          static_cast<int>(rules.difficulty()));

    // A rebuilt match keeps the selected level: the scene switches level and
    // then calls resetMatch(), and the level must not fall back to Medium.
    rules.resetMatch();
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Difficulty::Easy),
                          static_cast<int>(rules.difficulty()));

    // A default-constructed match is Medium, the demo's pre-menu behaviour.
    Rules defaults;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Difficulty::Medium),
                          static_cast<int>(defaults.difficulty()));
}

// -----------------------------------------------------------------------------
// 6. The level is part of the deterministic state: Hard replays identically.
// -----------------------------------------------------------------------------
void test_hard_level_replays_the_same_script(void) {
    Rules left(0, true, Difficulty::Hard);
    Rules right(0, true, Difficulty::Hard);

    for (int i = 0; i < 600; ++i) {
        // Change the player's held direction twice so the player paddle and the
        // ball leave the centre line during the replay.
        if (i == 0) {
            left.setPlayerInput(0, kButtonRight);
            right.setPlayerInput(0, kButtonRight);
        } else if (i == 300) {
            left.setPlayerInput(0, kButtonLeft);
            right.setPlayerInput(0, kButtonLeft);
        }
        left.step(kStepDtUs);
        right.step(kStepDtUs);
        assertSameState(left, right);
    }

    // The script actually exercised both paddles: the CPU moved away from the
    // centred position it starts at.
    TEST_ASSERT_TRUE(left.paddleY(1) != kPaddleCentreTop);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_speed_caps_are_ordered_fractions_of_the_player_paddle);
    RUN_TEST(test_imperfection_and_dead_zone_shrink_with_level);
    RUN_TEST(test_medium_matches_the_legacy_constants);
    RUN_TEST(test_a_harder_level_tracks_a_static_ball_more_closely);
    RUN_TEST(test_rules_round_trips_and_preserves_the_level);
    RUN_TEST(test_hard_level_replays_the_same_script);

    return UNITY_END();
}
