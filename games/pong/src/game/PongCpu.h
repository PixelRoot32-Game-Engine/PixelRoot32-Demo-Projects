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
 *     error  = ballY - paddleCentreY
 *     error += noise()                             // the level's imperfection
 *     speed  = clamp(error * gainNum / gainDen, +-maxSpeed)
 *     if |error| > deadZone: accumulator += scaleByTime(speed, deltaUs)
 *     paddle top edge += the whole pixels of the accumulator
 *
 * The demo ships three levels, and they are nothing but a `CpuTuning` table:
 * the court, the ball, the serve and the player paddle are identical at every
 * level. The numbers reproduce the retired physics demo at Medium:
 *
 *     Level   Gain   Cap (% of kPaddleSpeed)   Imperfection   Dead zone
 *     Easy    1/1    40 %  (76 px/s)           +-6 px         2 px
 *     Medium  3/2    60 %  (114 px/s)          +-3 px         0.5 px
 *     Hard    5/2    80 %  (152 px/s)          +-1 px         0.125 px
 *
 * Three knobs make each level feel different: the proportional gain (how hard
 * the paddle answers an error), the speed cap (the fastest it can ever travel,
 * always below the human's `kPaddleSpeed`, so a rally is winnable), and the
 * imperfection box added to the error -- a target offset in `[-r, +r]` px that
 * keeps the opponent from being a perfect wall. The dead zone is the smallest
 * error worth chasing, so a settled paddle does not buzz against a still ball.
 * The sub-pixel accumulator is the demo's float accumulator in Q8.8: movement
 * is asked for continuously and only whole pixels reach the paddle.
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
 * not change. `Medium` is the default so a match built without a level plays
 * exactly like the demo before the menu existed.
 */
enum class Difficulty : std::uint8_t {
    Easy,   ///< Slow, wide imperfection: a forgiving opponent.
    Medium, ///< The shipped controller: gain 3/2, 60 % cap, +-3 px.
    Hard    ///< Fast, nearly exact tracking: gain 5/2, 80 % cap, +-1 px.
};

/**
 * @struct CpuTuning
 * @brief The five numbers that define one difficulty level.
 *
 * `gainNum / gainDen` is the proportional gain, `maxSpeed` the cap in Q8.8
 * pixels per second, `noiseRadius` the half-width of the imperfection box in
 * Q8.8 and `deadZone` the smallest error the controller reacts to, in Q8.8.
 */
struct CpuTuning {
    Fixed gainNum;     ///< Gain numerator; paired with `gainDen`.
    Fixed gainDen;     ///< Gain denominator; always positive.
    Fixed maxSpeed;    ///< Speed cap, in Q8.8 pixels per second.
    Fixed noiseRadius; ///< Half-width of the imperfection box, in Q8.8; 0 disables it.
    Fixed deadZone;    ///< Errors at or below this size do not move the paddle, in Q8.8.
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

// The three levels of the tuning table, in Q8.8. The caps are 40 / 60 / 80 % of
// kPaddleSpeed (190 px/s), i.e. 76 / 114 / 152 px/s.
inline constexpr CpuTuning kCpuEasy = {1, 1, kPaddleSpeed * 40 / 100, 6 * kOne, 2 * kOne};
inline constexpr CpuTuning kCpuMedium = {
    kCpuGainNum, kCpuGainDen, kCpuMaxSpeed, kCpuNoiseRadius, kCpuDeadZone};
inline constexpr CpuTuning kCpuHard = {5, 2, kPaddleSpeed * 80 / 100, kOne, kOne / 8};

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

/**
 * @class CpuPaddle
 * @brief The opponent: a proportional chase with a bounded imperfection.
 *
 * The class owns nothing but its own state -- the tuning it plays, the noise
 * stream position and the sub-pixel remainder of the travel it has already
 * asked for. The paddle position itself is passed in and returned by `step()`,
 * so the caller (the court rules) stays the single owner of the paddle: no
 * accessor copies can disagree with each other.
 *
 * `step()` is a pure function of its arguments and that state. Two instances
 * built with the same tuning and seed and fed the same script of ball positions
 * end on the same Q8.8 value, on every build. Changing the tuning rewinds the
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
     * @param ballY Ball centre, in Q8.8.
     * @param paddleY Current paddle top edge, in Q8.8.
     * @param deltaUs Elapsed microseconds for this step.
     * @return The new paddle top edge, in Q8.8, clamped to the play area.
     *
     * The imperfection is sampled once per call, before the speed is computed,
     * so the offset the controller reacts to and the offset the paddle moves by
     * always belong to the same step.
     */
    [[nodiscard]] Fixed step(Fixed ballY, Fixed paddleY, std::uint32_t deltaUs);

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
