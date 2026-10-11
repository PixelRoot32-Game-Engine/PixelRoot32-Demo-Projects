#include "game/Board.h"

namespace tetris {

Board::Board() {
    clear();
}

void Board::clear() {
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            cells_[y][x] = Cell::Empty;
        }
    }
}

Cell Board::get(int x, int y) const {
    if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
        return Cell::Empty;
    }
    return cells_[y][x];
}

bool Board::set(int x, int y, Cell value) {
    if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
        return false;
    }
    cells_[y][x] = value;
    return true;
}

bool Board::isOccupied(int x, int y) const {
    if (x < 0 || x >= BOARD_WIDTH || y < 0 || y >= BOARD_HEIGHT) {
        return false;
    }
    return cells_[y][x] != Cell::Empty;
}

bool Board::isRowFull(int y) const {
    if (y < 0 || y >= BOARD_HEIGHT) {
        return false;
    }
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        if (cells_[y][x] == Cell::Empty) {
            return false;
        }
    }
    return true;
}

bool Board::isEmpty() const {
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            if (cells_[y][x] != Cell::Empty) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace tetris
