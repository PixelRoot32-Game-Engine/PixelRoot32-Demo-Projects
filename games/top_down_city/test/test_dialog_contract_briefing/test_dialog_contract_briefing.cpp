/**
 * @brief The payphone briefing: what the job is, before the clock starts.
 *
 * Answering a phone used to start the chapter on the spot, with a single
 * banner as the whole explanation. Each chapter now opens a short dialog
 * -- two text lines plus an ACCEPT / HANG UP choice -- and the chapter
 * itself starts only on ACCEPT. These tests pin the adapter's half of
 * that: what each chapter says, how the choice is answered, and that each
 * decision is reported exactly once.
 */
#include <unity.h>

#include <cstdint>
#include <cstring>

#include "game/dialog/ContractDialog.h"
#include "game/rules/Contract.h"

using top_down_city::ContractDialog;
namespace contract = top_down_city::contract;
namespace dlg = pixelroot32::gameplay;

void setUp() {}
void tearDown() {}

namespace {

/// Walk the two briefing lines so the runner sits on the choice.
void reachChoice(ContractDialog& dialog) {
    dialog.advance();
    dialog.advance();
}

/// The visible text of the current line, or nullptr when nothing is up.
const char* shown(ContractDialog& dialog) {
    const dlg::DialogLine* line = dialog.runner().currentLine();
    return line != nullptr ? line->text : nullptr;
}

void openChapter(ContractDialog& dialog, contract::Chapter chapter) {
    dialog.open(chapter);
    TEST_ASSERT_TRUE(dialog.isOpen());
}

}  // namespace

// --- Opening and closing -------------------------------------------------

void test_a_new_briefing_is_closed() {
    const ContractDialog dialog;
    TEST_ASSERT_FALSE(dialog.isOpen());
}

void test_open_starts_the_chapters_briefing() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(contract::Chapter::Boost),
        static_cast<std::uint8_t>(dialog.chapter()));
    TEST_ASSERT_NOT_NULL(shown(dialog));
}

void test_opening_past_the_story_opens_nothing() {
    ContractDialog dialog;
    dialog.open(contract::Chapter::Count);
    TEST_ASSERT_FALSE(dialog.isOpen());
}

void test_close_hangs_up() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    dialog.close();
    TEST_ASSERT_FALSE(dialog.isOpen());
}

void test_reopening_restarts_the_briefing() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    reachChoice(dialog);
    dialog.open(contract::Chapter::Hit);
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(contract::Chapter::Hit),
        static_cast<std::uint8_t>(dialog.chapter()));
    TEST_ASSERT_NOT_NULL(shown(dialog));
}

// --- What each chapter says ----------------------------------------------

void test_each_chapter_briefs_its_own_job() {
    ContractDialog dialog;
    const char* firstLines[3] = {nullptr, nullptr, nullptr};
    const contract::Chapter chapters[3] = {
        contract::Chapter::Boost,
        contract::Chapter::Hit,
        contract::Chapter::Frenzy,
    };
    for (int i = 0; i < 3; ++i) {
        dialog.open(chapters[i]);
        firstLines[i] = shown(dialog);
        TEST_ASSERT_NOT_NULL(firstLines[i]);
        dialog.close();
    }
    TEST_ASSERT_TRUE(std::strcmp(firstLines[0], firstLines[1]) != 0);
    TEST_ASSERT_TRUE(std::strcmp(firstLines[0], firstLines[2]) != 0);
    TEST_ASSERT_TRUE(std::strcmp(firstLines[1], firstLines[2]) != 0);
}

void test_the_boost_names_the_car_the_drop_and_the_pay() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    TEST_ASSERT_NOT_NULL(std::strstr(shown(dialog), "Marina"));
    dialog.advance();
    const char* detail = shown(dialog);
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "Suburbs"));
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "$100"));
}

void test_the_hit_names_the_station_and_the_retreat() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Hit);
    TEST_ASSERT_NOT_NULL(std::strstr(shown(dialog), "lobby"));
    dialog.advance();
    const char* detail = shown(dialog);
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "stars"));
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "$125"));
}

void test_the_frenzy_names_the_count_and_the_shotgun() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Frenzy);
    TEST_ASSERT_NOT_NULL(std::strstr(shown(dialog), "corner"));
    dialog.advance();
    const char* detail = shown(dialog);
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "12 bodies"));
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "shotgun"));
    TEST_ASSERT_NOT_NULL(std::strstr(detail, "$200"));
}

// --- Answering the phone ---------------------------------------------------

void test_the_choice_offers_accept_or_hang_up() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    reachChoice(dialog);
    TEST_ASSERT_EQUAL_UINT8(2, dialog.runner().choiceCount());
    TEST_ASSERT_EQUAL_STRING("ACCEPT",
                             dialog.runner().choice(0)->text);
    TEST_ASSERT_EQUAL_STRING("HANG UP",
                             dialog.runner().choice(1)->text);
}

void test_accept_reports_accepted_exactly_once() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    reachChoice(dialog);
    dialog.advance();  // ACCEPT is highlighted first
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Accepted);
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
}

void test_hang_up_reports_declined_exactly_once() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Hit);
    reachChoice(dialog);
    dialog.navigate(false, true);  // down to HANG UP
    dialog.advance();
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Declined);
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
}

void test_accept_ends_the_runner_with_the_verdict_still_pending() {
    // The contract the scene depends on: confirming the last line's
    // choice finishes the runner (isOpen goes false), and the decision
    // is still owed by takeVerdict(). Gating the serving site on isOpen()
    // would drop every ACCEPT on the floor.
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    reachChoice(dialog);
    dialog.advance();  // ACCEPT is highlighted first
    TEST_ASSERT_FALSE(dialog.isOpen());
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Accepted);
}

void test_hang_up_ends_the_runner_with_the_verdict_still_pending() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Hit);
    reachChoice(dialog);
    dialog.navigate(false, true);  // down to HANG UP
    dialog.advance();
    TEST_ASSERT_FALSE(dialog.isOpen());
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Declined);
}

void test_cancel_on_the_choice_hangs_up() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Frenzy);
    reachChoice(dialog);
    dialog.cancel();
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Declined);
}

void test_cancel_on_a_text_line_answers_nothing() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    dialog.cancel();
    TEST_ASSERT_TRUE(dialog.isOpen());
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
}

void test_no_decision_before_the_choice() {
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
    dialog.advance();
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
}

void test_up_at_the_top_stays_on_accept() {
    // The runner clamps rather than wraps; hanging up must be a walk
    // down, never an accidental walk up.
    ContractDialog dialog;
    openChapter(dialog, contract::Chapter::Boost);
    reachChoice(dialog);
    dialog.navigate(true, false);
    dialog.advance();
    TEST_ASSERT_TRUE(dialog.takeVerdict()
                     == ContractDialog::Verdict::Accepted);
}

void test_advancing_a_closed_briefing_does_nothing() {
    ContractDialog dialog;
    const std::uint16_t before = dialog.revision();
    dialog.advance();
    dialog.navigate(false, true);
    dialog.cancel();
    TEST_ASSERT_FALSE(dialog.isOpen());
    TEST_ASSERT_EQUAL_UINT16(before, dialog.revision());
    TEST_ASSERT_TRUE(dialog.takeVerdict() == ContractDialog::Verdict::None);
}

// --- What the frame skip sees ----------------------------------------------

void test_opening_advancing_and_closing_move_the_revision() {
    ContractDialog dialog;
    std::uint16_t before = dialog.revision();
    dialog.open(contract::Chapter::Boost);
    TEST_ASSERT_TRUE(dialog.revision() != before);
    before = dialog.revision();
    dialog.advance();
    TEST_ASSERT_TRUE(dialog.revision() != before);
    before = dialog.revision();
    dialog.close();
    TEST_ASSERT_TRUE(dialog.revision() != before);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_new_briefing_is_closed);
    RUN_TEST(test_open_starts_the_chapters_briefing);
    RUN_TEST(test_opening_past_the_story_opens_nothing);
    RUN_TEST(test_close_hangs_up);
    RUN_TEST(test_reopening_restarts_the_briefing);
    RUN_TEST(test_each_chapter_briefs_its_own_job);
    RUN_TEST(test_the_boost_names_the_car_the_drop_and_the_pay);
    RUN_TEST(test_the_hit_names_the_station_and_the_retreat);
    RUN_TEST(test_the_frenzy_names_the_count_and_the_shotgun);
    RUN_TEST(test_the_choice_offers_accept_or_hang_up);
    RUN_TEST(test_accept_reports_accepted_exactly_once);
    RUN_TEST(test_hang_up_reports_declined_exactly_once);
    RUN_TEST(test_accept_ends_the_runner_with_the_verdict_still_pending);
    RUN_TEST(test_hang_up_ends_the_runner_with_the_verdict_still_pending);
    RUN_TEST(test_cancel_on_the_choice_hangs_up);
    RUN_TEST(test_cancel_on_a_text_line_answers_nothing);
    RUN_TEST(test_no_decision_before_the_choice);
    RUN_TEST(test_up_at_the_top_stays_on_accept);
    RUN_TEST(test_advancing_a_closed_briefing_does_nothing);
    RUN_TEST(test_opening_advancing_and_closing_move_the_revision);
    return UNITY_END();
}
