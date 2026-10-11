/*
 * Unit tests for CollisionSystem: walls, floor, stacks and spawn entry (T02/T04).
 */
#include <unity.h>

#include "game/Board.h"
#include "game/CollisionSystem.h"
#include "game/TetrisTypes.h"

using namespace tetris;

static CollisionSystem collision;

void test_open_board_accepts_spawn(void) {
    Board board;
    for (std::uint8_t t = 0; t < TETROMINO_COUNT; ++t) {
        ActivePiece piece{static_cast<TetrominoType>(t), 3, -1, 0};
        if (static_cast<TetrominoType>(t) == TetrominoType::O) {
            piece.x = 3;
        }
        TEST_ASSERT_TRUE(collision.canPlace(board, piece));
    }
}

void test_walls_reject_side_moves(void) {
    Board board;
    ActivePiece left{TetrominoType::O, -2, 0, 0};
    TEST_ASSERT_FALSE(collision.canPlace(board, left));
    ActivePiece right{TetrominoType::O, 9, 0, 0};
    TEST_ASSERT_FALSE(collision.canPlace(board, right));
}

void test_floor_rejects_descent(void) {
    Board board;
    // O at y=18 occupies rows 18-19; one step lower exits the well.
    ActivePiece resting{TetrominoType::O, 3, 18, 0};
    TEST_ASSERT_TRUE(collision.canPlace(board, resting));
    ActivePiece below = resting;
    below.y += 1;
    TEST_ASSERT_FALSE(collision.canPlace(board, below));
}

void test_stack_blocks_overlap(void) {
    Board board;
    board.set(5, 19, Cell::T);
    ActivePiece piece{TetrominoType::O, 4, 17, 0};
    // O box at x=4 covers columns 5-6 rows 17-18: free.
    TEST_ASSERT_TRUE(collision.canPlace(board, piece));
    piece.y = 18;
    // Now covers rows 18-19, overlapping the locked cell at (5,19).
    TEST_ASSERT_FALSE(collision.canPlace(board, piece));
}

void test_negative_y_allowed_above_board(void) {
    Board board;
    ActivePiece piece{TetrominoType::I, 3, -1, 0};
    TEST_ASSERT_TRUE(collision.canPlace(board, piece));
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_open_board_accepts_spawn);
    RUN_TEST(test_walls_reject_side_moves);
    RUN_TEST(test_floor_rejects_descent);
    RUN_TEST(test_stack_blocks_overlap);
    RUN_TEST(test_negative_y_allowed_above_board);
    return UNITY_END();
}
