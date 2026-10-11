#pragma once

#include <cstdint>

#include "game/TetrisTypes.h"

namespace tetris {

/**
 * @class Board
 * @brief Fixed 10x20 cell storage with bounds-checked access (spec 5.1).
 *
 * The active piece is kept separate until lock; the board only holds
 * locked cells. No allocation, no engine dependency.
 */
class Board {
public:
    Board();

    /** @brief Clears every cell to Empty. */
    void clear();

    /**
     * @brief Reads a cell.
     * @param x Column in [0, BOARD_WIDTH).
     * @param y Row in [0, BOARD_HEIGHT).
     * @return Cell value, or Empty when out of bounds (never crashes).
     */
    [[nodiscard]] Cell get(int x, int y) const;

    /**
     * @brief Writes a cell.
     * @param x Column in [0, BOARD_WIDTH).
     * @param y Row in [0, BOARD_HEIGHT).
     * @param value Value to store.
     * @return False when out of bounds (nothing written).
     */
    bool set(int x, int y, Cell value);

    /**
     * @brief Tests whether a cell is occupied.
     * @param x Column in [0, BOARD_WIDTH).
     * @param y Row in [0, BOARD_HEIGHT).
     * @return True when inside the board and not Empty.
     */
    [[nodiscard]] bool isOccupied(int x, int y) const;

    /**
     * @brief Tests whether a row is completely filled.
     * @param y Row in [0, BOARD_HEIGHT).
     * @return True when every column is occupied.
     */
    [[nodiscard]] bool isRowFull(int y) const;

    /** @brief True when no cell is occupied. */
    [[nodiscard]] bool isEmpty() const;

private:
    Cell cells_[BOARD_HEIGHT][BOARD_WIDTH];

    friend class LineClearSystem;
};

}  // namespace tetris
