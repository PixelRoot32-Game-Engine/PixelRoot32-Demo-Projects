/**
 * @brief What a vest is worth, and what it cannot be turned into.
 *
 * One subtraction, and every way it can be wrong is silent: the player simply
 * survives more or fewer rounds than the shop implied, and the only way to
 * notice is to count them. The case that matters most is
 * `test_a_second_vest_is_not_a_thicker_one` -- nothing on screen distinguishes
 * a vest that sets from one that adds.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Armor.h"

namespace ar = top_down_city::armor;

void setUp() {}
void tearDown() {}

// --- Wearing one ---------------------------------------------------------

void test_a_run_starts_without_a_vest() {
    ar::Vest vest = ar::none();
    TEST_ASSERT_EQUAL_UINT8(0, vest.points);
    TEST_ASSERT_FALSE(ar::worn(vest));
}

void test_a_bought_vest_is_a_full_one() {
    ar::Vest vest = ar::none();
    ar::wear(vest);
    TEST_ASSERT_EQUAL_UINT8(ar::kVestPoints, vest.points);
    TEST_ASSERT_TRUE(ar::worn(vest));
}

void test_a_second_vest_is_not_a_thicker_one() {
    // `wear` SETS rather than adds. Adding would make the vest the one thing
    // in the demo money buys without limit: a player who never misses could
    // stand in front of the whole force and read the same screen as one who
    // bought a single vest.
    ar::Vest vest = ar::none();
    ar::wear(vest);
    ar::wear(vest);
    TEST_ASSERT_EQUAL_UINT8(ar::kVestPoints, vest.points);
}

void test_taking_a_vest_off_leaves_nothing() {
    // What an arrest does. The weapon is already confiscated on the way in,
    // and a vest that survived being taken to the station would be the one
    // purchase a fight cannot cost you.
    ar::Vest vest = ar::none();
    ar::wear(vest);
    ar::strip(vest);
    TEST_ASSERT_FALSE(ar::worn(vest));
}

// --- Taking a hit --------------------------------------------------------

void test_the_vest_is_spent_before_the_player_is() {
    ar::Vest vest = ar::none();
    ar::wear(vest);
    const std::uint8_t through = ar::absorb(vest, 20);
    TEST_ASSERT_EQUAL_UINT8(0, through);
    TEST_ASSERT_EQUAL_UINT8(ar::kVestPoints - 20, vest.points);
}

void test_a_hit_bigger_than_the_vest_spills_the_rest() {
    // The half that fails silently in the other direction: a vest that
    // swallowed the whole of an oversized hit would make the last point of
    // armour worth as much as a full one.
    ar::Vest vest = ar::none();
    ar::wear(vest);
    const std::uint8_t through =
        ar::absorb(vest, static_cast<std::uint8_t>(ar::kVestPoints + 30));
    TEST_ASSERT_EQUAL_UINT8(30, through);
    TEST_ASSERT_FALSE(ar::worn(vest));
}

void test_a_hit_exactly_the_size_of_the_vest_reaches_nobody() {
    ar::Vest vest = ar::none();
    ar::wear(vest);
    TEST_ASSERT_EQUAL_UINT8(0, ar::absorb(vest, ar::kVestPoints));
    TEST_ASSERT_FALSE(ar::worn(vest));
}

void test_a_spent_vest_absorbs_nothing() {
    ar::Vest vest = ar::none();
    TEST_ASSERT_EQUAL_UINT8(40, ar::absorb(vest, 40));
    TEST_ASSERT_EQUAL_UINT8(0, vest.points);
}

void test_absorbing_nothing_costs_nothing() {
    // Reachable: `incomingDamage` halves a bumper, and a weak enough hit
    // rounds to zero. A vest that spent a point on it would be worn away by
    // traffic that never hurt anybody.
    ar::Vest vest = ar::none();
    ar::wear(vest);
    TEST_ASSERT_EQUAL_UINT8(0, ar::absorb(vest, 0));
    TEST_ASSERT_EQUAL_UINT8(ar::kVestPoints, vest.points);
}

void test_a_vest_never_hands_on_more_than_it_was_given() {
    // The arithmetic is unsigned and one subtraction away from underflowing
    // into 255, which is a hit that kills through a vest that stopped it.
    for (int worn = 0; worn <= ar::kVestPoints; ++worn) {
        ar::Vest vest;
        vest.points = static_cast<std::uint8_t>(worn);
        for (int damage = 0; damage <= 255; ++damage) {
            ar::Vest copy = vest;
            const std::uint8_t through =
                ar::absorb(copy, static_cast<std::uint8_t>(damage));
            TEST_ASSERT_TRUE(through <= damage);
            TEST_ASSERT_EQUAL_INT(damage - through,
                                  vest.points - copy.points);
        }
    }
}

// --- Being worth buying --------------------------------------------------

void test_a_vest_is_worth_more_than_one_police_round_and_less_than_a_life() {
    // Both ends are the point of the item. Under a single round it is a
    // decoration; at or over a full life it is a second life, and the demo
    // already has one of those on the station steps.
    TEST_ASSERT_TRUE(ar::kVestPoints > 20);   // one police round
    TEST_ASSERT_TRUE(ar::kVestPoints < 100);  // kPlayerHealth
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_run_starts_without_a_vest);
    RUN_TEST(test_a_bought_vest_is_a_full_one);
    RUN_TEST(test_a_second_vest_is_not_a_thicker_one);
    RUN_TEST(test_taking_a_vest_off_leaves_nothing);
    RUN_TEST(test_the_vest_is_spent_before_the_player_is);
    RUN_TEST(test_a_hit_bigger_than_the_vest_spills_the_rest);
    RUN_TEST(test_a_hit_exactly_the_size_of_the_vest_reaches_nobody);
    RUN_TEST(test_a_spent_vest_absorbs_nothing);
    RUN_TEST(test_absorbing_nothing_costs_nothing);
    RUN_TEST(test_a_vest_never_hands_on_more_than_it_was_given);
    RUN_TEST(test_a_vest_is_worth_more_than_one_police_round_and_less_than_a_life);
    return UNITY_END();
}
