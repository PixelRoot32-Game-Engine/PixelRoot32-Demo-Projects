/*
 * Behaviour tests for the networking Pong rules core.
 *
 * The rules core is the authority of the whole demo: the CYD runs it, the
 * controllers only ever see what it exports. So these tests drive the public
 * surface the way the host adapter and the scene will (setPlayerInput, step,
 * snapshot, pollEvent) and assert the observable contract, never internals.
 *
 * Two properties get special attention because they cannot be fixed later
 * without changing the wire:
 *  - the phase enum values must stay equal to the net::kPhase* bytes, and the
 *    snapshot must stay inside int16 so a controller never sees a wrapped sign;
 *  - the simulation must be deterministic: no clock, no RNG, no float. The last
 *    test replays one input script through two instances and compares every
 *    exported field on every sixtieth step.
 *
 * The ball is never teleported by a test hook: the rules expose no setter on
 * purpose, so every scenario is built by serving and stepping, exactly as the
 * host will build it on the CYD.
 */
#include <unity.h>

#include <cstdint>

#include "game/PongRules.h"
#include "net/PongProtocol.h"

using namespace netpong::game;

namespace {

// The phase bytes and the input bits are duplicated in the wire layer by design
// (the rules core has no networking dependency), so pin the two copies together
// here: a change on either side stops the test build instead of the demo.
static_assert(static_cast<std::uint8_t>(Phase::WaitingForPlayers) == netpong::net::kPhaseWaitingForPlayers,
              "Phase::WaitingForPlayers must equal the wire value");
static_assert(static_cast<std::uint8_t>(Phase::Countdown) == netpong::net::kPhaseCountdown,
              "Phase::Countdown must equal the wire value");
static_assert(static_cast<std::uint8_t>(Phase::Play) == netpong::net::kPhasePlay,
              "Phase::Play must equal the wire value");
static_assert(static_cast<std::uint8_t>(Phase::Point) == netpong::net::kPhasePoint,
              "Phase::Point must equal the wire value");
static_assert(static_cast<std::uint8_t>(Phase::MatchOver) == netpong::net::kPhaseMatchOver,
              "Phase::MatchOver must equal the wire value");
static_assert(static_cast<std::uint8_t>(Phase::Paused) == netpong::net::kPhasePaused,
              "Phase::Paused must equal the wire value");
static_assert(kButtonLeft == netpong::net::kButtonLeft, "left button bit must match the wire");
static_assert(kButtonRight == netpong::net::kButtonRight, "right button bit must match the wire");

// Whole-pixel expectations derived from the fixed-point contract. They are
// written from the physical result (sin/cos of the angle times 180 px/s) rather
// than copied from the implementation table, so a mistyped table entry fails.
constexpr std::int16_t kBallPxPerSecond = static_cast<std::int16_t>(kBallSpeed >> kFixedShift);  // 180
constexpr std::int16_t kServe20VX = 168;     // 180 * cos(20 deg) = 169.1, table x240/256 -> 168.75
constexpr std::int16_t kServe20VXNeg = -169; // the arithmetic shift floors: -168.75 -> -169
constexpr std::int16_t kServe20VY = 61;      // 180 * sin(20 deg) = 61.6
constexpr std::int16_t kServe20VYNeg = -62;  // floor(-61.6)
constexpr std::int16_t kEdgeVX = 90;      // 180 * cos(60 deg) = 90
constexpr std::int16_t kEdgeVY = 156;     // 180 * sin(60 deg) = 155.9
constexpr std::int16_t kEdgeVYNeg = -157; // floor(-155.9) after the arithmetic shift
constexpr Fixed kHalfPaddle = (kPaddleHeight / 2) * kOne;

/// Number of fixed steps needed to cover `ms` of stepped time.
int stepsForMs(std::uint32_t ms) {
    const std::uint32_t totalUs = ms * 1000U;
    return static_cast<int>((totalUs + kStepDtUs - 1U) / kStepDtUs);
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

/// WaitingForPlayers -> Countdown -> Play with the default centre paddles.
void beginPlay(Rules& rules) {
    rules.setPlayersReady(true);
    stepN(rules, stepsForMs(kCountdownMs));
}

/// Starts a match with both paddles parked against the top wall.
///
/// A parked paddle is how a test asks the court, not a paddle, to decide a
/// rally: the served ball travels at y = 132 and the paddle occupies y = 24..68,
/// so every serve reaches the edge and scores. Parking takes 28 frames, all of
/// them inside the 1200 ms countdown, so the serve is still the first one.
void beginPlayWithPaddlesAtTop(Rules& rules) {
    rules.setPlayersReady(true);
    rules.setPlayerInput(0, kButtonLeft);
    rules.setPlayerInput(1, kButtonLeft);
    stepN(rules, 28);
    rules.setPlayerInput(0, 0);
    rules.setPlayerInput(1, 0);
    stepN(rules, stepsForMs(kCountdownMs) - 28);
}

/// Counts every event kind plus the owner of the events that carry one.
struct EventTally {
    int paddleHit = 0;
    int wallBounce = 0;
    int goal = 0;
    int serveStarted = 0;
    int matchOver = 0;
    int paused = 0;
    int resumed = 0;
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
            case EventType::Paused: ++paused; break;
            case EventType::Resumed: ++resumed; break;
        }
    }
};

void drainInto(Rules& rules, EventTally& tally) {
    Event event;
    while (rules.pollEvent(event)) {
        tally.add(event);
    }
}

/// Compares two snapshots field by field.
///
/// Deliberately not a memcmp: the struct has tail padding, which is not part of
/// the value and must never decide whether two matches are equal.
void assertSameSnapshot(const Snapshot& expected, const Snapshot& actual) {
    TEST_ASSERT_EQUAL_UINT16(expected.seq, actual.seq);
    TEST_ASSERT_EQUAL_UINT8(expected.phase, actual.phase);
    TEST_ASSERT_EQUAL_INT16(expected.ballX, actual.ballX);
    TEST_ASSERT_EQUAL_INT16(expected.ballY, actual.ballY);
    TEST_ASSERT_EQUAL_INT16(expected.ballVX, actual.ballVX);
    TEST_ASSERT_EQUAL_INT16(expected.ballVY, actual.ballVY);
    TEST_ASSERT_EQUAL_INT16(expected.p1Y, actual.p1Y);
    TEST_ASSERT_EQUAL_INT16(expected.p2Y, actual.p2Y);
    TEST_ASSERT_EQUAL_UINT8(expected.score1, actual.score1);
    TEST_ASSERT_EQUAL_UINT8(expected.score2, actual.score2);
    TEST_ASSERT_EQUAL_UINT8(expected.flags, actual.flags);
}

/// Asserts every int16 field really is an int16 value (never a wrapped Fixed).
void assertFieldsFitInt16(const Snapshot& snapshot) {
    TEST_ASSERT_TRUE(snapshot.ballX >= -32768 && snapshot.ballX <= 32767);
    TEST_ASSERT_TRUE(snapshot.ballY >= -32768 && snapshot.ballY <= 32767);
    TEST_ASSERT_TRUE(snapshot.ballVX >= -32768 && snapshot.ballVX <= 32767);
    TEST_ASSERT_TRUE(snapshot.ballVY >= -32768 && snapshot.ballVY <= 32767);
    TEST_ASSERT_TRUE(snapshot.p1Y >= -32768 && snapshot.p1Y <= 32767);
    TEST_ASSERT_TRUE(snapshot.p2Y >= -32768 && snapshot.p2Y <= 32767);
}

/// Asserts the exported ball speed is within 2 % of kBallSpeed.
///
/// Uses the snapshot's whole pixels per second, which is what a controller will
/// see: 2 % of 180 is 3.6 px/s, far above the sub-pixel loss of the shift.
void assertSpeedWithinTwoPercent(const Snapshot& snapshot) {
    const std::int64_t speedSq = static_cast<std::int64_t>(snapshot.ballVX) * snapshot.ballVX +
                                 static_cast<std::int64_t>(snapshot.ballVY) * snapshot.ballVY;
    const std::int64_t targetSq = static_cast<std::int64_t>(kBallPxPerSecond) * kBallPxPerSecond;
    TEST_ASSERT_TRUE_MESSAGE(speedSq * 100 >= targetSq * 96, "ball speed dropped below 98 %");
    TEST_ASSERT_TRUE_MESSAGE(speedSq * 100 <= targetSq * 104, "ball speed rose above 102 %");
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

/// Steps until a PaddleHit is tallied, returning false when the cap is hit.
bool stepUntilPaddleHit(Rules& rules, EventTally& tally, int maxSteps) {
    int steps = 0;
    while (tally.paddleHit == 0 && steps < maxSteps) {
        rules.step(kStepDtUs);
        drainInto(rules, tally);
        ++steps;
    }
    return tally.paddleHit > 0;
}

}  // namespace

// -----------------------------------------------------------------------------
// 1. A new match waits, scores nothing and keeps the ball still.
// -----------------------------------------------------------------------------
void test_new_match_waits_with_a_still_ball(void) {
    Rules rules;

    assertPhase(rules, Phase::WaitingForPlayers);
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(0));
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(1));
    TEST_ASSERT_EQUAL_INT32(160 * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());
    TEST_ASSERT_EQUAL_INT32(110 * kOne, rules.paddleY(0));
    TEST_ASSERT_EQUAL_INT32(110 * kOne, rules.paddleY(1));

    const Snapshot snapshot = rules.snapshot(7);
    TEST_ASSERT_EQUAL_UINT16(7, snapshot.seq);
    TEST_ASSERT_EQUAL_UINT8(netpong::net::kPhaseWaitingForPlayers, snapshot.phase);
    TEST_ASSERT_EQUAL_INT16(0, snapshot.ballVX);
    TEST_ASSERT_EQUAL_INT16(0, snapshot.ballVY);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.score1);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.score2);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.flags);

    // A match nobody joined does not move a ball, however much time passes.
    stepN(rules, stepsForMs(600));
    assertPhase(rules, Phase::WaitingForPlayers);
    TEST_ASSERT_EQUAL_INT32(160 * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());
    Event event;
    TEST_ASSERT_FALSE(rules.pollEvent(event));
}

// -----------------------------------------------------------------------------
// 2. Ready starts the countdown; the countdown expires into Play with a serve.
// -----------------------------------------------------------------------------
void test_ready_enters_countdown_then_play_and_serve(void) {
    Rules rules(0);

    rules.setPlayersReady(true);
    assertPhase(rules, Phase::Countdown);

    // One step short of the countdown: still not live, and no serve announced.
    stepN(rules, stepsForMs(kCountdownMs) - 1);
    assertPhase(rules, Phase::Countdown);
    Event event;
    TEST_ASSERT_FALSE(rules.pollEvent(event));

    rules.step(kStepDtUs);
    assertPhase(rules, Phase::Play);

    int serves = 0;
    std::uint8_t server = 0xFF;
    while (rules.pollEvent(event)) {
        if (event.type == EventType::ServeStarted) {
            ++serves;
            server = event.player;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, serves);
    TEST_ASSERT_EQUAL_UINT8(0, server);  // firstServer = 0 serves right
    TEST_ASSERT_EQUAL_INT32(160 * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());
}

// -----------------------------------------------------------------------------
// 3. Held input moves a paddle at kPaddleSpeed and clamps it to the play area.
// -----------------------------------------------------------------------------
void test_paddles_move_at_the_contract_speed_and_clamp(void) {
    Rules rules(0);
    rules.setPlayersReady(true);

    // 20 frames = 333 ms, about 63 px: measured before the clamp so the number
    // is the speed, not the wall. (A full second is 190 px, more than the 172 px
    // of travel between the clamps.)
    rules.setPlayerInput(0, kButtonRight);
    stepN(rules, 20);
    const std::int64_t travelledUs = static_cast<std::int64_t>(20) * kStepDtUs;
    const std::int32_t ideal = 110 * kOne + static_cast<std::int32_t>(
                                               (static_cast<std::int64_t>(kPaddleSpeed) * travelledUs) / 1000000);
    TEST_ASSERT_INT32_WITHIN(kOne, ideal, rules.paddleY(0));

    // Keeping it held for a full second pins the clamp at the bottom edge.
    rules.setPlayerInput(0, kButtonRight);
    stepN(rules, 40);
    TEST_ASSERT_EQUAL_INT32((kCourtHeight - kPaddleHeight) * kOne, rules.paddleY(0));

    // Releasing freezes the paddle exactly where it is.
    rules.setPlayerInput(0, 0);
    stepN(rules, 5);
    TEST_ASSERT_EQUAL_INT32((kCourtHeight - kPaddleHeight) * kOne, rules.paddleY(0));

    // Player two mirrors the input contract: left is up, and the top clamps.
    Rules other(0);
    other.setPlayersReady(true);
    other.setPlayerInput(1, kButtonLeft);
    stepN(other, 60);
    TEST_ASSERT_EQUAL_INT32(kHudHeight * kOne, other.paddleY(1));

    // Both buttons cancel: an ambiguous controller cannot drag the paddle.
    Rules cancelling(0);
    cancelling.setPlayersReady(true);
    cancelling.setPlayerInput(0, static_cast<std::uint8_t>(kButtonLeft | kButtonRight));
    stepN(cancelling, 10);
    TEST_ASSERT_EQUAL_INT32(110 * kOne, cancelling.paddleY(0));
}

// -----------------------------------------------------------------------------
// 4. Wall bounces flip the vertical velocity and queue one event per bounce.
// -----------------------------------------------------------------------------
void test_wall_bounce_flips_velocity_and_queues_one_event(void) {
    Rules rules(1);  // serves left, so the left paddle is the one in play
    rules.setPlayersReady(true);

    // Raise the paddle by seven frames so the ball hits its lower end, which
    // sends the ball down at the 60 degree maximum and towards the bottom wall.
    rules.setPlayerInput(0, kButtonLeft);
    stepN(rules, 7);
    rules.setPlayerInput(0, 0);
    stepN(rules, stepsForMs(kCountdownMs) - 7);

    EventTally tally;
    TEST_ASSERT_TRUE(stepUntilPaddleHit(rules, tally, 100));
    TEST_ASSERT_EQUAL_INT(1, tally.paddleHit);
    const Snapshot hit = rules.snapshot(0);
    TEST_ASSERT_EQUAL_INT16(kEdgeVX, hit.ballVX);
    TEST_ASSERT_EQUAL_INT16(kEdgeVY, hit.ballVY);

    // Down to the bottom wall: snapped to the last pixel the ball may occupy.
    int steps = 0;
    while (tally.wallBounce == 0 && steps < 200) {
        rules.step(kStepDtUs);
        drainInto(rules, tally);
        ++steps;
    }
    TEST_ASSERT_EQUAL_INT(1, tally.wallBounce);
    TEST_ASSERT_EQUAL_UINT8(0xFF, tally.lastWallPlayer);
    // The bounce is resolved inside the frame, so the remaining sub-steps may
    // already carry the ball one pixel back off the wall: it is never further
    // away than that, and it is heading up.
    TEST_ASSERT_INT32_WITHIN(kOne, (kCourtHeight - kBallRadius) * kOne, rules.ballY());
    TEST_ASSERT_TRUE(rules.snapshot(0).ballVY < 0);

    // Up to the top wall: exactly one more event, not one per step.
    steps = 0;
    while (tally.wallBounce == 1 && steps < 200) {
        rules.step(kStepDtUs);
        drainInto(rules, tally);
        ++steps;
    }
    TEST_ASSERT_EQUAL_INT(2, tally.wallBounce);
    TEST_ASSERT_INT32_WITHIN(kOne, (kHudHeight + kBallRadius) * kOne, rules.ballY());
    TEST_ASSERT_TRUE(rules.snapshot(0).ballVY > 0);
    TEST_ASSERT_EQUAL_INT(0, tally.goal);
}

// -----------------------------------------------------------------------------
// 5. A paddle hit flips vx, keeps the speed and maps the contact point to vy.
// -----------------------------------------------------------------------------
void test_paddle_hit_flips_vx_and_keeps_speed(void) {
    // Centre contact: vy is exactly zero.
    {
        Rules rules(1);
        beginPlay(rules);
        TEST_ASSERT_EQUAL_INT16(-kBallPxPerSecond, rules.snapshot(0).ballVX);

        EventTally tally;
        TEST_ASSERT_TRUE(stepUntilPaddleHit(rules, tally, 100));
        TEST_ASSERT_EQUAL_INT(1, tally.paddleHit);

        const Snapshot hit = rules.snapshot(0);
        TEST_ASSERT_EQUAL_INT16(kBallPxPerSecond, hit.ballVX);
        TEST_ASSERT_EQUAL_INT16(0, hit.ballVY);
        assertSpeedWithinTwoPercent(hit);
    }

    // Edge contact: the ball leaves at the 60 degree maximum, upwards.
    {
        Rules rules(1);
        rules.setPlayersReady(true);
        rules.setPlayerInput(0, kButtonRight);
        stepN(rules, 7);  // paddle centre at ~154, so the ball meets its top edge
        rules.setPlayerInput(0, 0);
        stepN(rules, stepsForMs(kCountdownMs) - 7);

        EventTally tally;
        TEST_ASSERT_TRUE(stepUntilPaddleHit(rules, tally, 100));
        TEST_ASSERT_EQUAL_INT(1, tally.paddleHit);

        const Snapshot hit = rules.snapshot(0);
        TEST_ASSERT_EQUAL_INT16(kEdgeVX, hit.ballVX);
        TEST_ASSERT_EQUAL_INT16(kEdgeVYNeg, hit.ballVY);
        assertSpeedWithinTwoPercent(hit);
    }
}

// -----------------------------------------------------------------------------
// 6. Sub-stepping catches a ball that crosses a paddle within one step.
// -----------------------------------------------------------------------------
void test_ball_crossing_a_paddle_in_one_step_is_caught(void) {
    Rules rules(1);
    beginPlay(rules);

    // Move at the fixed speed until the ball is close to the paddle but not
    // touching it (overlap starts below x = 17).
    int guard = 0;
    while (rules.ballX() > 30 * kOne && guard < 100) {
        rules.step(kStepDtUs);
        ++guard;
    }
    TEST_ASSERT_TRUE(rules.ballX() <= 30 * kOne);
    TEST_ASSERT_TRUE(rules.ballX() > 17 * kOne);

    // A 44444 us step moves the ball 8 px and crosses the 6 px paddle. Without
    // sub-steps the ball would appear on the far side and the rally would be
    // lost; ten such steps must produce exactly one hit and no goal.
    EventTally tally;
    for (int i = 0; i < 10; ++i) {
        rules.step(44444);
        drainInto(rules, tally);
    }
    TEST_ASSERT_EQUAL_INT(1, tally.paddleHit);
    TEST_ASSERT_EQUAL_INT(0, tally.goal);
    TEST_ASSERT_TRUE(rules.snapshot(0).ballVX > 0);
    TEST_ASSERT_TRUE(rules.ballX() >= (kPaddleLeftX + kPaddleWidth + kBallRadius) * kOne);
}

// -----------------------------------------------------------------------------
// 7. The ball past an edge scores for the opposite side and enters Point.
// -----------------------------------------------------------------------------
void test_ball_past_each_edge_scores_for_the_other_side(void) {
    // Left edge: player one scores.
    {
        Rules rules(1);
        beginPlayWithPaddlesAtTop(rules);
        EventTally tally;
        TEST_ASSERT_TRUE(stepUntilGoal(rules, tally, 200));
        TEST_ASSERT_EQUAL_INT(1, tally.goal);
        TEST_ASSERT_EQUAL_UINT8(1, tally.lastGoalPlayer);
        TEST_ASSERT_EQUAL_UINT8(1, rules.score(1));
        TEST_ASSERT_EQUAL_UINT8(0, rules.score(0));
        assertPhase(rules, Phase::Point);
        TEST_ASSERT_TRUE(rules.ballX() + kBallRadius * kOne < 0);
    }

    // Right edge: player zero scores.
    {
        Rules rules(0);
        beginPlayWithPaddlesAtTop(rules);
        EventTally tally;
        TEST_ASSERT_TRUE(stepUntilGoal(rules, tally, 200));
        TEST_ASSERT_EQUAL_INT(1, tally.goal);
        TEST_ASSERT_EQUAL_UINT8(0, tally.lastGoalPlayer);
        TEST_ASSERT_EQUAL_UINT8(1, rules.score(0));
        TEST_ASSERT_EQUAL_UINT8(0, rules.score(1));
        assertPhase(rules, Phase::Point);
        TEST_ASSERT_TRUE(rules.ballX() - kBallRadius * kOne > kCourtWidth * kOne);
    }
}

// -----------------------------------------------------------------------------
// 8. After kPointPauseMs the next serve alternates direction and angle.
// -----------------------------------------------------------------------------
void test_serves_alternate_direction_and_follow_the_angle_sequence(void) {
    Rules rules(1);  // first serve travels left
    beginPlayWithPaddlesAtTop(rules);

    EventTally totals;
    drainInto(rules, totals);
    TEST_ASSERT_EQUAL_INT(1, totals.serveStarted);
    TEST_ASSERT_EQUAL_UINT8(1, totals.lastServePlayer);

    const Snapshot serve0 = rules.snapshot(0);
    TEST_ASSERT_EQUAL_INT16(-kBallPxPerSecond, serve0.ballVX);
    TEST_ASSERT_EQUAL_INT16(0, serve0.ballVY);

    // First point ends the rally; the serve must wait the full pause.
    TEST_ASSERT_TRUE(stepUntilGoal(rules, totals, 200));
    assertPhase(rules, Phase::Point);
    stepN(rules, stepsForMs(kPointPauseMs) - 1);
    assertPhase(rules, Phase::Point);
    rules.step(kStepDtUs);
    assertPhase(rules, Phase::Play);

    EventTally second;
    drainInto(rules, second);
    TEST_ASSERT_EQUAL_INT(1, second.serveStarted);
    TEST_ASSERT_EQUAL_UINT8(0, second.lastServePlayer);
    const Snapshot serve1 = rules.snapshot(0);
    TEST_ASSERT_EQUAL_INT16(kServe20VX, serve1.ballVX);   // direction flipped
    TEST_ASSERT_EQUAL_INT16(kServe20VY, serve1.ballVY);   // angle +20 deg
    TEST_ASSERT_EQUAL_INT32(160 * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());

    // Second point; the third serve flips again and takes the -20 deg slot.
    TEST_ASSERT_TRUE(stepUntilGoal(rules, second, 200));
    assertPhase(rules, Phase::Point);
    stepN(rules, stepsForMs(kPointPauseMs));
    assertPhase(rules, Phase::Play);

    EventTally third;
    drainInto(rules, third);
    TEST_ASSERT_EQUAL_INT(1, third.serveStarted);
    TEST_ASSERT_EQUAL_UINT8(1, third.lastServePlayer);
    const Snapshot serve2 = rules.snapshot(0);
    TEST_ASSERT_EQUAL_INT16(kServe20VXNeg, serve2.ballVX);
    TEST_ASSERT_EQUAL_INT16(kServe20VYNeg, serve2.ballVY);
}

// -----------------------------------------------------------------------------
// 9. Five points end the match; restart rebuilds a 0-0 countdown.
// -----------------------------------------------------------------------------
void test_five_points_end_the_match_and_restart_resets_it(void) {
    Rules rules(1);
    beginPlayWithPaddlesAtTop(rules);

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
    TEST_ASSERT_EQUAL_UINT8(1, tally.lastMatchOverPlayer);
    TEST_ASSERT_EQUAL_UINT8(5, rules.score(1));
    TEST_ASSERT_EQUAL_UINT8(4, rules.score(0));

    // The score is frozen: a finished match ignores time.
    stepN(rules, stepsForMs(600));
    assertPhase(rules, Phase::MatchOver);
    TEST_ASSERT_EQUAL_UINT8(5, rules.score(1));
    TEST_ASSERT_EQUAL_UINT8(4, rules.score(0));
    drainInto(rules, tally);
    TEST_ASSERT_EQUAL_INT(1, tally.matchOver);

    rules.restart();
    assertPhase(rules, Phase::Countdown);
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(0));
    TEST_ASSERT_EQUAL_UINT8(0, rules.score(1));
    TEST_ASSERT_EQUAL_INT32(110 * kOne, rules.paddleY(0));

    EventTally restarted;
    stepN(rules, stepsForMs(kCountdownMs));
    assertPhase(rules, Phase::Play);
    drainInto(rules, restarted);
    TEST_ASSERT_EQUAL_INT(1, restarted.serveStarted);
    TEST_ASSERT_EQUAL_UINT8(1, restarted.lastServePlayer);  // first server again
    TEST_ASSERT_EQUAL_INT16(-kBallPxPerSecond, rules.snapshot(0).ballVX);
}

// -----------------------------------------------------------------------------
// 10. Pause freezes everything; resume rebuilds a countdown and a serve.
// -----------------------------------------------------------------------------
void test_pause_freezes_the_ball_and_resume_countdowns(void) {
    Rules rules(0);
    beginPlay(rules);
    stepN(rules, 10);

    const Fixed frozenX = rules.ballX();
    const Fixed frozenY = rules.ballY();
    TEST_ASSERT_TRUE(frozenX != 160 * kOne);  // the ball really was in flight

    rules.pause();
    assertPhase(rules, Phase::Paused);
    TEST_ASSERT_TRUE(rules.paused());
    EventTally tally;
    drainInto(rules, tally);
    TEST_ASSERT_EQUAL_INT(1, tally.paused);

    // 600 ms of steps must do nothing at all: no ball drift, no timer, no aim.
    stepN(rules, stepsForMs(600));
    assertPhase(rules, Phase::Paused);
    TEST_ASSERT_EQUAL_INT32(frozenX, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(frozenY, rules.ballY());

    rules.resume();
    assertPhase(rules, Phase::Countdown);
    TEST_ASSERT_FALSE(rules.paused());
    drainInto(rules, tally);
    TEST_ASSERT_EQUAL_INT(1, tally.resumed);

    // Countdown first, then a fresh serve from the centre.
    stepN(rules, stepsForMs(kCountdownMs) - 1);
    assertPhase(rules, Phase::Countdown);
    rules.step(kStepDtUs);
    assertPhase(rules, Phase::Play);
    TEST_ASSERT_EQUAL_INT32(160 * kOne, rules.ballX());
    TEST_ASSERT_EQUAL_INT32(132 * kOne, rules.ballY());
}

// -----------------------------------------------------------------------------
// 11. Snapshot preserves seq, shifts to pixels and stays inside int16.
// -----------------------------------------------------------------------------
void test_snapshot_keeps_seq_and_maps_to_whole_pixels(void) {
    Rules rules(1);
    // Paddles parked at the top: the ball can be read mid-flight and is then
    // free to leave the court, which is the only way to reach an out-of-bounds
    // snapshot through the public API.
    beginPlayWithPaddlesAtTop(rules);
    stepN(rules, 27);  // 81 px travelled, so every field is non-trivial

    const Snapshot snapshot = rules.snapshot(4242);
    TEST_ASSERT_EQUAL_UINT16(4242, snapshot.seq);
    TEST_ASSERT_EQUAL_UINT8(netpong::net::kPhasePlay, snapshot.phase);
    TEST_ASSERT_EQUAL_INT16(static_cast<std::int16_t>(rules.ballX() >> kFixedShift), snapshot.ballX);
    TEST_ASSERT_EQUAL_INT16(static_cast<std::int16_t>(rules.ballY() >> kFixedShift), snapshot.ballY);
    TEST_ASSERT_EQUAL_INT16(static_cast<std::int16_t>(rules.snapshot(0).ballVX), snapshot.ballVX);
    TEST_ASSERT_EQUAL_INT16(
        static_cast<std::int16_t>((rules.paddleY(0) + kHalfPaddle) >> kFixedShift), snapshot.p1Y);
    TEST_ASSERT_EQUAL_INT16(
        static_cast<std::int16_t>((rules.paddleY(1) + kHalfPaddle) >> kFixedShift), snapshot.p2Y);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.score1);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.score2);
    TEST_ASSERT_EQUAL_UINT8(0, snapshot.flags);

    // Both ends of the 16-bit sequence space survive untouched.
    TEST_ASSERT_EQUAL_UINT16(0, rules.snapshot(0).seq);
    TEST_ASSERT_EQUAL_UINT16(65535, rules.snapshot(65535).seq);

    // A goal leaves the ball outside the court: the shifted field must stay a
    // real int16 value rather than a wrapped Fixed.
    EventTally tally;
    TEST_ASSERT_TRUE(stepUntilGoal(rules, tally, 200));
    assertFieldsFitInt16(rules.snapshot(65535));
    TEST_ASSERT_TRUE(rules.snapshot(0).ballX < 0);
}

// -----------------------------------------------------------------------------
// 12. Two instances fed the same script export identical snapshots.
// -----------------------------------------------------------------------------
void test_two_rules_produce_identical_snapshots(void) {
    Rules left(0);
    Rules right(0);
    left.setPlayersReady(true);
    right.setPlayersReady(true);

    for (int step = 0; step < 1200; ++step) {
        // A deterministic script with all three input states (none, one, both)
        // on both players, plus goals and serves the script does not control.
        const std::uint8_t p1 = static_cast<std::uint8_t>((step / 37) % 3);
        const std::uint8_t p2 = static_cast<std::uint8_t>((step / 53) % 3);
        left.setPlayerInput(0, p1);
        left.setPlayerInput(1, p2);
        right.setPlayerInput(0, p1);
        right.setPlayerInput(1, p2);
        left.step(kStepDtUs);
        right.step(kStepDtUs);

        if ((step % 60) == 59) {
            const std::uint16_t seq = static_cast<std::uint16_t>(step);
            assertSameSnapshot(right.snapshot(seq), left.snapshot(seq));
        }
    }

    // The event stream is part of the observable contract too, so compare it
    // after the run: same kinds, same owners, same order.
    Event leftEvent;
    Event rightEvent;
    while (left.pollEvent(leftEvent)) {
        TEST_ASSERT_TRUE_MESSAGE(right.pollEvent(rightEvent), "event counts diverged");
        TEST_ASSERT_EQUAL_INT(static_cast<int>(leftEvent.type), static_cast<int>(rightEvent.type));
        TEST_ASSERT_EQUAL_UINT8(leftEvent.player, rightEvent.player);
    }
    TEST_ASSERT_FALSE(right.pollEvent(rightEvent));
}

// -----------------------------------------------------------------------------
// 13. The event ring has kMaxEvents slots and drops the newest when full.
// -----------------------------------------------------------------------------
void test_event_ring_drops_the_newest_when_full(void) {
    Rules rules(0);
    beginPlay(rules);

    // Drain the serve announcement so the ring starts empty.
    Event event;
    while (rules.pollEvent(event)) {
    }

    // Five pause/resume cycles queue ten events into eight slots.
    for (int i = 0; i < 5; ++i) {
        rules.pause();
        rules.resume();
    }

    int count = 0;
    EventType seen[Rules::kMaxEvents];
    while (rules.pollEvent(event)) {
        TEST_ASSERT_TRUE(count < Rules::kMaxEvents);
        seen[count++] = event.type;
    }
    TEST_ASSERT_EQUAL_INT(Rules::kMaxEvents, count);
    for (int i = 0; i < count; ++i) {
        const EventType expected = (i % 2 == 0) ? EventType::Paused : EventType::Resumed;
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(seen[i]));
    }
}

// -----------------------------------------------------------------------------
// 14. countdownRemainingMs() counts down on the fixed step and lands on zero
//     exactly when the serve goes live.
// -----------------------------------------------------------------------------
void test_countdown_remaining_reaches_zero_at_the_serve(void) {
    Rules rules(0);

    // Outside Countdown the getter reports zero, not a stale timer.
    assertPhase(rules, Phase::WaitingForPlayers);
    TEST_ASSERT_EQUAL_UINT32(0, rules.countdownRemainingMs());

    rules.setPlayersReady(true);
    assertPhase(rules, Phase::Countdown);
    TEST_ASSERT_EQUAL_UINT32(kCountdownMs, rules.countdownRemainingMs());

    // One nominal step removes one step's worth of time: 1200000 - 16667 us is
    // 1183333 us, and the getter reports whole milliseconds.
    rules.step(kStepDtUs);
    TEST_ASSERT_EQUAL_UINT32(1183, rules.countdownRemainingMs());

    // Every remaining step strictly decreases the timer and the last Countdown
    // sample is still positive: the transition, not a rounding artefact, is what
    // takes it to zero.
    std::uint32_t previous = rules.countdownRemainingMs();
    int guard = 0;
    while (rules.phase() == Phase::Countdown && guard++ < stepsForMs(kCountdownMs) + 5) {
        rules.step(kStepDtUs);
        if (rules.phase() == Phase::Countdown) {
            const std::uint32_t remaining = rules.countdownRemainingMs();
            TEST_ASSERT_TRUE(remaining < previous);
            previous = remaining;
        }
    }
    // The first nominal step above already consumed one of the 72 steps the
    // 1200 ms countdown needs, so the loop must end on the seventy-second.
    TEST_ASSERT_EQUAL_INT_MESSAGE(stepsForMs(kCountdownMs) - 1, guard,
                                  "the countdown must end on the exact fixed step");
    assertPhase(rules, Phase::Play);
    TEST_ASSERT_EQUAL_UINT32(0, rules.countdownRemainingMs());
    TEST_ASSERT_TRUE(previous > 0);

    // A paused match does not inherit the countdown of the phase it froze.
    rules.pause();
    assertPhase(rules, Phase::Paused);
    TEST_ASSERT_EQUAL_UINT32(0, rules.countdownRemainingMs());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

    RUN_TEST(test_new_match_waits_with_a_still_ball);
    RUN_TEST(test_ready_enters_countdown_then_play_and_serve);
    RUN_TEST(test_paddles_move_at_the_contract_speed_and_clamp);
    RUN_TEST(test_wall_bounce_flips_velocity_and_queues_one_event);
    RUN_TEST(test_paddle_hit_flips_vx_and_keeps_speed);
    RUN_TEST(test_ball_crossing_a_paddle_in_one_step_is_caught);
    RUN_TEST(test_ball_past_each_edge_scores_for_the_other_side);
    RUN_TEST(test_serves_alternate_direction_and_follow_the_angle_sequence);
    RUN_TEST(test_five_points_end_the_match_and_restart_resets_it);
    RUN_TEST(test_pause_freezes_the_ball_and_resume_countdowns);
    RUN_TEST(test_snapshot_keeps_seq_and_maps_to_whole_pixels);
    RUN_TEST(test_two_rules_produce_identical_snapshots);
    RUN_TEST(test_event_ring_drops_the_newest_when_full);
    RUN_TEST(test_countdown_remaining_reaches_zero_at_the_serve);

    return UNITY_END();
}
