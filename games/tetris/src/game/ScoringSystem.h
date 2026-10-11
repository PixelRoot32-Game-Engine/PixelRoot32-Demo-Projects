#pragma once

#include <cstdint>

namespace tetris {

/** Lines cleared every time the level rises by one. */
constexpr std::uint16_t LINES_PER_LEVEL = 10;
/** Highest level the fall curve models; higher play stays at top speed. */
constexpr std::uint8_t MAX_LEVEL = 15;

/**
 * @class ScoringSystem
 * @brief Points, lines and level progression (spec section 6.6).
 *
 * Base table 100/300/500/800 times the level at clear time. Saturates
 * the 32-bit score instead of wrapping on marathon sessions. Soft drop
 * pays 1/cell, hard drop 2/cell (Guideline bonus, optional per spec).
 */
class ScoringSystem {
public:
    ScoringSystem();

    /** @brief Resets score, lines and level to zero/one. */
    void reset();

    /**
     * @brief Awards a line clear and advances level every 10 lines.
     * @param count Simultaneous lines in [0, 4].
     */
    void addLines(std::uint8_t count);

    /**
     * @brief Awards manual descent distance.
     * @param cells Cells descended while soft-dropping (+1 each).
     */
    void addSoftDropCells(std::uint32_t cells);

    /**
     * @brief Awards instant-drop distance.
     * @param cells Cells travelled by the hard drop (+2 each).
     */
    void addHardDropCells(std::uint32_t cells);

    /** @brief Current score. */
    [[nodiscard]] std::uint32_t score() const;
    /** @brief Cleared lines total. */
    [[nodiscard]] std::uint16_t lines() const;
    /** @brief Current level in [1, MAX_LEVEL]. */
    [[nodiscard]] std::uint8_t level() const;

    /**
     * @brief Gravity interval for a level (spec section 6.4).
     * @param level Level in [1, MAX_LEVEL]; 0 falls back to level 1.
     * @return Milliseconds per row, floored at 50 ms.
     */
    [[nodiscard]] static std::uint32_t fallIntervalMs(std::uint8_t level);

private:
    std::uint32_t score_;
    std::uint16_t lines_;
    std::uint8_t level_;

    void addPoints(std::uint32_t points);
};

}  // namespace tetris
