/**
 * @brief The counter's catalogue: what is on it, in what order, and for how
 *        much.
 *
 * Three lines, shown in a picker the till opens over the room: RUN opens it,
 * UP and DOWN walk it, FIRE buys, RUN closes. The buttons were found rather
 * than added -- while the picker is up it owns the pad, and the one room in
 * the city with nobody in it is the one room where that costs nothing.
 *
 * So the list is a rule with an order, a wrap in BOTH directions and a price
 * each, and every one of those fails silently: a `next` that skips a line is a
 * product nobody can reach, a walk that does not wrap is a list the player
 * runs off the end of, and a price typed twice is a counter that advertises
 * one number and takes another.
 */
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "game/rules/Armor.h"
#include "game/rules/Economy.h"
#include "game/rules/Shop.h"
#include "game/rules/Weapon.h"

namespace sh = top_down_city::shop;
namespace ec = top_down_city::economy;
namespace ar = top_down_city::armor;
namespace w  = top_down_city::weapons;

void setUp() {}
void tearDown() {}

namespace {

constexpr std::uint8_t kLines = static_cast<std::uint8_t>(sh::Line::Count);

}  // namespace

// --- The list ------------------------------------------------------------

void test_every_line_says_what_it_is() {
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        TEST_ASSERT_NOT_NULL(o.label);
        TEST_ASSERT_TRUE(std::strlen(o.label) > 0);
    }
}

void test_a_label_fits_its_row() {
    // The picker draws the label at the left of a row and the price hard
    // against the right of it. Text is drawn from a pointer, not measured and
    // wrapped, so a label too long for its row is not shrunk -- it runs
    // straight through the price. CityConstants.h asserts the other end of
    // this: that a row of kMaxLabelChars plus a price still fits the panel.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        TEST_ASSERT_TRUE(std::strlen(o.label) <= sh::kMaxLabelChars);
    }
}

void test_the_line_count_matches_the_enum() {
    // The picker sizes its panel from kLineCount, which is a constant the
    // layout arithmetic can use. A fourth line added to the enum and not to
    // the panel is a product drawn off the bottom edge of its own box.
    TEST_ASSERT_EQUAL_UINT8(kLines, sh::kLineCount);
}

void test_walking_the_list_reaches_every_line_once_and_comes_back() {
    // A line the walk misses is a product that exists in the table and
    // nowhere the player can reach.
    bool seen[kLines] = {false};
    sh::Line line = sh::first();
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const std::uint8_t index = static_cast<std::uint8_t>(line);
        TEST_ASSERT_TRUE(index < kLines);
        TEST_ASSERT_FALSE(seen[index]);
        seen[index] = true;
        line = sh::next(line);
    }
    // And the step after the last one is the first again, not a fourth line
    // nobody wrote.
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(line));
    for (std::uint8_t i = 0; i < kLines; ++i) {
        TEST_ASSERT_TRUE(seen[i]);
    }
}

void test_walking_the_list_backwards_reaches_every_line_too() {
    // DOWN and UP, and the second one is not free: `prev` on an unsigned index
    // is the subtraction that wraps to 255 if the zero case is missed, and 255
    // indexes nothing. Same walk, other way round.
    bool seen[kLines] = {false};
    sh::Line line = sh::first();
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const std::uint8_t index = static_cast<std::uint8_t>(line);
        TEST_ASSERT_TRUE(index < kLines);
        TEST_ASSERT_FALSE(seen[index]);
        seen[index] = true;
        line = sh::prev(line);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(line));
    for (std::uint8_t i = 0; i < kLines; ++i) {
        TEST_ASSERT_TRUE(seen[i]);
    }
}

void test_up_then_down_is_where_you_started() {
    // The property that makes the picker feel like a list rather than a
    // sequence of events. It is also what a hand-written wrap gets wrong: an
    // off-by-one in either direction passes the two walks above and still
    // moves the selection when the player nudges it and nudges it back.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Line line = static_cast<sh::Line>(i);
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<std::uint8_t>(
                                       sh::prev(sh::next(line))));
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<std::uint8_t>(
                                       sh::next(sh::prev(line))));
    }
}

void test_the_ends_of_the_list_wrap_into_each_other() {
    const sh::Line last = static_cast<sh::Line>(kLines - 1);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(last),
                            static_cast<std::uint8_t>(sh::prev(sh::first())));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(sh::next(last)));
}

void test_an_impossible_line_is_the_first_one_rather_than_a_wild_read() {
    // Same policy as `weapons::spec`: this is reached from the draw path on a
    // board with no console to print an assertion to, so a wrong product is a
    // better failure than a wrong pointer.
    const sh::Line bogus = static_cast<sh::Line>(kLines + 7);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(sh::next(bogus)));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(sh::prev(bogus)));
    TEST_ASSERT_EQUAL_PTR(sh::offer(sh::first()).label,
                          sh::offer(bogus).label);
}

// --- What each line does -------------------------------------------------

void test_a_line_either_arms_you_or_armours_you() {
    // A line that does neither is a button that takes the money and changes
    // nothing, which is the one outcome a shop must never have.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        TEST_ASSERT_TRUE(o.arms || o.vest);
        // And never both: the banner names one thing, and a purchase that
        // did two would be read as the shop having glitched.
        TEST_ASSERT_FALSE(o.arms && o.vest);
    }
}

void test_the_counter_sells_both_of_the_player_weapons() {
    // The city has one pistol lying in it and nothing else. If a weapon is
    // missing from this list it is a weapon a run can permanently lose.
    bool sold[static_cast<std::uint8_t>(w::WeaponId::Count)] = {false};
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        if (o.arms) {
            sold[static_cast<std::uint8_t>(o.weapon)] = true;
        }
    }
    TEST_ASSERT_TRUE(sold[static_cast<std::uint8_t>(w::WeaponId::Pistol)]);
    TEST_ASSERT_TRUE(sold[static_cast<std::uint8_t>(w::WeaponId::Shotgun)]);
}

void test_nobody_sells_the_police_sidearm() {
    // It is in the table because the force carries it, not because it is a
    // product: it fires slower and hits softer than the pistol, so a player
    // who bought one would have paid to be worse armed.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        if (o.arms) {
            TEST_ASSERT_TRUE(o.weapon != w::WeaponId::PolicePistol);
        }
    }
}

void test_the_vest_line_is_the_vest_the_armour_module_describes() {
    bool found = false;
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        if (!o.vest) {
            continue;
        }
        found = true;
        // Pinned rather than repeated. Two constants for one vest is a shop
        // that sells fifty points and hands over thirty.
        TEST_ASSERT_EQUAL_UINT8(ar::kVestPoints, o.armor);
    }
    TEST_ASSERT_TRUE(found);
}

void test_a_weapon_line_carries_no_armour() {
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const sh::Offer& o = sh::offer(static_cast<sh::Line>(i));
        if (o.arms) {
            TEST_ASSERT_EQUAL_UINT8(0, o.armor);
        }
    }
}

// --- What each line costs ------------------------------------------------

void test_the_prices_are_the_economys_and_not_a_second_copy() {
    // The balance lives in Economy.h, which is the file with the tests that
    // pin it against the delivery fee. A price typed here as well would drift
    // out of that check on the first tuning pass.
    TEST_ASSERT_EQUAL_UINT16(ec::kPistolPrice,
                             sh::offer(sh::Line::Pistol).price);
    TEST_ASSERT_EQUAL_UINT16(ec::kVestPrice,
                             sh::offer(sh::Line::Vest).price);
    TEST_ASSERT_EQUAL_UINT16(ec::kShotgunPrice,
                             sh::offer(sh::Line::Shotgun).price);
}

void test_the_list_is_cheapest_first() {
    // The order is the design. A player who walks in with one delivery's
    // wages sees something they can afford on the frame they arrive, and the
    // list gets more expensive as they press -- so the button teaches the
    // progression rather than hiding it behind a wrap.
    std::uint16_t previous = 0;
    sh::Line line = sh::first();
    for (std::uint8_t i = 0; i < kLines; ++i) {
        const std::uint16_t price = sh::offer(line).price;
        TEST_ASSERT_TRUE(price > previous);
        previous = price;
        line = sh::next(line);
    }
}

void test_nothing_is_free() {
    // `economy::spend` succeeds on a price of zero, deliberately. A line
    // priced there would be a product the courier run is not needed for at
    // all.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        TEST_ASSERT_TRUE(sh::offer(static_cast<sh::Line>(i)).price > 0);
    }
}

void test_every_price_fits_the_digits_the_picker_draws() {
    // The picker formats the price into a fixed buffer rather than through
    // any formatting call -- there is none anywhere in this HUD. A price
    // wider than the buffer does not overflow it; it silently loses its
    // leading digit, and a 1500 dollar shotgun is advertised at 500.
    for (std::uint8_t i = 0; i < kLines; ++i) {
        TEST_ASSERT_TRUE(sh::offer(static_cast<sh::Line>(i)).price
                         <= sh::kMaxDrawnPrice);
    }
    // And the cap is the purse's, or the shop could price something the
    // player is unable to hold the money for.
    TEST_ASSERT_TRUE(sh::kMaxDrawnPrice >= ec::kMaxCash);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_every_line_says_what_it_is);
    RUN_TEST(test_a_label_fits_its_row);
    RUN_TEST(test_the_line_count_matches_the_enum);
    RUN_TEST(test_walking_the_list_reaches_every_line_once_and_comes_back);
    RUN_TEST(test_walking_the_list_backwards_reaches_every_line_too);
    RUN_TEST(test_up_then_down_is_where_you_started);
    RUN_TEST(test_the_ends_of_the_list_wrap_into_each_other);
    RUN_TEST(test_an_impossible_line_is_the_first_one_rather_than_a_wild_read);
    RUN_TEST(test_a_line_either_arms_you_or_armours_you);
    RUN_TEST(test_the_counter_sells_both_of_the_player_weapons);
    RUN_TEST(test_nobody_sells_the_police_sidearm);
    RUN_TEST(test_the_vest_line_is_the_vest_the_armour_module_describes);
    RUN_TEST(test_a_weapon_line_carries_no_armour);
    RUN_TEST(test_the_prices_are_the_economys_and_not_a_second_copy);
    RUN_TEST(test_the_list_is_cheapest_first);
    RUN_TEST(test_nothing_is_free);
    RUN_TEST(test_every_price_fits_the_digits_the_picker_draws);
    return UNITY_END();
}
