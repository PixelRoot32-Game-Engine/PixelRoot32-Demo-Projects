#pragma once

#include <unity.h>

/**
 * @brief Prepares the state a test needs.
 *
 * The tic clock owns its state per instance: a test builds a `TicClock` itself
 * and lets it go out of scope, so there is no shared setup to perform.
 */
inline void test_setup() {}

/**
 * @brief Releases whatever `test_setup()` prepared.
 */
inline void test_teardown() {}
