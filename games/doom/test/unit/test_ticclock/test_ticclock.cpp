/*
 * Behaviour tests for the doom port's 35 Hz tic clock.
 *
 * DOOM's simulation is driven by a 35 Hz tic, and the port derives that tic from
 * the microsecond counter instead of a frame counter or a per-frame subtraction.
 * The contract these tests pin is therefore arithmetic, not cosmetic:
 *
 *  - the tic at elapsed time `t` is exactly `t * 35 / 1000000`, not a sum of
 *    truncated per-sample deltas, so 600 simulated seconds are 21,000 tics no
 *    matter how the samples were sliced;
 *  - the microsecond source is 32-bit and wraps every ~71 minutes, so the clock
 *    extends the sample to 64 bits and never reports a lower tic;
 *  - an unchanged sample is not a tic, and neither is an out-of-order one.
 *
 * Every test drives only the public surface (`reset`, `update`, `tic`, `phase`)
 * and compares against arithmetic written independently on the test side. The
 * expected values are derived from the definition, not from the implementation,
 * so a drifting implementation cannot make them pass by agreeing with itself.
 *
 * `TicClock` is intentionally free of Arduino, engine and stdlib-container
 * dependencies; this environment compiles `src/time/` alone to keep it that way.
 */
#include <cstdint>

#include <unity.h>

#include "time/TicClock.h"

#include "../../test_config.h"

using doom::time::TicClock;

void setUp(void) {
    test_setup();
}

void tearDown() {
    test_teardown();
}

namespace {

constexpr std::uint32_t kTicsPerSecond = 35;
constexpr std::uint32_t kMicrosPerSecond = 1000000;

/// 600 seconds: the window every drift claim in the port is stated over.
constexpr std::uint32_t kTenMinutesMicros = 600u * kMicrosPerSecond;

/// 21,000: 600 s at 35 Hz.
constexpr std::uint32_t kTenMinutesTics = 21000;

/// The tic the definition `t * 35 / 1000000` names for `elapsedMicros`.
std::uint32_t ticAfter(std::uint64_t elapsedMicros) {
    return static_cast<std::uint32_t>(elapsedMicros * kTicsPerSecond / kMicrosPerSecond);
}

}  // namespace

// 600 s fed as 1,000 us steps: exactly 21,000 tics, and every intermediate
// sample agrees with the definition (so an accumulated-delta clock fails even
// though its final value could still be 21,000).
void test_one_millisecond_steps_yield_exactly_21000_tics(void) {
    TicClock clock;
    clock.reset(0);

    std::uint32_t previous = 0;
    for (std::uint32_t step = 1; step <= 600000; ++step) {
        const std::uint32_t tic = clock.update(step * 1000u);
        TEST_ASSERT_TRUE(tic >= previous);
        TEST_ASSERT_EQUAL_UINT32(ticAfter(static_cast<std::uint64_t>(step) * 1000u), tic);
        previous = tic;
    }

    TEST_ASSERT_EQUAL_UINT32(kTenMinutesTics, clock.tic());
}

// The same 600 s fed as irregular increments: the slices cannot change the
// answer. Each step is 1,000 us plus a jitter term, and the term list sums to
// zero, so the last sample is exactly 600,000,000 us.
void test_jittered_steps_yield_exactly_21000_tics(void) {
    // Sums to 0 and never takes a step below 750 us, so time only moves forward.
    constexpr std::int32_t kJitter[] = {119, -87, 250, -250, 33, -65};
    constexpr std::uint32_t kJitterCount = sizeof(kJitter) / sizeof(kJitter[0]);
    static_assert(600000u % kJitterCount == 0u,
                  "600,000 steps must be a whole number of jitter windows");

    TicClock clock;
    clock.reset(0);

    std::uint64_t elapsed = 0;
    std::uint32_t previous = 0;
    for (std::uint32_t step = 0; step < 600000; ++step) {
        elapsed += static_cast<std::uint64_t>(1000 + kJitter[step % kJitterCount]);
        const std::uint32_t tic = clock.update(static_cast<std::uint32_t>(elapsed));
        TEST_ASSERT_TRUE(tic >= previous);
        TEST_ASSERT_EQUAL_UINT32(ticAfter(elapsed), tic);
        previous = tic;
    }

    TEST_ASSERT_EQUAL_UINT64(static_cast<std::uint64_t>(kTenMinutesMicros), elapsed);
    TEST_ASSERT_EQUAL_UINT32(kTenMinutesTics, clock.tic());
}

// A caller that jumps ten minutes in one sample must get ten minutes of tics,
// not a clamped step: the clock derives from the sample, it does not integrate
// a frame delta.
void test_a_single_ten_minute_jump_yields_21000_tics(void) {
    constexpr std::uint32_t kOrigin = 5000000u;

    TicClock clock;
    clock.reset(kOrigin);
    TEST_ASSERT_EQUAL_UINT32(0u, clock.tic());

    clock.update(kOrigin);
    TEST_ASSERT_EQUAL_UINT32(0u, clock.tic());

    const std::uint32_t tic = clock.update(kOrigin + kTenMinutesMicros);
    TEST_ASSERT_EQUAL_UINT32(kTenMinutesTics, tic);
    TEST_ASSERT_EQUAL_UINT32(kTenMinutesTics, clock.tic());
}

// `micros()` is 32-bit and wraps every ~71 minutes. 16 us before the wrap plus
// 600 s of 1,000 us samples crosses it once; the tic count must keep advancing
// by exactly the elapsed microseconds.
void test_a_micros_wrap_keeps_the_count_monotone(void) {
    constexpr std::uint32_t kStart = 0xFFFFF000u;

    TicClock clock;
    clock.reset(kStart);

    std::uint32_t raw = kStart;
    std::uint64_t elapsed = 0;
    std::uint32_t previous = 0;
    for (std::uint32_t step = 1; step <= 600000; ++step) {
        raw += 1000u;  // wraps inside the loop
        elapsed += 1000u;

        const std::uint32_t tic = clock.update(raw);
        TEST_ASSERT_EQUAL_UINT32(ticAfter(elapsed), tic);
        TEST_ASSERT_TRUE(tic >= previous);
        previous = tic;
    }

    TEST_ASSERT_EQUAL_UINT32(kTenMinutesTics, previous);
}

// Re-reading the same microsecond value is not time passing: only a crossed tic
// boundary may change the answer.
void test_an_unchanged_sample_never_moves_the_tic(void) {
    constexpr std::uint32_t kSample = 123456u;

    TicClock clock;
    clock.reset(kSample);

    const std::uint32_t before = clock.update(kSample);
    for (int repeat = 0; repeat < 1000; ++repeat) {
        TEST_ASSERT_EQUAL_UINT32(before, clock.update(kSample));
    }

    // 1/35 s is 28,571.4 us: 28,571 us is still inside the tic (999,985
    // millionths of it) and 28,572 us crosses it (1,000,020 millionths).
    TEST_ASSERT_EQUAL_UINT32(before, clock.update(kSample + 28571u));
    TEST_ASSERT_EQUAL_UINT32(before + 1u, clock.update(kSample + 28572u));
}

// An out-of-order sample (the caller repeating an older reading) must not
// rewind the clock; the next forward sample resumes from the high-water mark.
void test_an_out_of_order_sample_never_rewinds_the_tic(void) {
    constexpr std::uint32_t kBase = 100000u;

    TicClock clock;
    clock.reset(kBase);

    const std::uint32_t tic = clock.update(kBase + 280000u);
    TEST_ASSERT_EQUAL_UINT32(9u, tic);  // 280,000 us * 35 / 1e6 = 9.8 tics

    TEST_ASSERT_EQUAL_UINT32(tic, clock.update(kBase + 250000u));
    TEST_ASSERT_EQUAL_UINT32(tic, clock.update(kBase + 279000u));
    TEST_ASSERT_EQUAL_UINT32(tic, clock.update(kBase + 280000u));

    // Once the source passes the high-water mark, time moves again.
    TEST_ASSERT_EQUAL_UINT32(10u, clock.update(kBase + 300000u));
}

// The fractional position is the remainder of the same exact division, so it is
// available without a second accumulator and without losing a microsecond.
void test_phase_is_the_exact_remainder_of_the_tic(void) {
    TicClock clock;
    clock.reset(0);

    clock.update(28571u);
    TEST_ASSERT_EQUAL_UINT32(0u, clock.tic());
    TEST_ASSERT_EQUAL_UINT32(999985u, clock.phase());  // 28,571 * 35

    clock.update(28572u);
    TEST_ASSERT_EQUAL_UINT32(1u, clock.tic());
    TEST_ASSERT_EQUAL_UINT32(20u, clock.phase());  // 28,572 * 35 - 1,000,000
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_one_millisecond_steps_yield_exactly_21000_tics);
    RUN_TEST(test_jittered_steps_yield_exactly_21000_tics);
    RUN_TEST(test_a_single_ten_minute_jump_yields_21000_tics);
    RUN_TEST(test_a_micros_wrap_keeps_the_count_monotone);
    RUN_TEST(test_an_unchanged_sample_never_moves_the_tic);
    RUN_TEST(test_an_out_of_order_sample_never_rewinds_the_tic);
    RUN_TEST(test_phase_is_the_exact_remainder_of_the_tic);

    return UNITY_END();
}
