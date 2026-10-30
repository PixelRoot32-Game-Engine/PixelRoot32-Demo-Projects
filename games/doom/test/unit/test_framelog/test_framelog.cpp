/*
 * Behaviour tests for the doom port's frame-pacing recorder.
 *
 * `FrameLog` is the instrument Phase 3 will judge the real renderer with, so
 * these tests pin the record itself, not an implementation detail:
 *
 *  - the CSV field order and spelling are exactly the contract a parser reads,
 *    including the trailing newline;
 *  - a frame with zero length is a legal frame, not a division-by-zero trap;
 *  - the tic and the heap reading are reported unsigned, so a value above
 *    INT32_MAX cannot turn negative on the wire;
 *  - `present_us` is a saturating remainder, so a render longer than its own
 *    period prints 0 instead of wrapping to a huge unsigned number;
 *  - one closed frame produces exactly one line, and the index advances once
 *    per line.
 *
 * Every test drives only the public surface (`beginFrame`, `endDraw`,
 * `endFrame`) through a sink that appends to a fixed buffer. The expected
 * strings are written out in full, so a formatting change cannot pass by
 * agreeing with the implementation.
 *
 * `FrameLog` is deliberately free of Arduino, engine and stdlib-container
 * dependencies; this environment compiles `src/perf/` and `src/time/` alone to
 * keep it that way.
 */
#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "perf/FrameLog.h"

#include "../../test_config.h"

using doom::perf::FrameLog;

namespace {

/// Fixed capture buffer: the recorder never allocates, so the test must not
/// either. Two maximum-width lines (under 200 bytes) fit with room to spare.
char g_output[1024];
std::size_t g_length = 0;

void captureLine(const char* line) {
    while (*line != '\0' && g_length + 1 < sizeof g_output) {
        g_output[g_length] = *line;
        ++g_length;
        ++line;
    }
    g_output[g_length] = '\0';
}

void resetCapture() {
    g_length = 0;
    g_output[0] = '\0';
}

}  // namespace

void setUp(void) {
    test_setup();
    resetCapture();
}

void tearDown() {
    test_teardown();
}

// The record a parser reads: exact names, order and separators, with the
// trailing newline that makes it one CSV line per frame.
void test_csv_field_order_and_format(void) {
    FrameLog log(captureLine);

    log.beginFrame(1000u, 7u);
    log.endDraw(3000u, 123456u);
    log.endFrame(5000u);

    TEST_ASSERT_EQUAL_STRING(
        "FRAME,i=0,tic=7,render_us=2000,present_us=2000,free_heap=123456\n", g_output);
}

// A zero-length frame is legal: the period and the render interval are both 0,
// and the residual is 0 rather than a division or a wrap.
void test_zero_length_frame_is_accepted(void) {
    FrameLog log(captureLine);

    log.beginFrame(1000u, 0u);
    log.endDraw(1000u, 42u);
    log.endFrame(1000u);

    TEST_ASSERT_EQUAL_STRING("FRAME,i=0,tic=0,render_us=0,present_us=0,free_heap=42\n",
                             g_output);
}

// The tic counter and the heap reading are unsigned 32-bit values. 0xFFFFFFFF
// must reach the wire as 4294967295, never as -1.
void test_counters_and_heap_are_reported_unsigned(void) {
    constexpr std::uint32_t kMaxUnsigned = 0xFFFFFFFFu;

    FrameLog log(captureLine);

    log.beginFrame(0u, kMaxUnsigned);
    log.endDraw(0u, kMaxUnsigned);
    log.endFrame(10u);

    TEST_ASSERT_EQUAL_STRING(
        "FRAME,i=0,tic=4294967295,render_us=0,present_us=10,free_heap=4294967295\n",
        g_output);
}

// The render interval can exceed the period only if the source jumps or the
// caller supplies out-of-order samples. The record must clamp to 0, not
// underflow to ~4.29 billion microseconds.
void test_present_never_underflows_when_render_exceeds_period(void) {
    FrameLog log(captureLine);

    // Near the 32-bit micros() wrap: render = 0x400 = 1024 us, period =
    // 0x200 = 512 us. The unsigned subtraction is wrap-safe, and 1024 > 512
    // must clamp.
    log.beginFrame(0xFFFFFF00u, 3u);
    log.endDraw(0x00000300u, 100u);
    log.endFrame(0x00000100u);

    TEST_ASSERT_EQUAL_STRING("FRAME,i=0,tic=3,render_us=1024,present_us=0,free_heap=100\n",
                             g_output);
}

// One closed frame, one line; the index advances once per line and the fields
// stay in order across records.
void test_each_closed_frame_emits_one_indexed_line(void) {
    FrameLog log(captureLine);

    log.beginFrame(100u, 1u);
    log.endDraw(200u, 10u);
    log.endFrame(300u);

    log.beginFrame(400u, 2u);
    log.endDraw(600u, 20u);
    log.endFrame(800u);

    TEST_ASSERT_EQUAL_STRING(
        "FRAME,i=0,tic=1,render_us=100,present_us=100,free_heap=10\n"
        "FRAME,i=1,tic=2,render_us=200,present_us=200,free_heap=20\n",
        g_output);
}

// Closing a frame that was never opened is a no-op: no phantom line, no index
// movement behind the harness's back.
void test_end_frame_without_an_open_frame_emits_nothing(void) {
    FrameLog log(captureLine);

    log.endFrame(500u);
    TEST_ASSERT_EQUAL_STRING("", g_output);

    log.beginFrame(600u, 4u);
    log.endDraw(700u, 30u);
    log.endFrame(900u);

    TEST_ASSERT_EQUAL_STRING("FRAME,i=0,tic=4,render_us=100,present_us=200,free_heap=30\n",
                             g_output);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_csv_field_order_and_format);
    RUN_TEST(test_zero_length_frame_is_accepted);
    RUN_TEST(test_counters_and_heap_are_reported_unsigned);
    RUN_TEST(test_present_never_underflows_when_render_exceeds_period);
    RUN_TEST(test_each_closed_frame_emits_one_indexed_line);
    RUN_TEST(test_end_frame_without_an_open_frame_emits_nothing);

    return UNITY_END();
}
