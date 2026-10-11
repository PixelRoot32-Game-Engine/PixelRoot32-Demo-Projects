/*
 * Unit tests for flood reveal, detonation and victory (spec AC-04,
 * AC-06, AC-07), exercised through the field and the session.
 */
#include <unity.h>

#include "game/GameSession.h"
#include "game/Minefield.h"

using namespace minesweeper;

void test_empty_board_reveals_everything_and_wins(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{4, 4, 0}, 1));
    TEST_ASSERT_TRUE(field.reveal(Position{0, 0}));
    TEST_ASSERT_EQUAL_UINT16(16, field.getRevealedCount());
    TEST_ASSERT_TRUE(field.isVictory());
    TEST_ASSERT_FALSE(field.hasDetonated());
}

void test_reveal_expands_connected_safe_region(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initializeSafe(BoardConfig{5, 5, 1}, 9, Position{4, 4}));
    // Open a zero cell: with a single mine the flood reaches every safe
    // cell, numbers included, in one move.
    Position zero{0, 0};
    bool found = false;
    for (std::uint8_t y = 0; y < 5 && !found; ++y) {
        for (std::uint8_t x = 0; x < 5 && !found; ++x) {
            const Cell* cell = field.getCell(Position{x, y});
            if (cell != nullptr && !cell->hasMine && cell->adjacentMines == 0) {
                zero = Position{x, y};
                found = true;
            }
        }
    }
    TEST_ASSERT_TRUE(found);
    // AC-04: the expansion covers every reachable safe cell at once.
    // Note (4,0) in this layout touches only numbers and the mine, so the
    // classic flood leaves exactly that one safe cell hidden.
    TEST_ASSERT_TRUE(field.reveal(zero));
    TEST_ASSERT_EQUAL_UINT16(23, field.getRevealedCount());
    TEST_ASSERT_FALSE(field.isVictory());
    // Opening each leftover safe cell finishes the board.
    for (std::uint8_t y = 0; y < 5; ++y) {
        for (std::uint8_t x = 0; x < 5; ++x) {
            const Cell* cell = field.getCell(Position{x, y});
            if (cell != nullptr && !cell->hasMine && cell->state == CellState::Hidden) {
                TEST_ASSERT_TRUE(field.reveal(Position{x, y}));
            }
        }
    }
    TEST_ASSERT_EQUAL_UINT16(24, field.getRevealedCount());
    TEST_ASSERT_TRUE(field.isVictory());
}

void test_numbered_cell_stops_expansion(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{3, 3, 8}, 3));
    // 8 mines on 3x3 leaves exactly one safe cell holding the number 8.
    Position safe{0, 0};
    bool found = false;
    for (std::uint8_t y = 0; y < 3 && !found; ++y) {
        for (std::uint8_t x = 0; x < 3 && !found; ++x) {
            const Cell* cell = field.getCell(Position{x, y});
            if (cell != nullptr && !cell->hasMine) {
                safe = Position{x, y};
                found = true;
            }
        }
    }
    TEST_ASSERT_TRUE(found);
    const std::uint16_t before = field.getRevealedCount();
    TEST_ASSERT_TRUE(field.reveal(safe));
    TEST_ASSERT_EQUAL_UINT16(before + 1, field.getRevealedCount());
    TEST_ASSERT_TRUE(field.isVictory());
}

void test_detonation_records_position(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{3, 3, 8}, 3));
    Position mine{0, 0};
    bool found = false;
    for (std::uint8_t y = 0; y < 3 && !found; ++y) {
        for (std::uint8_t x = 0; x < 3 && !found; ++x) {
            const Cell* cell = field.getCell(Position{x, y});
            if (cell != nullptr && cell->hasMine) {
                mine = Position{x, y};
                found = true;
            }
        }
    }
    TEST_ASSERT_TRUE(found);
    // AC-06 at field level: hitting a mine reports the blast site.
    TEST_ASSERT_TRUE(field.reveal(mine));
    TEST_ASSERT_TRUE(field.hasDetonated());
    Position blast{0, 0};
    TEST_ASSERT_TRUE(field.detonatedPosition(blast));
    TEST_ASSERT_EQUAL_UINT8(mine.x, blast.x);
    TEST_ASSERT_EQUAL_UINT8(mine.y, blast.y);
    TEST_ASSERT_FALSE(field.isVictory());
}

void test_victory_needs_no_flags(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{4, 4, 0}, 11));
    TEST_ASSERT_TRUE(session.reveal(Position{1, 1}, 1000));
    // AC-07: an empty board wins with zero flags planted.
    TEST_ASSERT_EQUAL_UINT16(0, session.getMinefield().getFlagCount());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Won),
                          static_cast<int>(session.getStatus()));
}

void test_first_reveal_is_always_safe(void) {
    const BoardConfig config = boardConfigFor(Difficulty::Beginner);
    for (std::uint8_t y = 0; y < config.height; ++y) {
        for (std::uint8_t x = 0; x < config.width; ++x) {
            for (std::uint32_t seed = 1; seed <= 5; ++seed) {
                GameSession session;
                TEST_ASSERT_TRUE(session.start(config, seed));
                TEST_ASSERT_TRUE(session.reveal(Position{x, y}, 1000));
                TEST_ASSERT_FALSE(session.getMinefield().hasDetonated());
                TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Playing),
                                      static_cast<int>(session.getStatus()));
            }
        }
    }
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_empty_board_reveals_everything_and_wins);
    RUN_TEST(test_reveal_expands_connected_safe_region);
    RUN_TEST(test_numbered_cell_stops_expansion);
    RUN_TEST(test_detonation_records_position);
    RUN_TEST(test_victory_needs_no_flags);
    RUN_TEST(test_first_reveal_is_always_safe);

    return UNITY_END();
}
