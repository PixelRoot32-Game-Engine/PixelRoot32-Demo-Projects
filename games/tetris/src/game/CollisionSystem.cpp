#include "game/CollisionSystem.h"

#include "game/Tetromino.h"

namespace tetris {

bool CollisionSystem::canPlace(const Board& board, const ActivePiece& piece) const {
    for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
        const BlockOffset block = Tetromino::blockAt(piece.type, piece.rotation, i);
        const int x = static_cast<int>(piece.x) + block.x;
        const int y = static_cast<int>(piece.y) + block.y;
        if (x < 0 || x >= BOARD_WIDTH) {
            return false;
        }
        if (y >= BOARD_HEIGHT) {
            return false;
        }
        if (y >= 0 && board.isOccupied(x, y)) {
            return false;
        }
    }
    return true;
}

}  // namespace tetris
