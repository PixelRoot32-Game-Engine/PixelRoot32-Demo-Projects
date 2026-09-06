// Host tests for the day/night maths in src/game/rules/DayNight.h.
//
// The subject has no engine dependency, so this suite needs no display, no
// framebuffer and no PlatformIO board: `pio test -e host_test`.

#include <unity.h>

#include <cstdlib>

#include "game/rules/DayNight.h"

namespace dn = top_down_city::daynight;

namespace {

constexpr dn::Ambient kDaylight = { 255, 255, 255 };

std::uint8_t red(std::uint16_t c)   { return static_cast<std::uint8_t>((c >> 11) & 0x1F); }
std::uint8_t green(std::uint16_t c) { return static_cast<std::uint8_t>((c >> 5) & 0x3F); }
std::uint8_t blue(std::uint16_t c)  { return static_cast<std::uint8_t>(c & 0x1F); }

constexpr std::uint8_t kMidnightStep = 0;    // 00:00
constexpr std::uint8_t kNoonStep     = 24;   // 12:00

}  // namespace

void setUp() {}
void tearDown() {}

// --- The clock ------------------------------------------------------------

void test_step_advances_once_per_step_period() {
    TEST_ASSERT_EQUAL_UINT8(0, dn::stepAt(0));
    TEST_ASSERT_EQUAL_UINT8(0, dn::stepAt(dn::kStepMs - 1));
    TEST_ASSERT_EQUAL_UINT8(1, dn::stepAt(dn::kStepMs));
    TEST_ASSERT_EQUAL_UINT8(dn::kSteps - 1, dn::stepAt(dn::kCycleMs - 1));
}

void test_clock_wraps_at_the_end_of_the_day() {
    TEST_ASSERT_EQUAL_UINT8(0, dn::stepAt(dn::kCycleMs));
    TEST_ASSERT_EQUAL_UINT8(3, dn::stepAt(dn::kCycleMs * 2 + dn::kStepMs * 3));
}

void test_step_reads_as_a_wall_clock() {
    TEST_ASSERT_EQUAL_UINT8(0,  dn::hourOf(0));
    TEST_ASSERT_EQUAL_UINT8(0,  dn::minuteOf(0));
    TEST_ASSERT_EQUAL_UINT8(0,  dn::hourOf(1));
    TEST_ASSERT_EQUAL_UINT8(30, dn::minuteOf(1));
    TEST_ASSERT_EQUAL_UINT8(8,  dn::hourOf(dn::kStartStep));
    TEST_ASSERT_EQUAL_UINT8(0,  dn::minuteOf(dn::kStartStep));
    TEST_ASSERT_EQUAL_UINT8(23, dn::hourOf(dn::kSteps - 1));
    TEST_ASSERT_EQUAL_UINT8(30, dn::minuteOf(dn::kSteps - 1));
}

void test_wall_clock_reduces_an_out_of_range_step() {
    TEST_ASSERT_EQUAL_UINT8(0, dn::hourOf(dn::kSteps));
    TEST_ASSERT_EQUAL_UINT8(0, dn::minuteOf(dn::kSteps));
}

// --- The light ------------------------------------------------------------

void test_midday_leaves_the_generated_art_alone() {
    const dn::Ambient noon = dn::ambientAt(kNoonStep);
    TEST_ASSERT_EQUAL_UINT8(255, noon.r);
    TEST_ASSERT_EQUAL_UINT8(255, noon.g);
    TEST_ASSERT_EQUAL_UINT8(255, noon.b);
}

void test_night_is_darker_than_day_on_every_channel() {
    const dn::Ambient night = dn::ambientAt(kMidnightStep);
    const dn::Ambient day   = dn::ambientAt(kNoonStep);
    TEST_ASSERT_LESS_THAN_UINT8(day.r, night.r);
    TEST_ASSERT_LESS_THAN_UINT8(day.g, night.g);
    TEST_ASSERT_LESS_THAN_UINT8(day.b, night.b);
}

void test_night_is_blue_not_grey() {
    // A flat darkening reads as a dimmer switch. Keeping more blue than red is
    // what makes it read as moonlight.
    const dn::Ambient night = dn::ambientAt(kMidnightStep);
    TEST_ASSERT_GREATER_THAN_UINT8(night.r, night.b);
}

void test_dusk_is_warm() {
    // Somewhere between the last daylight step and full night the light has to
    // be warmer than it is blue, or the sunset is just an early night.
    bool warmStepFound = false;
    for (std::uint8_t step = 36; step < 42; ++step) {
        const dn::Ambient light = dn::ambientAt(step);
        if (light.r > light.b) {
            warmStepFound = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(warmStepFound);
}

void test_the_light_never_jumps() {
    // Adjacent steps must be close, including across midnight: a large gap is
    // a visible flash on a panel that only repaints when the step changes.
    for (std::uint8_t step = 0; step < dn::kSteps; ++step) {
        const dn::Ambient a = dn::ambientAt(step);
        const dn::Ambient b = dn::ambientAt(static_cast<std::uint8_t>((step + 1) % dn::kSteps));
        TEST_ASSERT_LESS_OR_EQUAL_INT(40, abs(static_cast<int>(a.r) - static_cast<int>(b.r)));
        TEST_ASSERT_LESS_OR_EQUAL_INT(40, abs(static_cast<int>(a.g) - static_cast<int>(b.g)));
        TEST_ASSERT_LESS_OR_EQUAL_INT(40, abs(static_cast<int>(a.b) - static_cast<int>(b.b)));
    }
}

// --- The tint ------------------------------------------------------------

void test_daylight_is_the_identity_transform() {
    // Every representable RGB565 colour, not a sample: the generated palettes
    // are the source of truth for the city's look and daylight must not move
    // a single entry of them.
    for (std::uint32_t c = 0; c <= 0xFFFF; ++c) {
        const std::uint16_t color = static_cast<std::uint16_t>(c);
        TEST_ASSERT_EQUAL_HEX16(color, dn::tint(color, kDaylight));
    }
}

void test_the_indoor_step_is_full_daylight() {
    // Interiors are lit by their own ceiling, so the cycle is pinned to this
    // step while the player is inside. It has to sit on the flat daylight
    // plateau of the curve, not merely near it: anything else and walking
    // into the station at noon would visibly change the room's colours.
    TEST_ASSERT_EQUAL_UINT8(255, dn::ambientAt(dn::kDaylightStep).r);
    TEST_ASSERT_EQUAL_UINT8(255, dn::ambientAt(dn::kDaylightStep).g);
    TEST_ASSERT_EQUAL_UINT8(255, dn::ambientAt(dn::kDaylightStep).b);
    TEST_ASSERT_LESS_THAN_UINT8(dn::kSteps, dn::kDaylightStep);
}

void test_transparent_entry_stays_transparent() {
    for (std::uint8_t step = 0; step < dn::kSteps; ++step) {
        TEST_ASSERT_EQUAL_HEX16(0x0000, dn::tint(0x0000, dn::ambientAt(step)));
    }
}

void test_a_visible_colour_never_tints_to_transparent() {
    // The failure this guards against is not "too dark": it is a hole in the
    // tilemap, because the framebuffer skips any colour that packs to zero.
    for (std::uint32_t c = 1; c <= 0xFFFF; ++c) {
        for (std::uint8_t step = 0; step < dn::kSteps; ++step) {
            const std::uint16_t out = dn::tint(static_cast<std::uint16_t>(c),
                                               dn::ambientAt(step));
            TEST_ASSERT_NOT_EQUAL(0x0000, out);
        }
    }
}

void test_night_darkens_a_bright_colour() {
    const std::uint16_t white = 0xFFFF;
    const std::uint16_t out = dn::tint(white, dn::ambientAt(kMidnightStep));
    TEST_ASSERT_LESS_THAN_UINT8(red(white), red(out));
    TEST_ASSERT_LESS_THAN_UINT8(green(white), green(out));
    TEST_ASSERT_LESS_THAN_UINT8(blue(white), blue(out));
}

void test_tint_preserves_relative_brightness() {
    // Contrast is what a palette swap must not destroy: the pavement has to
    // stay lighter than the kerb at night, or the city turns into a silhouette.
    const dn::Ambient night = dn::ambientAt(kMidnightStep);
    for (std::uint8_t v = 0; v < 31; ++v) {
        const std::uint16_t dark   = static_cast<std::uint16_t>(v << 11);
        const std::uint16_t bright = static_cast<std::uint16_t>((v + 1) << 11);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(red(dn::tint(bright, night)),
                                        red(dn::tint(dark, night)));
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_step_advances_once_per_step_period);
    RUN_TEST(test_clock_wraps_at_the_end_of_the_day);
    RUN_TEST(test_step_reads_as_a_wall_clock);
    RUN_TEST(test_wall_clock_reduces_an_out_of_range_step);
    RUN_TEST(test_midday_leaves_the_generated_art_alone);
    RUN_TEST(test_night_is_darker_than_day_on_every_channel);
    RUN_TEST(test_night_is_blue_not_grey);
    RUN_TEST(test_dusk_is_warm);
    RUN_TEST(test_the_light_never_jumps);
    RUN_TEST(test_daylight_is_the_identity_transform);
    RUN_TEST(test_the_indoor_step_is_full_daylight);
    RUN_TEST(test_transparent_entry_stays_transparent);
    RUN_TEST(test_a_visible_colour_never_tints_to_transparent);
    RUN_TEST(test_night_darkens_a_bright_colour);
    RUN_TEST(test_tint_preserves_relative_brightness);
    return UNITY_END();
}
