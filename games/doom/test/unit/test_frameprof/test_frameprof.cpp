/*
 * Behaviour tests for the DOOM port's frame-driver arithmetic (D3.6).
 *
 * The suite covers the arithmetic and the lines, not the board: `FrameDriver`'s
 * first half is engine-free, Arduino-free and esp_timer-free by construction, so
 * a host test can fold synthetic frames into an accumulator and assert exactly
 * what the boot log would say. Every expectation below is written out in full --
 * the exact line a parser reads -- rather than derived from the implementation,
 * so a formatting change cannot pass by agreeing with itself.
 *
 * The cases, and why each one exists:
 *
 *  - the mean, the minimum and the maximum over N frames, with the extremes in
 *    the middle of the sequence rather than at its ends;
 *  - the summary line field for field, including `other_us`, the part of the
 *    frame that no pass accounted for;
 *  - a zero-sample summary: every mean is 0, the per-mille is 0 instead of a
 *    division by zero, and the verdict is `none` -- a summary with no sample
 *    must not report a pass it did not measure;
 *  - the gate boundary at exactly 20,000 us: 19,999 passes, 20,000 passes (it is
 *    a budget, not a threshold to beat by one) and 20,001 fails, both from the
 *    predicate and from the formatted line;
 *  - the frame line field for field, and the per-phase breakdown line;
 *  - a line that does not fit its buffer is reported as 0 and left empty, never
 *    written truncated;
 *  - the disabled gate writes nothing and calls no sink; the enabled gate, in the
 *    same binary, writes exactly one line.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <unity.h>

#include "doomc/FrameDriver.h"

#include "../../test_config.h"

using doom::doomc::FrameProfileReport;
using doom::doomc::FrameProfileSample;
using doom::doomc::FrameProfileStats;

namespace {

/// Fixed capture buffer: nothing here allocates, so the test must not either.
/// Two maximum-width lines (under 512 bytes each) fit with room to spare.
char g_output[1024];
std::size_t g_length = 0;
int g_calls = 0;

void captureLine(const char* line) {
    ++g_calls;
    while (*line != '\0' && g_length + 1 < sizeof g_output) {
        g_output[g_length] = *line;
        ++g_length;
        ++line;
    }
    g_output[g_length] = '\0';
}

void resetCapture() {
    g_length = 0;
    g_calls = 0;
    g_output[0] = '\0';
}

/// One sample with every field distinct, so a formatter that prints the wrong
/// field cannot pass by coincidence.
FrameProfileSample makeSample(std::uint32_t frameMicros) {
    FrameProfileSample sample;
    sample.frame = 3;
    sample.run = 2;
    sample.walkMicros = 100;
    sample.wallsMicros = 2000;
    sample.planesMicros = 500;
    sample.maskedMicros = 300;
    sample.residencyMicros = 1200;
    sample.frameMicros = frameMicros;
    sample.drawsegs = 39;
    sample.planesDrawn = 16;
    sample.planesAllocated = 20;
    sample.spritesSubmitted = 129;
    sample.spritesDrawn = 26;
    sample.texResolves = 78;
    sample.texDecodes = 72;
    sample.flatResolves = 32;
    sample.flatDecodes = 16;
    sample.spriteResolves = 52;
    sample.spriteDecodes = 52;
    sample.bytesDecoded = 345678u;
    return sample;
}

}  // namespace

void setUp(void) {
    test_setup();
    resetCapture();
}

void tearDown() {
    test_teardown();
}

// Three frames whose extremes are the first and the last, so the accumulator
// cannot pass by keeping only the latest reading.
void test_mean_min_max_over_three_frames(void) {
    FrameProfileStats stats;
    doom::doomc::frameProfileReset(&stats);

    FrameProfileSample first = makeSample(1000u);
    first.walkMicros = 10u;
    first.wallsMicros = 100u;
    first.planesMicros = 5u;
    first.maskedMicros = 50u;
    first.residencyMicros = 300u;
    first.bytesDecoded = 1000u;

    FrameProfileSample second = makeSample(2000u);
    second.walkMicros = 20u;
    second.wallsMicros = 200u;
    second.planesMicros = 6u;
    second.maskedMicros = 60u;
    second.residencyMicros = 300u;
    second.bytesDecoded = 2000u;

    FrameProfileSample third = makeSample(3000u);
    third.walkMicros = 30u;
    third.wallsMicros = 300u;
    third.planesMicros = 7u;
    third.maskedMicros = 70u;
    third.residencyMicros = 300u;
    third.bytesDecoded = 3000u;

    doom::doomc::frameProfileRecord(&stats, first);
    doom::doomc::frameProfileRecord(&stats, second);
    doom::doomc::frameProfileRecord(&stats, third);

    TEST_ASSERT_EQUAL_UINT32(3u, stats.samples);
    TEST_ASSERT_EQUAL_UINT64(6000u, stats.frameSum);
    TEST_ASSERT_EQUAL_UINT32(2000u, doom::doomc::frameProfileMean(stats.frameSum, stats.samples));
    TEST_ASSERT_EQUAL_UINT32(1000u, stats.frameMin);
    TEST_ASSERT_EQUAL_UINT32(3000u, stats.frameMax);
    TEST_ASSERT_EQUAL_UINT32(20u, doom::doomc::frameProfileMean(stats.walkSum, stats.samples));
    TEST_ASSERT_EQUAL_UINT32(200u, doom::doomc::frameProfileMean(stats.wallsSum, stats.samples));
    TEST_ASSERT_EQUAL_UINT32(6u, doom::doomc::frameProfileMean(stats.planesSum, stats.samples));
    TEST_ASSERT_EQUAL_UINT32(60u, doom::doomc::frameProfileMean(stats.maskedSum, stats.samples));
    TEST_ASSERT_EQUAL_UINT64(6000u, stats.bytesSum);
}

// The summary line, field for field. `walk + walls + planes + masked` is 286 us
// of a 2,000 us mean frame, so 1,714 us are the work outside the four passes --
// printed as `other_us` instead of being folded into a phase.
void test_summary_line_is_exact(void) {
    FrameProfileStats stats;
    doom::doomc::frameProfileReset(&stats);

    FrameProfileSample first = makeSample(1000u);
    first.walkMicros = 10u;
    first.wallsMicros = 100u;
    first.planesMicros = 5u;
    first.maskedMicros = 50u;
    first.residencyMicros = 300u;

    FrameProfileSample second = makeSample(2000u);
    second.walkMicros = 20u;
    second.wallsMicros = 200u;
    second.planesMicros = 6u;
    second.maskedMicros = 60u;
    second.residencyMicros = 300u;

    FrameProfileSample third = makeSample(3000u);
    third.walkMicros = 30u;
    third.wallsMicros = 300u;
    third.planesMicros = 7u;
    third.maskedMicros = 70u;
    third.residencyMicros = 300u;

    doom::doomc::frameProfileRecord(&stats, first);
    doom::doomc::frameProfileRecord(&stats, second);
    doom::doomc::frameProfileRecord(&stats, third);

    char line[doom::doomc::kFrameProfileLineCapacity];
    const std::size_t length =
        doom::doomc::frameProfileFormatSummary(stats, 20u, 2u, line, sizeof line);

    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "FRAMEPROF,summary,views=20,runs=2,samples=3,frame_mean_us=2000,frame_min_us=1000,"
        "frame_max_us=3000,walk_us=20,walls_us=200,planes_us=6,masked_us=60,other_us=1714,"
        "residency_us=300,residency_permille=150,gate_us=20000,gate=pass\n",
        line);
}

// A summary with no sample has no mean, no ratio and no verdict. It must not
// divide by zero and must not call itself a pass.
void test_zero_sample_summary_has_no_division_and_no_verdict(void) {
    FrameProfileStats stats;
    doom::doomc::frameProfileReset(&stats);

    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::frameProfileMean(0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::frameProfilePermille(500u, 0u));

    char line[doom::doomc::kFrameProfileLineCapacity];
    const std::size_t length =
        doom::doomc::frameProfileFormatSummary(stats, 20u, 2u, line, sizeof line);

    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "FRAMEPROF,summary,views=20,runs=2,samples=0,frame_mean_us=0,frame_min_us=0,"
        "frame_max_us=0,walk_us=0,walls_us=0,planes_us=0,masked_us=0,other_us=0,residency_us=0,"
        "residency_permille=0,gate_us=20000,gate=none\n",
        line);
}

// The gate is a budget, not a threshold that has to be beaten by one microsecond:
// 19,999 and 20,000 pass, 20,001 fails.
void test_gate_boundary_is_inclusive_at_twenty_thousand(void) {
    TEST_ASSERT_TRUE(doom::doomc::frameProfileGatePass(19999u));
    TEST_ASSERT_TRUE(doom::doomc::frameProfileGatePass(20000u));
    TEST_ASSERT_FALSE(doom::doomc::frameProfileGatePass(20001u));
    TEST_ASSERT_TRUE(doom::doomc::frameProfileGatePass(0u));
}

// The same three boundary values through the line a reader actually sees. The
// phases sum to 50 us, so `other_us` is the frame minus 50, and the per-mille is
// 1,200 / frame in thousandths.
void test_gate_verdict_in_summary_at_the_boundary(void) {
    struct Case {
        std::uint32_t frame;
        const char* verdict;
    };
    const Case cases[] = {{19999u, "pass"}, {20000u, "pass"}, {20001u, "fail"}};

    for (std::size_t index = 0; index < sizeof cases / sizeof cases[0]; ++index) {
        FrameProfileStats stats;
        doom::doomc::frameProfileReset(&stats);

        FrameProfileSample sample = makeSample(cases[index].frame);
        sample.walkMicros = 10u;
        sample.wallsMicros = 20u;
        sample.planesMicros = 10u;
        sample.maskedMicros = 10u;
        sample.residencyMicros = 1200u;
        doom::doomc::frameProfileRecord(&stats, sample);

        char line[doom::doomc::kFrameProfileLineCapacity];
        const std::size_t length =
            doom::doomc::frameProfileFormatSummary(stats, 20u, 2u, line, sizeof line);
        TEST_ASSERT_TRUE(length > 0u);
        TEST_ASSERT_EQUAL_INT(0, std::strncmp(line, "FRAMEPROF,summary,", 18));

        char other[32];
        std::snprintf(other, sizeof other, "other_us=%u,", static_cast<unsigned>(cases[index].frame - 50u));
        TEST_ASSERT_TRUE(std::strstr(line, other) != nullptr);

        const unsigned permille = static_cast<unsigned>(
            (1200ull * 1000ull) / static_cast<unsigned long long>(cases[index].frame));
        char tail[96];
        std::snprintf(tail, sizeof tail, "residency_permille=%u,gate_us=20000,gate=%s\n",
                      permille, cases[index].verdict);
        TEST_ASSERT_TRUE(std::strstr(line, tail) != nullptr);
    }
}

// The frame line, field for field. The hash is a plain string here: the board
// prints 64 hex characters, the formatter never inspects them.
void test_frame_line_is_exact(void) {
    const FrameProfileSample sample = makeSample(2950u);

    char line[doom::doomc::kFrameProfileLineCapacity];
    const std::size_t length =
        doom::doomc::frameProfileFormatFrame(sample, "deadbeef", line, sizeof line);

    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "FRAMEPROF,frame=03,run=2,walk_us=100,walls_us=2000,planes_us=500,masked_us=300,"
        "residency_us=1200,frame_us=2950,drawsegs=39,planes_drawn=16,planes_alloc=20,"
        "sprites_submitted=129,sprites_drawn=26,tex_res=78,tex_dec=72,flat_res=32,flat_dec=16,"
        "sprite_res=52,sprite_dec=52,bytes_decoded=345678,ovf=0,hash=deadbeef\n",
        line);
}

// The breakdown line repeats the five times and adds the decode share, which is
// what makes "this view is decode-bound" readable without arithmetic.
void test_breakdown_line_is_exact(void) {
    const FrameProfileSample sample = makeSample(2950u);

    char line[doom::doomc::kFrameProfileLineCapacity];
    const std::size_t length = doom::doomc::frameProfileFormatBreakdown(sample, line, sizeof line);

    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "FRAMEPROF,breakdown,frame=03,run=2,walk_us=100,walls_us=2000,planes_us=500,"
        "masked_us=300,residency_us=1200,frame_us=2950,residency_permille=406,drawsegs=39\n",
        line);
}

// A line that cannot fit is reported as 0 and left empty: a truncated CSV line
// would be worse than none, because a parser would read half a frame.
void test_line_that_does_not_fit_is_not_written(void) {
    const FrameProfileSample sample = makeSample(2950u);
    char small[16];
    small[0] = 'x';

    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<std::uint32_t>(doom::doomc::frameProfileFormatFrame(
                                     sample, "deadbeef", small, sizeof small)));
    TEST_ASSERT_EQUAL_INT('\0', small[0]);

    small[0] = 'x';
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<std::uint32_t>(
                                     doom::doomc::frameProfileFormatBreakdown(sample, small,
                                                                              sizeof small)));
    TEST_ASSERT_EQUAL_INT('\0', small[0]);

    FrameProfileStats stats;
    doom::doomc::frameProfileReset(&stats);
    small[0] = 'x';
    TEST_ASSERT_EQUAL_UINT32(0u, static_cast<std::uint32_t>(
                                     doom::doomc::frameProfileFormatSummary(stats, 20u, 2u, small,
                                                                            sizeof small)));
    TEST_ASSERT_EQUAL_INT('\0', small[0]);
}

// The shipped build's gate: this environment does not define
// PIXELROOT32_DOOM_FRAME_PROBE, so the disabled form is the one a shipped image
// compiles. It must touch nothing and call no sink.
void test_disabled_report_emits_nothing(void) {
    TEST_ASSERT_FALSE(doom::doomc::kFrameProbeEnabled);

    const FrameProfileSample sample = makeSample(2950u);
    TEST_ASSERT_EQUAL_UINT32(0u, FrameProfileReport<false>::frame(sample, "deadbeef", &captureLine));
    TEST_ASSERT_EQUAL_INT(0, g_calls);
    TEST_ASSERT_EQUAL_STRING("", g_output);
}

// The enabled form, in the same binary, writes exactly one line through the sink.
void test_enabled_report_emits_one_line(void) {
    const FrameProfileSample sample = makeSample(2950u);
    TEST_ASSERT_EQUAL_UINT32(1u, FrameProfileReport<true>::frame(sample, "deadbeef", &captureLine));
    TEST_ASSERT_EQUAL_INT(1, g_calls);
    TEST_ASSERT_EQUAL_STRING(
        "FRAMEPROF,frame=03,run=2,walk_us=100,walls_us=2000,planes_us=500,masked_us=300,"
        "residency_us=1200,frame_us=2950,drawsegs=39,planes_drawn=16,planes_alloc=20,"
        "sprites_submitted=129,sprites_drawn=26,tex_res=78,tex_dec=72,flat_res=32,flat_dec=16,"
        "sprite_res=52,sprite_dec=52,bytes_decoded=345678,ovf=0,hash=deadbeef\n",
        g_output);
}

// A null sink is a caller bug, not a crash: the enabled gate answers 0 and
// writes nothing.
void test_enabled_report_without_a_sink_writes_nothing(void) {
    const FrameProfileSample sample = makeSample(2950u);
    TEST_ASSERT_EQUAL_UINT32(0u, FrameProfileReport<true>::frame(sample, "deadbeef", nullptr));
    TEST_ASSERT_EQUAL_INT(0, g_calls);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mean_min_max_over_three_frames);
    RUN_TEST(test_summary_line_is_exact);
    RUN_TEST(test_zero_sample_summary_has_no_division_and_no_verdict);
    RUN_TEST(test_gate_boundary_is_inclusive_at_twenty_thousand);
    RUN_TEST(test_gate_verdict_in_summary_at_the_boundary);
    RUN_TEST(test_frame_line_is_exact);
    RUN_TEST(test_breakdown_line_is_exact);
    RUN_TEST(test_line_that_does_not_fit_is_not_written);
    RUN_TEST(test_disabled_report_emits_nothing);
    RUN_TEST(test_enabled_report_emits_one_line);
    RUN_TEST(test_enabled_report_without_a_sink_writes_nothing);
    return UNITY_END();
}
