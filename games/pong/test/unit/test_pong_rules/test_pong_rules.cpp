/*
 * Behaviour tests for the games/pong court rules.
 *
 * The rules layer is the authority of the local match: the scene feeds it the
 * D-pad and advances it with a fixed 60 Hz step, and everything the player sees
 * -- ball, paddles, score, the phase word -- is read back from it. These tests
 * therefore drive only the public surface (`setPlayerInput`, `step`,
 * `pollEvent`, the accessors) and assert the observable contract, never
 * internals. The ball is never teleported by a test hook: every scenario is
 * built by serving and stepping, exactly as the scene builds it on the board.
 *
 * Two properties get special attention because no later task can repair them
 * without changing the match:
 *  - the simulation is deterministic: no clock, no RNG, no float. The last test
 *    replays one input script through two instances and compares every exported
 *    field on every step;
 *  - the phase set is the local one: Countdown, Play, Point, MatchOver. There is
 *    no waiting gate and no pause, so a fresh match is already counting down.
 *
 * Velocities are not exported on purpose (the renderer reads positions), so the
 * tests derive a velocity from the distance the ball covers in one fixed step.
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

/// Paddle top edge when both paddles are centred in the play area (110 px).
constexpr Fixed kPaddleCentreTop =
    (kHudHeight + (kCourtHeight - kHudHeight - kPaddleHeight) / 2) * kOne;

/// Fixed steps the pre-serve countdown lasts at kStepDtUs (72 at 60 Hz).
constexpr int kStepsPerCountdown =
    static_cast<int>((kCountdownMs * 1000U + kStepDtUs - 1U) / kStepDtUs);

/// Number of fixed steps needed to cover `ms` of stepped time.
int stepsForMs(std::uint32_t ms) {
    return static_cast<int>((ms * 1000U + kStepDtUs - 1U) / kStepDtUs);
}

/// Steps `count` nominal 60 Hz frames.
void stepN(Rules& rules, int count) {
    for (int i = 0; i < count; ++i) {
        rules.step(kStepDtUs);
    }
}

void assertPhase(const Rules& rules, Phase expected) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(rules.phase()));
}

Fixed absFixed(Fixed value) {
    return value < 0 ? -value : value;
}

/// Params a paddle against the HUD band during the countdown, then releases.
///
/// A parked paddle is how a test asks the court, not a paddle, to decide a
/// rally: the served ball travels at y = 132 while a parked paddle occupies
/// y = 24..68, so every serve reaches an edge and scores. Parking takes 28
/// frames, all of them inside the 1200 ms countdown.
void parkPaddlesAtTop(Rules& rules) {
    rules.setPlayerInput(0, kButtonLeft);
    rules.setPlayerInput(1, kButtonLeft);
    stepN(rules, 28);
    rules.setPlayerInput(0, 0);
    rules.setPlayerInput(1, 0);
}

/// Counts every event kind plus the owner of the events that carry one.
struct EventTally {
    int paddleHit = 0;
    int wallBounce = 0;
    int goal = 0;
    int serveStarted = 0;
    int matchOver = 0;
    std::uint8_t lastGoalPlayer = 0xFF;
    std::uint8_t lastServePlayer = 0xFF;
    std::uint8_t lastMatchOverPlayer = 0xFF;
    std::uint8_t lastWallPlayer = 0x00;

    void add(const Event& event) {
        switch (event.type) {
            case EventType::PaddleHit: ++paddleHit; break;
            case EventType::WallBounce: ++wallBounce; lastWallPlayer = event.player; break;
            case EventType::Goal: ++goal; lastGoalPlayer = event.player; break;
            case EventType::ServeStarted: ++serveStarted; lastServePlayer = event.player; break;
            case EventType::MatchOver: ++matchOver; lastMatchOverPlayer = event.player; break;
        }
    }
};

void drainInto(Rules& rules, EventTally& tally) {
    Event event;
    while (rules.pollEvent(event)) {
        tally.add(event);
    }
}

/// Steps until a Goal is tallied, returning false when the cap is hit.
bool stepUntilGoal(Rules& rules, EventTally& tally, int maxSteps) {
    int steps = 0;
    while (tally.goal == 0 && steps < maxSteps) {
        rules.step(kStepDtUs);
        drainInto(rules, tally);
        ++steps;
    }
    return tally.goal > 0;
}

/// Steps until one event of `wanted` type is polled, returning it.
///
/// Every other event polled on the way is discarded: these tests look at one
/// edge at a time. The ring-order test below deliberately does not use this.
bool stepUntilEvent(Rules& rules, EventType wanted, Event& out, int maxSteps) {
    for (int i = 0; i < maxSteps; ++i) {
        rules.step(kStepDtUs);
        Event event;
        while (rules.pollEvent(event)) {
            if (event.type == wanted) {
                out = event;
                return true;
            }
        }
    }
    return false;
}

/// One serve and the travel of its first live step.
struct ServeProbe {
    std::uint8_t player = 0xFF;
    Fixed x = 0;
    Fixed y = 0;
    Fixed dx = 0;
    Fixed dy = 0;
};

/// Steps until the next ServeStarted and measures one step of travel from it.
bool waitForServe(Rules& rules, ServeProbe& out, int maxSteps = 400) {
    for (int i = 0; i < maxSteps; ++i) {
        rules.step(kStepDtUs);
        Event event;
        while (rules.pollEvent(event)) {
            if (event.type != EventType::ServeStarted) {
                continue;
            }
            out.player = event.player;
            out.x = rules.ballX();
            out.y = rules.ballY();
            rules.step(kStepDtUs);
            out.dx = rules.ballX() - out.x;
            out.dy = rules.ballY() - out.y;
            return true;
        }
    }
    return false;
}

/// Compares every exported field of two matches. Not a memcmp: the class has
/// private padding that is not part of its value.
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
// 1. A fresh match counts down at 0-0 with a still ball and centred paddles.
// -----------------------------------------------------------------------------
void test_fresh_match_counts_down_at_zero_zero_with_a_still_ball(void) {
    Rules rules;

    assertPhase(rules, Phase::Countdown);
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(0));
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(1));
    TEST_ASSERT_EQUAL_INT32((kCourtWidth / 2) * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(kBallCenterX, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());
    TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, rules.paddleY(0));
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, rules.paddleY(1));
    TEST_ASSERT_EQUAL_UINT32(kCountdownMs, rules.countdownRemainingMs());

    // Nothing queued by a match that has not served yet.
    Event event;
    TEST_ASSERT_FALSE(rules.pollEvent(event));

    // Out-of-range slots export zero instead of a neighbour's value.
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(2));
    TEST_ASSERT_EQUAL_INT32(0, rules.paddleY(2));
}

// -----------------------------------------------------------------------------
// 2. The countdown expiring starts the serve: Play, a live ball, one event.
// -----------------------------------------------------------------------------
void test_countdown_reaching_zero_serves_the_ball(void) {
    Rules rules(0);

    // One step short of the countdown: still not live, and no serve announced.
    stepN(rules, kStepsPerCountdown - 1);
    assertPhase(rules, Phase::Countdown);
    TEST_ASSERT_TRUE(rules.countdownRemainingMs() > 0);
    Event event;
    TEST_ASSERT_FALSE(rules.pollEvent(event));

    rules.step(kStepDtUs);
    assertPhase(rules, Phase::Play);
    TEST_ASSERT_EQUAL_UINT32(0, rules.countdownRemainingMs());
    TEST_ASSERT_EQUAL_INT32(kBallCenterX, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());

    int serves = 0;
    std::uint8_t server = 0xFF;
    while (rules.pollEvent(event)) {
        if (event.type == EventType::ServeStarted) {
            ++serves;
            server = event.player;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, serves);
    TEST_ASSERT_EQUAL_UINT8(0, server);  // firstServer = 0 serves to the right

    // The serve is live: the ball leaves the centre on the next step, and it
    // leaves horizontally because the first serve is the flat one.
    const Fixed beforeX = rules.ballX();
    const Fixed beforeY = rules.ballY();
    rules.step(kStepDtUs);
    TEST_ASSERT_EQUAL_INT32(3 * kOne, rules.ballX() - beforeX);
    TEST_ASSERT_EQUAL_INT32(beforeY, rules.ballY());
}

// -----------------------------------------------------------------------------
// 3. The ball is parked while the countdown runs, however much time passes.
// -----------------------------------------------------------------------------
void test_ball_does_not_move_during_the_countdown(void) {
    Rules rules(0);

    std::uint32_t previous = rules.countdownRemainingMs();
    for (int i = 0; i < kStepsPerCountdown - 1; ++i) {
        rules.step(kStepDtUs);
        TEST_ASSERT_EQUAL_INT32(kBallCenterX, rules.ballX());
        TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());
        assertPhase(rules, Phase::Countdown);
        const std::uint32_t remaining = rules.countdownRemainingMs();
        TEST_ASSERT_TRUE(remaining < previous);
        previous = remaining;
    }
    TEST_ASSERT_TRUE(previous > 0);
}

// -----------------------------------------------------------------------------
// 4. Held input moves a paddle in Countdown and Play, cancels, and clamps.
// -----------------------------------------------------------------------------
void test_held_input_moves_the_paddle_and_clamps_to_the_play_area(void) {
    // Countdown: paddle 0 rises at kPaddleSpeed. (A full second is 190 px, more
    // than the 86 px of travel between the clamps.) Twenty frames is 333 ms,
    // about 63 px: measured before the clamp so the number is the speed.
    Rules rising(0);
    rising.setPlayerInput(0, kButtonLeft);
    stepN(rising, 20);
    const std::int64_t travelledUs = static_cast<std::int64_t>(20) * kStepDtUs;
    const Fixed ideal = kPaddleCentreTop - static_cast<Fixed>(
                                             (static_cast<std::int64_t>(kPaddleSpeed) * travelledUs) / 1000000);
    TEST_ASSERT_INT32_WITHIN(kOne, ideal, rising.paddleY(0));
    TEST_ASSERT_TRUE(rising.paddleY(0) < kPaddleCentreTop);

    // Keeping it held pins the paddle against the HUD band.
    stepN(rising, 40);
    TEST_ASSERT_EQUAL_INT32(kHudHeight * kOne, rising.paddleY(0));

    // Releasing freezes the paddle exactly where it is.
    rising.setPlayerInput(0, 0);
    stepN(rising, 5);
    TEST_ASSERT_EQUAL_INT32(kHudHeight * kOne, rising.paddleY(0));

    // Both buttons cancel: an ambiguous controller cannot drag the paddle.
    Rules cancelling(0);
    cancelling.setPlayerInput(0, static_cast<std::uint8_t>(kButtonLeft | kButtonRight));
    stepN(cancelling, 10);
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, cancelling.paddleY(0));

    // The right slot is driven independently; T2 replaces it with the CPU, and
    // until then a test needs manual control of it.
    Rules rightSlot(0);
    rightSlot.setPlayerInput(1, kButtonRight);
    stepN(rightSlot, 20);
    TEST_ASSERT_TRUE(rightSlot.paddleY(1) > kPaddleCentreTop);
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, rightSlot.paddleY(0));

    // Play: the same held direction still moves the paddle, all the way down to
    // the last drawable paddle position.
    Rules playing(0);
    stepN(playing, kStepsPerCountdown);
    assertPhase(playing, Phase::Play);
    const Fixed before = playing.paddleY(0);
    playing.setPlayerInput(0, kButtonRight);
    stepN(playing, 10);
    TEST_ASSERT_TRUE(playing.paddleY(0) > before);
    stepN(playing, 60);
    TEST_ASSERT_EQUAL_INT32((kCourtHeight - kPaddleHeight) * kOne, playing.paddleY(0));
}

// -----------------------------------------------------------------------------
// 5. A wall bounce reverses the vertical travel and queues one event.
// -----------------------------------------------------------------------------
void test_wall_bounce_reverses_the_vertical_travel(void) {
    Rules rules(1);  // serves left, so the left paddle is the one in play

    // Raise the paddle by seven frames so the ball meets its lower end, which
    // sends it down at the 60 degree maximum, towards the bottom wall.
    rules.setPlayerInput(0, kButtonLeft);
    stepN(rules, 7);
    rules.setPlayerInput(0, 0);

    Event hit;
    TEST_ASSERT_TRUE(stepUntilEvent(rules, EventType::PaddleHit, hit, 100));
    TEST_ASSERT_EQUAL_UINT8(0, hit.player);

    Event bounce;
    TEST_ASSERT_TRUE(stepUntilEvent(rules, EventType::WallBounce, bounce, 200));
    TEST_ASSERT_EQUAL_UINT8(0xFF, bounce.player);
    // The bounce is resolved inside the frame, so the remaining sub-steps may
    // already carry the ball a pixel or two back off the wall: it is never
    // further away than that.
    TEST_ASSERT_INT32_WITHIN(3 * kOne, (kCourtHeight - kBallRadius) * kOne, rules.ballY());

    // From here the ball climbs instead of descending.
    const Fixed atBounce = rules.ballY();
    stepN(rules, 2);
    TEST_ASSERT_TRUE(rules.ballY() < atBounce);
}

// -----------------------------------------------------------------------------
// 6. A paddle hit flips the horizontal travel and names the paddle's owner.
// -----------------------------------------------------------------------------
void test_paddle_hit_flips_the_horizontal_travel(void) {
    // Left paddle, dead centre: the ball returns flat.
    {
        Rules rules(1);
        Event hit;
        TEST_ASSERT_TRUE(stepUntilEvent(rules, EventType::PaddleHit, hit, 200));
        TEST_ASSERT_EQUAL_UINT8(0, hit.player);
        TEST_ASSERT_TRUE(rules.ballX() <= (kPaddleLeftX + kPaddleWidth + kBallRadius + 2) * kOne);

        const Fixed after = rules.ballX();
        stepN(rules, 3);
        TEST_ASSERT_TRUE(rules.ballX() > after);
        TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());
    }

    // Right paddle, dead centre: the ball returns left.
    {
        Rules rules(0);
        Event hit;
        TEST_ASSERT_TRUE(stepUntilEvent(rules, EventType::PaddleHit, hit, 200));
        TEST_ASSERT_EQUAL_UINT8(1, hit.player);
        TEST_ASSERT_TRUE(rules.ballX() >= (kPaddleRightX - kBallRadius - 2) * kOne);

        const Fixed after = rules.ballX();
        stepN(rules, 3);
        TEST_ASSERT_TRUE(rules.ballX() < after);
        TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());
    }
}

// -----------------------------------------------------------------------------
// 7. The ball past each edge scores for the opposite side, then waits the
//    point pause before serving again.
// -----------------------------------------------------------------------------
void test_ball_past_each_edge_scores_for_the_other_side(void) {
    // Right edge: player 0 scores.
    {
        Rules rules(0);
        parkPaddlesAtTop(rules);
        stepN(rules, kStepsPerCountdown - 28);

        EventTally tally;
        drainInto(rules, tally);
        TEST_ASSERT_EQUAL_INT(1, tally.serveStarted);
        TEST_ASSERT_EQUAL_UINT8(0, tally.lastServePlayer);

        TEST_ASSERT_TRUE(stepUntilGoal(rules, tally, 200));
        TEST_ASSERT_EQUAL_INT(1, tally.goal);
        TEST_ASSERT_EQUAL_UINT8(0, tally.lastGoalPlayer);
        TEST_ASSERT_EQUAL_UINT8(1, rules.score(0));
        TEST_ASSERT_EQUAL_UINT8(0, rules.score(1));
        assertPhase(rules, Phase::Point);
        TEST_ASSERT_TRUE(rules.ballX() - kBallRadius * kOne > kCourtWidth * kOne);

        // The serve waits the whole pause, then rebuilds the centre.
        stepN(rules, stepsForMs(kPointPauseMs) - 1);
        assertPhase(rules, Phase::Point);
        rules.step(kStepDtUs);
        assertPhase(rules, Phase::Play);
        TEST_ASSERT_EQUAL_INT32(kBallCenterX, rules.ballX());
        TEST_ASSERT_EQUAL_INT32(kBallCenterY, rules.ballY());

        EventTally next;
        drainInto(rules, next);
        TEST_ASSERT_EQUAL_INT(1, next.serveStarted);
        TEST_ASSERT_EQUAL_UINT8(1, next.lastServePlayer);  // direction flipped
    }

    // Left edge: player 1 scores.
    {
        Rules rules(1);
        parkPaddlesAtTop(rules);
        stepN(rules, kStepsPerCountdown - 28);

        EventTally tally;
        TEST_ASSERT_TRUE(stepUntilGoal(rules, tally, 200));
        TEST_ASSERT_EQUAL_INT(1, tally.goal);
        TEST_ASSERT_EQUAL_UINT8(1, tally.lastGoalPlayer);
        TEST_ASSERT_EQUAL_UINT8(1, rules.score(1));
        TEST_ASSERT_EQUAL_UINT8(0, rules.score(0));
        assertPhase(rules, Phase::Point);
        TEST_ASSERT_TRUE(rules.ballX() + kBallRadius * kOne < 0);
    }
}

// -----------------------------------------------------------------------------
// 8. Serve direction alternates and the three-serve angle sequence repeats.
// -----------------------------------------------------------------------------
void test_serves_alternate_direction_and_repeat_the_angle_sequence(void) {
    Rules rules(0);
    parkPaddlesAtTop(rules);
    stepN(rules, kStepsPerCountdown - 29);  // one step short of the first serve

    ServeProbe probes[5];
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_TRUE_MESSAGE(waitForServe(rules, probes[i]), "a serve never arrived");
        TEST_ASSERT_EQUAL_INT32(kBallCenterX, probes[i].x);
        TEST_ASSERT_EQUAL_INT32(kBallCenterY, probes[i].y);
    }

    // 180 px/s at 60 Hz is exactly three pixels: the flat serve.
    TEST_ASSERT_EQUAL_INT32(3 * kOne, probes[0].dx);
    TEST_ASSERT_EQUAL_INT32(0, probes[0].dy);
    TEST_ASSERT_EQUAL_UINT8(0, probes[0].player);

    // Next serve: the other direction and the +20 degree slot.
    TEST_ASSERT_TRUE(probes[1].dx < 0);
    TEST_ASSERT_TRUE(probes[1].dy > 0);
    TEST_ASSERT_EQUAL_UINT8(1, probes[1].player);
    TEST_ASSERT_TRUE(absFixed(probes[1].dx) < absFixed(probes[0].dx));  // cos(20) < 1
    TEST_ASSERT_TRUE(probes[1].dy > 0);                                 // sin(20), ~one pixel
    TEST_ASSERT_TRUE(probes[1].dy < 2 * kOne);

    // Third serve: direction flips again and the angle mirrors.
    TEST_ASSERT_TRUE(probes[2].dx > 0);
    TEST_ASSERT_TRUE(probes[2].dy < 0);
    TEST_ASSERT_EQUAL_UINT8(0, probes[2].player);
    TEST_ASSERT_EQUAL_INT32(probes[1].dx, -probes[2].dx);
    TEST_ASSERT_EQUAL_INT32(probes[1].dy, -probes[2].dy);

    // Fourth serve closes the angle cycle back on the flat slot, opposite the
    // first serve's direction.
    TEST_ASSERT_EQUAL_INT32(-3 * kOne, probes[3].dx);
    TEST_ASSERT_EQUAL_INT32(0, probes[3].dy);
    TEST_ASSERT_EQUAL_UINT8(1, probes[3].player);

    // Fifth serve repeats the second slot's angle with the direction flipped:
    // the three-entry sequence has wrapped.
    TEST_ASSERT_TRUE(probes[4].dx > 0);
    TEST_ASSERT_EQUAL_INT32(absFixed(probes[1].dx), absFixed(probes[4].dx));
    TEST_ASSERT_EQUAL_INT32(probes[1].dy, probes[4].dy);
    TEST_ASSERT_EQUAL_UINT8(0, probes[4].player);

    // firstServer = 1 opens towards the left instead.
    Rules leftFirst(1);
    parkPaddlesAtTop(leftFirst);
    stepN(leftFirst, kStepsPerCountdown - 29);
    ServeProbe opening;
    TEST_ASSERT_TRUE(waitForServe(leftFirst, opening));
    TEST_ASSERT_EQUAL_INT32(-3 * kOne, opening.dx);
    TEST_ASSERT_EQUAL_UINT8(1, opening.player);
}

// -----------------------------------------------------------------------------
// 9. Reaching kWinScore enters MatchOver, queues the event and freezes.
// -----------------------------------------------------------------------------
void test_reaching_the_win_score_ends_the_match(void) {
    Rules rules(0);
    parkPaddlesAtTop(rules);
    stepN(rules, kStepsPerCountdown - 28);

    EventTally tally;
    int guard = 0;
    while (rules.phase() != Phase::MatchOver && guard < 4000) {
        rules.step(kStepDtUs);
        drainInto(rules, tally);
        ++guard;
    }
    assertPhase(rules, Phase::MatchOver);
    TEST_ASSERT_EQUAL_INT(9, tally.goal);
    TEST_ASSERT_EQUAL_INT(1, tally.matchOver);
    TEST_ASSERT_EQUAL_UINT8(0, tally.lastMatchOverPlayer);
    TEST_ASSERT_EQUAL_UINT8(kWinScore, rules.score(0));
    TEST_ASSERT_EQUAL_UINT8(kWinScore - 1, rules.score(1));

    // A finished match consumes no time: frozen score, no repeated event.
    stepN(rules, stepsForMs(600));
    assertPhase(rules, Phase::MatchOver);
    TEST_ASSERT_EQUAL_UINT8(kWinScore, rules.score(0));
    TEST_ASSERT_EQUAL_UINT8(kWinScore - 1, rules.score(1));
    drainInto(rules, tally);
    TEST_ASSERT_EQUAL_INT(1, tally.matchOver);
}

// -----------------------------------------------------------------------------
// 10. restart() is ignored outside MatchOver; from it the match rebuilds.
// -----------------------------------------------------------------------------
void test_restart_only_rebuilds_a_finished_match(void) {
    // Ignored in Countdown: the timer keeps its place.
    Rules counting(0);
    stepN(counting, 10);
    const std::uint32_t remaining = counting.countdownRemainingMs();
    counting.restart();
    assertPhase(counting, Phase::Countdown);
    TEST_ASSERT_EQUAL_UINT32(remaining, counting.countdownRemainingMs());

    // Ignored in Play.
    Rules playing(0);
    stepN(playing, kStepsPerCountdown);
    assertPhase(playing, Phase::Play);
    playing.restart();
    assertPhase(playing, Phase::Play);

    // Ignored in Point, even with a point already on the board.
    Rules pointing(0);
    parkPaddlesAtTop(pointing);
    stepN(pointing, kStepsPerCountdown - 28);
    EventTally pointTally;
    TEST_ASSERT_TRUE(stepUntilGoal(pointing, pointTally, 200));
    assertPhase(pointing, Phase::Point);
    pointing.restart();
    assertPhase(pointing, Phase::Point);
    TEST_ASSERT_EQUAL_UINT8(1, pointing.score(0));

    // From MatchOver it rebuilds a 0-0 countdown at the centre.
    Rules finished(0);
    parkPaddlesAtTop(finished);
    stepN(finished, kStepsPerCountdown - 28);
    EventTally finishedTally;
    int guard = 0;
    while (finished.phase() != Phase::MatchOver && guard < 4000) {
        finished.step(kStepDtUs);
        // The scene drains the ring every frame; keep that contract here so the
        // rebuilt match is measured from a clean event stream.
        drainInto(finished, finishedTally);
        ++guard;
    }
    assertPhase(finished, Phase::MatchOver);

    finished.restart();
    assertPhase(finished, Phase::Countdown);
    TEST_ASSERT_EQUAL_UINT8(0, finished.score(0));
    TEST_ASSERT_EQUAL_UINT8(0, finished.score(1));
    TEST_ASSERT_EQUAL_INT32(kBallCenterX, finished.ballX());
    TEST_ASSERT_EQUAL_INT32(kBallCenterY, finished.ballY());
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, finished.paddleY(0));
    TEST_ASSERT_EQUAL_INT32(kPaddleCentreTop, finished.paddleY(1));
    TEST_ASSERT_EQUAL_UINT32(kCountdownMs, finished.countdownRemainingMs());

    // And the rebuilt match serves again, from the first serve's direction.
    ServeProbe serve;
    TEST_ASSERT_TRUE(waitForServe(finished, serve));
    TEST_ASSERT_EQUAL_INT32(3 * kOne, serve.dx);
    TEST_ASSERT_EQUAL_UINT8(0, serve.player);
}

// -----------------------------------------------------------------------------
// 11. The event ring keeps order and drops the newest when it is full.
// -----------------------------------------------------------------------------
void test_event_ring_preserves_order_and_drops_the_newest(void) {
    Rules rules(0);
    parkPaddlesAtTop(rules);
    stepN(rules, kStepsPerCountdown - 28);

    // Run the whole set without draining: nine serves, nine goals and the final
    // MatchOver queue nineteen events into eight slots.
    int guard = 0;
    while (rules.phase() != Phase::MatchOver && guard < 4000) {
        rules.step(kStepDtUs);
        ++guard;
    }
    assertPhase(rules, Phase::MatchOver);

    Event event;
    int count = 0;
    EventType seen[Rules::kMaxEvents];
    std::uint8_t owner[Rules::kMaxEvents];
    while (rules.pollEvent(event)) {
        TEST_ASSERT_TRUE(count < Rules::kMaxEvents);
        seen[count] = event.type;
        owner[count] = event.player;
        ++count;
    }
    TEST_ASSERT_EQUAL_INT(Rules::kMaxEvents, count);

    // The oldest eight survive, in order: serve, goal, serve, goal, ... The
    // newest three -- the last serve, its goal and the MatchOver -- are lost.
    for (int i = 0; i < count; ++i) {
        const EventType expected = (i % 2 == 0) ? EventType::ServeStarted : EventType::Goal;
        const std::uint8_t expectedOwner = static_cast<std::uint8_t>((i / 2) % 2);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(seen[i]));
        TEST_ASSERT_EQUAL_UINT8(expectedOwner, owner[i]);
    }

    // Nothing followed the eight kept events.
    TEST_ASSERT_FALSE(rules.pollEvent(event));
}

// -----------------------------------------------------------------------------
// 12. Two instances fed the same script export identical state on every step.
// -----------------------------------------------------------------------------
void test_two_rules_replay_the_same_match(void) {
    Rules left(0);
    Rules right(0);

    for (int step = 0; step < 1200; ++step) {
        // A deterministic script with every input state (none, up, down, both)
        // on both slots, including the serves and goals it does not control.
        const std::uint8_t p1 = static_cast<std::uint8_t>((step / 37) % 4);
        const std::uint8_t p2 = static_cast<std::uint8_t>((step / 53) % 4);
        left.setPlayerInput(0, p1);
        left.setPlayerInput(1, p2);
        right.setPlayerInput(0, p1);
        right.setPlayerInput(1, p2);
        left.step(kStepDtUs);
        right.step(kStepDtUs);

        assertSameState(right, left);

        // The event stream is part of the contract too: same kinds, same
        // owners, same order, on the same step.
        Event leftEvent;
        Event rightEvent;
        while (left.pollEvent(leftEvent)) {
            TEST_ASSERT_TRUE_MESSAGE(right.pollEvent(rightEvent), "event counts diverged");
            TEST_ASSERT_EQUAL_INT(static_cast<int>(leftEvent.type), static_cast<int>(rightEvent.type));
            TEST_ASSERT_EQUAL_UINT8(leftEvent.player, rightEvent.player);
        }
        TEST_ASSERT_FALSE(right.pollEvent(rightEvent));
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_fresh_match_counts_down_at_zero_zero_with_a_still_ball);
    RUN_TEST(test_countdown_reaching_zero_serves_the_ball);
    RUN_TEST(test_ball_does_not_move_during_the_countdown);
    RUN_TEST(test_held_input_moves_the_paddle_and_clamps_to_the_play_area);
    RUN_TEST(test_wall_bounce_reverses_the_vertical_travel);
    RUN_TEST(test_paddle_hit_flips_the_horizontal_travel);
    RUN_TEST(test_ball_past_each_edge_scores_for_the_other_side);
    RUN_TEST(test_serves_alternate_direction_and_repeat_the_angle_sequence);
    RUN_TEST(test_reaching_the_win_score_ends_the_match);
    RUN_TEST(test_restart_only_rebuilds_a_finished_match);
    RUN_TEST(test_event_ring_preserves_order_and_drops_the_newest);
    RUN_TEST(test_two_rules_replay_the_same_match);

    return UNITY_END();
}
