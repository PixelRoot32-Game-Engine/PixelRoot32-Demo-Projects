/*
 * Behaviour tests for the DOOM port's flash-read profile (D2.5).
 *
 * The suite covers the arithmetic and the lines, not the board: `FlashProfile`
 * is engine-free and Arduino-free by construction, so a host test can fold
 * synthetic reads into an accumulator and assert exactly what the boot log
 * would say. Every expectation below is written out in full -- the exact line a
 * parser reads -- rather than derived from the implementation, so a formatting
 * change cannot pass by agreeing with itself.
 *
 * The cases, and why each one exists:
 *
 *  - a zero-read pass is legal: min/max are 0, throughput is 0 (not a division
 *    by zero) and the line still has every field;
 *  - counts, totals, min and max are exact, including a min/max that is not the
 *    first or the last read;
 *  - the >1 ms and >4 ms buckets are strictly above their edges, so a read of
 *    exactly 1,000 us or exactly 4,000 us is in neither bucket;
 *  - the histogram buckets partition every elapsed time, and their sum is the
 *    read count;
 *  - throughput is integer tenths of a mebibyte per second, and 0 when nothing
 *    was timed;
 *  - the ratio and the frame budget are integer per-mille and per-tenth-percent
 *    arithmetic, with the zero denominator answered as 0;
 *  - the record set is bounded: reads past the capacity still reach the totals
 *    and are counted as dropped;
 *  - a failed read (0 bytes, counted by the caller) still prints and still
 *    contributes its elapsed time;
 *  - the disabled gate emits nothing and the enabled gate emits exactly one
 *    line, from the same binary. This environment deliberately does not define
 *    PIXELROOT32_DOOM_FLASH_PROFILE, so `kFlashProfileEnabled` is false here and
 *    the disabled form is the one a shipped build compiles.
 */
#include <cstddef>
#include <cstdint>

#include <unity.h>

#include "doomc/FlashProfile.h"

#include "../../test_config.h"

using doom::doomc::FlashProfileAccumulator;
using doom::doomc::FlashProfileReport;
using doom::doomc::FlashProfileStats;

namespace {

/// Fixed capture buffer: nothing here allocates, so the test must not either.
/// Two maximum-width lines (under 320 bytes each) fit with room to spare.
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

// A pass with nothing in it reports zeroes and claims no throughput. It must not
// divide by zero, and it must still print every field: a parser reading a boot
// log where no lump resolved has to see the same shape as a full pass.
void test_zero_reads_reports_zeroes_and_no_throughput(void) {
    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);

    TEST_ASSERT_EQUAL_UINT32(0u, accumulator.stats.reads);
    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::flashProfileMiBTenths(0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::flashProfileMiBTenths(4096u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::flashProfileRatioPermille(0u, 0u));
    TEST_ASSERT_EQUAL_UINT32(0u, doom::doomc::flashProfileFramePercentTenths(0u, 0u));
    TEST_ASSERT_EQUAL_INT(-1, doom::doomc::flashProfileSlowestRecordIndex(accumulator));

    char line[doom::doomc::kFlashProfileLineCapacity];
    const std::size_t length = doom::doomc::flashProfileFormatPass(accumulator, "cold", "all", line,
                                                                   sizeof line);
    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "WADPROF,pass=cold,phase=all,total_us=0,lumps=0,bytes=0,mbps=0.0,min_us=0,max_us=0,"
        "gt1ms=0,gt4ms=0,slowest_lump=-1,slowest_us=0,records=0,dropped=0,failed=0\n",
        line);
}

// Counts, totals, min and max, over reads whose extremes are in the middle of
// the sequence rather than at its ends.
void test_counts_totals_min_and_max(void) {
    FlashProfileStats stats;
    doom::doomc::flashProfileReset(&stats);

    doom::doomc::flashProfileRecord(&stats, 1000u, 5u);
    doom::doomc::flashProfileRecord(&stats, 2000u, 7u);
    doom::doomc::flashProfileRecord(&stats, 3000u, 3u);

    TEST_ASSERT_EQUAL_UINT32(3u, stats.reads);
    TEST_ASSERT_EQUAL_UINT64(6000u, stats.totalBytes);
    TEST_ASSERT_EQUAL_UINT64(15u, stats.totalMicros);
    TEST_ASSERT_EQUAL_UINT32(3u, stats.minMicros);
    TEST_ASSERT_EQUAL_UINT32(7u, stats.maxMicros);
    // 3, 5 and 7 us are all inside the first (0..10 us) bucket.
    TEST_ASSERT_EQUAL_UINT32(3u, stats.histogram[0]);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.histogram[1]);
}

// >= 1 ms is not "above 1 ms". Exactly 1000 us and exactly 4000 us land in
// neither bucket; one microsecond later lands in both.
void test_one_and_four_millisecond_buckets_are_strictly_above(void) {
    FlashProfileStats stats;
    doom::doomc::flashProfileReset(&stats);

    doom::doomc::flashProfileRecord(&stats, 1u, 999u);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.over1ms);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.over4ms);

    doom::doomc::flashProfileRecord(&stats, 1u, 1000u);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.over1ms);

    doom::doomc::flashProfileRecord(&stats, 1u, 1001u);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.over1ms);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.over4ms);

    doom::doomc::flashProfileRecord(&stats, 1u, 4000u);
    TEST_ASSERT_EQUAL_UINT32(2u, stats.over1ms);
    TEST_ASSERT_EQUAL_UINT32(0u, stats.over4ms);

    doom::doomc::flashProfileRecord(&stats, 1u, 4001u);
    TEST_ASSERT_EQUAL_UINT32(3u, stats.over1ms);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.over4ms);

    TEST_ASSERT_EQUAL_UINT32(5u, stats.reads);
}

// The histogram is a partition: every duration has exactly one bucket, and the
// bucket counts add up to the read count.
void test_histogram_buckets_partition_every_duration(void) {
    using doom::doomc::flashProfileBin;

    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileBin(0u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileBin(10u));
    TEST_ASSERT_EQUAL_UINT32(1u, flashProfileBin(11u));
    TEST_ASSERT_EQUAL_UINT32(1u, flashProfileBin(50u));
    TEST_ASSERT_EQUAL_UINT32(2u, flashProfileBin(51u));
    TEST_ASSERT_EQUAL_UINT32(2u, flashProfileBin(100u));
    TEST_ASSERT_EQUAL_UINT32(3u, flashProfileBin(101u));
    TEST_ASSERT_EQUAL_UINT32(3u, flashProfileBin(250u));
    TEST_ASSERT_EQUAL_UINT32(4u, flashProfileBin(251u));
    TEST_ASSERT_EQUAL_UINT32(4u, flashProfileBin(1000u));
    TEST_ASSERT_EQUAL_UINT32(5u, flashProfileBin(1001u));
    TEST_ASSERT_EQUAL_UINT32(5u, flashProfileBin(4000u));
    TEST_ASSERT_EQUAL_UINT32(6u, flashProfileBin(4001u));
    TEST_ASSERT_EQUAL_UINT32(6u, flashProfileBin(0xFFFFFFFFu));

    FlashProfileStats stats;
    doom::doomc::flashProfileReset(&stats);
    doom::doomc::flashProfileRecord(&stats, 1u, 4u);
    doom::doomc::flashProfileRecord(&stats, 1u, 60u);
    doom::doomc::flashProfileRecord(&stats, 1u, 3000u);

    std::uint32_t total = 0u;
    for (int index = 0; index < doom::doomc::kFlashProfileHistogramBins; ++index) {
        total += stats.histogram[index];
    }
    TEST_ASSERT_EQUAL_UINT32(stats.reads, total);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.histogram[0]);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.histogram[2]);
    TEST_ASSERT_EQUAL_UINT32(1u, stats.histogram[5]);
}

// Throughput is tenths of a mebibyte per second, integer only: one mebibyte in
// one second is 1.0, and 68,168 B in 1 ms is 65.0.
void test_throughput_is_tenths_of_a_mebibyte_per_second(void) {
    using doom::doomc::flashProfileMiBTenths;

    TEST_ASSERT_EQUAL_UINT32(10u, flashProfileMiBTenths(1048576u, 1000000u));
    TEST_ASSERT_EQUAL_UINT32(20u, flashProfileMiBTenths(2097152u, 1000000u));
    TEST_ASSERT_EQUAL_UINT32(5u, flashProfileMiBTenths(1048576u, 2000000u));
    TEST_ASSERT_EQUAL_UINT32(650u, flashProfileMiBTenths(68168u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileMiBTenths(0u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileMiBTenths(68168u, 0u));
}

// The ratio is per-mille, so a warm pass that costs half the cold one is 500,
// and a warm pass that costs more than the cold one is reported above 1000.
void test_ratio_is_per_mille_and_zero_when_nothing_was_measured(void) {
    using doom::doomc::flashProfileRatioPermille;

    TEST_ASSERT_EQUAL_UINT32(500u, flashProfileRatioPermille(500u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(1000u, flashProfileRatioPermille(1000u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(1250u, flashProfileRatioPermille(1250u, 1000u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileRatioPermille(250u, 0u));
}

// The budget line is the cold total in tenths of a percent of a 35 Hz frame.
void test_frame_budget_is_tenths_of_a_percent(void) {
    using doom::doomc::flashProfileFramePercentTenths;

    TEST_ASSERT_EQUAL_UINT32(500u, flashProfileFramePercentTenths(14285u, 28570u));
    TEST_ASSERT_EQUAL_UINT32(1000u, flashProfileFramePercentTenths(28570u, 28570u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileFramePercentTenths(0u, 28570u));
    TEST_ASSERT_EQUAL_UINT32(0u, flashProfileFramePercentTenths(14285u, 0u));
}

// The record set is bounded: past the capacity the totals keep counting the read
// and `dropped` says the detail is gone. recorded + dropped is always reads.
void test_record_set_is_bounded_and_counts_drops(void) {
    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);

    const int extra = 5;
    const int total = doom::doomc::kFlashProfileRecordCapacity + extra;
    for (int index = 0; index < total; ++index) {
        // Lump index and elapsed time both vary, so a wrong record cannot pass
        // by matching a constant.
        doom::doomc::flashProfileRecord(&accumulator, index, 100u, static_cast<std::uint32_t>(index));
    }

    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(total), accumulator.stats.reads);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(doom::doomc::kFlashProfileRecordCapacity),
                             accumulator.recorded);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(extra), accumulator.dropped);
    TEST_ASSERT_EQUAL_UINT32(accumulator.stats.reads, accumulator.recorded + accumulator.dropped);

    // The last record that fit is the capacity-th read, and it is the slowest.
    const int slowest = doom::doomc::flashProfileSlowestRecordIndex(accumulator);
    TEST_ASSERT_EQUAL_INT(doom::doomc::kFlashProfileRecordCapacity - 1, slowest);
    TEST_ASSERT_EQUAL_INT(doom::doomc::kFlashProfileRecordCapacity - 1,
                          accumulator.records[slowest].lump);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(doom::doomc::kFlashProfileRecordCapacity - 1),
                             accumulator.records[slowest].micros);
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(total - 1), accumulator.stats.maxMicros);
}

// A read the container refused still costs time: the caller records 0 bytes and
// increments `failed`, and the line says both.
void test_failed_read_costs_time_and_prints_as_zero_bytes(void) {
    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);

    doom::doomc::flashProfileRecord(&accumulator, 7, 4096u, 20u);
    doom::doomc::flashProfileRecord(&accumulator, 8, 0u, 900u);
    ++accumulator.stats.failed;

    char line[doom::doomc::kFlashProfileLineCapacity];
    const std::size_t length = doom::doomc::flashProfileFormatPass(accumulator, "cold", "patches",
                                                                   line, sizeof line);
    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "WADPROF,pass=cold,phase=patches,total_us=920,lumps=2,bytes=4096,mbps=4.2,min_us=20,"
        "max_us=900,gt1ms=0,gt4ms=0,slowest_lump=8,slowest_us=900,records=2,dropped=0,failed=1\n",
        line);
}

// One read of one mebibyte in one second: every field readable without
// arithmetic, so the expected line can be written by hand.
void test_summary_line_is_exact(void) {
    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);
    doom::doomc::flashProfileRecord(&accumulator, 42, 1048576u, 1000000u);

    char line[doom::doomc::kFlashProfileLineCapacity];
    const std::size_t length = doom::doomc::flashProfileFormatPass(accumulator, "cold", "map", line,
                                                                   sizeof line);
    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING(
        "WADPROF,pass=cold,phase=map,total_us=1000000,lumps=1,bytes=1048576,mbps=1.0,min_us=1000000,"
        "max_us=1000000,gt1ms=1,gt4ms=1,slowest_lump=42,slowest_us=1000000,records=1,dropped=0,"
        "failed=0\n",
        line);
}

// The two verdict lines: the warm/cold ratio and the per-frame budget.
void test_ratio_and_budget_lines_are_exact(void) {
    char line[doom::doomc::kFlashProfileLineCapacity];

    std::size_t length = doom::doomc::flashProfileFormatRatio(1000u, 250u, line, sizeof line);
    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING("WADPROF,ratio,cold_us=1000,warm_us=250,warm_permille=250\n", line);

    length = doom::doomc::flashProfileFormatBudget(12345u, 28570u, line, sizeof line);
    TEST_ASSERT_TRUE(length > 0u);
    TEST_ASSERT_EQUAL_STRING("WADPROF,budget,frame_us=28570,cold_us=12345,pct_x10=432\n", line);
}

// A line that does not fit reports 0 and leaves nothing half-written to sink.
void test_truncated_line_reports_zero(void) {
    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);

    char small[16];
    const std::size_t length = doom::doomc::flashProfileFormatPass(accumulator, "cold", "all", small,
                                                                   sizeof small);
    TEST_ASSERT_TRUE(length == 0u);
}

// The gate is compile-time: the disabled form formats nothing and calls no sink,
// while the enabled form -- the one the probe instantiates when the flag is set
// -- writes exactly one line. One binary observes both.
void test_disabled_build_reports_nothing_and_enabled_build_reports_once(void) {
    // This environment does not define PIXELROOT32_DOOM_FLASH_PROFILE, so the
    // build under test is the disabled one.
    TEST_ASSERT_FALSE(doom::doomc::kFlashProfileEnabled);

    FlashProfileAccumulator accumulator;
    doom::doomc::flashProfileReset(&accumulator);
    doom::doomc::flashProfileRecord(&accumulator, 3, 100u, 10u);

    TEST_ASSERT_EQUAL_UINT32(0u, FlashProfileReport<false>::pass(accumulator, "cold", "all",
                                                                 captureLine));
    TEST_ASSERT_EQUAL_STRING("", g_output);
    TEST_ASSERT_EQUAL_size_t(0u, g_length);

    // The same read, through the enabled form: one line, exactly the record a
    // boot log carries.
    TEST_ASSERT_EQUAL_UINT32(1u, FlashProfileReport<true>::pass(accumulator, "cold", "all",
                                                                captureLine));
    TEST_ASSERT_EQUAL_STRING(
        "WADPROF,pass=cold,phase=all,total_us=10,lumps=1,bytes=100,mbps=9.5,min_us=10,max_us=10,"
        "gt1ms=0,gt4ms=0,slowest_lump=3,slowest_us=10,records=1,dropped=0,failed=0\n",
        g_output);

    // A null sink is not a crash and not a line.
    resetCapture();
    TEST_ASSERT_EQUAL_UINT32(0u, FlashProfileReport<true>::pass(accumulator, "cold", "all",
                                                                nullptr));
    TEST_ASSERT_EQUAL_STRING("", g_output);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    test_setup();
    UNITY_BEGIN();

    RUN_TEST(test_zero_reads_reports_zeroes_and_no_throughput);
    RUN_TEST(test_counts_totals_min_and_max);
    RUN_TEST(test_one_and_four_millisecond_buckets_are_strictly_above);
    RUN_TEST(test_histogram_buckets_partition_every_duration);
    RUN_TEST(test_throughput_is_tenths_of_a_mebibyte_per_second);
    RUN_TEST(test_ratio_is_per_mille_and_zero_when_nothing_was_measured);
    RUN_TEST(test_frame_budget_is_tenths_of_a_percent);
    RUN_TEST(test_record_set_is_bounded_and_counts_drops);
    RUN_TEST(test_failed_read_costs_time_and_prints_as_zero_bytes);
    RUN_TEST(test_summary_line_is_exact);
    RUN_TEST(test_ratio_and_budget_lines_are_exact);
    RUN_TEST(test_truncated_line_reports_zero);
    RUN_TEST(test_disabled_build_reports_nothing_and_enabled_build_reports_once);

    return UNITY_END();
}
