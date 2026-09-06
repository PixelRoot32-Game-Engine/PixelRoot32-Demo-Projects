/**
 * @brief The courier run: how long a leg is worth, and what a streak survives.
 *
 * The one failure that matters is not the arithmetic but an allowance too
 * tight to walk: a destination on the far side of the island and a clock only
 * a car could beat tells a player who has not found one, over and over, that
 * they are too slow at a game they cannot see the rules of.
 *
 * So the allowance is checked against the walk speed rather than eyeballed,
 * over every distance the map can produce -- and that test is the reason the
 * rule lives here rather than inline in the scene.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Mission.h"

namespace ms = top_down_city::mission;

void setUp() {}
void tearDown() {}

namespace {

/// Steps a player walking in a straight line needs for this many tiles,
/// rounded up. The mirror of what the scene's kWalkSpeedSub buys -- and
/// CityConstants.h asserts the two still agree.
int stepsToWalk(int tiles) {
    return tiles * ms::kOnFootStepsPerTile;
}

}  // namespace

// --- The allowance -------------------------------------------------------

void test_every_leg_is_long_enough_to_walk() {
    // The one that matters. 128x128 tiles means a Manhattan leg can be 254,
    // and every one of them has to be beatable on foot -- a player who has
    // not found a car yet is the player most likely to be trying.
    for (int tiles = 0; tiles <= 254; ++tiles) {
        TEST_ASSERT_TRUE(ms::allowanceSteps(tiles) >= stepsToWalk(tiles));
    }
}

void test_a_leg_carries_slack_for_the_detour() {
    // Manhattan distance is not the path: buildings, water and the park all
    // push a courier sideways. An allowance with no margin over the straight
    // line is an allowance nobody makes.
    const int tiles = 40;
    TEST_ASSERT_TRUE(ms::allowanceSteps(tiles) > stepsToWalk(tiles) * 5 / 4);
}

void test_a_longer_leg_is_never_worth_less_time() {
    std::uint16_t previous = 0;
    for (int tiles = 0; tiles <= 254; ++tiles) {
        const std::uint16_t allowance = ms::allowanceSteps(tiles);
        TEST_ASSERT_TRUE(allowance >= previous);
        previous = allowance;
    }
}

void test_a_leg_of_no_distance_still_has_a_clock() {
    // Two targets could land close together, and a zero allowance would fail
    // the run on the step it was handed out.
    TEST_ASSERT_TRUE(ms::allowanceSteps(0) > 0);
}

void test_the_clamp_is_a_guard_and_not_a_rule() {
    // The clamp is a guard against a distance from off the island, not a
    // design decision about how long a job may run: the widest Manhattan leg
    // a 128x128 map can produce is 254 tiles, and at kOnFootStepsPerTile that
    // is 6216 steps against a ceiling of 65535. So both halves are worth
    // pinning -- it has to fire on a distance no map could hand out, and it
    // must never fire on one that could, or the longest journeys would all be
    // worth the same.
    TEST_ASSERT_EQUAL_UINT16(ms::kMaxAllowanceSteps,
                             ms::allowanceSteps(1000000));

    // The widest Manhattan leg on a 128x128 island.
    TEST_ASSERT_TRUE(ms::allowanceSteps(254) < ms::kMaxAllowanceSteps);
    for (int tiles = 0; tiles <= 254; ++tiles) {
        TEST_ASSERT_TRUE(ms::allowanceSteps(tiles) < ms::kMaxAllowanceSteps);
    }
}

void test_a_negative_distance_is_treated_as_none() {
    // The scene subtracts two tile coordinates to get here. A sign slip
    // should hand out a short run, not a wrapped one.
    TEST_ASSERT_EQUAL_UINT16(ms::allowanceSteps(0), ms::allowanceSteps(-40));
}

// --- Running a leg -------------------------------------------------------

void test_a_new_run_starts_on_its_target_with_a_full_clock() {
    const ms::State st = ms::begin(3, 20);
    TEST_ASSERT_EQUAL_UINT8(3, st.target);
    TEST_ASSERT_EQUAL_UINT16(ms::allowanceSteps(20), st.stepsLeft);
    TEST_ASSERT_EQUAL_UINT8(0, st.streak);
    TEST_ASSERT_FALSE(ms::expired(st));
}

void test_the_clock_runs_down_and_expires_exactly_once() {
    ms::State st = ms::begin(0, 10);
    const std::uint16_t allowance = st.stepsLeft;
    for (std::uint16_t i = 0; i < allowance - 1; ++i) {
        ms::tick(st);
        TEST_ASSERT_FALSE(ms::expired(st));
    }
    ms::tick(st);
    TEST_ASSERT_TRUE(ms::expired(st));
}

void test_ticking_an_expired_run_never_underflows() {
    // The scene notices an expiry on the same step and hands out a new leg,
    // but "the scene notices" is not something a uint16_t should depend on.
    ms::State st = ms::begin(0, 1);
    for (int i = 0; i < 100000; ++i) {
        ms::tick(st);
    }
    TEST_ASSERT_EQUAL_UINT16(0, st.stepsLeft);
    TEST_ASSERT_TRUE(ms::expired(st));
}

// --- The streak ----------------------------------------------------------

void test_a_delivery_lengthens_the_streak_and_moves_the_target() {
    ms::State st = ms::begin(1, 10);
    ms::delivered(st, 4, 25);
    TEST_ASSERT_EQUAL_UINT8(1, st.streak);
    TEST_ASSERT_EQUAL_UINT8(4, st.target);
    TEST_ASSERT_EQUAL_UINT16(ms::allowanceSteps(25), st.stepsLeft);
    TEST_ASSERT_FALSE(ms::expired(st));
}

void test_a_failure_resets_the_streak_but_keeps_the_run_going() {
    // There is no game over here and no menu to send anybody to. Missing one
    // costs the streak and hands out the next leg, because a courier loop
    // that stops when you are late is a loop most players see once.
    ms::State st = ms::begin(1, 10);
    ms::delivered(st, 2, 10);
    ms::delivered(st, 3, 10);
    TEST_ASSERT_EQUAL_UINT8(2, st.streak);
    ms::failed(st, 5, 30);
    TEST_ASSERT_EQUAL_UINT8(0, st.streak);
    TEST_ASSERT_EQUAL_UINT8(5, st.target);
    TEST_ASSERT_FALSE(ms::expired(st));
}

void test_the_best_streak_survives_a_failure() {
    ms::State st = ms::begin(0, 10);
    for (int i = 0; i < 7; ++i) {
        ms::delivered(st, 1, 10);
    }
    TEST_ASSERT_EQUAL_UINT8(7, st.streak);
    TEST_ASSERT_EQUAL_UINT8(7, st.best);
    ms::failed(st, 2, 10);
    TEST_ASSERT_EQUAL_UINT8(0, st.streak);
    TEST_ASSERT_EQUAL_UINT8(7, st.best);
}

void test_the_streak_saturates_rather_than_wrapping() {
    // A very good player on a uint8_t. Wrapping to zero after 255 deliveries
    // would read as the run having been failed.
    ms::State st = ms::begin(0, 1);
    for (int i = 0; i < 400; ++i) {
        ms::delivered(st, 1, 1);
    }
    TEST_ASSERT_EQUAL_UINT8(ms::kMaxStreak, st.streak);
    TEST_ASSERT_EQUAL_UINT8(ms::kMaxStreak, st.best);
}

// --- Choosing the next target --------------------------------------------

void test_the_next_target_is_never_the_one_just_reached() {
    // Otherwise a delivery is followed by standing still and delivering
    // again, which is a streak the player did not earn and a mission the
    // player cannot see the point of.
    for (std::uint8_t current = 0; current < 8; ++current) {
        for (std::uint32_t roll = 0; roll < 40; ++roll) {
            TEST_ASSERT_TRUE(ms::nextTarget(current, 8, roll) != current);
        }
    }
}

void test_the_next_target_is_always_in_the_table() {
    for (std::uint8_t count = 2; count <= 16; ++count) {
        for (std::uint32_t roll = 0; roll < 100; ++roll) {
            const std::uint8_t next = ms::nextTarget(3 % count, count, roll);
            TEST_ASSERT_TRUE(next < count);
        }
    }
}

void test_a_table_of_one_has_nowhere_else_to_go() {
    // Degenerate, and it must answer rather than loop looking for a second
    // entry. The generator emits one target per district and could in
    // principle emit one in total.
    TEST_ASSERT_EQUAL_UINT8(0, ms::nextTarget(0, 1, 7));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_leg_is_long_enough_to_walk);
    RUN_TEST(test_a_leg_carries_slack_for_the_detour);
    RUN_TEST(test_a_longer_leg_is_never_worth_less_time);
    RUN_TEST(test_a_leg_of_no_distance_still_has_a_clock);
    RUN_TEST(test_the_clamp_is_a_guard_and_not_a_rule);
    RUN_TEST(test_a_negative_distance_is_treated_as_none);
    RUN_TEST(test_a_new_run_starts_on_its_target_with_a_full_clock);
    RUN_TEST(test_the_clock_runs_down_and_expires_exactly_once);
    RUN_TEST(test_ticking_an_expired_run_never_underflows);
    RUN_TEST(test_a_delivery_lengthens_the_streak_and_moves_the_target);
    RUN_TEST(test_a_failure_resets_the_streak_but_keeps_the_run_going);
    RUN_TEST(test_the_best_streak_survives_a_failure);
    RUN_TEST(test_the_streak_saturates_rather_than_wrapping);
    RUN_TEST(test_the_next_target_is_never_the_one_just_reached);
    RUN_TEST(test_the_next_target_is_always_in_the_table);
    RUN_TEST(test_a_table_of_one_has_nowhere_else_to_go);
    return UNITY_END();
}
