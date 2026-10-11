#pragma once

#include "game/Board.h"
#include "game/TetrisTypes.h"

namespace tetris {

/**
 * @class CollisionSystem
 * @brief Placement validation for moves and rotations (spec section 6.2).
 *
 * Checks the four blocks of a candidate piece. Blocks above the board
 * (y < 0) are allowed while the piece enters; horizontal exits, floor
 * exits and overlaps reject the move. Never indexes outside the board.
 */
class CollisionSystem {
public:
    /**
     * @brief Tests whether a piece fits on the board as given.
     * @param board Locked cells.
     * @param piece Candidate position and orientation.
     * @return True when every block is legal.
     */
    [[nodiscard]] bool canPlace(const Board& board, const ActivePiece& piece) const;
};

}  // namespace tetris
