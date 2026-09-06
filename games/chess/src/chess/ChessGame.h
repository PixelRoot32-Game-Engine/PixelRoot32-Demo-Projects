/*
 * ChessGame.h - One game of chess: position, history and terminal state.
 *
 * ChessRules answers "what is legal here". ChessGame is what a match needs on
 * top of that: whose turn it is, what has been captured, whether the game is
 * over and why, and the position history the repetition rule needs.
 *
 * Like the rules layer this has no engine dependency and allocates nothing.
 */
#pragma once

#include "chess/ChessRules.h"

namespace chess {

/**
 * @enum GameStatus
 * @brief How the game currently stands.
 *
 * Check is deliberately not a terminal value: the side to move still has an
 * answer, and the UI wants to say so without ending the game.
 */
enum class GameStatus : uint8_t {
    Playing = 0,
    Check,                     ///< Side to move is in check but has an answer.
    Checkmate,
    Stalemate,
    DrawFiftyMove,
    DrawRepetition,
    DrawInsufficientMaterial,
    Resigned
};

/**
 * @brief Whether a status means the game has ended.
 * @param status Status to classify.
 * @return true for every status except Playing and Check.
 */
constexpr bool isGameOver(GameStatus status) {
    return status != GameStatus::Playing && status != GameStatus::Check;
}

/**
 * @class ChessGame
 * @brief A single match: the current position, its history and its outcome.
 *
 * Every buffer is fixed-size so a game can be a plain member of a scene with no
 * heap traffic at any point.
 */
class ChessGame {
public:
    /**
     * @brief Plies kept for history and repetition detection.
     *
     * A game that runs past this keeps playing correctly; only the recorded
     * history stops growing, and in practice the fifty-move rule ends things
     * long before.
     */
    static constexpr uint16_t kMaxPlies = 300;

    /** @brief Most pieces one side can lose. */
    static constexpr uint8_t kMaxCaptured = 16;

    /** @brief Construct a game already set up at the starting position. */
    ChessGame() { reset(); }

    /** @brief Start a fresh game from the standard opening array. */
    void reset();

    /**
     * @brief Start from a FEN string.
     * @param fen Null-terminated FEN string.
     * @return true on success; the game is left untouched on bad input.
     */
    bool resetFromFen(const char* fen);

    /**
     * @brief The position as it stands.
     * @return Const reference to the live position.
     */
    const Position& position() const { return position_; }

    /**
     * @brief Whose turn it is.
     * @return The side to move.
     */
    Side sideToMove() const { return position_.sideToMove; }

    /**
     * @brief How the game currently stands.
     * @return The current status.
     */
    GameStatus status() const { return status_; }

    /**
     * @brief Winner of a decided game.
     * @return The winning side. Meaningless unless status() is Checkmate or Resigned.
     */
    Side winner() const { return winner_; }

    /**
     * @brief King the UI should flag as being in check.
     * @return Its square, or kNoSquare when nobody is in check.
     */
    Square checkedKingSquare() const;

    /**
     * @brief Every legal move in the current position.
     * @return Const reference to the cached move list.
     */
    const MoveList& legalMoves() const { return legalMoves_; }

    /**
     * @brief Collect the legal moves starting at one square.
     * @param from Origin square.
     * @param out Caller-owned array receiving the moves.
     * @param capacity Size of `out`; extra moves are dropped.
     * @return How many moves were written.
     */
    uint8_t legalMovesFrom(Square from, Move* out, uint8_t capacity) const;

    /**
     * @brief Whether a square holds a piece its owner can actually move.
     * @param from Square to test.
     * @return true when the side to move owns a piece there with a legal move.
     */
    bool canSelect(Square from) const;

    /**
     * @brief Whether a move is legal but ambiguous until a piece is chosen.
     *
     * The UI uses this to open the promotion picker before committing.
     *
     * @param from Origin square.
     * @param to Destination square.
     * @return true when from->to is a pawn promotion.
     */
    bool needsPromotionChoice(Square from, Square to) const;

    /**
     * @brief Play a move.
     * @param from Origin square.
     * @param to Destination square.
     * @param promotion Piece to promote to; ignored unless the move is a
     *        promotion, in which case PieceType::None is rejected.
     * @return true when the move was played. On false the game is untouched,
     *         either because the move is illegal or because the game has ended.
     */
    bool tryMove(Square from, Square to, PieceType promotion = PieceType::None);

    /**
     * @brief End the game immediately; the other side wins.
     * @param side The side giving up.
     */
    void resign(Side side);

    /**
     * @brief The move just played.
     * @return Pointer to it, or nullptr at the start of a game.
     */
    const Move* lastMove() const;

    /**
     * @brief How many plies have been played.
     * @return The ply count, capped at kMaxPlies.
     */
    uint16_t plyCount() const { return plyCount_; }

    /**
     * @brief How many pieces a side has lost.
     * @param loser Side whose losses are counted.
     * @return The number of its pieces that have been captured.
     */
    uint8_t capturedCount(Side loser) const { return capturedCount_[static_cast<uint8_t>(loser)]; }

    /**
     * @brief One captured piece, in the order it was taken.
     * @param loser Side that lost the piece.
     * @param index Position in the capture order.
     * @return The piece type, or PieceType::None when index is out of range.
     */
    PieceType capturedPiece(Side loser, uint8_t index) const;

private:
    /** @brief Recompute the legal moves and the status after the board changes. */
    void refreshDerivedState();

    /**
     * @brief Add a piece to its owner's captured list.
     * @param piece The piece that was taken.
     */
    void recordCapture(Piece piece);

    /**
     * @brief How often the current position has occurred this game.
     * @return The occurrence count, including the present one.
     */
    uint8_t repetitionCount() const;

    Position   position_{};
    MoveList   legalMoves_{};
    GameStatus status_ = GameStatus::Playing;
    Side       winner_ = Side::White;

    Move     history_[kMaxPlies]{};
    uint64_t positionKeys_[kMaxPlies + 1]{};  ///< Key before each ply, plus the current one.
    uint16_t plyCount_ = 0;

    PieceType captured_[2][kMaxCaptured]{};   ///< Indexed by the Side that lost them.
    uint8_t   capturedCount_[2]{};
};

}  // namespace chess
