/*
 * ChessRules.h - Traditional chess rules: board, move generation, legality.
 *
 * This header has no engine dependency on purpose. The rules are pure data and
 * pure functions so they can be unit-tested on the host (see test/test_rules)
 * without a display, and so the scene layer stays a thin presenter over them.
 *
 * Everything is fixed-size: no allocation happens anywhere in this module, which
 * is what lets it run inside the game loop on an ESP32.
 */
#pragma once

#include <cstdint>

namespace chess {

// --- Pieces ------------------------------------------------------------------

/**
 * @enum PieceType
 * @brief Kind of piece occupying a square.
 *
 * None is 0 so that a packed Piece of value 0 reads as an empty square, which
 * lets a board be cleared with a single zero-fill.
 */
enum class PieceType : uint8_t {
    None = 0,
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King
};

/**
 * @enum Side
 * @brief Which player owns a piece, and whose turn it is.
 */
enum class Side : uint8_t {
    White = 0,
    Black = 1
};

/**
 * @brief The other player.
 * @param side Side to flip.
 * @return Black for White, White for Black.
 */
constexpr Side opponent(Side side) {
    return side == Side::White ? Side::Black : Side::White;
}

/**
 * @brief A piece packed into one byte: bits 0-2 the type, bit 3 the side.
 *
 * Packing keeps a whole position at 64 bytes, small enough to copy by value
 * when generating legal moves rather than unwinding a move stack.
 */
using Piece = uint8_t;

/** @brief Value stored in an unoccupied square. */
constexpr Piece kEmptySquare = 0;

/**
 * @brief Pack a type and a side into a Piece.
 * @param type Kind of piece.
 * @param side Owning player.
 * @return The packed piece byte.
 */
constexpr Piece makePiece(PieceType type, Side side) {
    return static_cast<Piece>(static_cast<uint8_t>(type) | (static_cast<uint8_t>(side) << 3));
}

/**
 * @brief Kind of piece held in a packed byte.
 * @param piece Packed piece.
 * @return Its PieceType, or PieceType::None if the square is empty.
 */
constexpr PieceType typeOf(Piece piece) {
    return static_cast<PieceType>(piece & 0x07);
}

/**
 * @brief Owner of a packed piece.
 * @param piece Packed piece; must not be empty.
 * @return The owning side.
 */
constexpr Side sideOf(Piece piece) {
    return static_cast<Side>((piece >> 3) & 0x01);
}

/**
 * @brief Whether a packed byte represents an empty square.
 * @param piece Packed piece.
 * @return true when no piece occupies the square.
 */
constexpr bool isEmpty(Piece piece) {
    return (piece & 0x07) == 0;
}

// --- Squares -----------------------------------------------------------------

/**
 * @brief Square index 0..63, a1 = 0, h1 = 7, a8 = 56.
 *
 * Signed so that kNoSquare can be a negative sentinel instead of costing a
 * separate validity flag at every call site.
 */
using Square = int8_t;

/** @brief Sentinel for "no square", e.g. a point outside the board. */
constexpr Square kNoSquare = -1;

/**
 * @brief File (column) of a square.
 * @param sq Square index.
 * @return 0 for the a-file through 7 for the h-file.
 */
constexpr int fileOf(Square sq) { return sq & 7; }

/**
 * @brief Rank (row) of a square.
 * @param sq Square index.
 * @return 0 for rank 1 (White's back rank) through 7 for rank 8.
 */
constexpr int rankOf(Square sq) { return sq >> 3; }

/**
 * @brief Square index for a file and rank.
 * @param file 0..7, a through h.
 * @param rank 0..7, rank 1 through rank 8.
 * @return The packed square index.
 */
constexpr Square square(int file, int rank) {
    return static_cast<Square>(rank * 8 + file);
}

/**
 * @brief Whether a file/rank pair lies on the board.
 * @param file Candidate file, may be out of range.
 * @param rank Candidate rank, may be out of range.
 * @return true when both are within 0..7.
 */
constexpr bool onBoard(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

// --- Moves -------------------------------------------------------------------

/**
 * @enum CastlingRight
 * @brief Bit per castling right, held together in one byte of a Position.
 */
enum CastlingRight : uint8_t {
    kWhiteKingSide  = 1,
    kWhiteQueenSide = 2,
    kBlackKingSide  = 4,
    kBlackQueenSide = 8,
    kAllCastling    = 15
};

/**
 * @enum MoveFlag
 * @brief What a move does beyond moving a piece, as an OR-able bit set.
 *
 * A move can carry several at once - a promotion that also captures sets both
 * kPromotion and kCapture - which is why these are flags and not an enum of
 * mutually exclusive move kinds.
 */
enum MoveFlag : uint8_t {
    kQuiet       = 0,
    kCapture     = 1 << 0,
    kDoublePush  = 1 << 1,
    kEnPassant   = 1 << 2,
    kCastleKing  = 1 << 3,
    kCastleQueen = 1 << 4,
    kPromotion   = 1 << 5
};

/**
 * @struct Move
 * @brief One move: where from, where to, and what is special about it.
 */
struct Move {
    Square    from      = kNoSquare;      ///< Origin square.
    Square    to        = kNoSquare;      ///< Destination square.
    uint8_t   flags     = kQuiet;         ///< OR of MoveFlag bits.
    PieceType promotion = PieceType::None;///< Chosen piece; only read when kPromotion is set.

    /**
     * @brief Whether this is the default-constructed placeholder.
     * @return true when the move names no origin square.
     */
    bool isNull() const { return from == kNoSquare; }
};

/**
 * @brief Compare two moves field by field.
 * @param a First move.
 * @param b Second move.
 * @return true when both describe the same move, promotion piece included.
 */
inline bool operator==(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.flags == b.flags && a.promotion == b.promotion;
}

/**
 * @struct MoveList
 * @brief Fixed-capacity list of moves.
 *
 * 218 is the highest number of legal moves reachable in any legal chess
 * position; the capacity here leaves headroom for pseudo-legal generation,
 * which runs before illegal moves are filtered out. Fixed size keeps move
 * generation allocation-free so it can run inside the game loop.
 */
struct MoveList {
    static constexpr uint8_t kCapacity = 255;  ///< Slots available.

    Move    moves[kCapacity];  ///< The moves; only the first `count` are valid.
    uint8_t count = 0;         ///< Number of moves currently held.

    /** @brief Drop every move, keeping the storage. */
    void clear() { count = 0; }

    /**
     * @brief Append a move, silently ignoring it when the list is full.
     * @param move Move to append.
     */
    void add(const Move& move) {
        if (count < kCapacity) moves[count++] = move;
    }
};

// --- Position ----------------------------------------------------------------

/**
 * @struct Position
 * @brief A complete chess position: placement plus everything the rules need.
 */
struct Position {
    Piece    squares[64]     = {};          ///< Board contents, indexed by Square.
    Side     sideToMove      = Side::White; ///< Player to move.
    uint8_t  castlingRights  = kAllCastling;///< OR of CastlingRight bits still available.
    Square   epSquare        = kNoSquare;   ///< Square *behind* a double push, or kNoSquare.
    uint8_t  halfmoveClock   = 0;           ///< Plies since the last capture or pawn move.
    uint16_t fullmoveNumber  = 1;           ///< Move number, incremented after Black moves.
};

/**
 * @struct Undo
 * @brief State that makeMove destroys and unmakeMove needs to restore.
 *
 * Everything else in a Position is recoverable from the move itself, so only
 * the lost information is recorded.
 */
struct Undo {
    Piece   captured       = kEmptySquare;  ///< Piece taken, or kEmptySquare.
    Square  capturedSquare = kNoSquare;     ///< Differs from Move::to on en passant.
    uint8_t castlingRights = 0;             ///< Rights before the move.
    Square  epSquare       = kNoSquare;     ///< En passant target before the move.
    uint8_t halfmoveClock  = 0;             ///< Clock before the move.
};

// --- API ---------------------------------------------------------------------

/**
 * @brief Reset to the standard starting array with White to move.
 * @param pos Position to overwrite.
 */
void setupInitialPosition(Position& pos);

/**
 * @brief Parse a FEN string into a position.
 *
 * Used by tests and by fixed openings; the game itself never needs it.
 *
 * @param fen Null-terminated FEN string. The two move counters may be omitted.
 * @param pos Receives the parsed position, and is left untouched on failure.
 * @return true when the whole string parsed.
 */
bool parseFen(const char* fen, Position& pos);

/**
 * @brief Whether a side attacks a square.
 *
 * Ignores whether making that capture would expose the attacker's own king,
 * which is exactly the semantics castling legality and check detection need.
 *
 * @param pos Position to inspect.
 * @param sq Square being attacked.
 * @param by Side doing the attacking.
 * @return true when at least one piece of `by` bears on `sq`.
 */
bool isSquareAttacked(const Position& pos, Square sq, Side by);

/**
 * @brief Locate a king.
 * @param pos Position to search.
 * @param side Whose king to find.
 * @return Its square, or kNoSquare if that king is not on the board.
 */
Square findKing(const Position& pos, Side side);

/**
 * @brief Whether a side is currently in check.
 * @param pos Position to inspect.
 * @param side Side whose king is tested.
 * @return true when that king is attacked.
 */
bool isInCheck(const Position& pos, Side side);

/**
 * @brief Generate every move for the side to move, legal or not.
 *
 * The result still contains moves that leave the mover's own king in check;
 * generateLegalMoves filters those out.
 *
 * @param pos Position to generate from.
 * @param out Receives the moves; cleared first.
 */
void generatePseudoLegalMoves(const Position& pos, MoveList& out);

/**
 * @brief Generate every legal move for the side to move.
 * @param pos Position to generate from.
 * @param out Receives the moves; cleared first. An empty result means mate or
 *            stalemate, which isInCheck then tells apart.
 */
void generateLegalMoves(const Position& pos, MoveList& out);

/**
 * @brief Apply a move to a position.
 * @param pos Position to mutate.
 * @param move Move to play; must be one this position generated.
 * @param undo Receives what unmakeMove needs to take the move back.
 */
void makeMove(Position& pos, const Move& move, Undo& undo);

/**
 * @brief Exact inverse of makeMove for the same move and undo record.
 * @param pos Position to mutate.
 * @param move The move that was played.
 * @param undo The record makeMove filled in for that move.
 */
void unmakeMove(Position& pos, const Move& move, const Undo& undo);

/**
 * @brief Whether neither side can possibly deliver mate.
 *
 * Covers the dead positions of FIDE 9.6: bare kings, king and a single minor,
 * and same-coloured bishops. King and two knights is excluded because mate is
 * reachable there with cooperation, so it is not an automatic draw.
 *
 * @param pos Position to inspect.
 * @return true when the material on the board can never mate.
 */
bool hasInsufficientMaterial(const Position& pos);

/**
 * @brief Hash of everything that makes two positions "the same" for repetition.
 *
 * Covers placement, side to move, castling rights and the en passant file. The
 * move counters are deliberately excluded, because the threefold repetition
 * rule does not care how the position was reached.
 *
 * @param pos Position to hash.
 * @return Its Zobrist-style key.
 */
uint64_t positionKey(const Position& pos);

}  // namespace chess
