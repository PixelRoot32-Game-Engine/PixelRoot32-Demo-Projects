/*
 * Unit tests for PromotionPicker: the promotion choice as a dialog choice line.
 *
 * What moved to the engine's DialogRunner is the picker's state -- open or
 * not, which pieces, which one was taken. What these tests pin is what the
 * player touches: the same four pieces in the same order, the same cells, and
 * the same rule that a tap anywhere else puts the pawn back.
 */
#include <unity.h>

#include "ChessConstants.h"
#include "chess/ChessRules.h"
#include "dialog/PromotionPicker.h"

using chessdemo::PromotionPicker;
using chess::PieceType;
using TapResult = PromotionPicker::TapResult;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr PieceType kExpected[] = {
    PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight
};

void assertPiece(PieceType expected, PieceType actual) {
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(expected), static_cast<uint8_t>(actual));
}

}  // namespace

// --- Opening -----------------------------------------------------------------

void test_a_new_picker_is_closed(void) {
    const PromotionPicker picker;
    TEST_ASSERT_FALSE(picker.isOpen());
    TEST_ASSERT_EQUAL_UINT8(0, picker.choiceCount());
}

void test_opening_offers_queen_rook_bishop_knight_in_that_order(void) {
    PromotionPicker picker;
    picker.open();
    TEST_ASSERT_TRUE(picker.isOpen());
    TEST_ASSERT_EQUAL_UINT8(chessdemo::kPromoChoiceCount, picker.choiceCount());
    for (uint8_t i = 0; i < picker.choiceCount(); ++i) {
        assertPiece(kExpected[i], picker.pieceAt(i));
    }
}

// --- Geometry ----------------------------------------------------------------

void test_the_cells_sit_where_the_picker_always_drew_them(void) {
    // The formula the draw loop and the hit test each used to spell out.
    for (uint8_t i = 0; i < chessdemo::kPromoChoiceCount; ++i) {
        int x = -1, y = -1, w = -1, h = -1;
        PromotionPicker::cellRect(i, x, y, w, h);
        TEST_ASSERT_EQUAL_INT(chessdemo::kPromoX + 4 + i * chessdemo::kPromoCell, x);
        TEST_ASSERT_EQUAL_INT(chessdemo::kPromoRowY, y);
        TEST_ASSERT_EQUAL_INT(chessdemo::kPromoCell, w);
        TEST_ASSERT_EQUAL_INT(chessdemo::kPromoCell, h);
    }
}

void test_the_panel_is_as_wide_as_its_cells_and_margins(void) {
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(chessdemo::kPromoChoiceCount - 1, x, y, w, h);
    TEST_ASSERT_EQUAL_INT(chessdemo::kPromoX + chessdemo::kPromoWidth - 4, x + w);
    TEST_ASSERT_EQUAL_INT(184, chessdemo::kPromoWidth);
}

// --- Choosing ----------------------------------------------------------------

void test_a_tap_on_a_cell_chooses_that_piece_and_closes(void) {
    for (uint8_t i = 0; i < chessdemo::kPromoChoiceCount; ++i) {
        PromotionPicker picker;
        picker.open();
        int x = 0, y = 0, w = 0, h = 0;
        PromotionPicker::cellRect(i, x, y, w, h);

        PieceType chosen = PieceType::Pawn;
        TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Chosen),
                                static_cast<uint8_t>(picker.tap(x + w / 2, y + h / 2, chosen)));
        assertPiece(kExpected[i], chosen);
        TEST_ASSERT_FALSE(picker.isOpen());
    }
}

void test_a_cell_includes_its_top_left_edge(void) {
    PromotionPicker picker;
    picker.open();
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(1, x, y, w, h);
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Chosen),
                            static_cast<uint8_t>(picker.tap(x, y, chosen)));
    assertPiece(PieceType::Rook, chosen);
}

void test_a_cell_excludes_its_right_edge_which_is_the_next_cell(void) {
    PromotionPicker picker;
    picker.open();
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(0, x, y, w, h);
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Chosen),
                            static_cast<uint8_t>(picker.tap(x + w, y, chosen)));
    assertPiece(PieceType::Rook, chosen);
}

void test_the_last_cells_right_edge_is_outside(void) {
    PromotionPicker picker;
    picker.open();
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(chessdemo::kPromoChoiceCount - 1, x, y, w, h);
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Cancelled),
                            static_cast<uint8_t>(picker.tap(x + w, y, chosen)));
    assertPiece(PieceType::Pawn, chosen);
}

// --- Cancelling --------------------------------------------------------------

void test_a_tap_off_the_panel_cancels_and_closes(void) {
    PromotionPicker picker;
    picker.open();
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Cancelled),
                            static_cast<uint8_t>(picker.tap(0, 0, chosen)));
    assertPiece(PieceType::Pawn, chosen);
    TEST_ASSERT_FALSE(picker.isOpen());
}

void test_a_tap_on_the_panel_but_not_a_cell_still_cancels(void) {
    // The title strip and the margins are part of the plate, not a choice.
    PromotionPicker picker;
    picker.open();
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Cancelled),
                            static_cast<uint8_t>(picker.tap(chessdemo::kPromoX + 3,
                                                            chessdemo::kPromoRowY, chosen)));
    TEST_ASSERT_FALSE(picker.isOpen());

    picker.open();
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Cancelled),
                            static_cast<uint8_t>(picker.tap(chessdemo::kPromoX + 10,
                                                            chessdemo::kPromoY + 5, chosen)));
}

void test_a_tap_on_a_closed_picker_is_ignored(void) {
    PromotionPicker picker;
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(0, x, y, w, h);
    PieceType chosen = PieceType::Pawn;
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Ignored),
                            static_cast<uint8_t>(picker.tap(x, y, chosen)));
    assertPiece(PieceType::Pawn, chosen);
}

void test_close_shuts_an_open_picker(void) {
    PromotionPicker picker;
    picker.open();
    picker.close();
    TEST_ASSERT_FALSE(picker.isOpen());
}

void test_the_picker_opens_again_after_a_choice(void) {
    PromotionPicker picker;
    picker.open();
    PieceType chosen = PieceType::Pawn;
    int x = 0, y = 0, w = 0, h = 0;
    PromotionPicker::cellRect(3, x, y, w, h);
    (void)picker.tap(x, y, chosen);

    picker.open();
    TEST_ASSERT_TRUE(picker.isOpen());
    PromotionPicker::cellRect(2, x, y, w, h);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TapResult::Chosen),
                            static_cast<uint8_t>(picker.tap(x, y, chosen)));
    assertPiece(PieceType::Bishop, chosen);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_new_picker_is_closed);
    RUN_TEST(test_opening_offers_queen_rook_bishop_knight_in_that_order);
    RUN_TEST(test_the_cells_sit_where_the_picker_always_drew_them);
    RUN_TEST(test_the_panel_is_as_wide_as_its_cells_and_margins);
    RUN_TEST(test_a_tap_on_a_cell_chooses_that_piece_and_closes);
    RUN_TEST(test_a_cell_includes_its_top_left_edge);
    RUN_TEST(test_a_cell_excludes_its_right_edge_which_is_the_next_cell);
    RUN_TEST(test_the_last_cells_right_edge_is_outside);
    RUN_TEST(test_a_tap_off_the_panel_cancels_and_closes);
    RUN_TEST(test_a_tap_on_the_panel_but_not_a_cell_still_cancels);
    RUN_TEST(test_a_tap_on_a_closed_picker_is_ignored);
    RUN_TEST(test_close_shuts_an_open_picker);
    RUN_TEST(test_the_picker_opens_again_after_a_choice);
    return UNITY_END();
}
