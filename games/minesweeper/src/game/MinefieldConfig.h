#pragma once

#include <cstdint>

namespace minesweeper {

/**
 * @file MinefieldConfig.h
 * @brief Engine-free board types, difficulties and compile-time limits.
 *
 * This header (like the rest of src/game/) must never include engine,
 * SDL2 or Arduino headers, so the rules core builds for host unit tests.
 */

/** Playable difficulty levels, matching the spec section 3.2 table. */
enum class Difficulty : std::uint8_t {
    Beginner = 0,
    Intermediate = 1,
    Expert = 2
};

/** Visibility state of a single cell. */
enum class CellState : std::uint8_t {
    Hidden,
    Revealed,
    Flagged
};

/** Lifecycle of one playthrough, owned by GameSession. */
enum class GameStatus : std::uint8_t {
    Ready,
    Playing,
    Won,
    Lost
};

/** Board coordinates. uint8_t is enough: the largest board is 30x16. */
struct Position {
    std::uint8_t x;
    std::uint8_t y;
};

/** One board cell. All fields are always written explicitly on prepare(). */
struct Cell {
    std::uint8_t adjacentMines;
    CellState state;
    bool hasMine;
};

/** Board dimensions and mine count requested for a playthrough. */
struct BoardConfig {
    std::uint8_t width;
    std::uint8_t height;
    std::uint16_t mineCount;
};

/** Hard capacity: the Expert board (30x16 = 480 cells). */
constexpr std::uint8_t MAX_BOARD_WIDTH = 30;
constexpr std::uint8_t MAX_BOARD_HEIGHT = 16;
constexpr std::uint16_t MAX_BOARD_CELLS = 480;

/**
 * @brief Returns the board geometry for a difficulty level.
 * @param difficulty Selected level.
 * @return Width, height and mine count (9x9/10, 16x16/40, 30x16/99).
 */
constexpr BoardConfig boardConfigFor(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Intermediate:
            return BoardConfig{16, 16, 40};
        case Difficulty::Expert:
            return BoardConfig{30, 16, 99};
        case Difficulty::Beginner:
        default:
            return BoardConfig{9, 9, 10};
    }
}

/**
 * @brief Short display name for a difficulty level.
 * @param difficulty Selected level.
 * @return Uppercase name; never nullptr.
 */
constexpr const char* difficultyName(Difficulty difficulty) {
    switch (difficulty) {
        case Difficulty::Intermediate:
            return "MEDIUM";
        case Difficulty::Expert:
            return "EXPERT";
        case Difficulty::Beginner:
        default:
            return "EASY";
    }
}

/**
 * @brief Checks a board request against memory and rules limits.
 * @param config Requested dimensions and mine count.
 * @return True when the board fits the fixed buffers and mines < cells.
 */
constexpr bool isValidConfig(const BoardConfig& config) {
    if (config.width == 0 || config.height == 0) {
        return false;
    }
    if (config.width > MAX_BOARD_WIDTH || config.height > MAX_BOARD_HEIGHT) {
        return false;
    }
    const std::uint16_t cells =
        static_cast<std::uint16_t>(config.width) * static_cast<std::uint16_t>(config.height);
    return config.mineCount < cells;
}

}  // namespace minesweeper
