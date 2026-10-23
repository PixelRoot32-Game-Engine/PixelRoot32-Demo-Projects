/*
 * RED suite for score, lives, waves and the extra ship (S12).
 *
 * The reference keeps four score digits, one extra ship at 1500, three ships
 * and a session high score. The stubs return zero, so every test below fails
 * until T1.3 implements the counters.
 */
#include <unity.h>

#include "rules/Scoring.h"

using namespace spaceinvaders::rules;

void setUp(void) {}
void tearDown(void) {}

void test_scoring_accumulates_points(void) {
    Scoring scoring;
    scoring.reset();

    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(scoring.score()));

    scoring.add(30);
    scoring.add(20);
    scoring.add(10);

    TEST_ASSERT_EQUAL_INT(60, static_cast<int>(scoring.score()));
}

void test_scoring_clamps_at_9999(void) {
    Scoring scoring;
    scoring.reset();

    scoring.add(9998);
    TEST_ASSERT_EQUAL_INT(9998, static_cast<int>(scoring.score()));

    scoring.add(1);
    TEST_ASSERT_EQUAL_INT(9999, static_cast<int>(scoring.score()));

    scoring.add(500);
    TEST_ASSERT_EQUAL_INT(9999, static_cast<int>(scoring.score()));
}

void test_scoring_high_byte_indexes_the_reload_ladder(void) {
    Scoring scoring;
    scoring.reset();

    scoring.add(0x1234);

    TEST_ASSERT_EQUAL_INT(0x1234, static_cast<int>(scoring.score()));
    TEST_ASSERT_EQUAL_INT(0x12, static_cast<int>(scoring.highByte()));
}

void test_scoring_awards_the_extra_ship_at_1500_once(void) {
    Scoring scoring;
    scoring.reset();

    scoring.add(1499);
    TEST_ASSERT_FALSE(scoring.consumeExtraShip());
    TEST_ASSERT_FALSE(scoring.extraShipAwarded());

    scoring.add(1);
    TEST_ASSERT_TRUE(scoring.consumeExtraShip());
    TEST_ASSERT_TRUE(scoring.extraShipAwarded());

    TEST_ASSERT_FALSE(scoring.consumeExtraShip());
}

void test_scoring_lives_start_at_three_and_stop_at_zero(void) {
    Scoring scoring;
    scoring.reset();
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(scoring.lives()));

    scoring.loseLife();
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(scoring.lives()));

    scoring.loseLife();
    scoring.loseLife();
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(scoring.lives()));

    scoring.loseLife();
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(scoring.lives()));
}

void test_scoring_advances_the_wave_counter(void) {
    Scoring scoring;
    scoring.reset();
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(scoring.wave()));

    scoring.nextWave();
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(scoring.wave()));

    scoring.nextWave();
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(scoring.wave()));
}

void test_scoring_high_score_keeps_the_best_run(void) {
    Scoring scoring;
    scoring.reset();

    scoring.add(250);
    scoring.commitHighScore();
    TEST_ASSERT_EQUAL_INT(250, static_cast<int>(scoring.highScore()));

    scoring.reset();
    scoring.add(100);
    scoring.commitHighScore();
    TEST_ASSERT_EQUAL_INT(250, static_cast<int>(scoring.highScore()));

    scoring.add(200);
    scoring.commitHighScore();
    TEST_ASSERT_EQUAL_INT(300, static_cast<int>(scoring.highScore()));
}

void test_scoring_reset_starts_a_run_but_keeps_the_high_score(void) {
    Scoring scoring;
    scoring.reset();
    scoring.add(250);
    scoring.commitHighScore();
    scoring.loseLife();
    scoring.nextWave();
    scoring.add(ExtraShipScore);
    TEST_ASSERT_TRUE(scoring.consumeExtraShip());

    scoring.reset();

    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(scoring.score()));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(scoring.lives()));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(scoring.wave()));
    TEST_ASSERT_FALSE(scoring.extraShipAwarded());
    TEST_ASSERT_EQUAL_INT(250, static_cast<int>(scoring.highScore()));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_scoring_accumulates_points);
    RUN_TEST(test_scoring_clamps_at_9999);
    RUN_TEST(test_scoring_high_byte_indexes_the_reload_ladder);
    RUN_TEST(test_scoring_awards_the_extra_ship_at_1500_once);
    RUN_TEST(test_scoring_lives_start_at_three_and_stop_at_zero);
    RUN_TEST(test_scoring_advances_the_wave_counter);
    RUN_TEST(test_scoring_high_score_keeps_the_best_run);
    RUN_TEST(test_scoring_reset_starts_a_run_but_keeps_the_high_score);

    return UNITY_END();
}
