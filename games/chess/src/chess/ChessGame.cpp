/*
 * ChessGame.cpp - Match state on top of the rules core.
 */
#include "chess/ChessGame.h"

namespace chess {

void ChessGame::reset() {
    setupInitialPosition(position_);

    plyCount_          = 0;
    winner_            = Side::White;
    capturedCount_[0]  = 0;
    capturedCount_[1]  = 0;
    positionKeys_[0]   = positionKey(position_);

    refreshDerivedState();
}

bool ChessGame::resetFromFen(const char* fen) {
    Position parsed;
    if (!parseFen(fen, parsed)) return false;

    position_          = parsed;
    plyCount_          = 0;
    winner_            = Side::White;
    capturedCount_[0]  = 0;
    capturedCount_[1]  = 0;
    positionKeys_[0]   = positionKey(position_);

    refreshDerivedState();
    return true;
}

Square ChessGame::checkedKingSquare() const {
    if (!isInCheck(position_, position_.sideToMove)) return kNoSquare;
    return findKing(position_, position_.sideToMove);
}

uint8_t ChessGame::legalMovesFrom(Square from, Move* out, uint8_t capacity) const {
    if (out == nullptr) return 0;

    uint8_t written = 0;
    for (uint8_t i = 0; i < legalMoves_.count && written < capacity; ++i) {
        if (legalMoves_.moves[i].from == from) out[written++] = legalMoves_.moves[i];
    }
    return written;
}

bool ChessGame::canSelect(Square from) const {
    if (isGameOver(status_)) return false;
    if (from < 0 || from >= 64) return false;

    const Piece piece = position_.squares[from];
    if (isEmpty(piece) || sideOf(piece) != position_.sideToMove) return false;

    for (uint8_t i = 0; i < legalMoves_.count; ++i) {
        if (legalMoves_.moves[i].from == from) return true;
    }
    return false;
}

bool ChessGame::needsPromotionChoice(Square from, Square to) const {
    for (uint8_t i = 0; i < legalMoves_.count; ++i) {
        const Move& move = legalMoves_.moves[i];
        if (move.from == from && move.to == to && (move.flags & kPromotion)) return true;
    }
    return false;
}

bool ChessGame::tryMove(Square from, Square to, PieceType promotion) {
    if (isGameOver(status_)) return false;

    const Move* chosen = nullptr;
    for (uint8_t i = 0; i < legalMoves_.count; ++i) {
        const Move& move = legalMoves_.moves[i];
        if (move.from != from || move.to != to) continue;

        if (move.flags & kPromotion) {
            // A promotion is only unambiguous once the player has picked a piece.
            if (move.promotion != promotion) continue;
        }
        chosen = &move;
        break;
    }
    if (chosen == nullptr) return false;

    const Move move = *chosen;  // copy: makeMove invalidates the move list below

    // The victim has to be read before the board changes under us.
    if (move.flags & kEnPassant) {
        recordCapture(position_.squares[square(fileOf(move.to), rankOf(move.from))]);
    } else if (!isEmpty(position_.squares[move.to])) {
        recordCapture(position_.squares[move.to]);
    }

    Undo undo;
    makeMove(position_, move, undo);

    if (plyCount_ < kMaxPlies) {
        history_[plyCount_] = move;
        ++plyCount_;
        positionKeys_[plyCount_] = positionKey(position_);
    }

    refreshDerivedState();
    return true;
}

void ChessGame::resign(Side side) {
    status_ = GameStatus::Resigned;
    winner_ = opponent(side);
}

const Move* ChessGame::lastMove() const {
    if (plyCount_ == 0) return nullptr;
    return &history_[plyCount_ - 1];
}

PieceType ChessGame::capturedPiece(Side loser, uint8_t index) const {
    const uint8_t side = static_cast<uint8_t>(loser);
    if (index >= capturedCount_[side]) return PieceType::None;
    return captured_[side][index];
}

void ChessGame::refreshDerivedState() {
    generateLegalMoves(position_, legalMoves_);

    const bool inCheck = isInCheck(position_, position_.sideToMove);

    if (legalMoves_.count == 0) {
        if (inCheck) {
            status_ = GameStatus::Checkmate;
            winner_ = opponent(position_.sideToMove);
        } else {
            status_ = GameStatus::Stalemate;
        }
        return;
    }

    // Draw checks run after mate: a mate on the board always wins the race.
    if (hasInsufficientMaterial(position_)) {
        status_ = GameStatus::DrawInsufficientMaterial;
        return;
    }
    if (position_.halfmoveClock >= 100) {
        status_ = GameStatus::DrawFiftyMove;
        return;
    }
    if (repetitionCount() >= 3) {
        status_ = GameStatus::DrawRepetition;
        return;
    }

    status_ = inCheck ? GameStatus::Check : GameStatus::Playing;
}

void ChessGame::recordCapture(Piece piece) {
    if (isEmpty(piece)) return;

    const uint8_t loser = static_cast<uint8_t>(sideOf(piece));
    if (capturedCount_[loser] >= kMaxCaptured) return;

    captured_[loser][capturedCount_[loser]] = typeOf(piece);
    ++capturedCount_[loser];
}

uint8_t ChessGame::repetitionCount() const {
    const uint64_t current = positionKeys_[plyCount_];

    uint8_t seen = 0;
    // Only positions since the last irreversible move can repeat, and the
    // halfmove clock is exactly how far back that is.
    const uint16_t oldest = (plyCount_ > position_.halfmoveClock)
                                ? static_cast<uint16_t>(plyCount_ - position_.halfmoveClock)
                                : 0;

    for (uint16_t ply = oldest; ply <= plyCount_; ++ply) {
        if (positionKeys_[ply] == current) ++seen;
    }
    return seen;
}

}  // namespace chess
