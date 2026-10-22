#pragma once

#include <unity.h>

/**
 * @brief Prepares the state a test needs.
 *
 * The Pac-Man clone's rules layer holds no global state, so the shared setup
 * is intentionally empty: every test builds the objects it needs and lets
 * them go out of scope.
 */
inline void test_setup() {}

/**
 * @brief Releases whatever `test_setup()` prepared.
 */
inline void test_teardown() {}

/**
 * @brief Approximate float comparison for the few places that need one.
 *
 * @param expected The expected value.
 * @param actual The measured value.
 * @param delta The largest difference that still counts as equal.
 * @return true when the two values are within `delta` of each other.
 */
inline bool float_eq(float expected, float actual, float delta = 0.0001f) {
    const float difference = expected - actual;
    return (difference < 0.0f ? -difference : difference) <= delta;
}
