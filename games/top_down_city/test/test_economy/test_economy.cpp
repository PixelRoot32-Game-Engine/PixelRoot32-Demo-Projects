/**
 * @brief What a delivery is worth, and what it buys.
 *
 * The balance is the half with no visual failure at all: a fee too small is a
 * counter nobody ever reaches, a fee too large is a counter everybody reaches
 * on their first drop, and neither draws anything wrong.
 *
 * The two that matter are test_the_counter_costs_more_than_one_delivery and
 * test_the_counter_is_reachable_inside_one_clean_streak: they pin the price
 * against the fee rather than against a number typed twice, so a change to
 * either fails here instead of quietly turning the shop into a formality or a
 * wall.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/Economy.h"
#include "game/rules/Mission.h"

namespace ec = top_down_city::economy;
namespace ms = top_down_city::mission;

void setUp() {}
void tearDown() {}

// --- Earning -------------------------------------------------------------

void test_a_run_starts_broke() {
    ec::Purse purse = ec::clear();
    TEST_ASSERT_EQUAL_UINT16(0, purse.cash);
}

void test_the_first_drop_pays_the_base_fee_plus_one_streak() {
    // `streak` is the count AFTER the delivery, so the first drop is 1 rather
    // than 0. Reading it before would pay the base fee for every drop of a
    // perfect run and make the streak decorative.
    TEST_ASSERT_EQUAL_UINT16(ec::kDeliveryFee + ec::kStreakBonus,
                             ec::deliveryFee(1));
}

void test_a_longer_streak_pays_more() {
    std::uint16_t previous = ec::deliveryFee(1);
    for (std::uint8_t streak = 2; streak <= ec::kStreakBonusCap; ++streak) {
        const std::uint16_t fee = ec::deliveryFee(streak);
        TEST_ASSERT_TRUE(fee > previous);
        previous = fee;
    }
}

void test_the_streak_bonus_stops_climbing() {
    // Otherwise a player who never misses is eventually paid more per drop
    // than the shop sells anything for, and the money stops being a decision.
    const std::uint16_t atCap = ec::deliveryFee(ec::kStreakBonusCap);
    TEST_ASSERT_EQUAL_UINT16(atCap, ec::deliveryFee(ec::kStreakBonusCap + 1));
    TEST_ASSERT_EQUAL_UINT16(atCap, ec::deliveryFee(ms::kMaxStreak));
}

void test_a_streak_of_zero_still_pays() {
    // Not reachable through `delivered()`, which increments first. Answered
    // rather than asserted: a fee function that divides a run into "pays" and
    // "silently pays nothing" is one bad call site away from a courier who
    // works for free.
    TEST_ASSERT_EQUAL_UINT16(ec::kDeliveryFee, ec::deliveryFee(0));
}

void test_earnings_saturate_rather_than_wrap() {
    // The purse is a uint16_t and the HUD draws four digits. Wrapping would
    // take a player who has done nothing but deliver for ten minutes back to
    // nothing, which reads as the game taking their money.
    ec::Purse purse = ec::clear();
    for (int i = 0; i < 2000; ++i) {
        ec::earn(purse, ec::deliveryFee(ec::kStreakBonusCap));
    }
    TEST_ASSERT_EQUAL_UINT16(ec::kMaxCash, purse.cash);
}

// --- Spending ------------------------------------------------------------

void test_you_cannot_buy_what_you_cannot_afford() {
    ec::Purse purse = ec::clear();
    ec::earn(purse, ec::kShotgunPrice - 1);
    TEST_ASSERT_FALSE(ec::canAfford(purse, ec::kShotgunPrice));
    TEST_ASSERT_FALSE(ec::spend(purse, ec::kShotgunPrice));
    // And the refusal costs nothing. A spend that debits on the way to
    // returning false is a shop that charges for saying no.
    TEST_ASSERT_EQUAL_UINT16(ec::kShotgunPrice - 1, purse.cash);
}

void test_exact_change_is_enough() {
    ec::Purse purse = ec::clear();
    ec::earn(purse, ec::kShotgunPrice);
    TEST_ASSERT_TRUE(ec::canAfford(purse, ec::kShotgunPrice));
    TEST_ASSERT_TRUE(ec::spend(purse, ec::kShotgunPrice));
    TEST_ASSERT_EQUAL_UINT16(0, purse.cash);
}

void test_spending_takes_exactly_the_price() {
    ec::Purse purse = ec::clear();
    ec::earn(purse, ec::kMaxCash);
    TEST_ASSERT_TRUE(ec::spend(purse, ec::kShotgunPrice));
    TEST_ASSERT_EQUAL_UINT16(ec::kMaxCash - ec::kShotgunPrice, purse.cash);
}

void test_a_free_purchase_is_still_a_purchase() {
    // Nothing prices anything at zero today. It is checked because the
    // alternative -- `spend` returning false on a price of zero -- would be a
    // future free item that silently never arrives.
    ec::Purse purse = ec::clear();
    TEST_ASSERT_TRUE(ec::spend(purse, 0));
}

// --- Being worth the walk ------------------------------------------------

void test_the_counter_costs_more_than_one_delivery() {
    // A shop reachable on the first drop is not a reason to run the courier
    // loop, it is a cutscene with a button.
    TEST_ASSERT_TRUE(ec::kShotgunPrice > ec::deliveryFee(ec::kStreakBonusCap));
}

// --- The rest of the catalogue -------------------------------------------

void test_the_pistol_is_the_cheapest_thing_on_the_counter() {
    // It is the line a run comes back to after losing a fight, so it is the
    // one the price has to let a broke player reach. Everything else is an
    // upgrade and is allowed to be a walk.
    TEST_ASSERT_TRUE(ec::kPistolPrice < ec::kVestPrice);
    TEST_ASSERT_TRUE(ec::kVestPrice < ec::kShotgunPrice);
}

void test_nothing_is_affordable_on_the_first_drop() {
    // A counter the opening delivery already pays for is a shop the player
    // never has to save for, and saving is the whole reason the purse exists.
    const std::uint16_t first = ec::deliveryFee(1);
    TEST_ASSERT_TRUE(ec::kPistolPrice > first);
    TEST_ASSERT_TRUE(ec::kVestPrice > first);
    TEST_ASSERT_TRUE(ec::kShotgunPrice > first);
}

void test_the_upgrades_cost_more_than_the_best_single_delivery() {
    // The pistol deliberately does NOT: see the case below. These two are
    // what the streak is for, and a streak that buys nothing it could not buy
    // on one drop is a counter that never moves.
    const std::uint16_t best = ec::deliveryFee(ec::kStreakBonusCap);
    TEST_ASSERT_TRUE(ec::kVestPrice > best);
    TEST_ASSERT_TRUE(ec::kShotgunPrice > best);
}

void test_a_disarmed_courier_can_rearm_inside_two_clean_drops() {
    // The one case that decides whether the whole economy is a progression or
    // a trap. The city holds ONE pistol and it does not come back, so after
    // it has been taken and emptied the counter is the only weapon left in
    // the demo -- and a player who cannot reach it inside a couple of drops
    // is a player being asked to run the courier loop unarmed indefinitely.
    ec::Purse purse = ec::clear();
    ec::earn(purse, ec::deliveryFee(1));
    ec::earn(purse, ec::deliveryFee(2));
    TEST_ASSERT_TRUE(ec::canAfford(purse, ec::kPistolPrice));
}

void test_rearming_still_costs_a_second_drop() {
    // The other end of the same number. A pistol the first delivery pays for
    // makes running dry free, and a magazine that costs nothing is a magazine
    // that may as well be infinite -- which is what this stage removed.
    ec::Purse purse = ec::clear();
    ec::earn(purse, ec::deliveryFee(1));
    TEST_ASSERT_FALSE(ec::canAfford(purse, ec::kPistolPrice));
}

void test_the_counter_is_reachable_inside_one_clean_streak() {
    // The other end of the same claim, and the one that stops "expensive"
    // becoming unpayable: a player who does not miss must be able to buy a box
    // before the streak bonus has even capped, or the shop is a wall.
    ec::Purse purse = ec::clear();
    std::uint8_t drops = 0;
    for (std::uint8_t streak = 1; streak <= ec::kStreakBonusCap; ++streak) {
        ec::earn(purse, ec::deliveryFee(streak));
        ++drops;
        if (ec::canAfford(purse, ec::kShotgunPrice)) {
            break;
        }
    }
    TEST_ASSERT_TRUE(ec::canAfford(purse, ec::kShotgunPrice));
    // And not on the second one either: the walk to the shop has to be worth
    // making rather than something done between two drops.
    TEST_ASSERT_TRUE(drops >= 3);
}

// --- What the main mission pays ------------------------------------------

void test_a_boost_arms_the_player_for_the_next_chapter() {
    // The chapter after the boost hands the player something to shoot at, and
    // the city's one loose pistol may already be gone. A main mission that
    // does not cover the counter's cheapest line sends the player back to the
    // courier loop before the story can continue -- which is the story
    // stopping to ask for a grind.
    TEST_ASSERT_TRUE(ec::kBoostFee >= ec::kPistolPrice);
}

void test_a_boost_does_not_hand_over_the_top_of_the_ladder() {
    // The other end. One mission that buys the shotgun outright deletes the
    // courier run, the streak and the shop in a single payout -- the demo's
    // whole economy, spent on its first cutscene.
    TEST_ASSERT_TRUE(ec::kBoostFee < ec::kShotgunPrice);
}

void test_a_main_mission_pays_more_than_a_cold_delivery() {
    // Pinned against the fee rather than against a number typed twice. A
    // boost worth less than a drop is a phone nobody has a reason to answer.
    TEST_ASSERT_TRUE(ec::kBoostFee > ec::deliveryFee(0));
}

void test_the_hit_pays_more_than_the_boost() {
    // The chapters are a ladder and so are their payouts. The Hit asks for a
    // weapon, a station full of armed officers and a walk home under five
    // stars; the boost asks for a car. A second chapter worth no more than
    // the first is a story that stops being worth continuing at exactly the
    // point it gets harder.
    TEST_ASSERT_TRUE(ec::kHitFee > ec::kBoostFee);
}

void test_the_hit_does_not_hand_over_the_top_of_the_ladder() {
    // The same end the boost is pinned at, and for the same reason.
    TEST_ASSERT_TRUE(ec::kHitFee < ec::kShotgunPrice);
}

void test_the_hit_repays_the_gun_it_took_to_do_it() {
    // The bound that makes the chapter a progression rather than a toll. The
    // player cannot start the Hit unarmed, and the city's one loose pistol
    // may already be gone -- so the counter sold them the weapon. A payout
    // that only covers what the job cost leaves the player exactly where they
    // started, one chapter later, which reads as the story charging admission.
    TEST_ASSERT_TRUE(ec::kHitFee - ec::kPistolPrice > ec::deliveryFee(0));
}

void test_the_frenzy_pays_more_than_the_hit() {
    // The ladder, one rung further up. Chapter 3 asks for a shotgun, a corner
    // full of witnesses and a clock -- a last chapter worth no more than the
    // one before it is a story that stops paying at the point it stops being
    // survivable.
    TEST_ASSERT_TRUE(ec::kFrenzyFee > ec::kHitFee);
}

void test_the_frenzy_repays_the_gun_it_demanded() {
    // The one fee allowed above the top of the counter: the inverse of the
    // bound the other two are held to, because the player has already bought
    // the shotgun -- the rampage asks for more bodies than a pistol magazine
    // holds. A last chapter that paid back less than it charged is the
    // ladder's top rung costing money.
    TEST_ASSERT_TRUE(ec::kFrenzyFee > ec::kShotgunPrice);
}

void test_the_chapters_pay_in_the_order_they_are_played() {
    // Pinned as one chain rather than three pairwise bounds, so a fourth
    // chapter priced between two existing ones cannot slip in unnoticed.
    TEST_ASSERT_TRUE(ec::deliveryFee(0) < ec::kBoostFee);
    TEST_ASSERT_TRUE(ec::kBoostFee < ec::kHitFee);
    TEST_ASSERT_TRUE(ec::kHitFee < ec::kFrenzyFee);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_run_starts_broke);
    RUN_TEST(test_the_first_drop_pays_the_base_fee_plus_one_streak);
    RUN_TEST(test_a_longer_streak_pays_more);
    RUN_TEST(test_the_streak_bonus_stops_climbing);
    RUN_TEST(test_a_streak_of_zero_still_pays);
    RUN_TEST(test_earnings_saturate_rather_than_wrap);
    RUN_TEST(test_you_cannot_buy_what_you_cannot_afford);
    RUN_TEST(test_exact_change_is_enough);
    RUN_TEST(test_spending_takes_exactly_the_price);
    RUN_TEST(test_a_free_purchase_is_still_a_purchase);
    RUN_TEST(test_the_counter_costs_more_than_one_delivery);
    RUN_TEST(test_the_pistol_is_the_cheapest_thing_on_the_counter);
    RUN_TEST(test_nothing_is_affordable_on_the_first_drop);
    RUN_TEST(test_the_upgrades_cost_more_than_the_best_single_delivery);
    RUN_TEST(test_a_disarmed_courier_can_rearm_inside_two_clean_drops);
    RUN_TEST(test_rearming_still_costs_a_second_drop);
    RUN_TEST(test_the_counter_is_reachable_inside_one_clean_streak);
    RUN_TEST(test_a_boost_arms_the_player_for_the_next_chapter);
    RUN_TEST(test_a_boost_does_not_hand_over_the_top_of_the_ladder);
    RUN_TEST(test_a_main_mission_pays_more_than_a_cold_delivery);
    RUN_TEST(test_the_hit_pays_more_than_the_boost);
    RUN_TEST(test_the_hit_does_not_hand_over_the_top_of_the_ladder);
    RUN_TEST(test_the_hit_repays_the_gun_it_took_to_do_it);
    RUN_TEST(test_the_frenzy_pays_more_than_the_hit);
    RUN_TEST(test_the_frenzy_repays_the_gun_it_demanded);
    RUN_TEST(test_the_chapters_pay_in_the_order_they_are_played);
    return UNITY_END();
}
