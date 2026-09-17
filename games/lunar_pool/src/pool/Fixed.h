/*
 * Fixed.h - Fixed-point numeric base for the pool physics core.
 *
 * src/pool/ has no engine dependency and no float/double on purpose: the
 * physics must give bit-identical results on native (x86/x64) and on the
 * ESP32's Xtensa core, and floating point is not guaranteed to round the
 * same way across those two targets. Every quantity below is stored as an
 * integer count of a fixed unit; see the unit constants below for what each
 * one represents.
 */
#pragma once

#include <cstdint>

namespace pool {

// --- Units ---------------------------------------------------------------------

/** Raw position/velocity units per pixel: 1 px = 256 raw units. */
constexpr int32_t kPxScale = 256;

/** Simulation substeps per second (60 fps x 4 substeps per frame). */
constexpr int32_t kSubstepsPerSecond = 240;

/** Substeps advanced per 1/60 s frame. */
constexpr int32_t kSubstepsPerFrame = 4;

/** Ball radius in raw units (4 px). */
constexpr int32_t kBallRadiusRaw = 1024;

/** Ball-ball contact distance in raw units: twice the radius. */
constexpr int32_t kBallContactRaw = 2 * kBallRadiusRaw;

/**
 * Pocket capture radius in raw units (6 px). A ball center strictly inside
 * this distance of a hole is captured; "strictly inside" keeps a ball that
 * is exactly tangent to the capture circle on the table.
 */
constexpr int32_t kCaptureRadiusRaw = 1536;

/**
 * Speed removed per substep by friction, in raw units (~112 px/s^2 x 1/240 s,
 * floored to an integer).
 */
constexpr int32_t kFrictionPerSubstepRaw = 119;

/** Maximum launch speed in raw units: power level 10 x 36 px/s = 360 px/s. */
constexpr int32_t kMaxSpeedRaw = 92160;

/** Launch speed added per power-meter level, in raw units (36 px/s). */
constexpr int32_t kSpeedPerLevelRaw = 9216;

/**
 * Separation slop added past the minimum distance after a collision push, in
 * raw units, so a resolved contact does not immediately re-trigger from
 * rounding alone.
 */
constexpr int32_t kSlopRaw = 2;

static_assert(kMaxSpeedRaw / kSubstepsPerSecond < kBallRadiusRaw,
              "Per-substep displacement at max speed must stay below the ball "
              "radius, or a fast ball could tunnel through a thin cushion in "
              "one substep.");

// --- Rounded arithmetic ----------------------------------------------------

/**
 * @brief Divides two integers and rounds the quotient half away from zero.
 *
 * Every rounded division in the physics core goes through this function, so
 * every compiler and target rounds the same way. The ball-ball and cushion
 * collision-response formulas both need rounding-away-from-zero, not
 * truncation, to hit their documented tolerances.
 *
 * @param num Numerator. May be negative.
 * @param den Denominator. MUST be strictly positive.
 * @return num / den, rounded half away from zero.
 */
[[nodiscard]] int64_t divRound(int64_t num, int64_t den);

/**
 * @brief Computes the exact integer floor of the square root of a 64-bit value.
 *
 * Uses a digit-by-digit binary algorithm (no float/double), so the result
 * is bit-identical on every target. The result always fits in 32
 * bits because floor(sqrt(UINT64_MAX)) == UINT32_MAX.
 *
 * @param value Value to take the square root of.
 * @return floor(sqrt(value)).
 */
[[nodiscard]] uint32_t isqrt64(uint64_t value);

/**
 * @brief Converts a raw position/velocity value to whole pixels.
 * @param raw Value in raw units (1/256 px).
 * @return raw / 256, rounded half away from zero.
 */
[[nodiscard]] int32_t toPixel(int32_t raw);

}  // namespace pool
