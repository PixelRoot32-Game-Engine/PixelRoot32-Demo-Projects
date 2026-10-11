/*
 * Unit tests for Board storage: bounds, writes and row queries (T02/T12).
 */
#include <unity.h>

#include "game/Board.h"

using namespace tetris;

void test_board_starts_empty(void) {
    Board board;
    TEST_ASSERT_TRUE(board.isEmpty());
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::Empty),
                                  static_cast<int>(board.get(x, y)));
        }
        TEST_ASSERT_FALSE(board.isRowFull(y));
    }
}

void test_board_set_and_get(void) {
    Board board;
    TEST_ASSERT_TRUE(board.set(0, 0, Cell::T));
    TEST_ASSERT_TRUE(board.set(9, 19, Cell::I));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::T), static_cast<int>(board.get(0, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::I), static_cast<int>(board.get(9, 19)));
    TEST_ASSERT_TRUE(board.isOccupied(0, 0));
    TEST_ASSERT_FALSE(board.isEmpty());
}

void test_board_rejects_out_of_bounds(void) {
    Board board;
    TEST_ASSERT_FALSE(board.set(-1, 0, Cell::O));
    TEST_ASSERT_FALSE(board.set(10, 0, Cell::O));
    TEST_ASSERT_FALSE(board.set(0, -1, Cell::O));
    TEST_ASSERT_FALSE(board.set(0, 20, Cell::O));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::Empty), static_cast<int>(board.get(-1, 0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Cell::Empty), static_cast<int>(board.get(0, 20)));
    TEST_ASSERT_FALSE(board.isOccupied(-1, 0));
    TEST_ASSERT_FALSE(board.isOccupied(10, 19));
    TEST_ASSERT_TRUE(board.isEmpty());
}

void test_board_row_full_detection(void) {
    Board board;
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        TEST_ASSERT_FALSE(board.isRowFull(BOARD_HEIGHT - 1));
        board.set(x, BOARD_HEIGHT - 1, Cell::Z);
    }
    TEST_ASSERT_TRUE(board.isRowFull(BOARD_HEIGHT - 1));
    TEST_ASSERT_FALSE(board.isRowFull(0));
    TEST_ASSERT_FALSE(board.isRowFull(-1));
    TEST_ASSERT_FALSE(board.isRowFull(BOARD_HEIGHT));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_board_starts_empty);
    RUN_TEST(test_board_set_and_get);
    RUN_TEST(test_board_rejects_out_of_bounds);
    RUN_TEST(test_board_row_full_detection);
    return UNITY_END();
}
