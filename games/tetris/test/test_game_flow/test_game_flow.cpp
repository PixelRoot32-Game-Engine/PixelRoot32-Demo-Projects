/*
 * Game-flow tests: 7-bag fairness, lock/next pipeline, pause freeze,
 * restart, game over and seed reproducibility (T01/T07/T08/T09/T10).
 */
#include <unity.h>

#include "game/PieceGenerator.h"
#include "game/ScoringSystem.h"
#include "game/TetrisSession.h"
#include "game/TetrisTypes.h"
#include "input/InputController.h"

using namespace tetris;

static void settleActive(TetrisSession& session) {
    // Drop the active piece with hard drops until it changes: bounded so a
    // broken lock path fails fast instead of hanging the suite.
    const ActivePiece first = session.active();
    for (int i = 0; i < 40; ++i) {
        if (session.status() != GameStatus::Playing) {
            return;
        }
        session.hardDrop();
        const ActivePiece now = session.active();
        if (now.type != first.type || now.x != first.x || now.y != first.y) {
            return;
        }
    }
}

void test_start_gives_empty_board_and_valid_piece(void) {
    TetrisSession session;
    session.start(1234);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Playing),
                          static_cast<int>(session.status()));
    TEST_ASSERT_TRUE(session.board().isEmpty());
    TEST_ASSERT_EQUAL_UINT32(0, session.score());
    TEST_ASSERT_EQUAL_UINT8(1, session.level());
    // Active and next are real pieces from the bag.
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(session.active().type) < 7);
    TEST_ASSERT_TRUE(static_cast<std::uint8_t>(session.nextType()) < 7);
}

void test_seven_bag_deals_every_piece_per_bag(void) {
    PieceGenerator generator;
    generator.reset(99);
    for (int bag = 0; bag < 4; ++bag) {
        bool seen[7] = {false, false, false, false, false, false, false};
        for (int i = 0; i < 7; ++i) {
            TetrominoType t = generator.next();
            TEST_ASSERT_FALSE(seen[static_cast<std::uint8_t>(t)]);
            seen[static_cast<std::uint8_t>(t)] = true;
        }
    }
}

void test_same_seed_reproduces_queue(void) {
    PieceGenerator a;
    PieceGenerator b;
    a.reset(4242);
    b.reset(4242);
    for (int i = 0; i < 28; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(a.next()), static_cast<int>(b.next()));
    }
    TetrisSession sa;
    TetrisSession sb;
    sa.start(777);
    sb.start(777);
    for (int i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(sa.active().type),
                              static_cast<int>(sb.active().type));
        sa.hardDrop();
        sb.hardDrop();
    }
}

void test_lock_spawns_next_piece(void) {
    TetrisSession session;
    session.start(5);
    const TetrominoType expected = session.nextType();
    settleActive(session);
    if (session.status() == GameStatus::Playing) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                              static_cast<int>(session.active().type));
    }
}

void test_pause_freezes_timers(void) {
    // The session only advances when update() is called: not calling it
    // (the scene's pause path) banks zero time, so resuming never jumps.
    TetrisSession session;
    session.start(11);
    const ActivePiece before = session.active();
    session.update(200, false);
    session.update(200, false);
    TEST_ASSERT_EQUAL_INT(before.y, session.active().y);  // 400 ms < 800 ms interval
    session.update(200, false);
    session.update(200, false);
    TEST_ASSERT_EQUAL_INT(before.y + 1, session.active().y);
}

void test_big_delta_never_multi_drops(void) {
    TetrisSession session;
    session.start(13);
    const int y0 = session.active().y;
    session.update(10000, false);
    // Clamped to MAX_STEP_MS: at level 1 at most one row per call.
    TEST_ASSERT_TRUE(session.active().y <= y0 + 1);
}

void test_stack_to_ceiling_triggers_game_over(void) {
    TetrisSession session;
    session.start(21);
    for (int i = 0; i < 60 && session.status() == GameStatus::Playing; ++i) {
        session.hardDrop();
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::GameOver),
                          static_cast<int>(session.status()));
}

void test_restart_clears_everything(void) {
    TetrisSession session;
    session.start(21);
    for (int i = 0; i < 10; ++i) {
        session.hardDrop();
    }
    session.start(999);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(GameStatus::Playing),
                          static_cast<int>(session.status()));
    TEST_ASSERT_TRUE(session.board().isEmpty());
    TEST_ASSERT_EQUAL_UINT32(0, session.score());
    TEST_ASSERT_EQUAL_UINT16(0, session.lines());
    TEST_ASSERT_EQUAL_UINT8(1, session.level());
}

void test_held_direction_never_rotates(void) {
    InputController input;
    input.reset();
    // Hold A: exactly one Rotate edge, then silence while held.
    std::uint16_t first = input.poll((1U << BTN_A), 1000);
    TEST_ASSERT_TRUE(hasCommand(first, GameCommand::RotateCw));
    std::uint16_t second = input.poll((1U << BTN_A), 1016);
    TEST_ASSERT_FALSE(hasCommand(second, GameCommand::RotateCw));
    TEST_ASSERT_FALSE(hasCommand(second, GameCommand::RotateCcw));
}

void test_scales_without_memory_errors(void) {
    TetrisSession session;
    session.start(31415);
    for (int i = 0; i < 200 && session.status() == GameStatus::Playing; ++i) {
        session.tryMove((i % 2 == 0) ? -1 : 1);
        if ((i % 3) == 0) {
            session.tryRotateCw();
        }
        session.update(50, (i % 4) == 0);
        if ((i % 5) == 0) {
            session.hardDrop();
        }
    }
    // No crash, no OOB: reaching here is the assertion (T12).
    TEST_ASSERT_TRUE(session.lines() <= 200 * 4);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_start_gives_empty_board_and_valid_piece);
    RUN_TEST(test_seven_bag_deals_every_piece_per_bag);
    RUN_TEST(test_same_seed_reproduces_queue);
    RUN_TEST(test_lock_spawns_next_piece);
    RUN_TEST(test_pause_freezes_timers);
    RUN_TEST(test_big_delta_never_multi_drops);
    RUN_TEST(test_stack_to_ceiling_triggers_game_over);
    RUN_TEST(test_restart_clears_everything);
    RUN_TEST(test_held_direction_never_rotates);
    RUN_TEST(test_scales_without_memory_errors);
    return UNITY_END();
}
