/**
 * @brief Deferred settings save: a Start press marks the settings dirty, and
 *        the write is due only after a quiet second at a safe moment.
 *
 * Pinned here: nothing is due while clean; a press arms the gate and restarts
 * the idle count; the save is held while the moment is unsafe (driving or
 * moving) however long it waits; it fires once the idle time is reached and
 * is then cleared; a failed save stays dirty and retries after another idle
 * period rather than on the next frame; a press during the wait defers it.
 */
#include <unity.h>

#include "game/rules/SettingsSave.h"

namespace ss = top_down_city::settings_save;

void setUp() {}
void tearDown() {}

void test_clean_gate_is_never_due() {
    ss::Gate g = ss::idle();
    TEST_ASSERT_FALSE(g.dirty);
    TEST_ASSERT_FALSE(ss::due(g, 100000, true));
}

void test_press_arms_but_is_not_due_at_once() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_TRUE(g.dirty);
    TEST_ASSERT_FALSE(ss::due(g, 16, true));
}

void test_due_after_idle_time_when_safe() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_FALSE(ss::due(g, ss::kIdleMs - 1, true));
    TEST_ASSERT_TRUE(ss::due(g, 1, true));
}

void test_held_while_unsafe_then_due_on_first_safe_frame() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_FALSE(ss::due(g, 60000, false));
    TEST_ASSERT_TRUE(ss::due(g, 16, true));
}

void test_second_press_restarts_the_wait() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_FALSE(ss::due(g, ss::kIdleMs - 100, true));
    ss::pressed(g);
    TEST_ASSERT_FALSE(ss::due(g, ss::kIdleMs - 100, true));
    TEST_ASSERT_TRUE(ss::due(g, 100, true));
}

void test_saved_ok_clears() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_TRUE(ss::due(g, ss::kIdleMs, true));
    ss::saved(g, true);
    TEST_ASSERT_FALSE(g.dirty);
    TEST_ASSERT_FALSE(ss::due(g, ss::kIdleMs, true));
}

void test_saved_failure_stays_dirty_and_waits_again() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    TEST_ASSERT_TRUE(ss::due(g, ss::kIdleMs, true));
    ss::saved(g, false);
    TEST_ASSERT_TRUE(g.dirty);
    TEST_ASSERT_FALSE(ss::due(g, 16, true));
    TEST_ASSERT_TRUE(ss::due(g, ss::kIdleMs, true));
}

void test_huge_delta_does_not_overflow() {
    ss::Gate g = ss::idle();
    ss::pressed(g);
    for (int i = 0; i < 4; ++i) {
        (void)ss::due(g, 2000000000, false);
    }
    TEST_ASSERT_TRUE(ss::due(g, 2000000000, true));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clean_gate_is_never_due);
    RUN_TEST(test_press_arms_but_is_not_due_at_once);
    RUN_TEST(test_due_after_idle_time_when_safe);
    RUN_TEST(test_held_while_unsafe_then_due_on_first_safe_frame);
    RUN_TEST(test_second_press_restarts_the_wait);
    RUN_TEST(test_saved_ok_clears);
    RUN_TEST(test_saved_failure_stays_dirty_and_waits_again);
    RUN_TEST(test_huge_delta_does_not_overflow);
    return UNITY_END();
}
