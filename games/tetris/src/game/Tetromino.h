#pragma once

#include <cstdint>

#include "game/TetrisTypes.h"

namespace tetris {

/** Integer board offset of one block. */
struct BlockOffset {
    int8_t x;
    int8_t y;
};

/**
 * @class Tetromino
 * @brief Shape tables for the seven pieces (spec section 5.2).
 *
 * Pieces live in a 4x4 bounding box. Rotation 0 holds the spawn
 * orientation; rotations 1-3 are clockwise steps. The I piece rotates
 * inside the full 4x4 box, O never changes, and the rest rotate inside
 * the top-left 3x3 sub-box. All math is integer and branch-light, so
 * rotation never allocates and never touches the board.
 */
class Tetromino {
public:
    /**
     * @brief Block offsets of a piece in a given orientation.
     * @param type Piece type.
     * @param rotation Orientation in [0, 4). Values wrap with & 3.
     * @param index Block index in [0, 4).
     * @return Offset inside the 4x4 box; (0,0) when index is invalid.
     */
    [[nodiscard]] static BlockOffset blockAt(TetrominoType type, std::uint8_t rotation,
                                            std::uint8_t index);

    /**
     * @brief Spawn orientation blocks (rotation 0).
     * @param type Piece type.
     * @param index Block index in [0, 4).
     * @return Offset inside the 4x4 box.
     */
    [[nodiscard]] static BlockOffset spawnBlockAt(TetrominoType type, std::uint8_t index);

    /**
     * @brief Spawn column for the 4x4 box top-left corner.
     * @param type Piece type.
     * @return x so the piece enters centered (3 for 4-wide/3-wide boxes).
     */
    [[nodiscard]] static constexpr int8_t spawnX(TetrominoType type) {
        // O carries its 2x2 square at columns 1-2 of the box; shifting the
        // box one step right centers it like the other pieces.
        return (type == TetrominoType::O) ? 4 - 1 : 3;
    }

    /** @brief Spawn row: the box starts one row above the visible board. */
    [[nodiscard]] static constexpr int8_t spawnY() {
        return -1;
    }
};

}  // namespace tetris
