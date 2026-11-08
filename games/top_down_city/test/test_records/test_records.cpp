/**
 * @brief The city's records: best streak, chapters ever completed, best cash.
 *
 * Pinned here: merge is monotone (a value never goes down), it reports an
 * improvement exactly when something rose, and the payload round-trips while
 * anything malformed is refused without touching the caller's record.
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "game/rules/Contract.h"
#include "game/rules/Records.h"

namespace rc = top_down_city::records;
namespace ct = top_down_city::contract;

void setUp() {}
void tearDown() {}

static rc::Record make(std::uint8_t streak, std::uint8_t chapters,
                       std::uint16_t cash) {
    rc::Record r = rc::clear();
    r.bestStreak = streak;
    r.chapters = chapters;
    r.bestCash = cash;
    return r;
}

static std::uint8_t bit(ct::Chapter c) {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(c));
}

static bool same(const rc::Record& a, const rc::Record& b) {
    return a.bestStreak == b.bestStreak && a.chapters == b.chapters &&
           a.bestCash == b.bestCash;
}

void test_clear_records_nothing() {
    const rc::Record r = rc::clear();
    TEST_ASSERT_EQUAL_UINT8(0, r.bestStreak);
    TEST_ASSERT_EQUAL_UINT8(0, r.chapters);
    TEST_ASSERT_EQUAL_UINT16(0, r.bestCash);
}

void test_merge_raises_the_streak_alone() {
    rc::Record r = rc::clear();
    TEST_ASSERT_TRUE(rc::merge(r, make(5, 0, 0)));
    TEST_ASSERT_EQUAL_UINT8(5, r.bestStreak);
    TEST_ASSERT_EQUAL_UINT8(0, r.chapters);
    TEST_ASSERT_EQUAL_UINT16(0, r.bestCash);
}

void test_merge_raises_the_cash_alone() {
    rc::Record r = rc::clear();
    TEST_ASSERT_TRUE(rc::merge(r, make(0, 0, 340)));
    TEST_ASSERT_EQUAL_UINT8(0, r.bestStreak);
    TEST_ASSERT_EQUAL_UINT16(340, r.bestCash);
}

void test_merge_adds_a_chapter_alone() {
    rc::Record r = rc::clear();
    TEST_ASSERT_TRUE(rc::merge(r, make(0, bit(ct::Chapter::Hit), 0)));
    TEST_ASSERT_EQUAL_UINT8(bit(ct::Chapter::Hit), r.chapters);
    TEST_ASSERT_EQUAL_UINT8(0, r.bestStreak);
}

void test_merge_never_lowers_and_reports_no_improvement() {
    rc::Record r = make(7, bit(ct::Chapter::Boost), 500);
    const rc::Record before = r;
    TEST_ASSERT_FALSE(rc::merge(r, make(3, 0, 120)));
    TEST_ASSERT_TRUE(same(before, r));
    // Equal is not an improvement either.
    TEST_ASSERT_FALSE(rc::merge(r, before));
    TEST_ASSERT_TRUE(same(before, r));
    // Merging nothing is a no-op.
    TEST_ASSERT_FALSE(rc::merge(r, rc::clear()));
    TEST_ASSERT_TRUE(same(before, r));
}

void test_merge_mixed_run_keeps_the_higher_of_each() {
    rc::Record r = make(7, 0, 100);
    TEST_ASSERT_TRUE(rc::merge(r, make(2, 0, 900)));
    TEST_ASSERT_EQUAL_UINT8(7, r.bestStreak);
    TEST_ASSERT_EQUAL_UINT16(900, r.bestCash);
}

void test_chapters_accumulate_as_a_union() {
    rc::Record r = rc::clear();
    TEST_ASSERT_TRUE(rc::merge(r, make(0, bit(ct::Chapter::Boost), 0)));
    TEST_ASSERT_TRUE(rc::merge(r, make(0, bit(ct::Chapter::Frenzy), 0)));
    TEST_ASSERT_EQUAL_UINT8(bit(ct::Chapter::Boost) | bit(ct::Chapter::Frenzy),
                            r.chapters);
    // A chapter already held is not news.
    TEST_ASSERT_FALSE(rc::merge(r, make(0, bit(ct::Chapter::Boost), 0)));
}

void test_encode_decode_round_trips() {
    const rc::Record in = make(9, bit(ct::Chapter::Boost) | bit(ct::Chapter::Hit),
                               9999);
    std::uint8_t buf[rc::kPayloadSize];
    TEST_ASSERT_EQUAL_UINT32(rc::kPayloadSize,
                             rc::encode(in, buf, sizeof(buf)));
    rc::Record out = rc::clear();
    TEST_ASSERT_TRUE(rc::decode(buf, rc::kPayloadSize, out));
    TEST_ASSERT_TRUE(same(in, out));
}

void test_payload_is_little_endian() {
    const rc::Record in = make(0, 0, 0x0102);
    std::uint8_t buf[rc::kPayloadSize] = {};
    TEST_ASSERT_EQUAL_UINT32(rc::kPayloadSize,
                             rc::encode(in, buf, sizeof(buf)));
    // Layout: [streak][chapters][cash lo][cash hi]
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[3]);
}

void test_encode_refuses_a_too_small_buffer() {
    std::uint8_t buf[rc::kPayloadSize] = {0xAA, 0xAA, 0xAA, 0xAA};
    TEST_ASSERT_EQUAL_UINT32(0, rc::encode(make(1, 0, 1), buf,
                                           rc::kPayloadSize - 1));
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT32(0, rc::encode(make(1, 0, 1), buf, 0));
}

void test_decode_rejects_short_and_oversized_untouched() {
    std::uint8_t buf[rc::kPayloadSize + 1] = {};
    rc::encode(make(4, bit(ct::Chapter::Hit), 77), buf, sizeof(buf));
    const rc::Record sentinel = make(1, 1, 1);
    rc::Record out = sentinel;
    TEST_ASSERT_FALSE(rc::decode(buf, rc::kPayloadSize - 1, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
    TEST_ASSERT_FALSE(rc::decode(buf, rc::kPayloadSize + 1, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
    TEST_ASSERT_FALSE(rc::decode(buf, 0, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
    TEST_ASSERT_FALSE(rc::decode(nullptr, rc::kPayloadSize, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
}

void test_decode_rejects_out_of_domain_chapters_untouched() {
    std::uint8_t buf[rc::kPayloadSize] = {};
    rc::encode(make(4, 0, 77), buf, sizeof(buf));
    buf[1] = static_cast<std::uint8_t>(
        1u << static_cast<unsigned>(ct::Chapter::Count));
    const rc::Record sentinel = make(1, 1, 1);
    rc::Record out = sentinel;
    TEST_ASSERT_FALSE(rc::decode(buf, rc::kPayloadSize, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
}

void test_decode_rejects_cash_above_the_purse_cap() {
    std::uint8_t buf[rc::kPayloadSize] = {};
    rc::encode(make(0, 0, 0), buf, sizeof(buf));
    buf[2] = 0xFF;
    buf[3] = 0xFF;  // 65535 > economy::kMaxCash
    const rc::Record sentinel = make(1, 1, 1);
    rc::Record out = sentinel;
    TEST_ASSERT_FALSE(rc::decode(buf, rc::kPayloadSize, out));
    TEST_ASSERT_TRUE(same(sentinel, out));
}

void test_schema_version_is_one() {
    TEST_ASSERT_EQUAL_UINT16(1, rc::kSchemaVersion);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_clear_records_nothing);
    RUN_TEST(test_merge_raises_the_streak_alone);
    RUN_TEST(test_merge_raises_the_cash_alone);
    RUN_TEST(test_merge_adds_a_chapter_alone);
    RUN_TEST(test_merge_never_lowers_and_reports_no_improvement);
    RUN_TEST(test_merge_mixed_run_keeps_the_higher_of_each);
    RUN_TEST(test_chapters_accumulate_as_a_union);
    RUN_TEST(test_encode_decode_round_trips);
    RUN_TEST(test_payload_is_little_endian);
    RUN_TEST(test_encode_refuses_a_too_small_buffer);
    RUN_TEST(test_decode_rejects_short_and_oversized_untouched);
    RUN_TEST(test_decode_rejects_out_of_domain_chapters_untouched);
    RUN_TEST(test_decode_rejects_cash_above_the_purse_cap);
    RUN_TEST(test_schema_version_is_one);
    return UNITY_END();
}
