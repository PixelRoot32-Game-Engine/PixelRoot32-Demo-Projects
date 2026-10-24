/*
 * RED suite for the cannon clamp (S7, D2).
 *
 * The cannon is continuous: it steps one pixel per tick and the field edge is
 * the only thing that stops it. The clamp lives in the engine-free rules layer
 * so a wrong bound is a test failure rather than a cannon that walks off the
 * panel, which is what the old nine-lane system hid.
 */
#include <unity.h>

#include "rules/RulesConstants.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_cannon_x_clamps_below_the_field(void) {
    TEST_ASSERT_EQUAL_INT(0, clampCannonX(-1));
    TEST_ASSERT_EQUAL_INT(0, clampCannonX(-123));
}

void test_cannon_x_clamps_at_the_right_edge(void) {
    TEST_ASSERT_EQUAL_INT(CannonMaxX, clampCannonX(CannonMaxX + 1));
    TEST_ASSERT_EQUAL_INT(CannonMaxX, clampCannonX(999));
}

void test_cannon_x_keeps_values_inside_the_field(void) {
    TEST_ASSERT_EQUAL_INT(0, clampCannonX(0));
    TEST_ASSERT_EQUAL_INT(1, clampCannonX(1));
    TEST_ASSERT_EQUAL_INT(105, clampCannonX(105));
    TEST_ASSERT_EQUAL_INT(CannonMaxX, clampCannonX(CannonMaxX));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_cannon_x_clamps_below_the_field);
    RUN_TEST(test_cannon_x_clamps_at_the_right_edge);
    RUN_TEST(test_cannon_x_keeps_values_inside_the_field);

    return UNITY_END();
}
