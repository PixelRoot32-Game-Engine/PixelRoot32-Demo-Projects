/*
 * RED suite for the alien variant table (S2, S4).
 *
 * Widths, height, scores and the row mapping are the four numbers the rack and
 * the HUD share. They live in functions rather than in the header constants so
 * a wrong table is a test failure, not a silent include.
 */
#include <unity.h>

#include "rules/RulesConstants.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_alien_widths(void) {
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(alienWidth(AlienType::Squid)));
    TEST_ASSERT_EQUAL_INT(11, static_cast<int>(alienWidth(AlienType::Crab)));
    TEST_ASSERT_EQUAL_INT(12, static_cast<int>(alienWidth(AlienType::Octopus)));
}

void test_alien_heights(void) {
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(alienHeight(AlienType::Squid)));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(alienHeight(AlienType::Crab)));
    TEST_ASSERT_EQUAL_INT(8, static_cast<int>(alienHeight(AlienType::Octopus)));
}

void test_alien_scores(void) {
    TEST_ASSERT_EQUAL_INT(30, static_cast<int>(alienScore(AlienType::Squid)));
    TEST_ASSERT_EQUAL_INT(20, static_cast<int>(alienScore(AlienType::Crab)));
    TEST_ASSERT_EQUAL_INT(10, static_cast<int>(alienScore(AlienType::Octopus)));
}

void test_alien_type_per_row(void) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Squid),
                          static_cast<int>(alienTypeForRow(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Crab),
                          static_cast<int>(alienTypeForRow(1)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Crab),
                          static_cast<int>(alienTypeForRow(2)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Octopus),
                          static_cast<int>(alienTypeForRow(3)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(AlienType::Octopus),
                          static_cast<int>(alienTypeForRow(4)));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_alien_widths);
    RUN_TEST(test_alien_heights);
    RUN_TEST(test_alien_scores);
    RUN_TEST(test_alien_type_per_row);

    return UNITY_END();
}
