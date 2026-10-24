/*
 * Suite for the march beat (S16).
 *
 * Three independent facts are pinned: the live-count interval table and its
 * boundaries, that a beat fires exactly on the interval and never a tick early,
 * and that the four tones rotate 0,1,2,3,0 while a cleared rack neither beats
 * nor advances the tone. The tables are static data, so a wrong entry fails an
 * assertion rather than a lookup at runtime.
 */
#include <unity.h>

#include "rules/MarchSound.h"

using namespace spaceinvaders::rules;

namespace {

struct IntervalCase {
    uint8_t aliens;
    uint16_t interval;
};

const IntervalCase INTERVAL_CASES[] = {
    {55, 52}, {50, 52}, {49, 46}, {43, 46}, {42, 39}, {36, 39}, {28, 34},
    {22, 28}, {17, 24}, {13, 21}, {10, 19}, {8, 16},  {7, 14},  {6, 13},
    {5, 12},  {4, 11},  {3, 9},   {2, 7},   {1, 5},
};

const int INTERVAL_CASE_COUNT =
    static_cast<int>(sizeof(INTERVAL_CASES) / sizeof(INTERVAL_CASES[0]));

/// Asserts no beat for every tick before the interval and one on the last tick.
void expectBeatAfter(MarchSound& march, uint8_t aliens, uint16_t interval) {
    for (uint16_t tickIndex = 1; tickIndex < interval; ++tickIndex) {
        TEST_ASSERT_FALSE(march.tick(aliens));
    }
    TEST_ASSERT_TRUE(march.tick(aliens));
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_march_interval_table_and_boundaries(void) {
    for (int index = 0; index < INTERVAL_CASE_COUNT; ++index) {
        const IntervalCase& expected = INTERVAL_CASES[index];
        TEST_ASSERT_EQUAL_UINT16(expected.interval,
                                 MarchSound::intervalTicks(expected.aliens));
    }

    // A cleared rack has no beat, so it has no interval either.
    TEST_ASSERT_EQUAL_UINT16(0, MarchSound::intervalTicks(0));
}

void test_march_beat_fires_exactly_on_the_interval(void) {
    MarchSound march;
    march.reset();

    TEST_ASSERT_EQUAL_UINT16(52, march.lastIntervalTicks());

    // Fifty-one ticks must stay silent; the fifty-second is the beat.
    expectBeatAfter(march, 50, 52);

    // And the gap repeats, not just the first one.
    expectBeatAfter(march, 50, 52);
    expectBeatAfter(march, 50, 52);
}

void test_march_tones_cycle_zero_one_two_three_zero(void) {
    MarchSound march;
    march.reset();

    const int expected[5] = {0, 1, 2, 3, 0};
    for (int beat = 0; beat < 5; ++beat) {
        expectBeatAfter(march, 55, 52);
        TEST_ASSERT_EQUAL_INT(expected[beat], march.toneIndex());
    }
}

void test_march_zero_aliens_never_beats_or_advances_the_tone(void) {
    MarchSound march;
    march.reset();

    for (int tickIndex = 0; tickIndex < 200; ++tickIndex) {
        TEST_ASSERT_FALSE(march.tick(0));
    }
    TEST_ASSERT_EQUAL_INT(0, march.toneIndex());

    // The countdown was not consumed, so the rack's first beat still arrives
    // after one full fifty-five-alien interval and carries tone 0.
    expectBeatAfter(march, 50, 52);
    TEST_ASSERT_EQUAL_INT(0, march.toneIndex());

    // A cleared rack between two beats does not skip a voice either.
    for (int tickIndex = 0; tickIndex < 200; ++tickIndex) {
        TEST_ASSERT_FALSE(march.tick(0));
    }
    expectBeatAfter(march, 50, 52);
    TEST_ASSERT_EQUAL_INT(1, march.toneIndex());
}

void test_march_last_interval_follows_the_live_count_at_the_beat(void) {
    MarchSound march;
    march.reset();

    // A full rack starts on 52; the first beat reloads with the count of that
    // tick, so the next gap follows the table's three-alien entry.
    expectBeatAfter(march, 3, 52);
    TEST_ASSERT_EQUAL_UINT16(9, march.lastIntervalTicks());
    expectBeatAfter(march, 3, 9);
}

void test_march_reset_returns_to_tone_zero_and_full_rack_interval(void) {
    MarchSound march;
    march.reset();

    // Spend the first two voices at a fast table entry.
    expectBeatAfter(march, 3, 52);
    expectBeatAfter(march, 3, 9);
    TEST_ASSERT_EQUAL_INT(1, march.toneIndex());

    march.reset();
    TEST_ASSERT_EQUAL_INT(0, march.toneIndex());
    TEST_ASSERT_EQUAL_UINT16(52, march.lastIntervalTicks());
    expectBeatAfter(march, 55, 52);
    TEST_ASSERT_EQUAL_INT(0, march.toneIndex());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_march_interval_table_and_boundaries);
    RUN_TEST(test_march_beat_fires_exactly_on_the_interval);
    RUN_TEST(test_march_tones_cycle_zero_one_two_three_zero);
    RUN_TEST(test_march_zero_aliens_never_beats_or_advances_the_tone);
    RUN_TEST(test_march_last_interval_follows_the_live_count_at_the_beat);
    RUN_TEST(test_march_reset_returns_to_tone_zero_and_full_rack_interval);

    return UNITY_END();
}
