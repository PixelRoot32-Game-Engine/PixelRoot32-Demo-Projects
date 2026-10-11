/*
 * Unit tests for LineClearSystem: single, double, tetris and compaction (T05/T06).
 */
#include <unity.h>

#include "game/Board.h"
#include "game/LineClearSystem.h"

using namespace tetris;

static LineClearSystem clearer;

static void fillRow(Board& board, int y) {
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board.set(x, y, Cell::I);
    }
}

void test_no_full_rows_clears_nothing(void) {
    Board board;
    board.set(0, 19, Cell::T);
    TEST_ASSERT_EQUAL_UINT8(0, clearer.clearLines(board));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::T),
                          static_cast<int>(board.get(0, 19)));
}

void test_single_line_clears_and_compacts(void) {
    Board board;
    fillRow(board, 19);
    board.set(0, 18, Cell::Z);
    TEST_ASSERT_EQUAL_UINT8(1, clearer.clearLines(board));
    // The marker above falls one row.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::Z),
                          static_cast<int>(board.get(0, 19)));
    TEST_ASSERT_FALSE(board.isRowFull(19));
}

void test_tetris_clears_four(void) {
    Board board;
    for (int y = 16; y < 20; ++y) {
        fillRow(board, y);
    }
    TEST_ASSERT_EQUAL_UINT8(4, clearer.clearLines(board));
    TEST_ASSERT_TRUE(board.isEmpty());
}

void test_top_middle_bottom_rows_each_clear(void) {
    const int rows[3] = {0, 10, 19};
    for (int i = 0; i < 3; ++i) {
        Board board;
        fillRow(board, rows[i]);
        TEST_ASSERT_EQUAL_UINT8(1, clearer.clearLines(board));
        TEST_ASSERT_TRUE(board.isEmpty());
    }
}

void test_consecutive_lines_compact_without_gaps(void) {
    Board board;
    fillRow(board, 18);
    fillRow(board, 19);
    board.set(3, 17, Cell::L);
    board.set(4, 16, Cell::J);
    TEST_ASSERT_EQUAL_UINT8(2, clearer.clearLines(board));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::L),
                          static_cast<int>(board.get(3, 19)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::J),
                          static_cast<int>(board.get(4, 18)));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_no_full_rows_clears_nothing);
    RUN_TEST(test_single_line_clears_and_compacts);
    RUN_TEST(test_tetris_clears_four);
    RUN_TEST(test_top_middle_bottom_rows_each_clear);
    RUN_TEST(test_consecutive_lines_compact_without_gaps);
    return UNITY_END();
}
