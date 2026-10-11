/*
 * Unit tests for ScoringSystem: base table, level curve and saturation (T10 edge).
 */
#include <unity.h>

#include "game/ScoringSystem.h"

using namespace tetris;

void test_base_table_times_level(void) {
    ScoringSystem scoring;
    scoring.addLines(1);
    TEST_ASSERT_EQUAL_UINT32(100, scoring.score());
    scoring.reset();
    scoring.addLines(2);
    TEST_ASSERT_EQUAL_UINT32(300, scoring.score());
    scoring.reset();
    scoring.addLines(3);
    TEST_ASSERT_EQUAL_UINT32(500, scoring.score());
    scoring.reset();
    scoring.addLines(4);
    TEST_ASSERT_EQUAL_UINT32(800, scoring.score());
}

void test_level_advances_every_ten_lines(void) {
    ScoringSystem scoring;
    TEST_ASSERT_EQUAL_UINT8(1, scoring.level());
    for (int i = 0; i < 9; ++i) {
        scoring.addLines(1);
    }
    TEST_ASSERT_EQUAL_UINT8(1, scoring.level());
    scoring.addLines(1);
    TEST_ASSERT_EQUAL_UINT8(2, scoring.level());
    TEST_ASSERT_EQUAL_UINT16(10, scoring.lines());
}

void test_points_use_level_at_clear_time(void) {
    ScoringSystem scoring;
    for (int i = 0; i < 10; ++i) {
        scoring.addLines(1);
    }
    const std::uint32_t before = scoring.score();
    scoring.addLines(1);
    TEST_ASSERT_EQUAL_UINT32(before + 200U, scoring.score());
}

void test_fall_speed_increases_with_level(void) {
    const std::uint32_t l1 = ScoringSystem::fallIntervalMs(1);
    const std::uint32_t l5 = ScoringSystem::fallIntervalMs(5);
    const std::uint32_t l15 = ScoringSystem::fallIntervalMs(15);
    TEST_ASSERT_EQUAL_UINT32(800, l1);
    TEST_ASSERT_TRUE(l5 < l1);
    TEST_ASSERT_TRUE(l15 <= l5);
    TEST_ASSERT_TRUE(l15 >= 50);
}

void test_soft_and_hard_drop_bonus(void) {
    ScoringSystem scoring;
    scoring.addSoftDropCells(10);
    TEST_ASSERT_EQUAL_UINT32(10, scoring.score());
    scoring.addHardDropCells(10);
    TEST_ASSERT_EQUAL_UINT32(30, scoring.score());
}

void test_score_saturates_instead_of_wrapping(void) {
    ScoringSystem scoring;
    scoring.addHardDropCells(0xFFFFFFFFU / 2U);
    scoring.addHardDropCells(0xFFFFFFFFU / 2U);
    scoring.addLines(4);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFU, scoring.score());
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_base_table_times_level);
    RUN_TEST(test_level_advances_every_ten_lines);
    RUN_TEST(test_points_use_level_at_clear_time);
    RUN_TEST(test_fall_speed_increases_with_level);
    RUN_TEST(test_soft_and_hard_drop_bonus);
    RUN_TEST(test_score_saturates_instead_of_wrapping);
    return UNITY_END();
}
