/**
 * @brief The top strip's banner: a one-line dialog that dismisses itself.
 *
 * The strip used to be a countdown, a borrowed pointer and a serial number
 * kept only so the frame skip could tell two notices apart. It is now a
 * one-line script on the engine's DialogRunner, and these tests pin the three
 * things the player can see: WHAT it says, for HOW LONG, and that a new notice
 * over an old one is a change the frame skip notices.
 */
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "game/dialog/CityBanner.h"

using top_down_city::CityBanner;

void setUp() {}
void tearDown() {}

// --- What it says --------------------------------------------------------

void test_a_new_banner_shows_nothing() {
    const CityBanner banner;
    TEST_ASSERT_FALSE(banner.visible());
    TEST_ASSERT_NULL(banner.label());
}

void test_a_notice_is_up_with_its_own_words() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    TEST_ASSERT_TRUE(banner.visible());
    TEST_ASSERT_EQUAL_STRING("WANTED", banner.label());
}

void test_no_label_means_the_space_names_itself() {
    // nullptr is not "nothing": it is the district, which the scene reads
    // live, exactly as the strip did before it was a dialog.
    CityBanner banner;
    banner.show(nullptr, 2400);
    TEST_ASSERT_TRUE(banner.visible());
    TEST_ASSERT_NULL(banner.label());
}

// --- For how long --------------------------------------------------------

void test_a_banner_stays_up_until_its_last_millisecond() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    banner.update(2399);
    TEST_ASSERT_TRUE(banner.visible());
    banner.update(1);
    TEST_ASSERT_FALSE(banner.visible());
    TEST_ASSERT_NULL(banner.label());
}

void test_the_short_notice_lasts_a_third_of_a_banner() {
    CityBanner banner;
    banner.show("OUCH", 800);
    banner.update(799);
    TEST_ASSERT_TRUE(banner.visible());
    banner.update(1);
    TEST_ASSERT_FALSE(banner.visible());
}

void test_one_long_frame_takes_the_whole_banner_down() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    banner.update(5000);
    TEST_ASSERT_FALSE(banner.visible());
}

void test_a_new_notice_restarts_the_clock() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    banner.update(2000);
    banner.show("OUCH", 800);
    banner.update(799);
    TEST_ASSERT_TRUE(banner.visible());
    TEST_ASSERT_EQUAL_STRING("OUCH", banner.label());
    banner.update(1);
    TEST_ASSERT_FALSE(banner.visible());
}

void test_a_zero_length_notice_is_never_up() {
    // The countdown this replaced treated `ms <= 0` as down. A zero-length
    // line on the runner would instead wait for a button forever.
    CityBanner banner;
    banner.show("WANTED", 0);
    TEST_ASSERT_FALSE(banner.visible());
    banner.show("WANTED", -5);
    TEST_ASSERT_FALSE(banner.visible());
}

void test_a_notice_longer_than_the_runner_can_count_is_clamped_not_wrapped() {
    CityBanner banner;
    banner.show("WANTED", 70000);
    banner.update(65534);
    TEST_ASSERT_TRUE(banner.visible());
    banner.update(1);
    TEST_ASSERT_FALSE(banner.visible());
}

void test_hide_takes_it_down_at_once() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    banner.hide();
    TEST_ASSERT_FALSE(banner.visible());
}

// --- What the frame skip sees --------------------------------------------

void test_the_same_words_twice_are_still_a_change() {
    // Two identical notices in a row keep the strip up across the swap and
    // point at the same bytes, so only the revision can say it moved.
    CityBanner banner;
    banner.show("WANTED", 2400);
    const std::uint16_t before = banner.revision();
    banner.show("WANTED", 2400);
    TEST_ASSERT_TRUE(banner.revision() != before);
}

void test_a_rewritten_buffer_shown_again_is_a_change() {
    // The rampage counter rewrites one buffer in place. The banner borrows
    // the pointer, so the address never moves -- showing it again must.
    char counter[8] = "9 LEFT";
    CityBanner banner;
    banner.show(counter, 800);
    const std::uint16_t before = banner.revision();
    std::strcpy(counter, "8 LEFT");
    banner.show(counter, 800);
    TEST_ASSERT_TRUE(banner.revision() != before);
    TEST_ASSERT_EQUAL_STRING("8 LEFT", banner.label());
}

void test_running_out_is_a_change() {
    CityBanner banner;
    banner.show("WANTED", 2400);
    const std::uint16_t before = banner.revision();
    banner.update(2400);
    TEST_ASSERT_TRUE(banner.revision() != before);
}

void test_ticking_a_banner_that_is_down_changes_nothing() {
    CityBanner banner;
    banner.show("WANTED", 100);
    banner.update(100);
    const std::uint16_t before = banner.revision();
    banner.update(16);
    banner.hide();
    TEST_ASSERT_EQUAL_UINT16(before, banner.revision());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_new_banner_shows_nothing);
    RUN_TEST(test_a_notice_is_up_with_its_own_words);
    RUN_TEST(test_no_label_means_the_space_names_itself);
    RUN_TEST(test_a_banner_stays_up_until_its_last_millisecond);
    RUN_TEST(test_the_short_notice_lasts_a_third_of_a_banner);
    RUN_TEST(test_one_long_frame_takes_the_whole_banner_down);
    RUN_TEST(test_a_new_notice_restarts_the_clock);
    RUN_TEST(test_a_zero_length_notice_is_never_up);
    RUN_TEST(test_a_notice_longer_than_the_runner_can_count_is_clamped_not_wrapped);
    RUN_TEST(test_hide_takes_it_down_at_once);
    RUN_TEST(test_the_same_words_twice_are_still_a_change);
    RUN_TEST(test_a_rewritten_buffer_shown_again_is_a_change);
    RUN_TEST(test_running_out_is_a_change);
    RUN_TEST(test_ticking_a_banner_that_is_down_changes_nothing);
    return UNITY_END();
}
