#pragma once

#include <cstdint>

#include "game/Board.h"

namespace tetris {

/**
 * @class LineClearSystem
 * @brief Full-row detection and compaction (spec section 6.5).
 *
 * Scans bottom-up, drops every row above a full one, and re-checks the
 * same index so consecutive full rows all clear. Handles 1-4 lines at
 * once; stateless and allocation-free.
 */
class LineClearSystem {
public:
    /**
     * @brief Removes every full row and compacts the board.
     * @param board Board to compact in place.
     * @return Rows removed in [0, BOARD_HEIGHT].
     */
    [[nodiscard]] std::uint8_t clearLines(Board& board) const;
};

}  // namespace tetris
