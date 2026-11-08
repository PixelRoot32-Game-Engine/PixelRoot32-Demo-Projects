/**
 * @brief The record banner texts: the book on demand, and the "new best" line.
 *
 * Pinned here: an empty book says so, a full one shows every field it has,
 * each improvement variant (and the priority when several rose at once), the
 * numbers at their caps, the improved() helper that tells which field rose
 * without changing merge, and that a buffer too small is refused with an empty
 * string rather than a truncated one.
 */
#include <unity.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "game/rules/Contract.h"
#include "game/rules/Records.h"
#include "game/rules/RecordsText.h"

namespace rc = top_down_city::records;
namespace rt = top_down_city::records_text;
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

void test_empty_book_says_no_records() {
    char buf[rt::kTextCapacity];
    TEST_ASSERT_TRUE(rt::bookLine(rc::clear(), buf, sizeof buf) > 0);
    TEST_ASSERT_EQUAL_STRING("NO RECORDS YET", buf);
}

void test_book_with_streak_and_cash_only() {
    char buf[rt::kTextCapacity];
    rt::bookLine(make(7, 0, 1234), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BEST STREAK 7  $1234", buf);
}

void test_book_with_chapters_appends_the_count() {
    char buf[rt::kTextCapacity];
    rt::bookLine(make(3, bit(ct::Chapter::Boost) | bit(ct::Chapter::Hit), 50),
                 buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BEST STREAK 3  $50  CH 2/3", buf);
}

void test_book_with_only_a_chapter_is_not_empty() {
    char buf[rt::kTextCapacity];
    rt::bookLine(make(0, bit(ct::Chapter::Frenzy), 0), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BEST STREAK 0  $0  CH 1/3", buf);
}

void test_book_at_the_caps() {
    char buf[rt::kTextCapacity];
    rt::bookLine(make(255, 7, 9999), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("BEST STREAK 255  $9999  CH 3/3", buf);
    TEST_ASSERT_TRUE(std::strlen(buf) < rt::kTextCapacity);
}

void test_improved_reports_each_field() {
    const rc::Record base = make(5, bit(ct::Chapter::Boost), 100);
    rc::Improved a = rc::improved(base, make(6, bit(ct::Chapter::Boost), 100));
    TEST_ASSERT_TRUE(a.streak);
    TEST_ASSERT_FALSE(a.cash);
    TEST_ASSERT_FALSE(a.chapter);
    rc::Improved b = rc::improved(base, make(5, bit(ct::Chapter::Boost), 101));
    TEST_ASSERT_TRUE(b.cash);
    TEST_ASSERT_FALSE(b.streak);
    rc::Improved c = rc::improved(base, make(5, 3, 100));
    TEST_ASSERT_TRUE(c.chapter);
    TEST_ASSERT_FALSE(c.any() && c.streak);
}

void test_improved_none_when_equal_or_lower() {
    const rc::Record base = make(5, 3, 100);
    TEST_ASSERT_FALSE(rc::improved(base, make(5, 3, 100)).any());
    TEST_ASSERT_FALSE(rc::improved(base, make(1, 1, 1)).any());
}

void test_improved_agrees_with_merge() {
    rc::Record book = make(5, 1, 100);
    const rc::Record seen = make(5, 1, 400);
    const bool any = rc::improved(book, seen).any();
    TEST_ASSERT_EQUAL(rc::merge(book, seen), any);
}

void test_new_best_streak() {
    char buf[rt::kTextCapacity];
    rc::Improved imp = rc::Improved{};
    imp.streak = true;
    rt::improvedLine(make(7, 0, 1234), imp, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("NEW BEST STREAK 7", buf);
}

void test_new_best_cash() {
    char buf[rt::kTextCapacity];
    rc::Improved imp = rc::Improved{};
    imp.cash = true;
    rt::improvedLine(make(7, 0, 1234), imp, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("NEW BEST $1234", buf);
}

void test_new_chapter() {
    char buf[rt::kTextCapacity];
    rc::Improved imp = rc::Improved{};
    imp.chapter = true;
    rt::improvedLine(make(7, bit(ct::Chapter::Boost) | bit(ct::Chapter::Hit), 9),
                     imp, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("CHAPTER 2/3 CLEARED", buf);
}

void test_several_fields_chapter_beats_streak_beats_cash() {
    char buf[rt::kTextCapacity];
    rc::Improved all = rc::Improved{};
    all.streak = all.cash = all.chapter = true;
    rt::improvedLine(make(7, 1, 20), all, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("CHAPTER 1/3 CLEARED", buf);
    all.chapter = false;
    rt::improvedLine(make(7, 1, 20), all, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("NEW BEST STREAK 7", buf);
}

void test_improved_line_at_the_caps() {
    char buf[rt::kTextCapacity];
    rc::Improved imp = rc::Improved{};
    imp.streak = true;
    rt::improvedLine(make(255, 0, 9999), imp, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("NEW BEST STREAK 255", buf);
    imp.streak = false;
    imp.cash = true;
    rt::improvedLine(make(255, 0, 9999), imp, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("NEW BEST $9999", buf);
}

void test_nothing_improved_gives_empty_text() {
    char buf[rt::kTextCapacity];
    std::memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_UINT32(0, rt::improvedLine(make(1, 1, 1), rc::Improved{},
                                                 buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_too_small_buffer_is_refused_with_empty_text() {
    char buf[8];
    std::memset(buf, 'x', sizeof buf);
    TEST_ASSERT_EQUAL_UINT32(0, rt::bookLine(make(255, 7, 9999), buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("", buf);
    std::memset(buf, 'x', sizeof buf);
    rc::Improved imp = rc::Improved{};
    imp.streak = true;
    TEST_ASSERT_EQUAL_UINT32(
        0, rt::improvedLine(make(255, 0, 0), imp, buf, 5));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_exact_fit_buffer_works_and_null_or_zero_is_safe() {
    char buf[15];  // "NO RECORDS YET" + NUL
    TEST_ASSERT_EQUAL_UINT32(14, rt::bookLine(rc::clear(), buf, sizeof buf));
    TEST_ASSERT_EQUAL_STRING("NO RECORDS YET", buf);
    TEST_ASSERT_EQUAL_UINT32(0, rt::bookLine(rc::clear(), nullptr, 10));
    TEST_ASSERT_EQUAL_UINT32(0, rt::bookLine(rc::clear(), buf, 0));
}

void test_capacity_holds_the_longest_text() {
    char buf[rt::kTextCapacity];
    TEST_ASSERT_TRUE(rt::bookLine(make(255, 7, 9999), buf, sizeof buf) > 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_book_says_no_records);
    RUN_TEST(test_book_with_streak_and_cash_only);
    RUN_TEST(test_book_with_chapters_appends_the_count);
    RUN_TEST(test_book_with_only_a_chapter_is_not_empty);
    RUN_TEST(test_book_at_the_caps);
    RUN_TEST(test_improved_reports_each_field);
    RUN_TEST(test_improved_none_when_equal_or_lower);
    RUN_TEST(test_improved_agrees_with_merge);
    RUN_TEST(test_new_best_streak);
    RUN_TEST(test_new_best_cash);
    RUN_TEST(test_new_chapter);
    RUN_TEST(test_several_fields_chapter_beats_streak_beats_cash);
    RUN_TEST(test_improved_line_at_the_caps);
    RUN_TEST(test_nothing_improved_gives_empty_text);
    RUN_TEST(test_too_small_buffer_is_refused_with_empty_text);
    RUN_TEST(test_exact_fit_buffer_works_and_null_or_zero_is_safe);
    RUN_TEST(test_capacity_holds_the_longest_text);
    return UNITY_END();
}
