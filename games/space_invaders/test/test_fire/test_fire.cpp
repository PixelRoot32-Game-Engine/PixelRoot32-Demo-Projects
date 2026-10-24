/*
 * Enemy fire suite (S9).
 *
 * The ROM picks the reload budget from the score high byte at the thresholds
 * 0x02, 0x10, 0x20 and 0x30. The first five tests pin one band on both sides of
 * its boundary, so an off-by-one comparison fails instead of sliding through.
 * The rest drive EnemyFireTimer: the budget is a count of enemy-shot steps, not
 * ticks, so a 60 Hz run has to divide by the three-tick step to compare it with
 * the ladder.
 */
#include <unity.h>

#include "rules/FireTiming.h"
#include "rules/Shots.h"

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

void test_enemy_fire_timer_charges_a_full_budget_before_the_first_shot(void) {
    struct Band {
        uint16_t highByte;
        int budget;
    };
    const Band bands[] = {
        {0x00, 48}, {0x01, 48}, {0x02, 16}, {0x10, 11}, {0x20, 8}, {0x30, 7},
    };

    // A reset timer owes a whole budget: the very first shot is one gap away,
    // not immediate, which is what makes "48 shot steps" the start-of-run pace.
    for (int i = 0; i < static_cast<int>(sizeof(bands) / sizeof(bands[0])); ++i) {
        EnemyFireTimer timer;
        timer.reset(bands[i].highByte);
        TEST_ASSERT_EQUAL_INT(bands[i].budget, static_cast<int>(timer.remaining()));

        int firedAt = 0;
        for (int step = 1; step <= 64 && firedAt == 0; ++step) {
            if (timer.step()) {
                firedAt = step;
            }
        }

        TEST_ASSERT_EQUAL_INT(bands[i].budget, firedAt);
        // The step that fires also reloads, so the next gap is a full budget.
        TEST_ASSERT_EQUAL_INT(bands[i].budget, static_cast<int>(timer.remaining()));
    }
}

void test_enemy_fire_gap_follows_the_ladder(void) {
    // S9: the gap between two shots is the band's reload, so the same band must
    // produce the same wait twice in a row on every rung of the ladder.
    const uint16_t bands[] = {0x00, 0x02, 0x10, 0x20, 0x30};
    const int expected[] = {48, 16, 11, 8, 7};

    for (int i = 0; i < 5; ++i) {
        EnemyFireTimer timer;
        timer.reset(bands[i]);

        int fires = 0;
        int lastFire = 0;
        int gap = 0;
        for (int step = 1; step <= 200 && fires < 2; ++step) {
            if (!timer.step()) {
                continue;
            }
            if (fires == 1) {
                gap = step - lastFire;
            }
            lastFire = step;
            ++fires;
        }

        TEST_ASSERT_EQUAL_INT(2, fires);
        TEST_ASSERT_EQUAL_INT(expected[i], gap);
    }
}

void test_sixty_second_drive_fires_every_forty_eight_shot_steps(void) {
    // Acceptance for T4.1: 60 s of play at the starting score. The scene runs
    // 60 ticks per second and steps the shots every third tick, so the drive is
    // 3600 ticks = 1200 shot steps and 1200 / 48 lands on exactly 25 shots.
    const int driveTicks = 60 * 60;
    const int driveSteps = driveTicks / EnemyShotTicksPerStep;

    EnemyFireTimer timer;
    timer.reset(0);

    int fires = 0;
    int firstFire = 0;
    int previousFire = 0;
    for (int step = 1; step <= driveSteps; ++step) {
        if (!timer.step()) {
            continue;
        }
        if (fires == 0) {
            firstFire = step;
        } else {
            TEST_ASSERT_EQUAL_INT(48, step - previousFire);
        }
        previousFire = step;
        ++fires;
    }

    TEST_ASSERT_EQUAL_INT(48, firstFire);
    TEST_ASSERT_EQUAL_INT(25, fires);
}

void test_enemy_shot_whole_step_moves_four_or_five_pixels(void) {
    // The scene hands the motion one whole shot step per call instead of three
    // per-tick advances, so a step must land its full distance every time.
    EnemyShotMotion slow;
    slow.reset(100);
    TEST_ASSERT_EQUAL_INT(104, static_cast<int>(slow.step(false)));
    TEST_ASSERT_EQUAL_INT(108, static_cast<int>(slow.step(false)));

    EnemyShotMotion fast;
    fast.reset(100);
    TEST_ASSERT_EQUAL_INT(105, static_cast<int>(fast.step(true)));
    TEST_ASSERT_EQUAL_INT(110, static_cast<int>(fast.step(true)));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_reload_ladder_first_band);
    RUN_TEST(test_reload_ladder_second_band);
    RUN_TEST(test_reload_ladder_third_band);
    RUN_TEST(test_reload_ladder_fourth_band);
    RUN_TEST(test_reload_ladder_top_band);
    RUN_TEST(test_enemy_fire_timer_charges_a_full_budget_before_the_first_shot);
    RUN_TEST(test_enemy_fire_gap_follows_the_ladder);
    RUN_TEST(test_sixty_second_drive_fires_every_forty_eight_shot_steps);
    RUN_TEST(test_enemy_shot_whole_step_moves_four_or_five_pixels);

    return UNITY_END();
}
