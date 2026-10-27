#pragma once

#include <unity.h>

/**
 * @brief Prepares the state a test needs.
 *
 * The court rules hold no global state: a match is a `Rules` object the test
 * builds itself and lets go out of scope, so the shared setup is empty.
 */
inline void test_setup() {}

/**
 * @brief Releases whatever `test_setup()` prepared.
 */
inline void test_teardown() {}
