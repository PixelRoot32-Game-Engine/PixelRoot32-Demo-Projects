/*
 * RED suite for the fixed 60 Hz clock (S15).
 *
 * The scene used to integrate deltaTime straight into its timers, so the game
 * ran at whatever rate the frame loop delivered. These tests pin the
 * replacement contract: one rules tick per 16.667 ms of simulated time, a
 * bounded catch-up of at most five ticks per frame, and the leftover
 * sub-tick remainder carried to the next call.
 *
 * Every case drives the clock only through advance(). Nothing reads internal
 * storage except the public remainder accessor the reset case needs.
 *
 * Note on the expected totals: one tick is 16667 us, so exactly 2000 ms of
 * simulated time is 2 000 000 / 16 667 = 119 ticks, not 120. The tick count
 * is what matters; the frame chunking must not change it.
 */
#include <unity.h>

#include "rules/FixedStep.h"

using namespace spaceinvaders::rules;

namespace {

// Integer-millisecond jitter that averages the 16.667 ms period: the 60 Hz
// pattern repeats every three frames (50 ms) and the 30 Hz pattern every
// three frames (100 ms). Both reach exactly 2000 ms in 120 and 60 frames.
constexpr uint32_t Frames60[3] = {16, 17, 17};
constexpr uint32_t Frames30[3] = {33, 33, 34};

int runPattern(FixedStep& clock, const uint32_t (&pattern)[3], int frames) {
    int ticks = 0;
    for (int index = 0; index < frames; ++index) {
        ticks += clock.advance(pattern[index % 3]);
    }
    return ticks;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_sixty_fps_stream_runs_one_tick_per_period(void) {
    FixedStep clock;

    const int ticks = runPattern(clock, Frames60, 120);

    TEST_ASSERT_EQUAL_INT(119, ticks);
    TEST_ASSERT_EQUAL_UINT32(16627, clock.remainderMicros());
}

void test_thirty_fps_stream_matches_the_sixty_fps_total(void) {
    FixedStep sixty;
    FixedStep thirty;

    const int sixtyTicks = runPattern(sixty, Frames60, 120);
    const int thirtyTicks = runPattern(thirty, Frames30, 60);

    // Identical simulated time means identical tick count regardless of how
    // the time was chunked into frames (S15, S20).
    TEST_ASSERT_EQUAL_INT(sixtyTicks, thirtyTicks);
    TEST_ASSERT_EQUAL_INT(119, thirtyTicks);
    TEST_ASSERT_EQUAL_UINT32(16627, thirty.remainderMicros());
}

void test_two_seconds_of_sixty_hz_frames_runs_one_hundred_twenty_ticks(void) {
    FixedStep clock;

    for (int index = 0; index < 124; ++index) {
        TEST_ASSERT_LESS_OR_EQUAL_INT(FixedStep::MaxTicksPerFrame, clock.advance(16));
    }
    const int last = clock.advance(17);

    TEST_ASSERT_EQUAL_INT(1, last);
    TEST_ASSERT_EQUAL_UINT32(960, clock.remainderMicros());
}

void test_long_stall_caps_the_catch_up_and_drops_the_backlog(void) {
    FixedStep clock;

    const int stalled = clock.advance(1000);

    TEST_ASSERT_EQUAL_INT(FixedStep::MaxTicksPerFrame, stalled);
    TEST_ASSERT_EQUAL_INT(5, stalled);

    // The 54 ticks beyond the cap are dropped, not queued: the next frame
    // must not teleport the rack by replaying them.
    TEST_ASSERT_EQUAL_INT(0, clock.advance(0));
    TEST_ASSERT_EQUAL_INT(0, clock.advance(0));
}

void test_reset_drops_the_partial_accumulation(void) {
    FixedStep clock;

    TEST_ASSERT_EQUAL_INT(0, clock.advance(10));
    TEST_ASSERT_EQUAL_INT(0, clock.advance(6));
    TEST_ASSERT_EQUAL_UINT32(16000, clock.remainderMicros());

    clock.reset();

    TEST_ASSERT_EQUAL_UINT32(0, clock.remainderMicros());
    // Without the reset the 6 ms would complete the 16.667 ms period.
    TEST_ASSERT_EQUAL_INT(0, clock.advance(6));
    // And the clock still runs afterwards.
    TEST_ASSERT_EQUAL_INT(1, clock.advance(11));
}

void test_sixteen_ms_is_below_the_period_and_seventeen_ms_is_above_it(void) {
    FixedStep slow;
    FixedStep fast;

    TEST_ASSERT_EQUAL_INT(0, slow.advance(16));
    TEST_ASSERT_EQUAL_INT(1, fast.advance(17));

    TEST_ASSERT_EQUAL_UINT32(16000, slow.remainderMicros());
    TEST_ASSERT_EQUAL_UINT32(333, fast.remainderMicros());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_sixty_fps_stream_runs_one_tick_per_period);
    RUN_TEST(test_thirty_fps_stream_matches_the_sixty_fps_total);
    RUN_TEST(test_two_seconds_of_sixty_hz_frames_runs_one_hundred_twenty_ticks);
    RUN_TEST(test_long_stall_caps_the_catch_up_and_drops_the_backlog);
    RUN_TEST(test_reset_drops_the_partial_accumulation);
    RUN_TEST(test_sixteen_ms_is_below_the_period_and_seventeen_ms_is_above_it);

    return UNITY_END();
}
