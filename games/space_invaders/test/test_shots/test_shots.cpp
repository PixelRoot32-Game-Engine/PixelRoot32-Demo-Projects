/*
 * RED suite for projectile motion (S8, S9).
 *
 * The player shot crosses four pixels per tick; the enemy shot owns a
 * three-tick phase and steps four or five pixels only on the third call.
 * The stubs never move, so every assertion below fails until T1.3.
 */
#include <unity.h>

#include "rules/Shots.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_player_shot_advances_four_pixels_up(void) {
    TEST_ASSERT_EQUAL_INT(96, static_cast<int>(advancePlayerShot(100)));
    TEST_ASSERT_EQUAL_INT(4, static_cast<int>(advancePlayerShot(8)));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(advancePlayerShot(4)));
}

void test_enemy_shot_moves_on_every_third_tick(void) {
    EnemyShotMotion shot;
    shot.reset(100);
    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(shot.y()));

    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(shot.advance(false)));
    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(shot.advance(false)));
    TEST_ASSERT_EQUAL_INT(104, static_cast<int>(shot.advance(false)));

    TEST_ASSERT_EQUAL_INT(104, static_cast<int>(shot.advance(false)));
    TEST_ASSERT_EQUAL_INT(104, static_cast<int>(shot.advance(false)));
    TEST_ASSERT_EQUAL_INT(108, static_cast<int>(shot.advance(false)));
}

void test_enemy_shot_uses_five_pixels_when_fast(void) {
    EnemyShotMotion shot;
    shot.reset(100);

    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(shot.advance(true)));
    TEST_ASSERT_EQUAL_INT(100, static_cast<int>(shot.advance(true)));
    TEST_ASSERT_EQUAL_INT(105, static_cast<int>(shot.advance(true)));

    TEST_ASSERT_EQUAL_INT(105, static_cast<int>(shot.advance(true)));
    TEST_ASSERT_EQUAL_INT(105, static_cast<int>(shot.advance(true)));
    TEST_ASSERT_EQUAL_INT(110, static_cast<int>(shot.advance(true)));
}

void test_enemy_shot_is_fast_at_eight_aliens_and_not_at_nine(void) {
    TEST_ASSERT_TRUE(enemyShotIsFast(8));
    TEST_ASSERT_FALSE(enemyShotIsFast(9));
    TEST_ASSERT_TRUE(enemyShotIsFast(1));
    TEST_ASSERT_FALSE(enemyShotIsFast(55));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_player_shot_advances_four_pixels_up);
    RUN_TEST(test_enemy_shot_moves_on_every_third_tick);
    RUN_TEST(test_enemy_shot_uses_five_pixels_when_fast);
    RUN_TEST(test_enemy_shot_is_fast_at_eight_aliens_and_not_at_nine);

    return UNITY_END();
}
