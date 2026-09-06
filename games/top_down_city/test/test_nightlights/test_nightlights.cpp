// Host tests for the city's lights, in src/game/rules/NightLights.h.
//
// The subject has no engine dependency, so this suite needs no display, no
// framebuffer and no PlatformIO board: `pio test -e host_test`.
//
// The arithmetic is a weighted sum and a comparison; what is worth testing is
// the two properties the effect rests on: the schedule FOLLOWS the tint curve
// rather than sitting beside it, so a retune of DayNight.cpp cannot leave the
// lamps burning at noon; and the light table names entries nothing else uses,
// because leaving an entry untinted leaves it untinted everywhere.

#include <unity.h>

#include "game/rules/DayNight.h"
#include "game/rules/NightLights.h"

namespace dn = top_down_city::daynight;
namespace nl = top_down_city::nightlights;

namespace {

constexpr std::uint8_t kMidnightStep = 0;    // 00:00
constexpr std::uint8_t kNoonStep     = 24;   // 12:00

int switchesPerDay() {
    int changes = 0;
    for (std::uint8_t step = 0; step < dn::kSteps; ++step) {
        const std::uint8_t next =
            static_cast<std::uint8_t>((step + 1) % dn::kSteps);
        if (nl::lightsOn(step) != nl::lightsOn(next)) {
            ++changes;
        }
    }
    return changes;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- The schedule ---------------------------------------------------------

void test_the_lights_are_off_by_day_and_on_at_night() {
    TEST_ASSERT_FALSE(nl::lightsOn(kNoonStep));
    TEST_ASSERT_TRUE(nl::lightsOn(kMidnightStep));
}

void test_the_lights_are_off_when_the_demo_opens() {
    // The city opens at 08:00 and should look like a city before it looks
    // like a light show -- the same reason kStartStep is where it is.
    TEST_ASSERT_FALSE(nl::lightsOn(dn::kStartStep));
}

void test_the_lights_are_off_at_the_step_interiors_are_pinned_to() {
    // Indoors the tint is pinned at kDaylightStep, and CityDayNight reads the
    // lights at that same step. If it were a lit one, walking into the police
    // station would light every window in a building the player is inside.
    TEST_ASSERT_FALSE(nl::lightsOn(dn::kDaylightStep));
}

void test_the_lights_switch_exactly_twice_a_day() {
    // Once on at dusk, once off at dawn. Three or more means the threshold
    // has landed inside a wobble in the curve and the city is flickering.
    TEST_ASSERT_EQUAL_INT(2, switchesPerDay());
}

void test_the_lights_come_on_in_the_evening_and_go_out_in_the_morning() {
    // The exact steps, so that a retune of the curve that moves them has to
    // be a deliberate edit here rather than a silent change of hours.
    TEST_ASSERT_FALSE(nl::lightsOn(36));   // 18:00
    TEST_ASSERT_TRUE(nl::lightsOn(37));    // 18:30
    TEST_ASSERT_TRUE(nl::lightsOn(12));    // 06:00
    TEST_ASSERT_FALSE(nl::lightsOn(13));   // 06:30
}

void test_the_lit_hours_are_one_unbroken_night() {
    // Every lit step is contiguous ACROSS midnight, which is the shape the
    // two-switch count above cannot distinguish from its mirror. Walk from
    // the first dark step and the run of lit steps must be one block.
    std::uint8_t firstDark = 0;
    while (nl::lightsOn(firstDark)) {
        ++firstDark;
    }
    bool seenLit = false;
    bool darkAgainAfterLit = false;
    for (std::uint8_t i = 0; i < dn::kSteps; ++i) {
        const bool on =
            nl::lightsOn(static_cast<std::uint8_t>((firstDark + i) % dn::kSteps));
        if (on) {
            TEST_ASSERT_FALSE_MESSAGE(darkAgainAfterLit,
                                      "the lit hours are not one block");
            seenLit = true;
        } else if (seenLit) {
            darkAgainAfterLit = true;
        }
    }
    TEST_ASSERT_TRUE(seenLit);
}

void test_the_schedule_follows_the_tint_rather_than_sitting_beside_it() {
    // The property that makes this safe to retune: there is no step that is
    // lit while a darker step is not. The lights are a function of the curve,
    // so a change to the curve moves them and cannot leave them behind.
    for (std::uint8_t a = 0; a < dn::kSteps; ++a) {
        for (std::uint8_t b = 0; b < dn::kSteps; ++b) {
            const std::uint8_t la = nl::luminance(dn::ambientAt(a));
            const std::uint8_t lb = nl::luminance(dn::ambientAt(b));
            if (la < lb && !nl::lightsOn(a)) {
                TEST_ASSERT_FALSE_MESSAGE(
                    nl::lightsOn(b),
                    "a brighter step is lit while a darker one is not");
            }
        }
    }
}

void test_a_wrapped_step_is_reduced_rather_than_rejected() {
    TEST_ASSERT_EQUAL(nl::lightsOn(kNoonStep),
                      nl::lightsOn(static_cast<std::uint8_t>(kNoonStep + dn::kSteps)));
}

// --- Luminance ------------------------------------------------------------

void test_full_daylight_is_full_brightness() {
    // The identity ambient has to come out 255, because the threshold is
    // measured against it: anything less and "below full daylight" starts to
    // include noon.
    TEST_ASSERT_EQUAL_UINT8(255, nl::luminance(dn::Ambient{ 255, 255, 255 }));
    TEST_ASSERT_LESS_THAN_UINT8(nl::kLightsOnBelow,
                                nl::luminance(dn::Ambient{ 0, 0, 0 }));
}

void test_luminance_weights_green_hardest() {
    // Not a taste question: the night ambient is blue and bright in blue, and
    // a flat average of it would read as brighter than the dusk it follows.
    const std::uint8_t red   = nl::luminance(dn::Ambient{ 255, 0, 0 });
    const std::uint8_t green = nl::luminance(dn::Ambient{ 0, 255, 0 });
    const std::uint8_t blue  = nl::luminance(dn::Ambient{ 0, 0, 255 });
    TEST_ASSERT_GREATER_THAN_UINT8(red, green);
    TEST_ASSERT_GREATER_THAN_UINT8(blue, red);
}

// --- The light table ------------------------------------------------------

void test_a_light_is_looked_up_by_slot_and_entry() {
    for (std::uint8_t i = 0; i < nl::kLightCount; ++i) {
        TEST_ASSERT_TRUE(nl::isLight(nl::kLightSlot[i], nl::kLightEntry[i]));
    }
}

void test_the_transparent_entry_is_never_a_light() {
    // 0x0000 is the framebuffer's "skip this pixel", so a light there would
    // not glow -- it would fill in every hole in the tileset.
    for (std::uint8_t slot = 0; slot < 8; ++slot) {
        TEST_ASSERT_FALSE(nl::isLight(slot, 0));
    }
    for (std::uint8_t i = 0; i < nl::kLightCount; ++i) {
        TEST_ASSERT_NOT_EQUAL(0, nl::kLightEntry[i]);
    }
}

void test_an_entry_that_is_not_a_light_is_not_lit() {
    // The same entry number in a slot that does not own it, and an entry the
    // slot that does own one does not use. Both are ordinary paint.
    TEST_ASSERT_FALSE(nl::isLight(nl::kLightSlot[0], nl::kLightEntry[1]));
    TEST_ASSERT_FALSE(nl::isLight(nl::kLightSlot[1], nl::kLightEntry[0]));
}

void test_an_unknown_slot_has_no_lights() {
    // apply() walks all eight background slots and asks about every entry of
    // each, so out-of-table slots are the common case rather than an error.
    TEST_ASSERT_FALSE(nl::isLight(0, 11));
    TEST_ASSERT_FALSE(nl::isLight(4, 11));   // the interior's slot
    TEST_ASSERT_FALSE(nl::isLight(200, 11));
}

void test_no_slot_owns_two_lights() {
    // One entry per slot, and it matters: two would mean the art had run out
    // of room and started sharing, which is how a light ends up on something
    // that is not one.
    for (std::uint8_t i = 0; i < nl::kLightCount; ++i) {
        for (std::uint8_t j = static_cast<std::uint8_t>(i + 1);
             j < nl::kLightCount; ++j) {
            TEST_ASSERT_NOT_EQUAL(nl::kLightSlot[i], nl::kLightSlot[j]);
        }
    }
}

void test_every_light_entry_fits_a_sixteen_colour_palette() {
    for (std::uint8_t i = 0; i < nl::kLightCount; ++i) {
        TEST_ASSERT_LESS_THAN_UINT8(16, nl::kLightEntry[i]);
        TEST_ASSERT_LESS_THAN_UINT8(8, nl::kLightSlot[i]);
    }
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_lights_are_off_by_day_and_on_at_night);
    RUN_TEST(test_the_lights_are_off_when_the_demo_opens);
    RUN_TEST(test_the_lights_are_off_at_the_step_interiors_are_pinned_to);
    RUN_TEST(test_the_lights_switch_exactly_twice_a_day);
    RUN_TEST(test_the_lights_come_on_in_the_evening_and_go_out_in_the_morning);
    RUN_TEST(test_the_lit_hours_are_one_unbroken_night);
    RUN_TEST(test_the_schedule_follows_the_tint_rather_than_sitting_beside_it);
    RUN_TEST(test_a_wrapped_step_is_reduced_rather_than_rejected);
    RUN_TEST(test_full_daylight_is_full_brightness);
    RUN_TEST(test_luminance_weights_green_hardest);
    RUN_TEST(test_a_light_is_looked_up_by_slot_and_entry);
    RUN_TEST(test_the_transparent_entry_is_never_a_light);
    RUN_TEST(test_an_entry_that_is_not_a_light_is_not_lit);
    RUN_TEST(test_an_unknown_slot_has_no_lights);
    RUN_TEST(test_no_slot_owns_two_lights);
    RUN_TEST(test_every_light_entry_fits_a_sixteen_colour_palette);
    return UNITY_END();
}
