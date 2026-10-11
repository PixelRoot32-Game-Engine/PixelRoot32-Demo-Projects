/*
 * Unit tests for rotation: shape integrity, wall kicks and O invariance (T03).
 */
#include <unity.h>

#include "game/Board.h"
#include "game/CollisionSystem.h"
#include "game/Tetromino.h"
#include "game/TetrisSession.h"

using namespace tetris;

static bool sameBlocks(TetrominoType t, std::uint8_t a, std::uint8_t b) {
    bool used[4] = {false, false, false, false};
    for (std::uint8_t i = 0; i < 4; ++i) {
        BlockOffset ba = Tetromino::blockAt(t, a, i);
        bool found = false;
        for (std::uint8_t j = 0; j < 4; ++j) {
            if (used[j]) {
                continue;
            }
            BlockOffset bb = Tetromino::blockAt(t, b, j);
            if (ba.x == bb.x && ba.y == bb.y) {
                used[j] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

void test_every_piece_has_four_distinct_blocks(void) {
    for (std::uint8_t t = 0; t < TETROMINO_COUNT; ++t) {
        for (std::uint8_t r = 0; r < 4; ++r) {
            for (std::uint8_t i = 0; i < 4; ++i) {
                BlockOffset a =
                    Tetromino::blockAt(static_cast<TetrominoType>(t), r, i);
                TEST_ASSERT_TRUE(a.x >= 0 && a.x < 4);
                TEST_ASSERT_TRUE(a.y >= 0 && a.y < 4);
                for (std::uint8_t j = i + 1; j < 4; ++j) {
                    BlockOffset b =
                        Tetromino::blockAt(static_cast<TetrominoType>(t), r, j);
                    TEST_ASSERT_FALSE(a.x == b.x && a.y == b.y);
                }
            }
        }
    }
}

void test_full_turn_returns_to_spawn(void) {
    for (std::uint8_t t = 0; t < TETROMINO_COUNT; ++t) {
        TEST_ASSERT_TRUE(sameBlocks(static_cast<TetrominoType>(t), 0, 4));
    }
}

void test_o_piece_never_changes(void) {
    for (std::uint8_t r = 0; r < 4; ++r) {
        TEST_ASSERT_TRUE(sameBlocks(TetrominoType::O, 0, r));
    }
}

void test_rotation_against_wall_kicks_or_rejects_safely(void) {
    TetrisSession session;
    session.start(7);
    Board snapshot = session.board();
    // Drive the piece to the left wall, then rotate repeatedly: the board
    // must never change and the piece must stay inside the well.
    for (int i = 0; i < 6; ++i) {
        session.tryMove(-1);
    }
    CollisionSystem collision;
    for (int i = 0; i < 8; ++i) {
        session.tryRotateCw();
        TEST_ASSERT_TRUE(collision.canPlace(session.board(), session.active()));
    }
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(snapshot.get(x, y)),
                                  static_cast<int>(session.board().get(x, y)));
        }
    }
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_every_piece_has_four_distinct_blocks);
    RUN_TEST(test_full_turn_returns_to_spawn);
    RUN_TEST(test_o_piece_never_changes);
    RUN_TEST(test_rotation_against_wall_kicks_or_rejects_safely);
    return UNITY_END();
}
