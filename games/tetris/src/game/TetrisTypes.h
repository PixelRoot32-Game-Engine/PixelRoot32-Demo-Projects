#pragma once

#include <cstdint>

namespace tetris {

/**
 * @file TetrisTypes.h
 * @brief Engine-free shared types for the Tetris rules core (spec section 5).
 *
 * This header must never include engine, SDL2 or Arduino headers, so the
 * rules core builds for host unit tests unchanged.
 */

/** Classic board: 10 columns, 20 visible rows. */
constexpr int BOARD_WIDTH = 10;
constexpr int BOARD_HEIGHT = 20;

/** Stored cell value. One byte per cell, 200 bytes total. */
enum class Cell : std::uint8_t {
    Empty = 0,
    I,
    O,
    T,
    S,
    Z,
    J,
    L
};

/** The seven classic tetrominoes. */
enum class TetrominoType : std::uint8_t {
    I = 0,
    O,
    T,
    S,
    Z,
    J,
    L,
    Count
};

constexpr std::uint8_t TETROMINO_COUNT = 7;
constexpr std::uint8_t BLOCKS_PER_PIECE = 4;
constexpr std::uint8_t ROTATION_COUNT = 4;

/**
 * @struct ActivePiece
 * @brief Falling piece. (x, y) is the top-left of the 4x4 bounding box
 * on the board. y may be negative while the piece enters from above.
 */
struct ActivePiece {
    TetrominoType type;
    int8_t x;
    int8_t y;
    std::uint8_t rotation;
};

/** Global playthrough phase (spec section 5.3). */
enum class GameStatus : std::uint8_t {
    Ready,
    Playing,
    Paused,
    GameOver
};

/** Accumulated progress counters. */
struct GameStats {
    std::uint32_t score;
    std::uint16_t linesCleared;
    std::uint8_t level;
};

/**
 * @brief Maps a tetromino to its locked board cell value.
 * @param type Piece type.
 * @return Cell value stored on lock.
 */
constexpr Cell cellForType(TetrominoType type) {
    return static_cast<Cell>(static_cast<std::uint8_t>(type) + 1);
}

}  // namespace tetris
