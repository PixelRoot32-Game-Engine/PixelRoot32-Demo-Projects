/*
 * Trig.h - Angle-table sine/cosine for the pool physics core.
 *
 * Angles are a uint16_t count of 1/1024 of a revolution: 0 points along +x,
 * and increasing angle turns clockwise on screen (y grows downward). Values
 * are Q14 fixed point (16384 = 1.0) so a launch velocity component is a
 * single integer multiply-divide, with no float/double anywhere (this file
 * must give bit-identical results on native and on the ESP32's Xtensa
 * core, the same constraint as src/pool/Fixed.h).
 */
#pragma once

#include <cstdint>

namespace pool {

/** Q14 scale: 16384 represents 1.0. */
constexpr int32_t kQ14One = 16384;

/** Angle resolution: 1,024 steps per full revolution. */
constexpr uint16_t kAngleSteps = 1024;

/**
 * @brief Sine of an angle, in Q14 fixed point.
 *
 * Looks up a stored quarter-wave table (0..90 degrees) and folds the other
 * three quadrants by symmetry, so only 257 entries cover all 1,024 angles.
 *
 * @param angle Angle in 1/1024-revolution steps. Any value is accepted; it
 *   wraps modulo 1,024.
 * @return sin(angle) in Q14 fixed point, in the range [-16384, 16384].
 */
[[nodiscard]] int32_t sinQ14(uint16_t angle);

/**
 * @brief Cosine of an angle, in Q14 fixed point.
 *
 * Implemented as sinQ14(angle + 256), a quarter-turn phase shift.
 *
 * @param angle Angle in 1/1024-revolution steps. Any value is accepted; it
 *   wraps modulo 1,024.
 * @return cos(angle) in Q14 fixed point, in the range [-16384, 16384].
 */
[[nodiscard]] int32_t cosQ14(uint16_t angle);

}  // namespace pool
