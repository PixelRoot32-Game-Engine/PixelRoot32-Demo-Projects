/*
 * Unit tests for the Minefield rules core: generation, adjacency,
 * flags and board invariants (spec AC-02, AC-03, AC-05).
 */
#include <unity.h>

#include "game/Minefield.h"

using namespace minesweeper;

static int countMinesBrute(const Minefield& field) {
    const BoardConfig& config = field.getConfig();
    int mines = 0;
    for (std::uint8_t y = 0; y < config.height; ++y) {
        for (std::uint8_t x = 0; x < config.width; ++x) {
            const Cell* cell = field.getCell(Position{x, y});
            if (cell != nullptr && cell->hasMine) {
                ++mines;
            }
        }
    }
    return mines;
}

static int countAdjacentBrute(const Minefield& field, std::uint8_t x, std::uint8_t y) {
    const BoardConfig& config = field.getConfig();
    int mines = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int nx = static_cast<int>(x) + dx;
            const int ny = static_cast<int>(y) + dy;
            if (nx < 0 || ny < 0 || nx >= config.width || ny >= config.height) {
                continue;
            }
            const Cell* cell =
                field.getCell(Position{static_cast<std::uint8_t>(nx),
                                       static_cast<std::uint8_t>(ny)});
            if (cell != nullptr && cell->hasMine) {
                ++mines;
            }
        }
    }
    return mines;
}

void test_rejects_invalid_configs(void) {
    Minefield field;
    TEST_ASSERT_FALSE(field.prepare(BoardConfig{0, 9, 10}));
    TEST_ASSERT_FALSE(field.prepare(BoardConfig{9, 0, 10}));
    TEST_ASSERT_FALSE(field.prepare(BoardConfig{31, 16, 10}));
    TEST_ASSERT_FALSE(field.prepare(BoardConfig{30, 17, 10}));
    TEST_ASSERT_FALSE(field.prepare(BoardConfig{9, 9, 81}));  // mines < cells
    TEST_ASSERT_FALSE(field.initialize(BoardConfig{9, 9, 81}, 1));
}

void test_every_difficulty_places_exact_mines(void) {
    static const Difficulty levels[3] = {Difficulty::Beginner, Difficulty::Intermediate,
                                         Difficulty::Expert};
    for (int i = 0; i < 3; ++i) {
        const BoardConfig config = boardConfigFor(levels[i]);
        // AC-02: 100 boards per difficulty, exact mine count each time.
        for (std::uint32_t seed = 1; seed <= 100; ++seed) {
            Minefield field;
            TEST_ASSERT_TRUE(field.initialize(config, seed));
            TEST_ASSERT_EQUAL_INT(config.mineCount, countMinesBrute(field));
            TEST_ASSERT_EQUAL_INT(config.mineCount, field.getMineCount());
        }
    }
}

void test_adjacency_matches_real_mines(void) {
    static const Difficulty levels[3] = {Difficulty::Beginner, Difficulty::Intermediate,
                                         Difficulty::Expert};
    for (int i = 0; i < 3; ++i) {
        const BoardConfig config = boardConfigFor(levels[i]);
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            Minefield field;
            TEST_ASSERT_TRUE(field.initialize(config, seed + 1000));
            // AC-03: every safe cell number matches the true neighbours.
            for (std::uint8_t y = 0; y < config.height; ++y) {
                for (std::uint8_t x = 0; x < config.width; ++x) {
                    const Cell* cell = field.getCell(Position{x, y});
                    TEST_ASSERT_NOT_NULL(cell);
                    if (!cell->hasMine) {
                        TEST_ASSERT_EQUAL_INT(countAdjacentBrute(field, x, y),
                                              cell->adjacentMines);
                        TEST_ASSERT_TRUE(cell->adjacentMines <= 8);
                    }
                }
            }
        }
    }
}

void test_corners_count_three_neighbours(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{2, 2, 3}, 7));
    // A 2x2 board with 3 mines leaves one safe corner seeing all three.
    const BoardConfig& config = field.getConfig();
    int checked = 0;
    const Position corners[4] = {Position{0, 0}, Position{1, 0}, Position{0, 1},
                                 Position{1, 1}};
    for (int i = 0; i < 4; ++i) {
        const Cell* cell = field.getCell(corners[i]);
        TEST_ASSERT_NOT_NULL(cell);
        if (!cell->hasMine) {
            TEST_ASSERT_EQUAL_INT(3, cell->adjacentMines);
            ++checked;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, checked);
    (void)config;
}

void test_invalid_positions_change_nothing(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{9, 9, 10}, 42));
    const std::uint16_t revealedBefore = field.getRevealedCount();
    const std::uint16_t flagsBefore = field.getFlagCount();
    TEST_ASSERT_FALSE(field.reveal(Position{9, 0}));
    TEST_ASSERT_FALSE(field.reveal(Position{0, 9}));
    TEST_ASSERT_FALSE(field.toggleFlag(Position{9, 9}));
    TEST_ASSERT_NULL(field.getCell(Position{9, 0}));
    TEST_ASSERT_FALSE(field.isValid(Position{9, 9}));
    TEST_ASSERT_EQUAL_UINT16(revealedBefore, field.getRevealedCount());
    TEST_ASSERT_EQUAL_UINT16(flagsBefore, field.getFlagCount());
}

void test_flag_rules(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initialize(BoardConfig{9, 9, 10}, 42));
    const Position pos{3, 3};
    TEST_ASSERT_TRUE(field.toggleFlag(pos));
    TEST_ASSERT_EQUAL_UINT16(1, field.getFlagCount());
    // AC-05: a flagged cell refuses to open until the flag is lifted.
    TEST_ASSERT_FALSE(field.reveal(pos));
    TEST_ASSERT_TRUE(field.toggleFlag(pos));
    TEST_ASSERT_EQUAL_UINT16(0, field.getFlagCount());
    // A revealed cell refuses flags.
    TEST_ASSERT_TRUE(field.reveal(pos) || field.hasDetonated());
    if (!field.hasDetonated()) {
        TEST_ASSERT_FALSE(field.toggleFlag(pos));
    }
}

void test_revealed_cells_stay_revealed(void) {
    Minefield field;
    TEST_ASSERT_TRUE(field.initializeSafe(BoardConfig{9, 9, 10}, 5, Position{4, 4}));
    TEST_ASSERT_TRUE(field.reveal(Position{4, 4}));
    const std::uint16_t revealed = field.getRevealedCount();
    TEST_ASSERT_FALSE(field.reveal(Position{4, 4}));
    TEST_ASSERT_EQUAL_UINT16(revealed, field.getRevealedCount());
}

void test_same_seed_reproduces_board(void) {
    Minefield a;
    Minefield b;
    TEST_ASSERT_TRUE(a.initialize(BoardConfig{16, 16, 40}, 1234));
    TEST_ASSERT_TRUE(b.initialize(BoardConfig{16, 16, 40}, 1234));
    for (std::uint8_t y = 0; y < 16; ++y) {
        for (std::uint8_t x = 0; x < 16; ++x) {
            const Cell* ca = a.getCell(Position{x, y});
            const Cell* cb = b.getCell(Position{x, y});
            TEST_ASSERT_NOT_NULL(ca);
            TEST_ASSERT_NOT_NULL(cb);
            TEST_ASSERT_EQUAL_INT(ca->hasMine, cb->hasMine);
        }
    }
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_rejects_invalid_configs);
    RUN_TEST(test_every_difficulty_places_exact_mines);
    RUN_TEST(test_adjacency_matches_real_mines);
    RUN_TEST(test_corners_count_three_neighbours);
    RUN_TEST(test_invalid_positions_change_nothing);
    RUN_TEST(test_flag_rules);
    RUN_TEST(test_revealed_cells_stay_revealed);
    RUN_TEST(test_same_seed_reproduces_board);

    return UNITY_END();
}
