/**
 * @brief What a doorway is worth to somebody the police are looking for.
 *
 * The city sheds a star only by not being seen for six seconds, and an
 * interior is a room nobody on the force is standing in -- so without a price
 * every door is a button marked CLEAR MY WANTED LEVEL, and a level you can
 * wait out is a queue rather than a manhunt.
 *
 * The price is the burn, and all three of its failures are silent: one that
 * never fires makes every interior free; one that never expires punishes a
 * mistake the player can no longer undo; one SHORTER than a star's cooldown
 * costs nothing, which is the test at the bottom and the reason the constant
 * is checked against `wanted::kCoolSteps` rather than eyeballed.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Hideout.h"
#include "game/rules/Wanted.h"

namespace hd = top_down_city::hideout;
namespace wn = top_down_city::wanted;

void setUp() {}
void tearDown() {}

/// Named, because `onEntry(3, true)` reads as "three, true" at the call site
/// and the boolean is the whole subject of this file.
constexpr bool kUnwatched = false;
constexpr bool kWatched = true;

// --- Lighting it ---------------------------------------------------------

void test_a_fresh_burn_is_not_watching() {
    hd::Burn burn = hd::clear();
    TEST_ASSERT_FALSE(hd::watching(burn));
    TEST_ASSERT_EQUAL_UINT16(0, burn.stepsLeft);
}

void test_slipping_in_unseen_hides_you() {
    hd::Burn burn = hd::onEntry(3, kUnwatched);
    TEST_ASSERT_FALSE(hd::watching(burn));
}

void test_being_watched_through_the_door_burns_it() {
    hd::Burn burn = hd::onEntry(3, kWatched);
    TEST_ASSERT_TRUE(hd::watching(burn));
}

void test_nobody_is_watching_a_door_used_by_nobody_wanted() {
    // At zero stars there is no manhunt to hide from, so an officer who
    // happens to be looking at the doorway has seen a person go shopping.
    // Without this the first door a new player walks through would arm a
    // penalty against a crime they have not committed.
    hd::Burn burn = hd::onEntry(0, kWatched);
    TEST_ASSERT_FALSE(hd::watching(burn));
}

void test_every_star_count_burns_the_same() {
    // The burn is how long the police keep an eye on a door, and that does
    // not get shorter because the player is worse. A table here would be a
    // rung that makes a higher level SAFER.
    for (std::uint8_t stars = 1; stars <= wn::kMaxStars; ++stars) {
        hd::Burn burn = hd::onEntry(stars, kWatched);
        TEST_ASSERT_EQUAL_UINT16(hd::kBurnSteps, burn.stepsLeft);
    }
}

// --- Waiting it out ------------------------------------------------------

void test_the_burn_runs_down_and_lets_go() {
    hd::Burn burn = hd::onEntry(5, kWatched);
    for (int i = 0; i < hd::kBurnSteps; ++i) {
        TEST_ASSERT_TRUE(hd::watching(burn));
        hd::tick(burn);
    }
    TEST_ASSERT_FALSE(hd::watching(burn));
}

void test_ticking_past_zero_saturates() {
    // The scene ticks this every logic step whether or not anything is lit,
    // so wrapping at zero would re-arm the burn once every 1048 seconds --
    // a bug nobody would ever reproduce deliberately.
    hd::Burn burn = hd::clear();
    for (int i = 0; i < 4; ++i) {
        hd::tick(burn);
    }
    TEST_ASSERT_EQUAL_UINT16(0, burn.stepsLeft);
    TEST_ASSERT_FALSE(hd::watching(burn));
}

void test_a_second_entry_replaces_the_first() {
    // Stepping out and back in is the way to un-burn a door, so entry has to
    // OVERWRITE rather than extend. Accumulating would make the fix into the
    // punishment.
    hd::Burn burn = hd::onEntry(4, kWatched);
    hd::tick(burn);
    burn = hd::onEntry(4, kUnwatched);
    TEST_ASSERT_FALSE(hd::watching(burn));
}

// --- Being worth having --------------------------------------------------

void test_a_watched_door_costs_more_than_a_star() {
    // `wanted::tick` sheds one star per kCoolSteps of not being seen; a
    // shorter burn would let a player seen going in lose their stars on
    // exactly the schedule of a player who was not.
    TEST_ASSERT_TRUE(hd::kBurnSteps > wn::kCoolSteps);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_fresh_burn_is_not_watching);
    RUN_TEST(test_slipping_in_unseen_hides_you);
    RUN_TEST(test_being_watched_through_the_door_burns_it);
    RUN_TEST(test_nobody_is_watching_a_door_used_by_nobody_wanted);
    RUN_TEST(test_every_star_count_burns_the_same);
    RUN_TEST(test_the_burn_runs_down_and_lets_go);
    RUN_TEST(test_ticking_past_zero_saturates);
    RUN_TEST(test_a_second_entry_replaces_the_first);
    RUN_TEST(test_a_watched_door_costs_more_than_a_star);
    return UNITY_END();
}
