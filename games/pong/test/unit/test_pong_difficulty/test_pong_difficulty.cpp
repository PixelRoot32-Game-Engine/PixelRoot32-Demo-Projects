/*
 * Difficulty tests for the games/pong CPU opponent.
 *
 * The menu offers three levels (Easy, Medium, Hard) and they tune both how hard
 * the controller answers an error and how it reads the shot: the court, the
 * ball, the serve and the player paddle do not change. These cases pin that
 * promise:
 *  - the three caps are 40 / 60 / 80 % of the player's paddle, ordered;
 *  - the imperfection shrinks as the level rises, and so does the dead zone;
 *  - the styles are chase, predict and predict-and-aim;
 *  - Medium's numbers are still the legacy constants, so a default match kept
 *    its gain, cap, imperfection and dead zone;
 *  - `predictBallY()` folds the shot through the walls and recentres a ball that
 *    travels away;
 *  - predicting the intercept beats chasing the ball's current Y on the same
 *    shot, and the aiming style lands one `kCpuAimOffset` off the plain
 *    prediction, on the side that returns the ball away from the player;
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

/// Top edge of the play area, in Q8.8.
constexpr Fixed kPlayTop = kHudHeight * kOne;

/// Centre of the paddle whose top edge is `top`, in Q8.8.
constexpr Fixed paddleCentre(Fixed top) {
    return top + (kPaddleHeight / 2) * kOne;
}

Fixed absFixed(Fixed value) {
    return value < 0 ? -value : value;
}

// The controller cases below isolate one variable at a time: the two tunings of
// a pair carry identical numbers, so the only difference is the style, and the
// imperfection is always off so the measured gaps are the controller's.

/// Medium's numbers, chasing the ball's current Y.
constexpr CpuTuning kChaseMediumNoNoise = {
    kCpuGainNum, kCpuGainDen, kCpuMaxSpeed, 0, kCpuDeadZone, CpuStyle::Chase};

/// The same numbers, predicting the intercept.
constexpr CpuTuning kPredictMediumNoNoise = {
    kCpuGainNum, kCpuGainDen, kCpuMaxSpeed, 0, kCpuDeadZone, CpuStyle::Predict};

/// Hard's numbers, predicting the intercept.
constexpr CpuTuning kHardPredictNoNoise = {
    5, 2, kPaddleSpeed * 80 / 100, 0, kOne / 8, CpuStyle::Predict};

/// Hard's numbers, predicting and aiming away from the player.
constexpr CpuTuning kHardAimNoNoise = {
    5, 2, kPaddleSpeed * 80 / 100, 0, kOne / 8, CpuStyle::PredictAndAim};

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
// 3. The levels change how the CPU reads the shot.
// -----------------------------------------------------------------------------
void test_styles_match_the_levels(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CpuStyle::Chase),
                          static_cast<int>(cpuTuning(Difficulty::Easy).style));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CpuStyle::Predict),
                          static_cast<int>(cpuTuning(Difficulty::Medium).style));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(CpuStyle::PredictAndAim),
                          static_cast<int>(cpuTuning(Difficulty::Hard).style));
}

// -----------------------------------------------------------------------------
// 4. Medium's numbers are the legacy controller: the default match kept them.
// -----------------------------------------------------------------------------
void test_medium_matches_the_legacy_constants(void) {
    const CpuTuning medium = cpuTuning(Difficulty::Medium);

    // The style is deliberately not compared: Medium predicts now, and that is
    // the revision this table serves. The numbers around it did not move.
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
// 5. predictBallY() reads the intercept and folds it through the walls.
// -----------------------------------------------------------------------------
void test_predict_ball_y_reads_the_intercept(void) {
    // This suite runs on the 240-wide court, where the paddle plane is 106 px
    // ahead of the serve position. The expected values below are that travel at
    // 100 px/s: 1.06 s of flight.
    TEST_ASSERT_EQUAL_INT32(106 * kOne, kCpuPaddlePlaneX - kBallCenterX);

    const Fixed x = kBallCenterX;
    const Fixed vx = 100 * kOne;

    // A straight shot keeps the ball's height.
    TEST_ASSERT_EQUAL_INT32(100 * kOne, predictBallY(BallView{x, 100 * kOne, vx, 0}));

    // A downward shot that never reaches a wall: 60 px + 106 px.
    TEST_ASSERT_EQUAL_INT32(166 * kOne,
                            predictBallY(BallView{x, 60 * kOne, vx, 100 * kOne}));

    // Folded once off the bottom: 100 + 212 = 312 px reflects to 162 px inside
    // the ball's span (top 27 px, 210 px tall).
    TEST_ASSERT_EQUAL_INT32(162 * kOne,
                            predictBallY(BallView{x, 100 * kOne, vx, 200 * kOne}));

    // Folded more than once: 100 + 424 = 524 px reflects twice, to 104 px.
    TEST_ASSERT_EQUAL_INT32(104 * kOne,
                            predictBallY(BallView{x, 100 * kOne, vx, 400 * kOne}));

    // A ball travelling away, and the parked ball of the countdown, recentre.
    TEST_ASSERT_EQUAL_INT32(kBallCenterY,
                            predictBallY(BallView{x, 60 * kOne, -vx, 100 * kOne}));
    TEST_ASSERT_EQUAL_INT32(kBallCenterY,
                            predictBallY(BallView{x, 60 * kOne, 0, 100 * kOne}));
}

// -----------------------------------------------------------------------------
// 6. Reading the shot beats chasing it: the predictor meets the ball closer.
// -----------------------------------------------------------------------------
void test_predicting_meets_the_shot_closer_than_chasing(void) {
    CpuPaddle chaser(kChaseMediumNoNoise);
    CpuPaddle predictor(kPredictMediumNoNoise);

    Fixed chaseY = kPaddleCentreTop;
    Fixed predictY = kPaddleCentreTop;

    // A shot travelling right and down at 180 px/s on both axes. It starts above
    // the paddle, so a chase first moves the wrong way while the predictor is
    // already heading for the intercept.
    BallView ball{kBallCenterX, 60 * kOne, 180 * kOne, 180 * kOne};
    const Fixed stepX = scaleByTime(ball.vx, kStepDtUs);
    const Fixed stepY = scaleByTime(ball.vy, kStepDtUs);

    // One step per frame until the ball reaches the CPU's paddle plane.
    while (ball.x < kCpuPaddlePlaneX) {
        chaseY = chaser.step(ball, chaseY, kPaddleCentreTop, kStepDtUs);
        predictY = predictor.step(ball, predictY, kPaddleCentreTop, kStepDtUs);
        ball.x += stepX;
        ball.y += stepY;
    }

    const Fixed chaseGap = absFixed(ball.y - paddleCentre(chaseY));
    const Fixed predictGap = absFixed(ball.y - paddleCentre(predictY));

    TEST_ASSERT_TRUE(predictGap < chaseGap);
    // Both paddles actually moved, so the comparison is not about two parked
    // paddles.
    TEST_ASSERT_TRUE(chaseY != kPaddleCentreTop);
    TEST_ASSERT_TRUE(predictY != kPaddleCentreTop);
}

// -----------------------------------------------------------------------------
// 7. The aiming style ends kCpuAimOffset off the plain prediction, so the
//    return goes to the side the player is not on.
// -----------------------------------------------------------------------------
void test_hard_aims_the_return_away_from_the_player(void) {
    CpuPaddle plain(kHardPredictNoNoise);
    CpuPaddle aiming(kHardAimNoNoise);

    Fixed plainY = kPaddleCentreTop;
    Fixed aimY = kPaddleCentreTop;

    // The player is clearly above the centre, so the offset's sign is
    // unambiguous: `targetY -= kCpuAimOffset` moves the CPU's target up, the
    // ball meets the paddle below its centre, and the return goes down -- away
    // from the player.
    const Fixed opponentY = kPlayTop;
    BallView ball{kBallCenterX, 180 * kOne, 100 * kOne, 0};
    const Fixed stepX = scaleByTime(ball.vx, kStepDtUs);

    // Five seconds: both controllers settle on their target well before the end.
    for (int i = 0; i < 300; ++i) {
        plainY = plain.step(ball, plainY, opponentY, kStepDtUs);
        aimY = aiming.step(ball, aimY, opponentY, kStepDtUs);
        ball.x += stepX;
    }

    // The flat shot gives both styles the same intercept, so the only difference
    // is the offset the aiming level subtracts from its target.
    TEST_ASSERT_INT32_WITHIN(2 * kOne, paddleCentre(plainY) - kCpuAimOffset,
                             paddleCentre(aimY));
    TEST_ASSERT_TRUE(aimY < plainY);
}

// -----------------------------------------------------------------------------
// 8. Rules owns the level, and resetMatch() preserves it.
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

    // A default-constructed match is Medium, the demo's default behaviour.
    Rules defaults;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Difficulty::Medium),
                          static_cast<int>(defaults.difficulty()));
}

// -----------------------------------------------------------------------------
// 9. The level is part of the deterministic state: Hard replays identically.
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
    RUN_TEST(test_styles_match_the_levels);
    RUN_TEST(test_medium_matches_the_legacy_constants);
    RUN_TEST(test_predict_ball_y_reads_the_intercept);
    RUN_TEST(test_predicting_meets_the_shot_closer_than_chasing);
    RUN_TEST(test_hard_aims_the_return_away_from_the_player);
    RUN_TEST(test_rules_round_trips_and_preserves_the_level);
    RUN_TEST(test_hard_level_replays_the_same_script);

    return UNITY_END();
}
