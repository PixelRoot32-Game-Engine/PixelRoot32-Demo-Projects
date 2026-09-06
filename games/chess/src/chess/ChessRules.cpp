/*
 * ChessRules.cpp - Implementation of the traditional chess rules.
 *
 * Board representation is a plain 8x8 mailbox indexed 0..63. It is not the
 * fastest scheme available, but this is a teaching demo: readable beats clever,
 * and a full legal move generation at the depth a human game needs costs well
 * under a millisecond on an ESP32.
 */
#include "chess/ChessRules.h"

namespace chess {
namespace {

// --- Direction tables --------------------------------------------------------

struct Offset {
    int8_t file;
    int8_t rank;
};

constexpr Offset kKnightOffsets[8] = {
    {1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
};

constexpr Offset kKingOffsets[8] = {
    {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
};

constexpr Offset kBishopDirections[4] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr Offset kRookDirections[4]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

constexpr PieceType kPromotionChoices[4] = {
    PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight
};

/** Forward direction of a pawn: White moves towards rank 7, Black towards 0. */
constexpr int pawnDirection(Side side) { return side == Side::White ? 1 : -1; }
constexpr int pawnStartRank(Side side) { return side == Side::White ? 1 : 6; }
constexpr int pawnPromotionRank(Side side) { return side == Side::White ? 7 : 0; }

// --- Zobrist hashing ---------------------------------------------------------

/**
 * The hash table is built at compile time so it lives in flash rather than
 * costing 8 KB of ESP32 RAM. splitmix64 is used purely as a deterministic
 * generator of well-distributed constants.
 */
constexpr uint64_t splitmix64(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

struct ZobristKeys {
    uint64_t piece[16][64];
    uint64_t castling[16];
    uint64_t epFile[8];
    uint64_t blackToMove;
};

constexpr ZobristKeys buildZobristKeys() {
    ZobristKeys keys{};
    uint64_t state = 0x243F6A8885A308D3ull;

    for (int piece = 0; piece < 16; ++piece) {
        for (int sq = 0; sq < 64; ++sq) {
            keys.piece[piece][sq] = splitmix64(state);
        }
    }
    for (int rights = 0; rights < 16; ++rights) keys.castling[rights] = splitmix64(state);
    for (int file = 0; file < 8; ++file) keys.epFile[file] = splitmix64(state);
    keys.blackToMove = splitmix64(state);

    return keys;
}

constexpr ZobristKeys kZobrist = buildZobristKeys();

// --- Castling bookkeeping ----------------------------------------------------

/**
 * Rights that must be cleared whenever the piece on this square moves or is
 * captured. Covers both the rook squares and the two king squares in one table.
 */
constexpr uint8_t castlingMaskFor(Square sq) {
    return sq == square(0, 0) ? static_cast<uint8_t>(kWhiteQueenSide)
         : sq == square(7, 0) ? static_cast<uint8_t>(kWhiteKingSide)
         : sq == square(4, 0) ? static_cast<uint8_t>(kWhiteKingSide | kWhiteQueenSide)
         : sq == square(0, 7) ? static_cast<uint8_t>(kBlackQueenSide)
         : sq == square(7, 7) ? static_cast<uint8_t>(kBlackKingSide)
         : sq == square(4, 7) ? static_cast<uint8_t>(kBlackKingSide | kBlackQueenSide)
         : static_cast<uint8_t>(0);
}

// --- Move generation helpers -------------------------------------------------

void addPawnMove(MoveList& out, Square from, Square to, uint8_t flags, Side side) {
    if (rankOf(to) == pawnPromotionRank(side)) {
        for (PieceType promotion : kPromotionChoices) {
            Move move;
            move.from      = from;
            move.to        = to;
            move.flags     = static_cast<uint8_t>(flags | kPromotion);
            move.promotion = promotion;
            out.add(move);
        }
        return;
    }

    Move move;
    move.from  = from;
    move.to    = to;
    move.flags = flags;
    out.add(move);
}

void generatePawnMoves(const Position& pos, Square from, Side us, MoveList& out) {
    const int direction = pawnDirection(us);
    const int file      = fileOf(from);
    const int rank      = rankOf(from);

    // Single and double push. Both target squares must be empty.
    const int oneRank = rank + direction;
    if (onBoard(file, oneRank) && isEmpty(pos.squares[square(file, oneRank)])) {
        addPawnMove(out, from, square(file, oneRank), kQuiet, us);

        const int twoRank = rank + 2 * direction;
        if (rank == pawnStartRank(us) && isEmpty(pos.squares[square(file, twoRank)])) {
            Move move;
            move.from  = from;
            move.to    = square(file, twoRank);
            move.flags = kDoublePush;
            out.add(move);
        }
    }

    // Diagonal captures, including en passant.
    for (int deltaFile = -1; deltaFile <= 1; deltaFile += 2) {
        const int targetFile = file + deltaFile;
        if (!onBoard(targetFile, oneRank)) continue;

        const Square target = square(targetFile, oneRank);
        const Piece  victim = pos.squares[target];

        if (!isEmpty(victim) && sideOf(victim) != us) {
            addPawnMove(out, from, target, kCapture, us);
        } else if (target == pos.epSquare) {
            Move move;
            move.from  = from;
            move.to    = target;
            move.flags = static_cast<uint8_t>(kCapture | kEnPassant);
            out.add(move);
        }
    }
}

void generateStepMoves(const Position& pos, Square from, Side us,
                       const Offset* offsets, int offsetCount, MoveList& out) {
    const int file = fileOf(from);
    const int rank = rankOf(from);

    for (int i = 0; i < offsetCount; ++i) {
        const int targetFile = file + offsets[i].file;
        const int targetRank = rank + offsets[i].rank;
        if (!onBoard(targetFile, targetRank)) continue;

        const Square target = square(targetFile, targetRank);
        const Piece  victim = pos.squares[target];
        if (!isEmpty(victim) && sideOf(victim) == us) continue;

        Move move;
        move.from  = from;
        move.to    = target;
        move.flags = isEmpty(victim) ? static_cast<uint8_t>(kQuiet)
                                     : static_cast<uint8_t>(kCapture);
        out.add(move);
    }
}

void generateSlidingMoves(const Position& pos, Square from, Side us,
                          const Offset* directions, int directionCount, MoveList& out) {
    const int file = fileOf(from);
    const int rank = rankOf(from);

    for (int i = 0; i < directionCount; ++i) {
        int targetFile = file + directions[i].file;
        int targetRank = rank + directions[i].rank;

        while (onBoard(targetFile, targetRank)) {
            const Square target = square(targetFile, targetRank);
            const Piece  victim = pos.squares[target];

            if (!isEmpty(victim) && sideOf(victim) == us) break;

            Move move;
            move.from  = from;
            move.to    = target;
            move.flags = isEmpty(victim) ? static_cast<uint8_t>(kQuiet)
                                     : static_cast<uint8_t>(kCapture);
            out.add(move);

            if (!isEmpty(victim)) break;  // captures end the ray

            targetFile += directions[i].file;
            targetRank += directions[i].rank;
        }
    }
}

/** Shared by both castling sides: every listed square must be empty and safe. */
bool castlingPathIsClear(const Position& pos, Side us, int backRank,
                         const int* emptyFiles, int emptyCount,
                         const int* safeFiles, int safeCount) {
    for (int i = 0; i < emptyCount; ++i) {
        if (!isEmpty(pos.squares[square(emptyFiles[i], backRank)])) return false;
    }
    for (int i = 0; i < safeCount; ++i) {
        if (isSquareAttacked(pos, square(safeFiles[i], backRank), opponent(us))) return false;
    }
    return true;
}

void generateCastlingMoves(const Position& pos, Side us, MoveList& out) {
    const int  backRank  = (us == Side::White) ? 0 : 7;
    const auto kingRight = (us == Side::White) ? kWhiteKingSide : kBlackKingSide;
    const auto queenRight = (us == Side::White) ? kWhiteQueenSide : kBlackQueenSide;

    const Square kingSquare = square(4, backRank);
    if (pos.squares[kingSquare] != makePiece(PieceType::King, us)) return;

    const Piece ourRook = makePiece(PieceType::Rook, us);

    if ((pos.castlingRights & kingRight) && pos.squares[square(7, backRank)] == ourRook) {
        const int empty[] = {5, 6};
        const int safe[]  = {4, 5, 6};
        if (castlingPathIsClear(pos, us, backRank, empty, 2, safe, 3)) {
            Move move;
            move.from  = kingSquare;
            move.to    = square(6, backRank);
            move.flags = kCastleKing;
            out.add(move);
        }
    }

    if ((pos.castlingRights & queenRight) && pos.squares[square(0, backRank)] == ourRook) {
        // b1/b8 must be empty too, but the king never crosses it, so it is not
        // part of the safety check.
        const int empty[] = {1, 2, 3};
        const int safe[]  = {2, 3, 4};
        if (castlingPathIsClear(pos, us, backRank, empty, 3, safe, 3)) {
            Move move;
            move.from  = kingSquare;
            move.to    = square(2, backRank);
            move.flags = kCastleQueen;
            out.add(move);
        }
    }
}

// --- FEN parsing -------------------------------------------------------------

PieceType pieceTypeFromFenChar(char c) {
    switch (c) {
        case 'p': return PieceType::Pawn;
        case 'n': return PieceType::Knight;
        case 'b': return PieceType::Bishop;
        case 'r': return PieceType::Rook;
        case 'q': return PieceType::Queen;
        case 'k': return PieceType::King;
        default:  return PieceType::None;
    }
}

char toLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

const char* skipSpaces(const char* p) {
    while (*p == ' ') ++p;
    return p;
}

}  // namespace

// --- Public API --------------------------------------------------------------

void setupInitialPosition(Position& pos) {
    pos = Position{};

    constexpr PieceType backRank[8] = {
        PieceType::Rook, PieceType::Knight, PieceType::Bishop, PieceType::Queen,
        PieceType::King, PieceType::Bishop, PieceType::Knight, PieceType::Rook
    };

    for (int file = 0; file < 8; ++file) {
        pos.squares[square(file, 0)] = makePiece(backRank[file], Side::White);
        pos.squares[square(file, 1)] = makePiece(PieceType::Pawn, Side::White);
        pos.squares[square(file, 6)] = makePiece(PieceType::Pawn, Side::Black);
        pos.squares[square(file, 7)] = makePiece(backRank[file], Side::Black);
    }
}

bool parseFen(const char* fen, Position& pos) {
    if (fen == nullptr) return false;

    Position parsed{};
    parsed.castlingRights = 0;
    parsed.epSquare       = kNoSquare;

    const char* p    = fen;
    int         file = 0;
    int         rank = 7;  // FEN starts at the 8th rank

    for (; *p != '\0' && *p != ' '; ++p) {
        if (*p == '/') {
            if (file != 8 || rank == 0) return false;
            file = 0;
            --rank;
            continue;
        }
        if (*p >= '1' && *p <= '8') {
            file += *p - '0';
            if (file > 8) return false;
            continue;
        }

        const PieceType type = pieceTypeFromFenChar(toLowerAscii(*p));
        if (type == PieceType::None || file >= 8) return false;

        const Side side = (*p >= 'a' && *p <= 'z') ? Side::Black : Side::White;
        parsed.squares[square(file, rank)] = makePiece(type, side);
        ++file;
    }
    if (file != 8 || rank != 0) return false;

    p = skipSpaces(p);
    if (*p != 'w' && *p != 'b') return false;
    parsed.sideToMove = (*p == 'w') ? Side::White : Side::Black;
    ++p;

    p = skipSpaces(p);
    if (*p == '-') {
        ++p;
    } else {
        for (; *p != '\0' && *p != ' '; ++p) {
            switch (*p) {
                case 'K': parsed.castlingRights |= kWhiteKingSide; break;
                case 'Q': parsed.castlingRights |= kWhiteQueenSide; break;
                case 'k': parsed.castlingRights |= kBlackKingSide; break;
                case 'q': parsed.castlingRights |= kBlackQueenSide; break;
                default:  return false;
            }
        }
    }

    p = skipSpaces(p);
    if (*p == '-') {
        ++p;
    } else if (p[0] >= 'a' && p[0] <= 'h' && p[1] >= '1' && p[1] <= '8') {
        parsed.epSquare = square(p[0] - 'a', p[1] - '1');
        p += 2;
    } else {
        return false;
    }

    // The two move counters are optional; default them when absent.
    p = skipSpaces(p);
    if (*p >= '0' && *p <= '9') {
        int halfmoves = 0;
        for (; *p >= '0' && *p <= '9'; ++p) halfmoves = halfmoves * 10 + (*p - '0');
        parsed.halfmoveClock = static_cast<uint8_t>(halfmoves > 255 ? 255 : halfmoves);
    }

    p = skipSpaces(p);
    if (*p >= '0' && *p <= '9') {
        int fullmoves = 0;
        for (; *p >= '0' && *p <= '9'; ++p) fullmoves = fullmoves * 10 + (*p - '0');
        parsed.fullmoveNumber = static_cast<uint16_t>(fullmoves);
    } else {
        parsed.fullmoveNumber = 1;
    }

    pos = parsed;
    return true;
}

bool isSquareAttacked(const Position& pos, Square sq, Side by) {
    const int file = fileOf(sq);
    const int rank = rankOf(sq);

    // Pawns: a pawn on (f +/-1, rank - direction) attacks this square.
    const int pawnRank = rank - pawnDirection(by);
    for (int deltaFile = -1; deltaFile <= 1; deltaFile += 2) {
        const int pawnFile = file + deltaFile;
        if (!onBoard(pawnFile, pawnRank)) continue;
        if (pos.squares[square(pawnFile, pawnRank)] == makePiece(PieceType::Pawn, by)) return true;
    }

    const Piece enemyKnight = makePiece(PieceType::Knight, by);
    for (const Offset& offset : kKnightOffsets) {
        const int targetFile = file + offset.file;
        const int targetRank = rank + offset.rank;
        if (!onBoard(targetFile, targetRank)) continue;
        if (pos.squares[square(targetFile, targetRank)] == enemyKnight) return true;
    }

    const Piece enemyKing = makePiece(PieceType::King, by);
    for (const Offset& offset : kKingOffsets) {
        const int targetFile = file + offset.file;
        const int targetRank = rank + offset.rank;
        if (!onBoard(targetFile, targetRank)) continue;
        if (pos.squares[square(targetFile, targetRank)] == enemyKing) return true;
    }

    // Sliding pieces: walk each ray until it hits something.
    const PieceType diagonalAttackers[2]   = {PieceType::Bishop, PieceType::Queen};
    const PieceType orthogonalAttackers[2] = {PieceType::Rook, PieceType::Queen};

    for (int pass = 0; pass < 2; ++pass) {
        const Offset*   directions = (pass == 0) ? kBishopDirections : kRookDirections;
        const PieceType* attackers = (pass == 0) ? diagonalAttackers : orthogonalAttackers;

        for (int i = 0; i < 4; ++i) {
            int targetFile = file + directions[i].file;
            int targetRank = rank + directions[i].rank;

            while (onBoard(targetFile, targetRank)) {
                const Piece piece = pos.squares[square(targetFile, targetRank)];
                if (!isEmpty(piece)) {
                    if (sideOf(piece) == by &&
                        (typeOf(piece) == attackers[0] || typeOf(piece) == attackers[1])) {
                        return true;
                    }
                    break;
                }
                targetFile += directions[i].file;
                targetRank += directions[i].rank;
            }
        }
    }

    return false;
}

Square findKing(const Position& pos, Side side) {
    const Piece king = makePiece(PieceType::King, side);
    for (Square sq = 0; sq < 64; ++sq) {
        if (pos.squares[sq] == king) return sq;
    }
    return kNoSquare;
}

bool isInCheck(const Position& pos, Side side) {
    const Square kingSquare = findKing(pos, side);
    if (kingSquare == kNoSquare) return false;
    return isSquareAttacked(pos, kingSquare, opponent(side));
}

void generatePseudoLegalMoves(const Position& pos, MoveList& out) {
    out.clear();
    const Side us = pos.sideToMove;

    for (Square from = 0; from < 64; ++from) {
        const Piece piece = pos.squares[from];
        if (isEmpty(piece) || sideOf(piece) != us) continue;

        switch (typeOf(piece)) {
            case PieceType::Pawn:
                generatePawnMoves(pos, from, us, out);
                break;
            case PieceType::Knight:
                generateStepMoves(pos, from, us, kKnightOffsets, 8, out);
                break;
            case PieceType::Bishop:
                generateSlidingMoves(pos, from, us, kBishopDirections, 4, out);
                break;
            case PieceType::Rook:
                generateSlidingMoves(pos, from, us, kRookDirections, 4, out);
                break;
            case PieceType::Queen:
                generateSlidingMoves(pos, from, us, kBishopDirections, 4, out);
                generateSlidingMoves(pos, from, us, kRookDirections, 4, out);
                break;
            case PieceType::King:
                generateStepMoves(pos, from, us, kKingOffsets, 8, out);
                break;
            default:
                break;
        }
    }

    generateCastlingMoves(pos, us, out);
}

void generateLegalMoves(const Position& pos, MoveList& out) {
    MoveList pseudoLegal;
    generatePseudoLegalMoves(pos, pseudoLegal);

    Position working = pos;
    const Side us    = pos.sideToMove;

    out.clear();
    for (uint8_t i = 0; i < pseudoLegal.count; ++i) {
        Undo undo;
        makeMove(working, pseudoLegal.moves[i], undo);
        if (!isInCheck(working, us)) out.add(pseudoLegal.moves[i]);
        unmakeMove(working, pseudoLegal.moves[i], undo);
    }
}

void makeMove(Position& pos, const Move& move, Undo& undo) {
    const Side  us     = pos.sideToMove;
    const Piece moving = pos.squares[move.from];

    undo.captured       = kEmptySquare;
    undo.capturedSquare = kNoSquare;
    undo.castlingRights = pos.castlingRights;
    undo.epSquare       = pos.epSquare;
    undo.halfmoveClock  = pos.halfmoveClock;

    if (move.flags & kEnPassant) {
        // The captured pawn sits beside the destination, on the origin rank.
        const Square capturedSquare = square(fileOf(move.to), rankOf(move.from));
        undo.captured               = pos.squares[capturedSquare];
        undo.capturedSquare         = capturedSquare;
        pos.squares[capturedSquare] = kEmptySquare;
    } else if (!isEmpty(pos.squares[move.to])) {
        undo.captured       = pos.squares[move.to];
        undo.capturedSquare = move.to;
    }

    pos.squares[move.to] = (move.flags & kPromotion)
                               ? makePiece(move.promotion, us)
                               : moving;
    pos.squares[move.from] = kEmptySquare;

    if (move.flags & (kCastleKing | kCastleQueen)) {
        const int backRank = rankOf(move.to);
        const int rookFrom = (move.flags & kCastleKing) ? 7 : 0;
        const int rookTo   = (move.flags & kCastleKing) ? 5 : 3;
        pos.squares[square(rookTo, backRank)]   = pos.squares[square(rookFrom, backRank)];
        pos.squares[square(rookFrom, backRank)] = kEmptySquare;
    }

    // Moving off a key square, or capturing a rook on one, kills those rights.
    pos.castlingRights &= static_cast<uint8_t>(~castlingMaskFor(move.from));
    pos.castlingRights &= static_cast<uint8_t>(~castlingMaskFor(move.to));

    pos.epSquare = (move.flags & kDoublePush)
                       ? square(fileOf(move.from), rankOf(move.from) + pawnDirection(us))
                       : kNoSquare;

    const bool resetsClock = (typeOf(moving) == PieceType::Pawn) || (undo.captured != kEmptySquare);
    pos.halfmoveClock = resetsClock ? 0
                      : (pos.halfmoveClock < 255 ? static_cast<uint8_t>(pos.halfmoveClock + 1) : 255);

    if (us == Side::Black) ++pos.fullmoveNumber;
    pos.sideToMove = opponent(us);
}

void unmakeMove(Position& pos, const Move& move, const Undo& undo) {
    const Side us = opponent(pos.sideToMove);  // the side that made the move

    if (us == Side::Black && pos.fullmoveNumber > 1) --pos.fullmoveNumber;
    pos.sideToMove     = us;
    pos.castlingRights = undo.castlingRights;
    pos.epSquare       = undo.epSquare;
    pos.halfmoveClock  = undo.halfmoveClock;

    pos.squares[move.from] = (move.flags & kPromotion)
                                 ? makePiece(PieceType::Pawn, us)
                                 : pos.squares[move.to];
    pos.squares[move.to] = kEmptySquare;

    if (undo.capturedSquare != kNoSquare) {
        pos.squares[undo.capturedSquare] = undo.captured;
    }

    if (move.flags & (kCastleKing | kCastleQueen)) {
        const int backRank = rankOf(move.to);
        const int rookFrom = (move.flags & kCastleKing) ? 7 : 0;
        const int rookTo   = (move.flags & kCastleKing) ? 5 : 3;
        pos.squares[square(rookFrom, backRank)] = pos.squares[square(rookTo, backRank)];
        pos.squares[square(rookTo, backRank)]   = kEmptySquare;
    }
}

bool hasInsufficientMaterial(const Position& pos) {
    int  minorCount  = 0;
    int  bishopCount = 0;
    bool sawBishopOnDark = false;
    bool sawBishopOnLight = false;

    for (Square sq = 0; sq < 64; ++sq) {
        const Piece piece = pos.squares[sq];
        if (isEmpty(piece)) continue;

        switch (typeOf(piece)) {
            case PieceType::King:
                break;
            case PieceType::Bishop:
                ++minorCount;
                ++bishopCount;
                if (((fileOf(sq) + rankOf(sq)) & 1) == 0) sawBishopOnDark = true;
                else sawBishopOnLight = true;
                break;
            case PieceType::Knight:
                ++minorCount;
                break;
            default:
                return false;  // a pawn, rook or queen is always sufficient
        }
    }
    if (minorCount == 0) return true;   // K vs K
    if (minorCount == 1) return true;   // K+N vs K, K+B vs K

    // K+B vs K+B is drawn only when both bishops run on the same colour.
    return bishopCount == minorCount && bishopCount == 2 &&
           (sawBishopOnDark != sawBishopOnLight);
}

uint64_t positionKey(const Position& pos) {
    uint64_t key = 0;

    for (Square sq = 0; sq < 64; ++sq) {
        const Piece piece = pos.squares[sq];
        if (!isEmpty(piece)) key ^= kZobrist.piece[piece & 0x0F][sq];
    }

    key ^= kZobrist.castling[pos.castlingRights & 0x0F];
    if (pos.epSquare != kNoSquare) key ^= kZobrist.epFile[fileOf(pos.epSquare)];
    if (pos.sideToMove == Side::Black) key ^= kZobrist.blackToMove;

    return key;
}

}  // namespace chess
