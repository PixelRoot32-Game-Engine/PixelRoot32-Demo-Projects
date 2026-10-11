#include "game/LineClearSystem.h"

namespace tetris {

std::uint8_t LineClearSystem::clearLines(Board& board) const {
    std::uint8_t cleared = 0;
    int y = BOARD_HEIGHT - 1;
    while (y >= 0) {
        if (!board.isRowFull(y)) {
            --y;
            continue;
        }
        // Shift every row above down one and blank the top row.
        for (int row = y; row > 0; --row) {
            for (int x = 0; x < BOARD_WIDTH; ++x) {
                board.cells_[row][x] = board.cells_[row - 1][x];
            }
        }
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            board.cells_[0][x] = Cell::Empty;
        }
        ++cleared;
        // Re-check index y: it now holds the row that fell from above.
    }
    return cleared;
}

}  // namespace tetris
