/*
 * Unit tests for the angle trig table (src/pool/Trig.*).
 *
 * The 1,024-step angle wheel and the Q14 sine/cosine table are exercised
 * exhaustively (all 1,024 angles) rather than with a handful of samples,
 * because the physics core's launch and collision-response formulas depend
 * on every angle giving a self-consistent, exact-symmetry result.
 */
#include <unity.h>

#include <cstdint>
#include <cstdio>

#include "pool/Trig.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

// --- Cardinal values -----------------------------------------------------

void test_trig_sin_cardinal_values(void) {
    TEST_ASSERT_EQUAL_INT32(0, sinQ14(0));           // 0 degrees
    TEST_ASSERT_EQUAL_INT32(kQ14One, sinQ14(256));    // 90 degrees
    TEST_ASSERT_EQUAL_INT32(0, sinQ14(512));          // 180 degrees
    TEST_ASSERT_EQUAL_INT32(-kQ14One, sinQ14(768));   // 270 degrees
}

void test_trig_cos_cardinal_values(void) {
    TEST_ASSERT_EQUAL_INT32(kQ14One, cosQ14(0));      // 0 degrees
    TEST_ASSERT_EQUAL_INT32(0, cosQ14(256));          // 90 degrees
    TEST_ASSERT_EQUAL_INT32(-kQ14One, cosQ14(512));   // 180 degrees
    TEST_ASSERT_EQUAL_INT32(0, cosQ14(768));          // 270 degrees
}

// --- Exhaustive symmetry and identity checks over all 1,024 angles -----------

void test_trig_sin_odd_symmetry_over_half_turn(void) {
    // sin(a) == -sin(a + 512) for every angle: a half-turn flips the sign.
    for (uint32_t a = 0; a < kAngleSteps; ++a) {
        const uint16_t angle = static_cast<uint16_t>(a);
        const uint16_t opposite = static_cast<uint16_t>((angle + 512u) % kAngleSteps);
        char message[48];
        std::snprintf(message, sizeof(message), "angle=%u", angle);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(sinQ14(angle), -sinQ14(opposite), message);
    }
}

void test_trig_sin_mirror_symmetry_over_quarter_turn(void) {
    // sin(512 - a) == sin(a) for every angle: mirrors around the 90-degree axis.
    for (uint32_t a = 0; a < kAngleSteps; ++a) {
        const uint16_t angle = static_cast<uint16_t>(a);
        const uint16_t mirrored = static_cast<uint16_t>((1024u + 512u - angle) % kAngleSteps);
        char message[48];
        std::snprintf(message, sizeof(message), "angle=%u", angle);
        TEST_ASSERT_EQUAL_INT32_MESSAGE(sinQ14(angle), sinQ14(mirrored), message);
    }
}

void test_trig_pythagorean_identity_within_tolerance(void) {
    // sin^2 + cos^2 must stay within +-32,768 of 2^28 (Q14 squared) at every
    // angle: the table is rounded to the nearest Q14 integer, so this bounds
    // how far that rounding can drift from unit length.
    const int64_t target = int64_t{1} << 28;
    const int64_t tolerance = 32768;
    for (uint32_t a = 0; a < kAngleSteps; ++a) {
        const uint16_t angle = static_cast<uint16_t>(a);
        const int64_t s = sinQ14(angle);
        const int64_t c = cosQ14(angle);
        const int64_t sumSquares = s * s + c * c;
        const int64_t diff = sumSquares > target ? sumSquares - target : target - sumSquares;
        char message[64];
        std::snprintf(message, sizeof(message), "angle=%u sumSquares=%lld", angle,
                      static_cast<long long>(sumSquares));
        TEST_ASSERT_TRUE_MESSAGE(diff <= tolerance, message);
    }
}

void test_trig_quarter_wave_is_non_decreasing(void) {
    // The stored quarter wave (0..90 degrees) must never decrease, or a
    // 1-step angle increase could jump the launch direction backwards.
    int32_t previous = sinQ14(0);
    for (uint16_t angle = 1; angle <= 256; ++angle) {
        const int32_t current = sinQ14(angle);
        char message[48];
        std::snprintf(message, sizeof(message), "angle=%u", angle);
        TEST_ASSERT_TRUE_MESSAGE(current >= previous, message);
        previous = current;
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_trig_sin_cardinal_values);
    RUN_TEST(test_trig_cos_cardinal_values);
    RUN_TEST(test_trig_sin_odd_symmetry_over_half_turn);
    RUN_TEST(test_trig_sin_mirror_symmetry_over_quarter_turn);
    RUN_TEST(test_trig_pythagorean_identity_within_tolerance);
    RUN_TEST(test_trig_quarter_wave_is_non_decreasing);
    return UNITY_END();
}
