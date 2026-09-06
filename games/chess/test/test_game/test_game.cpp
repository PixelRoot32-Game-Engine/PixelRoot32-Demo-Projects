/*
 * Unit tests for ChessGame: turn order, terminal states and history.
 *
 * Move legality itself is covered by test_rules; these tests only care about
 * what a match adds on top of it.
 */
#include <unity.h>

#include "chess/ChessGame.h"

using namespace chess;

static ChessGame game;

// --- Start of a game ---------------------------------------------------------

void test_new_game_starts_with_white_and_twenty_moves(void) {
    game.reset();

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::White),
                            static_cast<uint8_t>(game.sideToMove()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Playing),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_EQUAL_UINT8(20, game.legalMoves().count);
    TEST_ASSERT_EQUAL_UINT16(0, game.plyCount());
    TEST_ASSERT_NULL(game.lastMove());
}

void test_turns_alternate_and_white_moves_first(void) {
    game.reset();

    TEST_ASSERT_TRUE(game.tryMove(square(4, 1), square(4, 3)));  // e2-e4
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::Black),
                            static_cast<uint8_t>(game.sideToMove()));

    TEST_ASSERT_TRUE(game.tryMove(square(4, 6), square(4, 4)));  // e7-e5
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::White),
                            static_cast<uint8_t>(game.sideToMove()));
    TEST_ASSERT_EQUAL_UINT16(2, game.plyCount());
}

void test_cannot_move_a_piece_of_the_side_not_to_move(void) {
    game.reset();

    TEST_ASSERT_FALSE(game.tryMove(square(4, 6), square(4, 4)));  // Black on move one
    TEST_ASSERT_FALSE(game.canSelect(square(4, 6)));
    TEST_ASSERT_TRUE(game.canSelect(square(4, 1)));
    TEST_ASSERT_EQUAL_UINT16(0, game.plyCount());
}

void test_illegal_move_is_rejected_and_changes_nothing(void) {
    game.reset();

    TEST_ASSERT_FALSE(game.tryMove(square(4, 1), square(4, 5)));  // e2-e6 is not a pawn move
    TEST_ASSERT_EQUAL_UINT16(0, game.plyCount());
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Pawn, Side::White),
                            game.position().squares[square(4, 1)]);
}

void test_blocked_piece_cannot_be_selected(void) {
    game.reset();
    TEST_ASSERT_FALSE(game.canSelect(square(0, 0)));  // rook boxed in behind its own pawn
}

// --- Terminal states ---------------------------------------------------------

void test_fools_mate_ends_the_game_with_black_winning(void) {
    game.reset();

    TEST_ASSERT_TRUE(game.tryMove(square(5, 1), square(5, 2)));  // 1. f3
    TEST_ASSERT_TRUE(game.tryMove(square(4, 6), square(4, 4)));  // 1... e5
    TEST_ASSERT_TRUE(game.tryMove(square(6, 1), square(6, 3)));  // 2. g4
    TEST_ASSERT_TRUE(game.tryMove(square(3, 7), square(7, 3)));  // 2... Qh4#

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Checkmate),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::Black),
                            static_cast<uint8_t>(game.winner()));
    TEST_ASSERT_TRUE(isGameOver(game.status()));
    TEST_ASSERT_EQUAL_INT(square(4, 0), game.checkedKingSquare());
}

void test_no_moves_accepted_after_the_game_ends(void) {
    game.reset();
    game.tryMove(square(5, 1), square(5, 2));
    game.tryMove(square(4, 6), square(4, 4));
    game.tryMove(square(6, 1), square(6, 3));
    game.tryMove(square(3, 7), square(7, 3));

    const uint16_t pliesAtMate = game.plyCount();
    TEST_ASSERT_FALSE(game.tryMove(square(4, 0), square(5, 1)));
    TEST_ASSERT_EQUAL_UINT16(pliesAtMate, game.plyCount());
}

void test_check_is_reported_without_ending_the_game(void) {
    // Black king on e8 faces a white rook on e1 down the open e-file.
    TEST_ASSERT_TRUE(game.resetFromFen("4k3/8/8/8/8/8/8/4R1K1 b - - 0 1"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Check),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_FALSE(isGameOver(game.status()));
    TEST_ASSERT_EQUAL_INT(square(4, 7), game.checkedKingSquare());
}

void test_stalemate_is_detected(void) {
    TEST_ASSERT_TRUE(game.resetFromFen("k7/2Q5/1K6/8/8/8/8/8 b - - 0 1"));

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Stalemate),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_TRUE(isGameOver(game.status()));
}

void test_insufficient_material_is_a_draw(void) {
    // The rook capture leaves bare kings.
    TEST_ASSERT_TRUE(game.resetFromFen("8/8/8/3rK3/8/8/8/7k w - - 0 1"));
    TEST_ASSERT_TRUE(game.tryMove(square(4, 4), square(3, 4)));  // Kxd5

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::DrawInsufficientMaterial),
                            static_cast<uint8_t>(game.status()));
}

void test_fifty_move_rule_is_a_draw(void) {
    // 99 halfmoves already elapsed; one more quiet move reaches 100.
    TEST_ASSERT_TRUE(game.resetFromFen("4k3/8/8/8/8/8/4R3/4K3 w - - 99 60"));
    TEST_ASSERT_TRUE(game.tryMove(square(4, 1), square(0, 1)));  // Ra2, quiet

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::DrawFiftyMove),
                            static_cast<uint8_t>(game.status()));
}

void test_threefold_repetition_is_a_draw(void) {
    game.reset();

    // Knights out and back twice: the starting position occurs three times.
    for (int repetition = 0; repetition < 2; ++repetition) {
        TEST_ASSERT_TRUE(game.tryMove(square(6, 0), square(5, 2)));  // Ng1-f3
        TEST_ASSERT_TRUE(game.tryMove(square(6, 7), square(5, 5)));  // Ng8-f6
        TEST_ASSERT_TRUE(game.tryMove(square(5, 2), square(6, 0)));  // Nf3-g1
        TEST_ASSERT_TRUE(game.tryMove(square(5, 5), square(6, 7)));  // Nf6-g8
    }

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::DrawRepetition),
                            static_cast<uint8_t>(game.status()));
}

void test_resigning_hands_the_win_to_the_opponent(void) {
    game.reset();
    game.resign(Side::White);

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Resigned),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::Black),
                            static_cast<uint8_t>(game.winner()));
    TEST_ASSERT_FALSE(game.tryMove(square(4, 1), square(4, 3)));
}

// --- UI-facing helpers -------------------------------------------------------

void test_legal_moves_from_a_square(void) {
    game.reset();

    Move    moves[32];
    uint8_t count = game.legalMovesFrom(square(6, 0), moves, 32);  // Ng1

    TEST_ASSERT_EQUAL_UINT8(2, count);  // f3 and h3
    for (uint8_t i = 0; i < count; ++i) {
        TEST_ASSERT_EQUAL_INT(square(6, 0), moves[i].from);
    }
}

void test_promotion_needs_an_explicit_choice(void) {
    TEST_ASSERT_TRUE(game.resetFromFen("8/4P3/8/8/8/8/8/4K2k w - - 0 1"));

    TEST_ASSERT_TRUE(game.needsPromotionChoice(square(4, 6), square(4, 7)));
    TEST_ASSERT_FALSE(game.tryMove(square(4, 6), square(4, 7), PieceType::None));

    TEST_ASSERT_TRUE(game.tryMove(square(4, 6), square(4, 7), PieceType::Knight));
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Knight, Side::White),
                            game.position().squares[square(4, 7)]);
}

void test_captures_are_recorded_per_losing_side(void) {
    // White pawn on e4, black pawn on d5.
    TEST_ASSERT_TRUE(game.resetFromFen("4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1"));
    TEST_ASSERT_TRUE(game.tryMove(square(4, 3), square(3, 4)));  // exd5

    TEST_ASSERT_EQUAL_UINT8(1, game.capturedCount(Side::Black));
    TEST_ASSERT_EQUAL_UINT8(0, game.capturedCount(Side::White));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PieceType::Pawn),
                            static_cast<uint8_t>(game.capturedPiece(Side::Black, 0)));
}

void test_en_passant_capture_is_recorded(void) {
    TEST_ASSERT_TRUE(game.resetFromFen("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"));
    TEST_ASSERT_TRUE(game.tryMove(square(4, 4), square(3, 5)));  // exd6 e.p.

    TEST_ASSERT_EQUAL_UINT8(1, game.capturedCount(Side::Black));
    TEST_ASSERT_TRUE(isEmpty(game.position().squares[square(3, 4)]));
}

void test_last_move_reflects_the_move_just_played(void) {
    game.reset();
    TEST_ASSERT_TRUE(game.tryMove(square(4, 1), square(4, 3)));

    const Move* last = game.lastMove();
    TEST_ASSERT_NOT_NULL(last);
    TEST_ASSERT_EQUAL_INT(square(4, 1), last->from);
    TEST_ASSERT_EQUAL_INT(square(4, 3), last->to);
}

void test_reset_clears_everything(void) {
    game.reset();
    game.tryMove(square(4, 1), square(4, 3));
    game.resign(Side::White);
    game.reset();

    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(GameStatus::Playing),
                            static_cast<uint8_t>(game.status()));
    TEST_ASSERT_EQUAL_UINT16(0, game.plyCount());
    TEST_ASSERT_EQUAL_UINT8(0, game.capturedCount(Side::White));
    TEST_ASSERT_EQUAL_UINT8(0, game.capturedCount(Side::Black));
    TEST_ASSERT_NULL(game.lastMove());
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_new_game_starts_with_white_and_twenty_moves);
    RUN_TEST(test_turns_alternate_and_white_moves_first);
    RUN_TEST(test_cannot_move_a_piece_of_the_side_not_to_move);
    RUN_TEST(test_illegal_move_is_rejected_and_changes_nothing);
    RUN_TEST(test_blocked_piece_cannot_be_selected);

    RUN_TEST(test_fools_mate_ends_the_game_with_black_winning);
    RUN_TEST(test_no_moves_accepted_after_the_game_ends);
    RUN_TEST(test_check_is_reported_without_ending_the_game);
    RUN_TEST(test_stalemate_is_detected);
    RUN_TEST(test_insufficient_material_is_a_draw);
    RUN_TEST(test_fifty_move_rule_is_a_draw);
    RUN_TEST(test_threefold_repetition_is_a_draw);
    RUN_TEST(test_resigning_hands_the_win_to_the_opponent);

    RUN_TEST(test_legal_moves_from_a_square);
    RUN_TEST(test_promotion_needs_an_explicit_choice);
    RUN_TEST(test_captures_are_recorded_per_losing_side);
    RUN_TEST(test_en_passant_capture_is_recorded);
    RUN_TEST(test_last_move_reflects_the_move_just_played);
    RUN_TEST(test_reset_clears_everything);

    return UNITY_END();
}
