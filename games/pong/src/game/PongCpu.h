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
 * This is the opponent of the single-player match. It is the AI mode of the
 * retired physics demo (`PaddleActor.cpp`) expressed in Q8.8 instead of float:
 *
 *     error  = ballY - paddleCentreY
 *     error += noise()                             // the +-3 px imperfection
 *     speed  = clamp(error * 3 / 2, +-kCpuMaxSpeed)
 *     if |error| > 0.5 px: accumulator += scaleByTime(speed, deltaUs)
 *     paddle top edge += the whole pixels of the accumulator
 *
 * Three numbers come straight from the demo because they are what makes the
 * opponent feel the way it does: the proportional gain 1.5, the speed cap at
 * 60 % of the human paddle (`kCpuMaxSpeed`, the demo's 90 against 150 px/s), and
 * the imperfection box `AI_TARGET_OFFSET = 6`, i.e. a target offset in
 * `[-3, +3]` px added to the error. The 0.5 px dead zone and the sub-pixel
 * accumulator are the demo's `AI_MOVEMENT_THRESHOLD` and its float accumulator.
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

/**
 * @class CpuPaddle
 * @brief The opponent: a proportional chase with a bounded imperfection.
 *
 * The class owns nothing but its own state -- the imperfection radius, the noise
 * stream position and the sub-pixel remainder of the travel it has already
 * asked for. The paddle position itself is passed in and returned by `step()`,
 * so the caller (the court rules) stays the single owner of the paddle: no
 * accessor copies can disagree with each other.
 *
 * `step()` is a pure function of its arguments and that state. Two instances
 * built with the same seed and fed the same script of ball positions end on the
 * same Q8.8 value, on every build.
 */
class CpuPaddle {
public:
    /**
     * @brief Builds a controller at the start of its noise stream.
     * @param noiseRadius Half-width of the imperfection box, in Q8.8. Pass 0 to
     *        disable the imperfection, which is how a test measures the gain and
     *        the cap without the noise in the way.
     * @param seed First value of the noise stream; also what `reset()` restores.
     */
    explicit CpuPaddle(Fixed noiseRadius = kCpuNoiseRadius, std::uint32_t seed = kCpuSeed);

    /**
     * @brief Returns the controller to the state it was built in.
     *
     * Clears the sub-pixel remainder and rewinds the noise stream to its seed,
     * so a match that starts over starts over exactly, not merely roughly.
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
    /// Half-width of the imperfection box, in Q8.8; 0 disables it.
    Fixed noiseRadius_ = kCpuNoiseRadius;
    /// Seed the noise stream is rewound to by `reset()`.
    std::uint32_t seed_ = kCpuSeed;
    /// Current state of the noise stream.
    std::uint32_t noise_ = kCpuSeed;
    /// Sub-pixel travel not yet handed to the paddle, in Q8.8.
    Fixed accumulator_ = 0;
};

}  // namespace pong::game
