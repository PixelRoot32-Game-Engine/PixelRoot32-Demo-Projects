/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "game/PongRules.h"

namespace netpong::game {
namespace {

/// Microseconds in one second, the denominator of every velocity integration.
constexpr std::int64_t kMicrosPerSecond = 1000000;

/// Hard ceiling for one step.
///
/// The host owns catch-up and calls this at 60 Hz; a caller that passes a whole
/// second would otherwise buy a thousand sub-steps for one stalled frame. A
/// tenth of a second still covers six frames of motion, which is more than the
/// host's three-step catch-up ever needs.
constexpr std::uint32_t kMaxStepUs = 100000;

/// Safety ceiling for the sub-step loop. At kMaxStepUs the ball needs at most
/// 18 sub-steps, so this only guards a future speed change.
constexpr int kMaxSubSteps = 64;

/// Top edge of the play area, below the HUD.
constexpr Fixed kPlayTop = kHudHeight * kOne;

/// Bottom edge of the play area (exclusive: the last drawable pixel is one less).
constexpr Fixed kPlayBottom = kCourtHeight * kOne;

/// Paddle top edge lower/upper limits, in Q8.8.
constexpr Fixed kPaddleTopLimit = kHudHeight * kOne;
constexpr Fixed kPaddleBottomLimit = (kCourtHeight - kPaddleHeight) * kOne;

/// Half the paddle height, in Q8.8; also the contact-offset clamp.
constexpr Fixed kHalfPaddle = (kPaddleHeight / 2) * kOne;

/// Ball radius in Q8.8.
constexpr Fixed kBallRadiusFixed = kBallRadius * kOne;

/// Paddle left edges in Q8.8.
constexpr Fixed kLeftPaddleX = kPaddleLeftX * kOne;
constexpr Fixed kRightPaddleX = kPaddleRightX * kOne;

/// Highest table index; 60 degrees in 7.5 degree steps is index 8.
constexpr int kMaxBounceIndex = 8;

static_assert(kMaxBounceAngleDeg == 60, "the bounce table is built for a 60 degree maximum");
static_assert(kPaddleHeight % 2 == 0, "the contact offset is measured from the paddle centre");

// Sine and cosine of 0, 7.5, ... 60 degrees, scaled by 256.
//
// The table replaces libm on purpose: sin/cos are the only place this layer
// would need floating point, and a 0.1 ulp difference between the host's libm
// and the ESP32's would change a bounce angle and therefore the whole replay.
// Each pair is within 0.3 % of a unit circle, so the reflected speed stays on
// kBallSpeed without a square root.
constexpr Fixed kSinTable[kMaxBounceIndex + 1] = {0, 33, 66, 98, 128, 156, 181, 203, 222};
constexpr Fixed kCosTable[kMaxBounceIndex + 1] = {256, 254, 247, 237, 222, 203, 181, 156, 128};

// Serve angle sequence (0, +20, -20 degrees) as (sin, cos), scaled by 256.
//
// A serve is deterministic by construction: the direction alternates and the
// angle walks this three-entry list, so two matches served from the same state
// leave the centre on the same vector, with no RNG and no clock involved.
constexpr Fixed kServeSin[3] = {0, 88, -88};
constexpr Fixed kServeCos[3] = {256, 240, 240};

/// Integrates a Q8.8 per-second value over `deltaUs`, rounding to nearest.
///
/// Truncation would lose up to a fixed unit per sub-step and make the ball
/// quietly slower than 180 px/s; rounding keeps a 60 Hz step of the nominal
/// velocity exactly three pixels, which is what the tests and the wire budget
/// assume. The rounding is symmetric around zero so a leftward and a rightward
/// ball travel the same distance.
constexpr Fixed scaleByTime(Fixed perSecond, std::uint32_t deltaUs) {
    const std::int64_t scaled =
        static_cast<std::int64_t>(perSecond) * static_cast<std::int64_t>(deltaUs);
    if (scaled >= 0) {
        return static_cast<Fixed>((scaled + kMicrosPerSecond / 2) / kMicrosPerSecond);
    }
    return static_cast<Fixed>(-((-scaled + kMicrosPerSecond / 2) / kMicrosPerSecond));
}

constexpr Fixed absFixed(Fixed value) {
    return value < 0 ? -value : value;
}

constexpr Fixed clampFixed(Fixed value, Fixed low, Fixed high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/// Clamps a shifted value into int16 without wrapping, so a field that ever ran
/// away cannot flip sign on the wire.
constexpr std::int16_t clampToInt16(Fixed value) {
    if (value > 32767) {
        return 32767;
    }
    if (value < -32768) {
        return -32768;
    }
    return static_cast<std::int16_t>(value);
}

}  // namespace

Rules::Rules(std::uint8_t firstServer)
    : firstServer_(firstServer == 1 ? 1 : 0), serveDir_(firstServer_ == 0 ? 1 : -1) {
    resetMatch();
}

void Rules::resetMatch() {
    phase_ = Phase::WaitingForPlayers;
    pausedFrom_ = Phase::WaitingForPlayers;
    phaseElapsedUs_ = 0;
    score1_ = 0;
    score2_ = 0;
    serveCount_ = 0;
    serveDir_ = (firstServer_ == 0) ? 1 : -1;
    input_[0] = 0;
    input_[1] = 0;
    centreActors();
    // A fresh match must not replay the previous one's events, a stale
    // MatchOver in particular.
    eventHead_ = 0;
    eventCount_ = 0;
}

void Rules::setPlayersReady(bool bothReady) {
    if (bothReady && phase_ == Phase::WaitingForPlayers) {
        phase_ = Phase::Countdown;
        phaseElapsedUs_ = 0;
    }
}

void Rules::setPlayerInput(std::uint8_t player, std::uint8_t buttons) {
    if (player < 2) {
        input_[player] = buttons;
    }
}

void Rules::step(std::uint32_t deltaUs) {
    if (deltaUs == 0) {
        return;
    }
    if (deltaUs > kMaxStepUs) {
        deltaUs = kMaxStepUs;
    }

    switch (phase_) {
        case Phase::WaitingForPlayers:
        case Phase::MatchOver:
        case Phase::Paused:
            // A frozen match consumes no timers: resume() restarts the
            // countdown explicitly rather than inheriting Paused time.
            return;
        case Phase::Countdown:
            updatePaddles(deltaUs);
            phaseElapsedUs_ += deltaUs;
            if (phaseElapsedUs_ >= kCountdownMs * 1000U) {
                beginServe();
            }
            return;
        case Phase::Point:
            // The paddles stay put between points: the next serve re-centres
            // the ball, and letting a held button drag a paddle during the
            // pause would reward holding the button while the rally is dead.
            phaseElapsedUs_ += deltaUs;
            if (phaseElapsedUs_ >= kPointPauseMs * 1000U) {
                beginServe();
            }
            return;
        case Phase::Play:
            updatePaddles(deltaUs);
            advanceBall(deltaUs);
            return;
    }
}

void Rules::pause() {
    if (phase_ != Phase::Play && phase_ != Phase::Countdown && phase_ != Phase::Point) {
        return;
    }
    pausedFrom_ = phase_;
    phase_ = Phase::Paused;
    pushEvent(EventType::Paused, 0xFF);
}

void Rules::resume() {
    if (phase_ != Phase::Paused) {
        return;
    }
    // Resume goes through the countdown rather than straight into Play: both
    // players just had their hands off the board, and a live ball on the first
    // frame after a reconnect is not a fair rally.
    phase_ = Phase::Countdown;
    phaseElapsedUs_ = 0;
    pushEvent(EventType::Resumed, 0xFF);
}

void Rules::restart() {
    if (phase_ != Phase::MatchOver) {
        return;
    }
    score1_ = 0;
    score2_ = 0;
    serveCount_ = 0;
    serveDir_ = (firstServer_ == 0) ? 1 : -1;
    centreActors();
    phase_ = Phase::Countdown;
    phaseElapsedUs_ = 0;
}

Phase Rules::phase() const {
    return phase_;
}

bool Rules::paused() const {
    return phase_ == Phase::Paused;
}

std::uint32_t Rules::countdownRemainingMs() const {
    if (phase_ != Phase::Countdown) {
        return 0;
    }
    const std::uint32_t totalUs = kCountdownMs * 1000U;
    if (phaseElapsedUs_ >= totalUs) {
        // step() turns this exact state into a serve, so a caller that asks
        // between the two reads "no time left" rather than a wrapped value.
        return 0;
    }
    return (totalUs - phaseElapsedUs_) / 1000U;
}

std::uint8_t Rules::score(std::uint8_t player) const {
    if (player == 0) {
        return score1_;
    }
    if (player == 1) {
        return score2_;
    }
    return 0;
}

Fixed Rules::ballX() const {
    return ballX_;
}

Fixed Rules::ballY() const {
    return ballY_;
}

Fixed Rules::paddleY(std::uint8_t player) const {
    return player < 2 ? paddleY_[player] : 0;
}

Snapshot Rules::snapshot(std::uint16_t seq) const {
    Snapshot snapshot;
    snapshot.seq = seq;
    snapshot.phase = static_cast<std::uint8_t>(phase_);
    snapshot.ballX = clampToInt16(ballX_ >> kFixedShift);
    snapshot.ballY = clampToInt16(ballY_ >> kFixedShift);
    snapshot.ballVX = clampToInt16(ballVX_ >> kFixedShift);
    snapshot.ballVY = clampToInt16(ballVY_ >> kFixedShift);
    // The wire field is the paddle centre (see net::State), while paddleY() is
    // the top edge the renderer draws from.
    snapshot.p1Y = clampToInt16((paddleY_[0] + kHalfPaddle) >> kFixedShift);
    snapshot.p2Y = clampToInt16((paddleY_[1] + kHalfPaddle) >> kFixedShift);
    snapshot.score1 = score1_;
    snapshot.score2 = score2_;
    snapshot.flags = 0;
    return snapshot;
}

bool Rules::pollEvent(Event& out) {
    if (eventCount_ == 0) {
        return false;
    }
    out = events_[eventHead_];
    eventHead_ = static_cast<std::uint8_t>((eventHead_ + 1) % kMaxEvents);
    --eventCount_;
    return true;
}

void Rules::centreActors() {
    const Fixed centreTop = (kHudHeight + (kCourtHeight - kHudHeight - kPaddleHeight) / 2) * kOne;
    paddleY_[0] = centreTop;
    paddleY_[1] = centreTop;
    ballX_ = kBallCenterX;
    ballY_ = kBallCenterY;
    ballVX_ = 0;
    ballVY_ = 0;
}

void Rules::beginServe() {
    const int angle = serveCount_ % 3;
    const std::int8_t direction = serveDir_;

    ballX_ = kBallCenterX;
    ballY_ = kBallCenterY;
    ballVX_ = static_cast<Fixed>(direction) * ((kBallSpeed * kServeCos[angle]) / kOne);
    ballVY_ = (kBallSpeed * kServeSin[angle]) / kOne;

    phase_ = Phase::Play;
    phaseElapsedUs_ = 0;
    // A serve to the right is served by player one, a serve to the left by
    // player two. The controllers only need it for the OLED label.
    pushEvent(EventType::ServeStarted, direction > 0 ? 0 : 1);

    serveDir_ = static_cast<std::int8_t>(-direction);
    ++serveCount_;
}

void Rules::updatePaddles(std::uint32_t deltaUs) {
    const Fixed travel = scaleByTime(kPaddleSpeed, deltaUs);
    for (std::uint8_t player = 0; player < 2; ++player) {
        const bool up = (input_[player] & kButtonLeft) != 0;
        const bool down = (input_[player] & kButtonRight) != 0;
        if (up == down) {
            // Neither button, or both at once: an ambiguous input must not
            // move the paddle in either direction.
            continue;
        }
        paddleY_[player] += up ? -travel : travel;
        paddleY_[player] = clampFixed(paddleY_[player], kPaddleTopLimit, kPaddleBottomLimit);
    }
}

void Rules::advanceBall(std::uint32_t deltaUs) {
    // Bound the whole-step displacement, then split it into sub-steps of at
    // most one pixel so a fast ball can never pass through a six-pixel paddle
    // (or a wall) between two samples.
    const Fixed dx = scaleByTime(ballVX_, deltaUs);
    const Fixed dy = scaleByTime(ballVY_, deltaUs);
    const Fixed maxDelta = absFixed(dx) > absFixed(dy) ? absFixed(dx) : absFixed(dy);

    int subSteps = 1;
    if (maxDelta > kOne) {
        subSteps = static_cast<int>((maxDelta + kOne - 1) / kOne);
    }
    if (subSteps > kMaxSubSteps) {
        subSteps = kMaxSubSteps;
    }

    const std::uint32_t baseUs = deltaUs / static_cast<std::uint32_t>(subSteps);
    std::uint32_t remainingUs = deltaUs;
    for (int i = 0; i < subSteps; ++i) {
        const bool last = (i + 1 == subSteps);
        const std::uint32_t subUs = last ? remainingUs : baseUs;
        remainingUs -= subUs;

        ballX_ += scaleByTime(ballVX_, subUs);
        ballY_ += scaleByTime(ballVY_, subUs);

        bounceWalls();
        // The ball can only meet one paddle per sub-step, and a hit already
        // reversed vx, so the second call cannot double-count the same contact.
        if (!hitPaddle(0)) {
            hitPaddle(1);
        }
        if (resolveGoal()) {
            return;
        }
    }
}

void Rules::bounceWalls() {
    if (ballY_ - kBallRadiusFixed < kPlayTop) {
        ballY_ = kPlayTop + kBallRadiusFixed;
        ballVY_ = absFixed(ballVY_);
        pushEvent(EventType::WallBounce, 0xFF);
    } else if (ballY_ + kBallRadiusFixed > kPlayBottom) {
        ballY_ = kPlayBottom - kBallRadiusFixed;
        ballVY_ = -absFixed(ballVY_);
        pushEvent(EventType::WallBounce, 0xFF);
    }
}

bool Rules::hitPaddle(std::uint8_t player) {
    const Fixed paddleLeft = (player == 0) ? kLeftPaddleX : kRightPaddleX;
    const Fixed paddleRight = paddleLeft + kPaddleWidth * kOne;
    const Fixed paddleTop = paddleY_[player];
    const Fixed paddleBottom = paddleTop + kPaddleHeight * kOne;

    // AABB against the ball's bounding square. A ball only bounces when it is
    // moving into the paddle, which also keeps a ball that was pushed out of
    // the paddle from hitting it again on the next sub-step.
    if (ballX_ + kBallRadiusFixed <= paddleLeft || ballX_ - kBallRadiusFixed >= paddleRight ||
        ballY_ + kBallRadiusFixed <= paddleTop || ballY_ - kBallRadiusFixed >= paddleBottom) {
        return false;
    }
    if (player == 0 ? ballVX_ >= 0 : ballVX_ <= 0) {
        return false;
    }

    // The contact point selects the exit angle: dead centre leaves the ball
    // flat, the outer edge grows to the 60 degree maximum. Integer division
    // truncates toward zero, so any contact inside the innermost 1/16 of the
    // paddle still counts as the centre and never flips the ball's vertical
    // direction by a single pixel of paddle noise.
    Fixed offset = ballY_ - (paddleTop + kHalfPaddle);
    offset = clampFixed(offset, -kHalfPaddle, kHalfPaddle);
    const int index = static_cast<int>((static_cast<std::int64_t>(offset) * kMaxBounceIndex) / kHalfPaddle);
    const int magnitude = index < 0 ? -index : index;

    ballVX_ = (player == 0 ? 1 : -1) * ((kBallSpeed * kCosTable[magnitude]) / kOne);
    ballVY_ = (index < 0 ? -1 : 1) * ((kBallSpeed * kSinTable[magnitude]) / kOne);

    // Park the ball on the paddle face instead of inside it, so the next
    // sub-step starts from a legal position.
    ballX_ = (player == 0) ? paddleRight + kBallRadiusFixed : paddleLeft - kBallRadiusFixed;
    pushEvent(EventType::PaddleHit, player);
    return true;
}

void Rules::awardPoint(std::uint8_t scorer) {
    if (scorer == 0) {
        if (score1_ < 255) {
            ++score1_;
        }
    } else if (score2_ < 255) {
        ++score2_;
    }
    pushEvent(EventType::Goal, scorer);

    if (score1_ >= kWinScore || score2_ >= kWinScore) {
        phase_ = Phase::MatchOver;
        phaseElapsedUs_ = 0;
        pushEvent(EventType::MatchOver, score1_ >= kWinScore ? 0 : 1);
        return;
    }
    phase_ = Phase::Point;
    phaseElapsedUs_ = 0;
}

bool Rules::resolveGoal() {
    if (ballX_ + kBallRadiusFixed < 0) {
        awardPoint(1);
        return true;
    }
    if (ballX_ - kBallRadiusFixed > kCourtWidth * kOne) {
        awardPoint(0);
        return true;
    }
    return false;
}

void Rules::pushEvent(EventType type, std::uint8_t player) {
    if (eventCount_ >= kMaxEvents) {
        // Drop newest: during a burst the serve and the hit that started it
        // are more useful to the scene than the tail of the burst.
        return;
    }
    events_[(eventHead_ + eventCount_) % kMaxEvents] = Event{type, player};
    ++eventCount_;
}

}  // namespace netpong::game
