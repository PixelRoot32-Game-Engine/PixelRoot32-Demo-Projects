/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "game/PongRules.h"  // court primitives first: PongCpu.h reads them
#include "game/PongCpu.h"

namespace pong::game {
namespace {

/// Half the paddle height, in Q8.8: the distance from the top edge to the centre.
constexpr Fixed kHalfPaddle = (kPaddleHeight / 2) * kOne;

/// Top edge limits of a paddle, in Q8.8. The same box the rules apply to the
/// player paddle, so the CPU can never occupy a pixel the player cannot.
constexpr Fixed kCpuTopLimit = kHudHeight * kOne;
constexpr Fixed kCpuBottomLimit = (kCourtHeight - kPaddleHeight) * kOne;

/// Numerical Recipes 32-bit linear congruential generator constants.
///
/// A full-period LCG is all the imperfection needs: it is cheap, it is exactly
/// reproducible in 32-bit unsigned arithmetic on every target, and it needs no
/// state beyond one word. std::minstd_rand and friends would drag in <random>
/// and the C++ runtime, which this layer deliberately does not use.
constexpr std::uint32_t kLcgMultiplier = 1664525u;
constexpr std::uint32_t kLcgIncrement = 1013904223u;

/// Bits discarded before the sample is taken.
///
/// The low bits of an LCG with a power-of-two modulus cycle far too quickly to
/// look random; the top 16 bits are the useful ones, and 16 bits are more than
/// the 1537 distinct offsets of the +-3 px box need.
constexpr int kLcgSampleShift = 16;

static_assert(kPaddleHeight % 2 == 0, "the chase is measured from the paddle centre");
static_assert(kCpuMaxSpeed < kPaddleSpeed, "the cpu must lose every speed race it can lose");
static_assert(kCpuDeadZone > 0, "a zero dead zone would let the paddle buzz on a parked ball");

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

}  // namespace

CpuPaddle::CpuPaddle(Fixed noiseRadius, std::uint32_t seed)
    : noiseRadius_(noiseRadius), seed_(seed), noise_(seed) {}

void CpuPaddle::reset() {
    noise_ = seed_;
    accumulator_ = 0;
}

Fixed CpuPaddle::step(Fixed ballY, Fixed paddleY, std::uint32_t deltaUs) {
    // The controller aims at the centre of its paddle, not at its top edge.
    Fixed error = ballY - (paddleY + kHalfPaddle);

    if (noiseRadius_ > 0) {
        // One sample per step. The generator advances even when the resulting
        // offset is unused, so the stream stays tied to the step count and not
        // to how the paddle happened to move.
        noise_ = noise_ * kLcgMultiplier + kLcgIncrement;

        // Uniform over [-noiseRadius_, +noiseRadius_] in Q8.8, both ends
        // included: `span` is 2r + 1 distinct values, and subtracting r centres
        // them on zero. The box is the demo's AI_TARGET_OFFSET / 2, so the
        // offset can never exceed it; a 65536 % span remainder makes the
        // distribution very slightly uneven at the edges, which is invisible in
        // a 3 px box and irrelevant to a replay.
        const std::uint32_t span = static_cast<std::uint32_t>(2 * noiseRadius_) + 1u;
        const Fixed sample =
            static_cast<Fixed>((noise_ >> kLcgSampleShift) % span) - noiseRadius_;
        error += sample;
    }

    // Gain 3/2. Truncation toward zero keeps the controller symmetric: errors of
    // the same size above and below the paddle produce the same speed.
    const Fixed speed = clampFixed(error * kCpuGainNum / kCpuGainDen, -kCpuMaxSpeed, kCpuMaxSpeed);

    if (absFixed(error) > kCpuDeadZone) {
        accumulator_ += scaleByTime(speed, deltaUs);
    }

    // A paddle moves in whole pixels; what is left over waits in the
    // accumulator for the next step. Truncating toward zero leaves the remainder
    // with the sign of the travel, so reversing direction does not first pay off
    // the other direction's sub-pixel debt.
    const Fixed wholePixels = accumulator_ / kOne;
    if (wholePixels != 0) {
        accumulator_ -= wholePixels * kOne;
        paddleY += wholePixels * kOne;
    }

    const Fixed clamped = clampFixed(paddleY, kCpuTopLimit, kCpuBottomLimit);
    if (clamped != paddleY) {
        // The clamp ate the travel: keeping the remainder would make the paddle
        // lurch the moment the ball turned away from the wall.
        accumulator_ = 0;
        return clamped;
    }
    return paddleY;
}

}  // namespace pong::game
