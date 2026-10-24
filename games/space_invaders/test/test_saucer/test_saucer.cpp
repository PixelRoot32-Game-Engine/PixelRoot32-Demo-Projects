/*
 * RED suite for the saucer (S11).
 *
 * Three independent facts are pinned: the 15-value score table and its wrap,
 * the 1536-tick spawn period gated on eight live aliens, and the entry side
 * taken from the parity of the shot counter. The stubs return zero, so the
 * table, the timer and the side all fail until T1.3.
 */
#include <unity.h>

#include "rules/Saucer.h"

using namespace spaceinvaders::rules;

namespace {

const uint16_t EXPECTED_SCORES[SaucerScoreCount] = {
    100, 50, 50, 100, 150, 100, 100, 50, 300, 100, 100, 100, 50, 150, 100,
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_saucer_score_table_in_order(void) {
    Saucer saucer;
    saucer.reset();

    for (int index = 0; index < SaucerScoreCount; ++index) {
        TEST_ASSERT_EQUAL_INT(EXPECTED_SCORES[index],
                              static_cast<int>(saucer.takeScore()));
    }
}

void test_saucer_score_table_wraps_to_one_hundred(void) {
    Saucer saucer;
    saucer.reset();

    for (int index = 0; index < SaucerScoreCount; ++index) {
        saucer.takeScore();
    }

    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(saucer.takeScore()));
    TEST_ASSERT_EQUAL_INT(50, static_cast<int>(saucer.takeScore()));
}

void test_saucer_current_score_matches_the_next_entry(void) {
    Saucer saucer;
    saucer.reset();

    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(saucer.currentScore()));
    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(saucer.takeScore()));
    TEST_ASSERT_EQUAL_INT(50, static_cast<int>(saucer.currentScore()));
}

void test_saucer_spawn_due_after_the_period(void) {
    Saucer saucer;
    saucer.reset();

    for (int tickIndex = 0; tickIndex < SaucerPeriodTicks - 1; ++tickIndex) {
        saucer.tick();
    }
    TEST_ASSERT_FALSE(saucer.spawnDue(11));

    saucer.tick();
    TEST_ASSERT_TRUE(saucer.spawnDue(11));
}

void test_saucer_spawn_due_needs_eight_aliens(void) {
    Saucer saucer;
    saucer.reset();

    for (int tickIndex = 0; tickIndex < SaucerPeriodTicks; ++tickIndex) {
        saucer.tick();
    }

    TEST_ASSERT_FALSE(saucer.spawnDue(7));
    TEST_ASSERT_TRUE(saucer.spawnDue(8));
}

void test_saucer_side_alternates_with_shot_count(void) {
    Saucer saucer;
    saucer.reset();

    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(saucer.side(0)));
    TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(saucer.side(1)));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(saucer.side(2)));
    TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(saucer.side(3)));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(saucer.side(4)));
}

void test_saucer_timer_counts_ticks_and_restarts_on_spawn(void) {
    Saucer saucer;
    saucer.reset();

    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(saucer.timer()));

    for (int tickIndex = 0; tickIndex < 60; ++tickIndex) {
        saucer.tick();
    }
    TEST_ASSERT_EQUAL_INT(60, static_cast<int>(saucer.timer()));

    saucer.onSpawned();
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(saucer.timer()));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_saucer_score_table_in_order);
    RUN_TEST(test_saucer_score_table_wraps_to_one_hundred);
    RUN_TEST(test_saucer_current_score_matches_the_next_entry);
    RUN_TEST(test_saucer_spawn_due_after_the_period);
    RUN_TEST(test_saucer_spawn_due_needs_eight_aliens);
    RUN_TEST(test_saucer_side_alternates_with_shot_count);
    RUN_TEST(test_saucer_timer_counts_ticks_and_restarts_on_spawn);

    return UNITY_END();
}
