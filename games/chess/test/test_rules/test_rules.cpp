/*
 * Unit tests for the chess rules core.
 *
 * The heart of this suite is perft: a full move-generation tree walk whose node
 * counts are published and independently verified. If castling, en passant,
 * promotion or check evasion is wrong anywhere, a perft count diverges. That is
 * a far stronger signal than hand-picked assertions.
 *
 * Reference values: https://www.chessprogramming.org/Perft_Results
 */
#include <unity.h>

#include "chess/ChessRules.h"

using namespace chess;

/** Recursively count leaf nodes of the legal move tree at the given depth. */
static uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1;

    MoveList moves;
    generateLegalMoves(pos, moves);

    if (depth == 1) return moves.count;

    uint64_t nodes = 0;
    for (uint8_t i = 0; i < moves.count; ++i) {
        Undo undo;
        makeMove(pos, moves.moves[i], undo);
        nodes += perft(pos, depth - 1);
        unmakeMove(pos, moves.moves[i], undo);
    }
    return nodes;
}

static Position fromFen(const char* fen) {
    Position pos;
    TEST_ASSERT_TRUE_MESSAGE(parseFen(fen, pos), fen);
    return pos;
}

// --- Board setup -------------------------------------------------------------

void test_initial_position_has_32_pieces(void) {
    Position pos;
    setupInitialPosition(pos);

    int count = 0;
    for (Square sq = 0; sq < 64; ++sq) {
        if (!isEmpty(pos.squares[sq])) ++count;
    }
    TEST_ASSERT_EQUAL_INT(32, count);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Side::White),
                            static_cast<uint8_t>(pos.sideToMove));
    TEST_ASSERT_EQUAL_UINT8(kAllCastling, pos.castlingRights);
}

void test_initial_position_piece_placement(void) {
    Position pos;
    setupInitialPosition(pos);

    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Rook, Side::White), pos.squares[square(0, 0)]);
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::King, Side::White), pos.squares[square(4, 0)]);
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Queen, Side::White), pos.squares[square(3, 0)]);
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Pawn, Side::Black), pos.squares[square(0, 6)]);
    TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::King, Side::Black), pos.squares[square(4, 7)]);
    TEST_ASSERT_TRUE(isEmpty(pos.squares[square(4, 3)]));
}

// --- Perft: the real correctness gate ----------------------------------------

void test_perft_initial_position(void) {
    Position pos;
    setupInitialPosition(pos);

    TEST_ASSERT_EQUAL_UINT64(20u, perft(pos, 1));
    TEST_ASSERT_EQUAL_UINT64(400u, perft(pos, 2));
    TEST_ASSERT_EQUAL_UINT64(8902u, perft(pos, 3));
    TEST_ASSERT_EQUAL_UINT64(197281u, perft(pos, 4));
}

/** Kiwipete: dense in castling, en passant and pins. */
void test_perft_kiwipete(void) {
    Position pos = fromFen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");

    TEST_ASSERT_EQUAL_UINT64(48u, perft(pos, 1));
    TEST_ASSERT_EQUAL_UINT64(2039u, perft(pos, 2));
    TEST_ASSERT_EQUAL_UINT64(97862u, perft(pos, 3));
}

/** Position 3: en passant discovered checks along a rank. */
void test_perft_endgame_en_passant(void) {
    Position pos = fromFen("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");

    TEST_ASSERT_EQUAL_UINT64(14u, perft(pos, 1));
    TEST_ASSERT_EQUAL_UINT64(191u, perft(pos, 2));
    TEST_ASSERT_EQUAL_UINT64(2812u, perft(pos, 3));
    TEST_ASSERT_EQUAL_UINT64(43238u, perft(pos, 4));
}

/** Position 4: promotions, including under-promotion with check. */
void test_perft_promotions(void) {
    Position pos = fromFen("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1");

    TEST_ASSERT_EQUAL_UINT64(6u, perft(pos, 1));
    TEST_ASSERT_EQUAL_UINT64(264u, perft(pos, 2));
    TEST_ASSERT_EQUAL_UINT64(9467u, perft(pos, 3));
}

/** Position 5: a known trap for buggy castling-rights updates. */
void test_perft_castling_rights(void) {
    Position pos = fromFen("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");

    TEST_ASSERT_EQUAL_UINT64(44u, perft(pos, 1));
    TEST_ASSERT_EQUAL_UINT64(1486u, perft(pos, 2));
    TEST_ASSERT_EQUAL_UINT64(62379u, perft(pos, 3));
}

// --- Terminal states ---------------------------------------------------------

void test_fools_mate_is_checkmate(void) {
    // 1. f3 e5 2. g4 Qh4#
    Position pos = fromFen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");

    TEST_ASSERT_TRUE(isInCheck(pos, Side::White));
    MoveList moves;
    generateLegalMoves(pos, moves);
    TEST_ASSERT_EQUAL_UINT8(0, moves.count);
}

void test_stalemate_has_no_moves_and_no_check(void) {
    // Black to move, king on a8 boxed in by the queen on c7: stalemate, not mate.
    Position pos = fromFen("k7/2Q5/1K6/8/8/8/8/8 b - - 0 1");

    TEST_ASSERT_FALSE(isInCheck(pos, Side::Black));
    MoveList moves;
    generateLegalMoves(pos, moves);
    TEST_ASSERT_EQUAL_UINT8(0, moves.count);
}

void test_pinned_piece_cannot_move(void) {
    // The white knight on e2 is pinned to the king on e1 by the rook on e8.
    Position pos = fromFen("4r3/8/8/8/8/8/4N3/4K3 w - - 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (uint8_t i = 0; i < moves.count; ++i) {
        TEST_ASSERT_NOT_EQUAL(square(4, 1), moves.moves[i].from);
    }
}

// --- Special moves -----------------------------------------------------------

void test_castling_is_generated_when_legal(void) {
    Position pos = fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);

    bool kingSide = false, queenSide = false;
    for (uint8_t i = 0; i < moves.count; ++i) {
        if (moves.moves[i].flags & kCastleKing) kingSide = true;
        if (moves.moves[i].flags & kCastleQueen) queenSide = true;
    }
    TEST_ASSERT_TRUE(kingSide);
    TEST_ASSERT_TRUE(queenSide);
}

void test_castling_blocked_through_attacked_square(void) {
    // The black rook on f8 attacks f1, so White may not castle king-side.
    Position pos = fromFen("5r2/8/8/8/8/8/8/4K2R w K - 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (uint8_t i = 0; i < moves.count; ++i) {
        TEST_ASSERT_FALSE(moves.moves[i].flags & kCastleKing);
    }
}

void test_castling_moves_the_rook(void) {
    Position pos = fromFen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (uint8_t i = 0; i < moves.count; ++i) {
        if (!(moves.moves[i].flags & kCastleKing)) continue;

        Undo undo;
        makeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::King, Side::White), pos.squares[square(6, 0)]);
        TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Rook, Side::White), pos.squares[square(5, 0)]);
        TEST_ASSERT_TRUE(isEmpty(pos.squares[square(7, 0)]));
        unmakeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Rook, Side::White), pos.squares[square(7, 0)]);
        return;
    }
    TEST_FAIL_MESSAGE("king-side castling was not generated");
}

void test_en_passant_removes_the_captured_pawn(void) {
    // White pawn on e5, Black just played d7-d5, so d6 is the en passant target.
    Position pos = fromFen("8/8/8/3pP3/8/8/8/4K2k w - d6 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (uint8_t i = 0; i < moves.count; ++i) {
        if (!(moves.moves[i].flags & kEnPassant)) continue;

        Undo undo;
        makeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Pawn, Side::White), pos.squares[square(3, 5)]);
        TEST_ASSERT_TRUE(isEmpty(pos.squares[square(3, 4)]));  // the captured pawn is gone
        unmakeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(makePiece(PieceType::Pawn, Side::Black), pos.squares[square(3, 4)]);
        return;
    }
    TEST_FAIL_MESSAGE("en passant capture was not generated");
}

void test_promotion_offers_four_choices(void) {
    Position pos = fromFen("8/4P3/8/8/8/8/8/4K2k w - - 0 1");

    MoveList moves;
    generateLegalMoves(pos, moves);

    int promotions = 0;
    bool queen = false, rook = false, bishop = false, knight = false;
    for (uint8_t i = 0; i < moves.count; ++i) {
        if (!(moves.moves[i].flags & kPromotion)) continue;
        ++promotions;
        switch (moves.moves[i].promotion) {
            case PieceType::Queen:  queen = true; break;
            case PieceType::Rook:   rook = true; break;
            case PieceType::Bishop: bishop = true; break;
            case PieceType::Knight: knight = true; break;
            default: TEST_FAIL_MESSAGE("invalid promotion piece"); break;
        }
    }
    TEST_ASSERT_EQUAL_INT(4, promotions);
    TEST_ASSERT_TRUE(queen && rook && bishop && knight);
}

// --- Draw conditions ---------------------------------------------------------

void test_insufficient_material(void) {
    TEST_ASSERT_TRUE(hasInsufficientMaterial(fromFen("8/8/8/4k3/8/8/8/4K3 w - - 0 1")));    // K vs K
    TEST_ASSERT_TRUE(hasInsufficientMaterial(fromFen("8/8/8/4k3/8/8/5N2/4K3 w - - 0 1")));  // K+N vs K
    TEST_ASSERT_TRUE(hasInsufficientMaterial(fromFen("8/8/8/4k3/8/8/5B2/4K3 w - - 0 1")));  // K+B vs K
    TEST_ASSERT_FALSE(hasInsufficientMaterial(fromFen("8/8/8/4k3/8/8/5R2/4K3 w - - 0 1"))); // K+R vs K
    TEST_ASSERT_FALSE(hasInsufficientMaterial(fromFen("8/8/8/4k3/8/8/5P2/4K3 w - - 0 1"))); // K+P vs K
}

void test_halfmove_clock_resets_on_pawn_move(void) {
    Position pos = fromFen("8/8/8/3p4/4P3/8/8/4K2k w - - 12 30");

    MoveList moves;
    generateLegalMoves(pos, moves);
    for (uint8_t i = 0; i < moves.count; ++i) {
        if (moves.moves[i].from != square(4, 3)) continue;  // any pawn move from e4
        Undo undo;
        makeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(0, pos.halfmoveClock);
        unmakeMove(pos, moves.moves[i], undo);
        TEST_ASSERT_EQUAL_UINT8(12, pos.halfmoveClock);
        return;
    }
    TEST_FAIL_MESSAGE("no pawn move generated");
}

void test_repetition_key_matches_for_equal_positions(void) {
    Position a, b;
    setupInitialPosition(a);
    setupInitialPosition(b);
    TEST_ASSERT_EQUAL_UINT64(positionKey(a), positionKey(b));

    MoveList moves;
    generateLegalMoves(a, moves);
    Undo undo;
    makeMove(a, moves.moves[0], undo);
    TEST_ASSERT_TRUE(positionKey(a) != positionKey(b));
    unmakeMove(a, moves.moves[0], undo);
    TEST_ASSERT_EQUAL_UINT64(positionKey(a), positionKey(b));
}

// --- Runner ------------------------------------------------------------------

void setUp(void) {}
void tearDown(void) {}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_initial_position_has_32_pieces);
    RUN_TEST(test_initial_position_piece_placement);

    RUN_TEST(test_perft_initial_position);
    RUN_TEST(test_perft_kiwipete);
    RUN_TEST(test_perft_endgame_en_passant);
    RUN_TEST(test_perft_promotions);
    RUN_TEST(test_perft_castling_rights);

    RUN_TEST(test_fools_mate_is_checkmate);
    RUN_TEST(test_stalemate_has_no_moves_and_no_check);
    RUN_TEST(test_pinned_piece_cannot_move);

    RUN_TEST(test_castling_is_generated_when_legal);
    RUN_TEST(test_castling_blocked_through_attacked_square);
    RUN_TEST(test_castling_moves_the_rook);
    RUN_TEST(test_en_passant_removes_the_captured_pawn);
    RUN_TEST(test_promotion_offers_four_choices);

    RUN_TEST(test_insufficient_material);
    RUN_TEST(test_halfmove_clock_resets_on_pawn_move);
    RUN_TEST(test_repetition_key_matches_for_equal_positions);

    return UNITY_END();
}
