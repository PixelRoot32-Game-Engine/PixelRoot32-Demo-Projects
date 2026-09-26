/*
 * Unit tests for the fixed-point numeric base (src/pool/Fixed.*).
 *
 * These pin the exact rounding behavior every physics formula in this codebase
 * depends on: divRound must round half away from zero on both signs, isqrt64
 * must be an exact floor at square boundaries, and toPixel must round negative
 * raw values consistently with divRound.
 */
#include <unity.h>

#include <cstdint>
#include <limits>

#include "pool/Fixed.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

// --- divRound ----------------------------------------------------------------

void test_fixed_divround_exact_positive(void) {
    TEST_ASSERT_EQUAL_INT64(3, divRound(6, 2));
    TEST_ASSERT_EQUAL_INT64(0, divRound(0, 5));
}

void test_fixed_divround_rounds_half_away_from_zero_positive(void) {
    // 5/2 = 2.5 -> rounds up to 3 (half away from zero).
    TEST_ASSERT_EQUAL_INT64(3, divRound(5, 2));
    // 6/4 = 1.5 -> rounds up to 2.
    TEST_ASSERT_EQUAL_INT64(2, divRound(6, 4));
}

void test_fixed_divround_rounds_half_away_from_zero_negative(void) {
    // -5/2 = -2.5 -> rounds away from zero to -3, never toward zero (-2).
    TEST_ASSERT_EQUAL_INT64(-3, divRound(-5, 2));
    TEST_ASSERT_EQUAL_INT64(-2, divRound(-6, 4));
}

void test_fixed_divround_truncates_below_half(void) {
    // 3/4 = 0.75 rounds up to 1; 1/4 = 0.25 rounds down to 0; signs mirror.
    TEST_ASSERT_EQUAL_INT64(1, divRound(3, 4));
    TEST_ASSERT_EQUAL_INT64(0, divRound(1, 4));
    TEST_ASSERT_EQUAL_INT64(-1, divRound(-3, 4));
    TEST_ASSERT_EQUAL_INT64(0, divRound(-1, 4));
}

void test_fixed_divround_int64_min_boundary(void) {
    // divRound negates through unsigned wraparound instead of plain `-num`
    // specifically so num == INT64_MIN does not invoke undefined behavior
    // (INT64_MIN has no positive int64_t equivalent). This pins that path.
    //
    // |INT64_MIN| = 2^63, and 2^63 is exactly divisible by 256, so the
    // rounding term (den/2 = 128) contributes nothing:
    //   (2^63 + 128) / 256 = 2^55 + 128/256, truncated = 2^55.
    // Result is negated back: -(2^55) = -(1LL << 55).
    TEST_ASSERT_EQUAL_INT64(-(1LL << 55),
                             divRound(std::numeric_limits<int64_t>::min(), 256));
}

void test_fixed_divround_int64_max_boundary(void) {
    // INT64_MAX = 2^63 - 1. (2^63 - 1 + 128) / 256 = (2^63 + 127) / 256.
    // 2^63 is exactly divisible by 256, so this is 2^55 + 127/256, and
    // integer division truncates the 127/256 remainder away: result = 2^55.
    // (As a decimal check: INT64_MAX / 256 = 36028797018963967.996...,
    // whose fractional part exceeds one half, so half-away-from-zero
    // rounding lands on 36028797018963968 = 2^55 = 1LL << 55.)
    TEST_ASSERT_EQUAL_INT64(1LL << 55,
                             divRound(std::numeric_limits<int64_t>::max(), 256));
}

void test_fixed_divround_large_negative_remainder(void) {
    // -123456789012347 / 4 = -30864197253086.75 exactly (123456789012347
    // mod 4 == 3, since ...347 mod 4 == 3). The fractional part 0.75
    // exceeds one half, so half-away-from-zero rounds the magnitude up:
    // -30864197253086 - 1 = -30864197253087 (never truncated toward zero).
    TEST_ASSERT_EQUAL_INT64(-30864197253087LL, divRound(-123456789012347LL, 4));
}

// --- isqrt64 -------------------------------------------------------------------

void test_fixed_isqrt64_zero_and_one(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, isqrt64(0u));
    TEST_ASSERT_EQUAL_UINT32(1u, isqrt64(1u));
}

void test_fixed_isqrt64_exact_square(void) {
    // k comfortably exceeds sqrt(2^31), so k*k only fits because isqrt64 takes
    // a uint64_t: this proves the function does real 64-bit math, not a
    // narrowed 32-bit shortcut.
    const uint64_t k = 46341u;
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(k), isqrt64(k * k));
}

void test_fixed_isqrt64_one_below_exact_square(void) {
    const uint64_t k = 46341u;
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(k - 1), isqrt64(k * k - 1));
}

void test_fixed_isqrt64_uint64_max(void) {
    // floor(sqrt(UINT64_MAX)) == UINT32_MAX exactly.
    TEST_ASSERT_EQUAL_UINT32(std::numeric_limits<uint32_t>::max(),
                              isqrt64(std::numeric_limits<uint64_t>::max()));
}

// --- toPixel ---------------------------------------------------------------

void test_fixed_topixel_positive(void) {
    TEST_ASSERT_EQUAL_INT32(1, toPixel(256));
    TEST_ASSERT_EQUAL_INT32(2, toPixel(384));  // 384/256 = 1.5 -> rounds to 2
}

void test_fixed_topixel_negative(void) {
    TEST_ASSERT_EQUAL_INT32(-1, toPixel(-256));
    TEST_ASSERT_EQUAL_INT32(-2, toPixel(-384));  // rounds away from zero, not toward it
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_fixed_divround_exact_positive);
    RUN_TEST(test_fixed_divround_rounds_half_away_from_zero_positive);
    RUN_TEST(test_fixed_divround_rounds_half_away_from_zero_negative);
    RUN_TEST(test_fixed_divround_truncates_below_half);
    RUN_TEST(test_fixed_divround_int64_min_boundary);
    RUN_TEST(test_fixed_divround_int64_max_boundary);
    RUN_TEST(test_fixed_divround_large_negative_remainder);
    RUN_TEST(test_fixed_isqrt64_zero_and_one);
    RUN_TEST(test_fixed_isqrt64_exact_square);
    RUN_TEST(test_fixed_isqrt64_one_below_exact_square);
    RUN_TEST(test_fixed_isqrt64_uint64_max);
    RUN_TEST(test_fixed_topixel_positive);
    RUN_TEST(test_fixed_topixel_negative);
    return UNITY_END();
}
