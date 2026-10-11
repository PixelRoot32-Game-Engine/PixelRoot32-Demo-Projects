/*
 * Unit tests for GameSession: clock behaviour, terminal states,
 * restart and seed reproducibility (spec AC-08).
 */
#include <unity.h>

#include "game/GameSession.h"

using namespace minesweeper;

void test_clock_starts_on_first_action(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{9, 9, 10}, 7));
    TEST_ASSERT_EQUAL_UINT32(0, session.getElapsedSeconds(5000));
    TEST_ASSERT_TRUE(session.toggleFlag(Position{0, 0}, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, session.getElapsedSeconds(1500));
    TEST_ASSERT_EQUAL_UINT32(4, session.getElapsedSeconds(5000));
}

void test_clock_freezes_on_loss(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{4, 4, 3}, 7));
    TEST_ASSERT_TRUE(session.reveal(Position{0, 0}, 2000));
    // Detonate a real mine: the first reveal is safe, so hunt the layout.
    Position mine{0, 0};
    bool found = false;
    for (std::uint8_t y = 0; y < 4 && !found; ++y) {
        for (std::uint8_t x = 0; x < 4 && !found; ++x) {
            const Cell* cell = session.getMinefield().getCell(Position{x, y});
            if (cell != nullptr && cell->hasMine && cell->state != CellState::Revealed) {
                mine = Position{x, y};
                found = true;
            }
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(session.reveal(mine, 3000));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Lost),
                          static_cast<int>(session.getStatus()));
    const std::uint32_t frozen = session.getElapsedSeconds(99999);
    TEST_ASSERT_EQUAL_UINT32(frozen, session.getElapsedSeconds(1234567));
}

void test_actions_ignored_after_game_over(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{4, 4, 3}, 7));
    TEST_ASSERT_TRUE(session.reveal(Position{0, 0}, 1000));
    Position mine{0, 0};
    bool found = false;
    for (std::uint8_t y = 0; y < 4 && !found; ++y) {
        for (std::uint8_t x = 0; x < 4 && !found; ++x) {
            const Cell* cell = session.getMinefield().getCell(Position{x, y});
            if (cell != nullptr && cell->hasMine && cell->state != CellState::Revealed) {
                mine = Position{x, y};
                found = true;
            }
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(session.reveal(mine, 1500));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Lost),
                          static_cast<int>(session.getStatus()));
    const std::uint16_t revealed = session.getMinefield().getRevealedCount();
    const std::uint16_t flags = session.getMinefield().getFlagCount();
    TEST_ASSERT_FALSE(session.reveal(Position{1, 1}, 2000));
    TEST_ASSERT_FALSE(session.toggleFlag(Position{0, 0}, 2000));
    TEST_ASSERT_EQUAL_UINT16(revealed, session.getMinefield().getRevealedCount());
    TEST_ASSERT_EQUAL_UINT16(flags, session.getMinefield().getFlagCount());
}

void test_restart_resets_everything(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{9, 9, 10}, 7));
    TEST_ASSERT_TRUE(session.reveal(Position{4, 4}, 1000));
    TEST_ASSERT_TRUE(session.toggleFlag(Position{0, 0}, 1500));
    TEST_ASSERT_TRUE(session.restart(99));
    // AC-08: status, clock, flags and cells return to the initial state.
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Ready),
                          static_cast<int>(session.getStatus()));
    TEST_ASSERT_EQUAL_UINT32(0, session.getElapsedSeconds(99999));
    TEST_ASSERT_EQUAL_UINT16(0, session.getMinefield().getFlagCount());
    TEST_ASSERT_EQUAL_UINT16(0, session.getMinefield().getRevealedCount());
    TEST_ASSERT_EQUAL_UINT16(10, session.getRemainingMines());
    TEST_ASSERT_FALSE(session.getMinefield().isPlaced());
}

void test_remaining_mines_saturates(void) {
    GameSession session;
    TEST_ASSERT_TRUE(session.start(BoardConfig{9, 9, 10}, 7));
    for (std::uint8_t i = 0; i < 10; ++i) {
        session.toggleFlag(Position{static_cast<std::uint8_t>(i % 9),
                                    static_cast<std::uint8_t>(i / 9)},
                           1000);
    }
    TEST_ASSERT_EQUAL_UINT16(0, session.getRemainingMines());
    // Over-flagging past the mine count still reads zero, never negative.
    session.toggleFlag(Position{1, 1}, 1500);
    session.toggleFlag(Position{2, 1}, 1500);
    TEST_ASSERT_EQUAL_UINT16(0, session.getRemainingMines());
    session.toggleFlag(Position{2, 1}, 2000);
    session.toggleFlag(Position{1, 1}, 2000);
    session.toggleFlag(Position{0, 0}, 2000);
    TEST_ASSERT_EQUAL_UINT16(1, session.getRemainingMines());
}

void test_same_seed_same_session_board(void) {
    GameSession a;
    GameSession b;
    TEST_ASSERT_TRUE(a.start(BoardConfig{9, 9, 10}, 4242));
    TEST_ASSERT_TRUE(b.start(BoardConfig{9, 9, 10}, 4242));
    // Deferred placement uses the same seed, so the first reveal at the
    // same cell must produce identical minefields.
    TEST_ASSERT_TRUE(a.reveal(Position{4, 4}, 1000));
    TEST_ASSERT_TRUE(b.reveal(Position{4, 4}, 1000));
    for (std::uint8_t y = 0; y < 9; ++y) {
        for (std::uint8_t x = 0; x < 9; ++x) {
            const Cell* ca = a.getMinefield().getCell(Position{x, y});
            const Cell* cb = b.getMinefield().getCell(Position{x, y});
            TEST_ASSERT_NOT_NULL(ca);
            TEST_ASSERT_NOT_NULL(cb);
            TEST_ASSERT_EQUAL_INT(ca->hasMine, cb->hasMine);
        }
    }
}

void test_rejects_bad_config(void) {
    GameSession session;
    TEST_ASSERT_FALSE(session.start(BoardConfig{9, 9, 81}, 1));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Ready),
                          static_cast<int>(session.getStatus()));
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_clock_starts_on_first_action);
    RUN_TEST(test_clock_freezes_on_loss);
    RUN_TEST(test_actions_ignored_after_game_over);
    RUN_TEST(test_restart_resets_everything);
    RUN_TEST(test_remaining_mines_saturates);
    RUN_TEST(test_same_seed_same_session_board);
    RUN_TEST(test_rejects_bad_config);

    return UNITY_END();
}
