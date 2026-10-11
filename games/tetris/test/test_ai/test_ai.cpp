/*
 * Unit tests for the attract-mode AI: pose validity, line-clear greed,
 * O-piece rotation tie-break and determinism.
 */
#include <unity.h>

#include "game/Board.h"
#include "game/CollisionSystem.h"
#include "game/TetrisAi.h"
#include "game/Tetromino.h"

using namespace tetris;

namespace {

// Drops a planned pose onto a scratch board, exactly like the AI simulates.
void applyPlacement(Board& board, TetrominoType type, AiPlacement placement) {
    CollisionSystem collision;
    ActivePiece probe{type, static_cast<int8_t>(placement.x), Tetromino::spawnY(),
                      placement.rotation};
    while (true) {
        ActivePiece below = probe;
        below.y = static_cast<int8_t>(below.y + 1);
        if (!collision.canPlace(board, below)) {
            break;
        }
        probe = below;
    }
    for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
        const BlockOffset block = Tetromino::blockAt(type, placement.rotation, i);
        const int sx = static_cast<int>(probe.x) + block.x;
        const int sy = static_cast<int>(probe.y) + block.y;
        if (sy >= 0) {
            board.set(sx, sy, cellForType(type));
        }
    }
}

}  // namespace

void test_ai_returns_placeable_pose_for_every_piece(void) {
    const TetrisAi ai;
    const Board board;
    for (std::uint8_t t = 0; t < TETROMINO_COUNT; ++t) {
        const TetrominoType type = static_cast<TetrominoType>(t);
        const AiPlacement move = ai.findBestPlacement(board, type);
        TEST_ASSERT_LESS_THAN(ROTATION_COUNT, move.rotation);
        const CollisionSystem collision;
        const ActivePiece probe{type, static_cast<int8_t>(move.x), Tetromino::spawnY(),
                                move.rotation};
        TEST_ASSERT_TRUE(collision.canPlace(board, probe));
    }
}

void test_ai_takes_immediate_line_clear(void) {
    // Bottom row filled except columns 0-3: a horizontal I at x=0 clears.
    Board board;
    for (int x = 4; x < BOARD_WIDTH; ++x) {
        board.set(x, BOARD_HEIGHT - 1, Cell::Z);
    }
    const TetrisAi ai;
    const AiPlacement move = ai.findBestPlacement(board, TetrominoType::I);
    TEST_ASSERT_EQUAL_UINT8(0, move.rotation);
    TEST_ASSERT_EQUAL_INT(0, move.x);
    applyPlacement(board, TetrominoType::I, move);
    TEST_ASSERT_TRUE(board.isRowFull(BOARD_HEIGHT - 1));
}

void test_ai_keeps_o_piece_unrotated(void) {
    // Every O rotation stamps the same square: the first candidate wins.
    const TetrisAi ai;
    const Board board;
    const AiPlacement move = ai.findBestPlacement(board, TetrominoType::O);
    TEST_ASSERT_EQUAL_UINT8(0, move.rotation);
}

void test_ai_is_deterministic(void) {
    Board board;
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board.set(x, BOARD_HEIGHT - 1, Cell::T);
        board.set(x, BOARD_HEIGHT - 2, Cell::T);
    }
    board.set(5, BOARD_HEIGHT - 3, Cell::S);
    board.set(2, BOARD_HEIGHT - 3, Cell::J);
    const TetrisAi ai;
    const AiPlacement first = ai.findBestPlacement(board, TetrominoType::T);
    const AiPlacement second = ai.findBestPlacement(board, TetrominoType::T);
    TEST_ASSERT_EQUAL_UINT8(first.rotation, second.rotation);
    TEST_ASSERT_EQUAL_INT(first.x, second.x);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ai_returns_placeable_pose_for_every_piece);
    RUN_TEST(test_ai_takes_immediate_line_clear);
    RUN_TEST(test_ai_keeps_o_piece_unrotated);
    RUN_TEST(test_ai_is_deterministic);
    return UNITY_END();
}
