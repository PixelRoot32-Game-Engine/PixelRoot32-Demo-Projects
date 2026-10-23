/*
 * RED suite for the enemy reload ladder (S9).
 *
 * The ROM picks the reload budget from the score high byte at the thresholds
 * 0x02, 0x10, 0x20 and 0x30. Each test pins one band on both sides of its
 * boundary, so an off-by-one comparison fails instead of sliding through.
 */
#include <unity.h>

#include "rules/FireTiming.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_reload_ladder_first_band(void) {
    TEST_ASSERT_EQUAL_INT(48, static_cast<int>(enemyReloadTicks(0)));
    TEST_ASSERT_EQUAL_INT(48, static_cast<int>(enemyReloadTicks(1)));
}

void test_reload_ladder_second_band(void) {
    TEST_ASSERT_EQUAL_INT(16, static_cast<int>(enemyReloadTicks(2)));
    TEST_ASSERT_EQUAL_INT(16, static_cast<int>(enemyReloadTicks(15)));
}

void test_reload_ladder_third_band(void) {
    TEST_ASSERT_EQUAL_INT(11, static_cast<int>(enemyReloadTicks(16)));
    TEST_ASSERT_EQUAL_INT(11, static_cast<int>(enemyReloadTicks(31)));
}

void test_reload_ladder_fourth_band(void) {
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(enemyReloadTicks(32)));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(enemyReloadTicks(47)));
}

void test_reload_ladder_top_band(void) {
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(enemyReloadTicks(48)));
    TEST_ASSERT_EQUAL_INT(7, static_cast<int>(enemyReloadTicks(255)));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_reload_ladder_first_band);
    RUN_TEST(test_reload_ladder_second_band);
    RUN_TEST(test_reload_ladder_third_band);
    RUN_TEST(test_reload_ladder_fourth_band);
    RUN_TEST(test_reload_ladder_top_band);

    return UNITY_END();
}
