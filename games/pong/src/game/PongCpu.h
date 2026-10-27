/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

/**
 * @file PongCpu.h
 * @brief Engine-free proportional controller that plays the right paddle.
 *
 * This is the opponent of the single-player match, expressed in Q8.8 instead of
 * float:
 *
 *     targetY = ball.y                       // Chase
 *     targetY = predictBallY(ball)           // Predict and PredictAndAim
 *     error   = targetY - paddleCentreY
 *     error  += noise()                      // the level's imperfection
 *     speed   = clamp(error * gainNum / gainDen, +-maxSpeed)
 *     if |error| > deadZone: accumulator += scaleByTime(speed, deltaUs)
 *     paddle top edge += the whole pixels of the accumulator
 *
 * The demo ships three levels, and they are a `CpuTuning` table plus the style
 * that decides *how* the controller reads the ball: the court, the ball, the
 * serve and the player paddle are identical at every level.
 *
 *     Level   Style    Gain   Cap (% of kPaddleSpeed)   Imperfection   Dead zone
 *     Easy    chase    1/1    40 %  (76 px/s)           +-6 px         2 px
 *     Medium  predict  3/2    60 %  (114 px/s)          +-3 px         0.5 px
 *     Hard    aim      5/2    85 %  (161.5 px/s)        +-1 px         0.125 px
 *
 * Three numeric knobs make each level feel different: the proportional gain (how
 * hard the paddle answers an error), the speed cap (the fastest it can ever
 * travel, always below the human's `kPaddleSpeed`, so a rally is winnable), and
 * the imperfection box added to the error -- a target offset in `[-r, +r]` px
 * that keeps the opponent from being a perfect wall. The dead zone is the
 * smallest error worth chasing, so a settled paddle does not buzz against a
 * still ball. The sub-pixel accumulator is the demo's float accumulator in
 * Q8.8: movement is asked for continuously and only whole pixels reach the
 * paddle.
 *
 * The fourth knob is the style, and it is what makes Medium and Hard read the
 * shot instead of merely moving faster:
 *
 *   - `Chase` drives to the ball's current Y, exactly like the pre-menu
 *     controller. Easy plays this.
 *   - `Predict` drives to the intercept on its own paddle plane, from the ball's
 *     velocity. While the ball travels away (or waits for the serve) it
 *     recentres. Medium plays this.
 *   - `PredictAndAim` predicts through wall bounces and offsets the target by
 *     `kCpuAimOffset`, so the ball meets the paddle off-centre and the return
 *     leaves towards the side of the court the player is not covering. Hard
 *     plays this.
 *
 * The prediction itself (`predictBallY()`) is a pure Q8.8 function: no clock, no
 * allocation and no state, so it costs one integer division per step and the
 * same ball view always answers the same height.
 *
 * The one thing the port replaces is the imperfection source. The demo mixed
 * `millis()` and the ball's x into every sample, so two runs never matched. Here
 * a 32-bit linear congruential generator, advanced once per step, produces the
 * offset stream: the same sequence of `step()` calls therefore replays the same
 * match on the host and on the board, with no clock, no float and no
 * allocation.
 *
 * @note This header is included by "game/PongRules.h" once the court primitives
 *       (`Fixed`, `kOne`, the paddle geometry and `scaleByTime()`) exist, and it
 *       carries no include of its own for that reason: the policy is meaningless
 *       without the court, and a self-include would make the two headers
 *       mutually dependent, because `Rules` owns a `CpuPaddle` while the
 *       `CpuPaddle` reads the `Rules` constants. Include "game/PongRules.h",
 *       never this file on its own.
 */
namespace pong::game {

/**
 * @enum Difficulty
 * @brief Selectable strength of the CPU opponent.
 *
 * The level only tunes the controller below; the court and the player's side do
 * not change. `Medium` is the default, and it predicts the intercept: the demo's
 * default match is more than a faster chase.
 */
enum class Difficulty : std::uint8_t {
    Easy,   ///< Chase the ball's current Y: the forgiving opponent.
    Medium, ///< Predict the intercept: gain 3/2, 60 % cap, +-3 px.
    Hard    ///< Predict through the walls and aim away: gain 5/2, 85 % cap, +-1 px.
};

/**
 * @enum CpuStyle
 * @brief How the controller reads the ball before it moves.
 *
 * The style is the part of a level's strength that is not a number: the same
 * gain and cap answer the same error differently depending on what the
 * controller thinks the target is.
 */
enum class CpuStyle : std::uint8_t {
    Chase,        ///< Drive to the ball's current Y (the pre-menu controller).
    Predict,      ///< Drive to the intercept on its own paddle plane.
    PredictAndAim ///< Predict through the walls, then aim away from the player.
};

/**
 * @struct BallView
 * @brief What the controller may know about the ball: centre and velocity.
 *
 * All fields are Q8.8: pixels for the position, pixels per second for the
 * velocity. The rules hand over the live ball, and a test can hand over a
 * scripted one, because the prediction reads nothing else.
 */
struct BallView {
    Fixed x;  ///< Ball centre X, in Q8.8 pixels.
    Fixed y;  ///< Ball centre Y, in Q8.8 pixels.
    Fixed vx; ///< Ball velocity X, in Q8.8 pixels per second.
    Fixed vy; ///< Ball velocity Y, in Q8.8 pixels per second.
};

/**
 * @struct CpuTuning
 * @brief The numbers and the style that define one difficulty level.
 *
 * `gainNum / gainDen` is the proportional gain, `maxSpeed` the cap in Q8.8
 * pixels per second, `noiseRadius` the half-width of the imperfection box in
 * Q8.8, `deadZone` the smallest error the controller reacts to, in Q8.8, and
 * `style` how it reads the ball before computing that error.
 */
struct CpuTuning {
    Fixed gainNum;     ///< Gain numerator; paired with `gainDen`.
    Fixed gainDen;     ///< Gain denominator; always positive.
    Fixed maxSpeed;    ///< Speed cap, in Q8.8 pixels per second.
    Fixed noiseRadius; ///< Half-width of the imperfection box, in Q8.8; 0 disables it.
    Fixed deadZone;    ///< Errors at or below this size do not move the paddle, in Q8.8.
    CpuStyle style;    ///< How the paddle reads the ball.
};

// The Medium numbers keep their historical names because they are both the
// default match and the values the pre-menu suite pinned.
//
// Speed = error * 3 / 2, the demo's `diff * 1.5`.
inline constexpr Fixed kCpuGainNum = 3;
inline constexpr Fixed kCpuGainDen = 2;

// The cap, as a percentage of the human paddle's speed: the demo ran its CPU at
// 90 px/s against the player's 150, and 90 / 150 is this ratio.
inline constexpr int kCpuSpeedPercent = 60;

// kPaddleSpeed * 60 / 100: 190 px/s * 0.6 = 114 px/s, which is 29184 in Q8.8.
inline constexpr Fixed kCpuMaxSpeed = kPaddleSpeed * kCpuSpeedPercent / 100;

// The demo's AI_TARGET_OFFSET = 6 box, centred on the ball: +-3 px, 768 in Q8.8.
inline constexpr Fixed kCpuNoiseRadius = (6 * kOne) / 2;

// The demo's 0.5 px AI_MOVEMENT_THRESHOLD: an error this small is ignored, so a
// centred paddle does not buzz against a centred ball.
inline constexpr Fixed kCpuDeadZone = kOne / 2;

// Seed of the noise stream: the 32-bit golden-ratio constant. Any other value
// replays deterministically too, which is what the tests use to prove the stream
// is really fed into the controller.
inline constexpr std::uint32_t kCpuSeed = 0x9E3779B9u;

// Geometry of the prediction. Everything is Q8.8 and constexpr so the whole
// intercept is a compilation-time constant whenever the ball view is.
//
// The paddle plane is the vertical line at the CPU paddle's left edge: the ball
// reaches it one horizontal radius early, but that half-width is the same for
// every shot, so folding it in would move every prediction by the same pixel and
// buy nothing.
inline constexpr Fixed kCpuPaddlePlaneX = kPaddleRightX * kOne;

// The ball's centre bounces inside these limits; `bounceWalls()` reflects it one
// radius inside each court edge. `kBallBounceSpan` is 210 px on the 240-tall
// court, and it is the period of the vertical triangle wave the prediction
// folds.
inline constexpr Fixed kBallBounceTop = (kHudHeight + kBallRadius) * kOne;
inline constexpr Fixed kBallBounceBottom = (kCourtHeight - kBallRadius) * kOne;
inline constexpr Fixed kBallBounceSpan = kBallBounceBottom - kBallBounceTop;

// The aim offset is three eighths of the paddle: 16 px. It is large enough that
// the contact point selects a real exit angle (the bounce table resolves 7.5
// degrees) and small enough that the ball always stays on the paddle.
inline constexpr Fixed kCpuAimOffset = (kPaddleHeight * 3 / 8) * kOne;

// The three levels of the tuning table, in Q8.8. The caps are 40 / 60 / 80 % of
// kPaddleSpeed (190 px/s), i.e. 76 / 114 / 152 px/s. Keeping hard under 85 %
// matters: past that the cap covers the whole play area in the time a flat shot
// takes to cross, and no aimed shot could beat it.
inline constexpr CpuTuning kCpuEasy = {
    1, 1, kPaddleSpeed * 40 / 100, 6 * kOne, 2 * kOne, CpuStyle::Chase};
inline constexpr CpuTuning kCpuMedium = {
    kCpuGainNum, kCpuGainDen, kCpuMaxSpeed, kCpuNoiseRadius, kCpuDeadZone, CpuStyle::Predict};
inline constexpr CpuTuning kCpuHard = {
    5, 2, kPaddleSpeed * 80 / 100, kOne, kOne / 8, CpuStyle::PredictAndAim};

/**
 * @brief Tuning table lookup.
 * @param level Requested level.
 * @return The level's numbers; Medium for any value outside the enum.
 *
 * A `static_cast<Difficulty>(250)` therefore cannot leave the controller in an
 * undefined state, which matters because the level may come from a saved menu
 * selection.
 */
[[nodiscard]] constexpr CpuTuning cpuTuning(Difficulty level) {
    switch (level) {
        case Difficulty::Easy:
            return kCpuEasy;
        case Difficulty::Hard:
            return kCpuHard;
        default:
            // Medium, and the safe answer for an unknown value.
            return kCpuMedium;
    }
}

static_assert(cpuTuning(Difficulty::Easy).maxSpeed < kPaddleSpeed,
              "every level must lose the speed race to the player paddle");
static_assert(cpuTuning(Difficulty::Medium).maxSpeed < kPaddleSpeed,
              "the medium cap stays below the player paddle");
static_assert(cpuTuning(Difficulty::Hard).maxSpeed < kPaddleSpeed,
              "the hard cap stays below the player paddle");

static_assert(cpuTuning(Difficulty::Easy).gainDen > 0, "the gain denominator is a divisor");
static_assert(cpuTuning(Difficulty::Medium).gainDen > 0, "the gain denominator is a divisor");
static_assert(cpuTuning(Difficulty::Hard).gainDen > 0, "the gain denominator is a divisor");

static_assert(cpuTuning(Difficulty::Easy).maxSpeed < cpuTuning(Difficulty::Medium).maxSpeed,
              "the cap must grow with the level");
static_assert(cpuTuning(Difficulty::Medium).maxSpeed < cpuTuning(Difficulty::Hard).maxSpeed,
              "the cap must grow with the level");

static_assert(cpuTuning(Difficulty::Easy).noiseRadius > cpuTuning(Difficulty::Medium).noiseRadius,
              "the imperfection must shrink as the level rises");
static_assert(cpuTuning(Difficulty::Medium).noiseRadius > cpuTuning(Difficulty::Hard).noiseRadius,
              "the imperfection must shrink as the level rises");

static_assert(kBallBounceSpan > 0, "the ball must have a span to bounce inside");
static_assert(kBallBounceSpan == (kCourtHeight - kHudHeight - 2 * kBallRadius) * kOne,
              "the bounce span is the play area minus one ball radius at each wall");

namespace detail {

/// Folds `y` into the ball's bounce span with the triangular wave the walls
/// describe. 64-bit because a very slow shot predicts a very distant crossing.
constexpr Fixed foldToBounceSpan(std::int64_t y) {
    const std::int64_t span = kBallBounceSpan;
    const std::int64_t period = 2 * span;
    std::int64_t m = (y - kBallBounceTop) % period;
    if (m < 0) {
        // C++ truncates the remainder toward zero, so a negative `rel` arrives
        // as a negative `m`; adding one period lands it in [0, period).
        m += period;
    }
    if (m > span) {
        // Beyond the top of the span: reflect by symmetry instead of bouncing
        // step by step.
        m = period - m;
    }
    return static_cast<Fixed>(kBallBounceTop + m);
}

}  // namespace detail

/**
 * @brief Folds an unbounded Y into the ball's bounce span.
 * @param y Any Y in Q8.8 pixels.
 * @return The same height inside `[kBallBounceTop, kBallBounceBottom]`, as if
 *         the ball had bounced off the walls the whole way down.
 *
 * The ball's vertical path is a triangle wave: it descends to the bottom limit,
 * reflects to the top limit, and repeats. Its period is `2 * kBallBounceSpan`
 * and it is symmetric about the span, so mapping the unbounded crossing into
 * the span is the same as simulating every bounce.
 */
[[nodiscard]] constexpr Fixed foldIntoBounceSpan(Fixed y) {
    return detail::foldToBounceSpan(y);
}

/**
 * @brief Predicts where the ball meets the CPU paddle's plane.
 * @param ball Ball centre and velocity, in Q8.8.
 * @return The ball's Y when its centre reaches `kCpuPaddlePlaneX`, folded
 *         through the wall bounces.
 *
 * A ball moving left, or parked during the countdown, is not coming: the answer
 * is then the play-area centre, which is what recentres the paddle between
 * rallies. A ball already past the plane answers its current Y, so the
 * controller degenerates into a chase instead of inventing a shot.
 *
 * This is a pure Q8.8 function: no clock, no allocation and no state. The time
 * to the plane is microseconds in 64-bit only inside the expression; the result
 * is a `Fixed` again, so the host and the board agree bit for bit.
 */
[[nodiscard]] constexpr Fixed predictBallY(const BallView& ball) {
    if (ball.vx <= 0) {
        return kBallCenterY;
    }
    const Fixed dx = kCpuPaddlePlaneX - ball.x;
    if (dx <= 0) {
        return ball.y;
    }

    // dx / vx seconds, in microseconds. dx and vx are Q8.8, so their ratio is
    // already a plain number of seconds; no shift is needed.
    const std::int64_t tUs = static_cast<std::int64_t>(dx) * kMicrosPerSecond / ball.vx;
    const std::int64_t y =
        static_cast<std::int64_t>(ball.y) +
        static_cast<std::int64_t>(ball.vy) * tUs / kMicrosPerSecond;

    // Reduce in 64-bit before narrowing: a nearly horizontal shot can predict a
    // crossing millions of pixels away, and folding only depends on the travel
    // modulo the wave's period.
    const std::int64_t period = 2 * static_cast<std::int64_t>(kBallBounceSpan);
    std::int64_t reduced = y % period;
    if (reduced < 0) {
        reduced += period;
    }
    return foldIntoBounceSpan(static_cast<Fixed>(reduced));
}

/**
 * @class CpuPaddle
 * @brief The opponent: a proportional controller with a bounded imperfection.
 *
 * The class owns nothing but its own state -- the tuning it plays, the noise
 * stream position and the sub-pixel remainder of the travel it has already
 * asked for. The paddle position itself is passed in and returned by `step()`,
 * so the caller (the court rules) stays the single owner of the paddle: no
 * accessor copies can disagree with each other.
 *
 * `step()` is a pure function of its arguments and that state. Two instances
 * built with the same tuning and seed and fed the same script of ball views end
 * on the same Q8.8 value, on every build. Changing the tuning rewinds the
 * stream, so a level switch starts a fresh, reproducible match.
 */
class CpuPaddle {
public:
    /**
     * @brief Builds a controller at the start of its noise stream.
     * @param tuning The level's numbers. 0 for `noiseRadius` disables the
     *        imperfection, which is how a test measures the gain and the cap
     *        without the noise in the way.
     * @param seed First value of the noise stream; also what `reset()` and
     *        `setTuning()` restore.
     */
    explicit CpuPaddle(CpuTuning tuning = cpuTuning(Difficulty::Medium),
                       std::uint32_t seed = kCpuSeed);

    /**
     * @brief Swaps the level's numbers and rewinds the controller.
     * @param tuning The numbers to play with from the next `step()` on.
     *
     * Clears the sub-pixel remainder and rewinds the noise stream to its seed,
     * exactly like `reset()`. A level change therefore starts a new deterministic
     * match instead of inheriting the previous level's accumulator and stream
     * position.
     */
    void setTuning(CpuTuning tuning);

    /**
     * @brief The numbers this controller is currently playing.
     * @return The tuning passed to the constructor or to `setTuning()`.
     */
    [[nodiscard]] CpuTuning tuning() const;

    /**
     * @brief Returns the controller to the state it was built in.
     *
     * Clears the sub-pixel remainder and rewinds the noise stream to its seed,
     * so a match that starts over starts over exactly, not merely roughly. The
     * tuning is kept: resetting a match does not reset the selected level.
     */
    void reset();

    /**
     * @brief Advances the paddle by one step.
     * @param ball Ball centre and velocity, in Q8.8.
     * @param paddleY Current paddle top edge, in Q8.8.
     * @param opponentPaddleY Current top edge of the player's paddle, in Q8.8.
     *        Read only by `PredictAndAim`, which sends the ball to the side the
     *        player is not on.
     * @param deltaUs Elapsed microseconds for this step.
     * @return The new paddle top edge, in Q8.8, clamped to the play area.
     *
     * The target is the ball's current Y for `Chase`, and `predictBallY()` for
     * the other two styles. `PredictAndAim` shifts the target by
     * `kCpuAimOffset` away from the player's paddle centre when the ball is
     * incoming, so the contact point is off-centre and the bounce table sends
     * the return to the far side.
     *
     * The imperfection is sampled once per call, before the speed is computed,
     * so the offset the controller reacts to and the offset the paddle moves by
     * always belong to the same step.
     */
    [[nodiscard]] Fixed step(const BallView& ball, Fixed paddleY, Fixed opponentPaddleY,
                             std::uint32_t deltaUs);

private:
    /// The level's numbers; every constant `step()` reads comes from here.
    CpuTuning tuning_ = cpuTuning(Difficulty::Medium);
    /// Seed the noise stream is rewound to by `reset()` and `setTuning()`.
    std::uint32_t seed_ = kCpuSeed;
    /// Current state of the noise stream.
    std::uint32_t noise_ = kCpuSeed;
    /// Sub-pixel travel not yet handed to the paddle, in Q8.8.
    Fixed accumulator_ = 0;
};

}  // namespace pong::game
