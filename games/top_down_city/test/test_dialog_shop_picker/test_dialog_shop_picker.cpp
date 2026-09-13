/**
 * @brief The corner shop's picker: one choice line on the engine's runner.
 *
 * The till's rules have not moved -- the catalogue, its order and its wrap
 * are game/rules/Shop.h and test_shop covers them. What these tests pin is
 * the seam between that list and DialogRunner, where the two disagree in two
 * places the player would feel at once: the runner clamps at the ends of a
 * list where the shop wraps, and the runner leaves a choice line on Confirm
 * where the shop stays open after a sale.
 */
#include <unity.h>

#include <cstdint>

#include "game/dialog/ShopPicker.h"
#include "game/rules/Shop.h"

using top_down_city::ShopPicker;
namespace sh = top_down_city::shop;

void setUp() {}
void tearDown() {}

namespace {

constexpr bool kNone = false;
constexpr bool kPressed = true;

void down(ShopPicker& picker) { picker.navigate(kNone, kPressed); }
void up(ShopPicker& picker) { picker.navigate(kPressed, kNone); }

}  // namespace

// --- Opening and closing -------------------------------------------------

void test_a_new_picker_is_closed() {
    const ShopPicker picker;
    TEST_ASSERT_FALSE(picker.isOpen());
}

void test_opening_lands_on_the_cheapest_line() {
    ShopPicker picker;
    picker.open();
    TEST_ASSERT_TRUE(picker.isOpen());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_the_rows_are_the_catalogue_in_its_order() {
    ShopPicker picker;
    picker.open();
    TEST_ASSERT_EQUAL_UINT8(sh::kLineCount, picker.rowCount());
    for (std::uint8_t i = 0; i < sh::kLineCount; ++i) {
        TEST_ASSERT_EQUAL_UINT8(i, static_cast<std::uint8_t>(picker.lineAt(i)));
    }
}

void test_a_closed_picker_has_no_rows() {
    const ShopPicker picker;
    TEST_ASSERT_EQUAL_UINT8(0, picker.rowCount());
}

void test_closing_and_reopening_forgets_the_selection() {
    ShopPicker picker;
    picker.open();
    down(picker);
    picker.close();
    TEST_ASSERT_FALSE(picker.isOpen());
    picker.open();
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(picker.selected()));
}

// --- Walking the list ----------------------------------------------------

void test_down_walks_to_the_next_line() {
    ShopPicker picker;
    picker.open();
    down(picker);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Vest),
                            static_cast<std::uint8_t>(picker.selected()));
    down(picker);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Shotgun),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_down_past_the_last_line_wraps_to_the_first() {
    // The runner alone would stop here; the shop never did.
    ShopPicker picker;
    picker.open();
    for (std::uint8_t i = 0; i < sh::kLineCount; ++i) {
        down(picker);
    }
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_up_past_the_first_line_wraps_to_the_last() {
    ShopPicker picker;
    picker.open();
    up(picker);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Shotgun),
                            static_cast<std::uint8_t>(picker.selected()));
    up(picker);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Vest),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_both_directions_at_once_is_down() {
    ShopPicker picker;
    picker.open();
    picker.navigate(kPressed, kPressed);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Vest),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_no_press_moves_nothing_and_changes_nothing() {
    ShopPicker picker;
    picker.open();
    const std::uint16_t before = picker.revision();
    picker.navigate(kNone, kNone);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(picker.selected()));
    TEST_ASSERT_EQUAL_UINT16(before, picker.revision());
}

void test_a_move_is_a_change_the_frame_skip_sees() {
    ShopPicker picker;
    picker.open();
    const std::uint16_t before = picker.revision();
    down(picker);
    TEST_ASSERT_TRUE(picker.revision() != before);
}

void test_walking_a_closed_picker_does_nothing() {
    ShopPicker picker;
    const std::uint16_t before = picker.revision();
    down(picker);
    up(picker);
    TEST_ASSERT_FALSE(picker.isOpen());
    TEST_ASSERT_EQUAL_UINT16(before, picker.revision());
}

// --- Buying --------------------------------------------------------------

void test_confirm_names_the_highlighted_line() {
    ShopPicker picker;
    picker.open();
    down(picker);
    sh::Line bought = sh::first();
    TEST_ASSERT_TRUE(picker.confirm(bought));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Vest),
                            static_cast<std::uint8_t>(bought));
}

void test_a_sale_leaves_the_picker_open_on_the_same_line() {
    // The runner leaves a choice line on Confirm and resets the selection
    // when it enters one. The shop stays up after a sale, still on the row
    // the player just bought -- so a second press buys the same thing again.
    ShopPicker picker;
    picker.open();
    down(picker);
    down(picker);
    sh::Line bought = sh::first();
    TEST_ASSERT_TRUE(picker.confirm(bought));
    TEST_ASSERT_TRUE(picker.isOpen());
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Shotgun),
                            static_cast<std::uint8_t>(picker.selected()));
    TEST_ASSERT_TRUE(picker.confirm(bought));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Shotgun),
                            static_cast<std::uint8_t>(bought));
}

void test_the_walk_still_wraps_after_a_sale() {
    ShopPicker picker;
    picker.open();
    up(picker);
    sh::Line bought = sh::first();
    TEST_ASSERT_TRUE(picker.confirm(bought));
    down(picker);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::first()),
                            static_cast<std::uint8_t>(picker.selected()));
}

void test_confirm_on_a_closed_picker_buys_nothing() {
    ShopPicker picker;
    sh::Line bought = sh::Line::Shotgun;
    TEST_ASSERT_FALSE(picker.confirm(bought));
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(sh::Line::Shotgun),
                            static_cast<std::uint8_t>(bought));
}

void test_opening_and_closing_are_changes_the_frame_skip_sees() {
    ShopPicker picker;
    std::uint16_t before = picker.revision();
    picker.open();
    TEST_ASSERT_TRUE(picker.revision() != before);
    before = picker.revision();
    picker.close();
    TEST_ASSERT_TRUE(picker.revision() != before);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_new_picker_is_closed);
    RUN_TEST(test_opening_lands_on_the_cheapest_line);
    RUN_TEST(test_the_rows_are_the_catalogue_in_its_order);
    RUN_TEST(test_a_closed_picker_has_no_rows);
    RUN_TEST(test_closing_and_reopening_forgets_the_selection);
    RUN_TEST(test_down_walks_to_the_next_line);
    RUN_TEST(test_down_past_the_last_line_wraps_to_the_first);
    RUN_TEST(test_up_past_the_first_line_wraps_to_the_last);
    RUN_TEST(test_both_directions_at_once_is_down);
    RUN_TEST(test_no_press_moves_nothing_and_changes_nothing);
    RUN_TEST(test_a_move_is_a_change_the_frame_skip_sees);
    RUN_TEST(test_walking_a_closed_picker_does_nothing);
    RUN_TEST(test_confirm_names_the_highlighted_line);
    RUN_TEST(test_a_sale_leaves_the_picker_open_on_the_same_line);
    RUN_TEST(test_the_walk_still_wraps_after_a_sale);
    RUN_TEST(test_confirm_on_a_closed_picker_buys_nothing);
    RUN_TEST(test_opening_and_closing_are_changes_the_frame_skip_sees);
    return UNITY_END();
}
