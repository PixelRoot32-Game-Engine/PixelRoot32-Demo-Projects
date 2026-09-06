/*
 * Unit tests for the engine-free input patterns in src/patterns/.
 *
 * These four types are the layers a game builds *on top of* the four raw
 * InputManager verbs, and none of them touches the engine — which is exactly
 * why they can be tested on the host with no display, no SDL2 and no board.
 *
 * The tests deliberately concentrate on boundaries: a value landing exactly on
 * a window edge, a release in the middle of a repeat train, a combo whose
 * second edge arrives one millisecond too late, a ring buffer wrapping past
 * its capacity, and a peak that must survive the activity that produced it.
 */
#include <unity.h>

#include "patterns/InputPatterns.h"

using namespace digital_buttons;

void setUp() {}
void tearDown() {}

// =============================================================================
// RepeatTimer
// =============================================================================

/** The rising edge fires immediately: a menu must move on the first frame. */
static void test_repeat_fires_on_rising_edge() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 16));
}

/** Nothing fires while the button is up, however much time passes. */
static void test_repeat_silent_while_released() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_FALSE(timer.tick(false, 1000));
    TEST_ASSERT_FALSE(timer.tick(false, 1000));
}

/** After the rising edge the timer stays quiet for the whole initial delay. */
static void test_repeat_waits_out_the_initial_delay() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 16));

    unsigned long elapsed = 0;
    while (elapsed < 384) {  // 24 frames of 16 ms, one short of 400
        TEST_ASSERT_FALSE(timer.tick(true, 16));
        elapsed += 16;
    }
}

/** Exactly on the edge counts as reached, not as one millisecond short. */
static void test_repeat_fires_exactly_at_the_initial_delay() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 1));   // rising edge
    TEST_ASSERT_FALSE(timer.tick(true, 399));
    TEST_ASSERT_TRUE(timer.tick(true, 1));   // cumulative 400
}

/** Same rule for the repeat interval that follows the initial delay. */
static void test_repeat_fires_exactly_at_the_interval() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 1));    // rising edge
    TEST_ASSERT_TRUE(timer.tick(true, 400));  // initial delay
    TEST_ASSERT_FALSE(timer.tick(true, 79));
    TEST_ASSERT_TRUE(timer.tick(true, 1));    // cumulative 80
}

/** A frame longer than the interval fires once, never twice. */
static void test_repeat_fires_at_most_once_per_tick() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 1));
    TEST_ASSERT_TRUE(timer.tick(true, 400));
    // 500 ms in one frame covers six intervals; the timer still reports one.
    TEST_ASSERT_TRUE(timer.tick(true, 500));
}

/**
 * The surplus of a long frame is carried, not discarded.
 *
 * The 500 ms frame above lands 420 ms past a deadline that was 80 ms away.
 * 420 % 80 leaves 20 ms already served, so the next repeat is owed 60 ms —
 * not a fresh 80. Without the carry the train would drift later on every
 * long frame, which is the whole point of the modulo in tick().
 */
static void test_repeat_carries_the_surplus_into_the_next_interval() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 1));
    TEST_ASSERT_TRUE(timer.tick(true, 400));
    TEST_ASSERT_TRUE(timer.tick(true, 500));  // carry = (500 - 80) % 80 = 20

    TEST_ASSERT_FALSE(timer.tick(true, 59));
    TEST_ASSERT_TRUE(timer.tick(true, 1));    // cumulative 60, not 80
}

/** Releasing mid-train disarms the timer; the next press starts over. */
static void test_repeat_resets_on_release_mid_train() {
    RepeatTimer timer(400, 80);

    TEST_ASSERT_TRUE(timer.tick(true, 1));
    TEST_ASSERT_TRUE(timer.tick(true, 400));
    TEST_ASSERT_FALSE(timer.tick(true, 40));  // halfway to the next repeat

    TEST_ASSERT_FALSE(timer.tick(false, 16));  // released

    // A fresh press fires immediately and then owes the full initial delay
    // again, not the 40 ms that were left over.
    TEST_ASSERT_TRUE(timer.tick(true, 16));
    TEST_ASSERT_FALSE(timer.tick(true, 399));
    TEST_ASSERT_TRUE(timer.tick(true, 1));
}

/** A zero interval degrades to one millisecond instead of dividing by zero. */
static void test_repeat_tolerates_a_zero_interval() {
    RepeatTimer timer(10, 0);

    TEST_ASSERT_TRUE(timer.tick(true, 1));
    TEST_ASSERT_TRUE(timer.tick(true, 10));
    TEST_ASSERT_TRUE(timer.tick(true, 1));
}

// =============================================================================
// InputBuffer
// =============================================================================

/** Nothing buffered means nothing to consume. */
static void test_buffer_empty_consume_is_false() {
    InputBuffer buffer(150);

    TEST_ASSERT_FALSE(buffer.consume(0));
    TEST_ASSERT_FALSE(buffer.consume(1000));
}

/** A press consumed inside the window succeeds. */
static void test_buffer_consumes_inside_the_window() {
    InputBuffer buffer(150);

    buffer.press(1000);

    TEST_ASSERT_TRUE(buffer.pending(1100));
    TEST_ASSERT_TRUE(buffer.consume(1100));
}

/** Exactly at the window edge still counts. */
static void test_buffer_consumes_exactly_at_the_window_edge() {
    InputBuffer buffer(150);

    buffer.press(1000);

    TEST_ASSERT_TRUE(buffer.consume(1150));
}

/** One millisecond past the edge does not. */
static void test_buffer_rejects_one_ms_past_the_window() {
    InputBuffer buffer(150);

    buffer.press(1000);

    TEST_ASSERT_FALSE(buffer.pending(1151));
    TEST_ASSERT_FALSE(buffer.consume(1151));
}

/** A single press feeds a single consumer. */
static void test_buffer_consume_is_single_shot() {
    InputBuffer buffer(150);

    buffer.press(1000);

    TEST_ASSERT_TRUE(buffer.consume(1010));
    TEST_ASSERT_FALSE(buffer.consume(1020));
}

/** A stale press is dropped by the failed consume, not left to fire later. */
static void test_buffer_drops_the_stale_press_on_a_failed_consume() {
    InputBuffer buffer(150);

    buffer.press(1000);

    TEST_ASSERT_FALSE(buffer.consume(2000));  // too late
    TEST_ASSERT_FALSE(buffer.consume(2001));  // and it did not survive
}

/** A second press replaces the first: the buffer holds one input, not a queue. */
static void test_buffer_keeps_only_the_latest_press() {
    InputBuffer buffer(150);

    buffer.press(1000);
    buffer.press(1200);

    TEST_ASSERT_TRUE(buffer.consume(1300));
}

/** clear() disarms without consuming. */
static void test_buffer_clear_disarms() {
    InputBuffer buffer(150);

    buffer.press(1000);
    buffer.clear();

    TEST_ASSERT_FALSE(buffer.consume(1010));
}

// =============================================================================
// ComboDetector
// =============================================================================

/** Both edges in the same call is the tightest possible combo. */
static void test_combo_fires_on_simultaneous_edges() {
    ComboDetector combo(100);

    TEST_ASSERT_TRUE(combo.feed(true, true, 16));
}

/** One edge alone never fires. */
static void test_combo_ignores_a_lone_edge() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_FALSE(combo.feed(false, false, 16));
}

/** The second edge inside the tolerance completes the combo. */
static void test_combo_fires_within_tolerance() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_TRUE(combo.feed(false, true, 50));
}

/** Exactly at the tolerance still counts. */
static void test_combo_fires_exactly_at_the_tolerance() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_TRUE(combo.feed(false, true, 100));
}

/** One millisecond past it does not: the first edge has already expired. */
static void test_combo_expires_one_ms_past_the_tolerance() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_FALSE(combo.feed(false, true, 101));
}

/** Order does not matter — B first works exactly like A first. */
static void test_combo_is_order_independent() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(false, true, 16));
    TEST_ASSERT_TRUE(combo.feed(true, false, 30));
}

/** Firing clears both slots, so one press pair yields one combo. */
static void test_combo_fires_once_per_pair() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_TRUE(combo.feed(false, true, 30));
    TEST_ASSERT_FALSE(combo.feed(false, false, 16));
    TEST_ASSERT_FALSE(combo.feed(false, false, 16));
}

/** An expired first edge does not poison the next attempt. */
static void test_combo_recovers_after_an_expiry() {
    ComboDetector combo(100);

    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_FALSE(combo.feed(false, false, 500));  // A expires unused
    TEST_ASSERT_FALSE(combo.feed(true, false, 16));
    TEST_ASSERT_TRUE(combo.feed(false, true, 20));
}

// =============================================================================
// EdgeRateMeter
// =============================================================================

/** A fresh meter reports nothing. */
static void test_meter_starts_at_zero() {
    EdgeRateMeter meter;

    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(0));
    TEST_ASSERT_EQUAL_UINT8(0, meter.peak());
}

/** Edges inside the trailing second are all counted. */
static void test_meter_counts_the_trailing_second() {
    EdgeRateMeter meter;

    meter.edge(1000);
    meter.edge(1100);
    meter.edge(1200);

    TEST_ASSERT_EQUAL_UINT8(3, meter.ratePerSecond(1300));
}

/** 999 ms old is still inside the window. */
static void test_meter_includes_an_edge_999ms_old() {
    EdgeRateMeter meter;

    meter.edge(1000);

    TEST_ASSERT_EQUAL_UINT8(1, meter.ratePerSecond(1999));
}

/** Exactly 1000 ms old has left it: the window is the trailing 1000 ms, open
 *  at the far end. */
static void test_meter_excludes_an_edge_exactly_1000ms_old() {
    EdgeRateMeter meter;

    meter.edge(1000);

    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(2000));
}

/** The live rate falls back to zero once the mashing stops. */
static void test_meter_rate_decays_to_zero() {
    EdgeRateMeter meter;

    for (unsigned long t = 0; t < 1000; t += 100) {
        meter.edge(t);
    }
    TEST_ASSERT_EQUAL_UINT8(10, meter.ratePerSecond(999));
    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(5000));
}

/** The peak does not: it is the whole reason the meter is worth keeping. */
static void test_meter_retains_the_peak_after_the_rate_decays() {
    EdgeRateMeter meter;

    for (unsigned long t = 0; t < 1000; t += 100) {
        meter.edge(t);
    }
    TEST_ASSERT_EQUAL_UINT8(10, meter.ratePerSecond(999));

    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(5000));
    TEST_ASSERT_EQUAL_UINT8(10, meter.peak());
}

/** More edges than the ring holds: the oldest are overwritten, and the count
 *  saturates at the capacity instead of running off the end of the array. */
static void test_meter_ring_wraps_without_overcounting() {
    EdgeRateMeter meter;

    // 40 edges 10 ms apart — 400 ms of history, all inside the window, but
    // more edges than the 32-slot ring can hold.
    for (unsigned long i = 0; i < 40; ++i) {
        meter.edge(i * 10);
    }

    TEST_ASSERT_EQUAL_UINT8(EdgeRateMeter::kCapacity, meter.ratePerSecond(400));
}

/** After a wrap the oldest surviving stamps are the most recent ones, so an
 *  old burst does not reappear in a later window. */
static void test_meter_ring_wrap_discards_the_oldest_stamps() {
    EdgeRateMeter meter;

    for (unsigned long i = 0; i < 40; ++i) {
        meter.edge(i * 10);
    }

    // The newest stamp is at 390 ms. Two seconds later nothing survives.
    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(2400));
}

/** reset() clears history and peak alike. */
static void test_meter_reset_clears_everything() {
    EdgeRateMeter meter;

    meter.edge(0);
    meter.edge(100);
    (void)meter.ratePerSecond(200);

    meter.reset();

    TEST_ASSERT_EQUAL_UINT8(0, meter.ratePerSecond(200));
    TEST_ASSERT_EQUAL_UINT8(0, meter.peak());
}

// =============================================================================

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_repeat_fires_on_rising_edge);
    RUN_TEST(test_repeat_silent_while_released);
    RUN_TEST(test_repeat_waits_out_the_initial_delay);
    RUN_TEST(test_repeat_fires_exactly_at_the_initial_delay);
    RUN_TEST(test_repeat_fires_exactly_at_the_interval);
    RUN_TEST(test_repeat_fires_at_most_once_per_tick);
    RUN_TEST(test_repeat_carries_the_surplus_into_the_next_interval);
    RUN_TEST(test_repeat_resets_on_release_mid_train);
    RUN_TEST(test_repeat_tolerates_a_zero_interval);

    RUN_TEST(test_buffer_empty_consume_is_false);
    RUN_TEST(test_buffer_consumes_inside_the_window);
    RUN_TEST(test_buffer_consumes_exactly_at_the_window_edge);
    RUN_TEST(test_buffer_rejects_one_ms_past_the_window);
    RUN_TEST(test_buffer_consume_is_single_shot);
    RUN_TEST(test_buffer_drops_the_stale_press_on_a_failed_consume);
    RUN_TEST(test_buffer_keeps_only_the_latest_press);
    RUN_TEST(test_buffer_clear_disarms);

    RUN_TEST(test_combo_fires_on_simultaneous_edges);
    RUN_TEST(test_combo_ignores_a_lone_edge);
    RUN_TEST(test_combo_fires_within_tolerance);
    RUN_TEST(test_combo_fires_exactly_at_the_tolerance);
    RUN_TEST(test_combo_expires_one_ms_past_the_tolerance);
    RUN_TEST(test_combo_is_order_independent);
    RUN_TEST(test_combo_fires_once_per_pair);
    RUN_TEST(test_combo_recovers_after_an_expiry);

    RUN_TEST(test_meter_starts_at_zero);
    RUN_TEST(test_meter_counts_the_trailing_second);
    RUN_TEST(test_meter_includes_an_edge_999ms_old);
    RUN_TEST(test_meter_excludes_an_edge_exactly_1000ms_old);
    RUN_TEST(test_meter_rate_decays_to_zero);
    RUN_TEST(test_meter_retains_the_peak_after_the_rate_decays);
    RUN_TEST(test_meter_ring_wraps_without_overcounting);
    RUN_TEST(test_meter_ring_wrap_discards_the_oldest_stamps);
    RUN_TEST(test_meter_reset_clears_everything);

    return UNITY_END();
}
