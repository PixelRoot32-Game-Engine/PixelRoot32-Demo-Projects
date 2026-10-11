#pragma once

#include <cstdint>

#include "game/MinefieldConfig.h"

namespace minesweeper {

/**
 * @class Minefield
 * @brief Board storage and minesweeper rules, with no drawing or input.
 *
 * All storage is fixed-size (Expert board: 480 cells). Mine placement uses
 * a partial Fisher-Yates draw over eligible cells driven by an internal
 * xorshift generator, so equal seed plus equal config reproduces the board.
 * Flood reveal is iterative over a preallocated work buffer: no recursion,
 * no heap, no per-frame allocation.
 */
class Minefield {
public:
    Minefield();

    /**
     * @brief Clears the board and validates geometry, without placing mines.
     * @param config Requested dimensions and mine count.
     * @return True when the config fits the fixed buffers.
     */
    bool prepare(const BoardConfig& config);

    /**
     * @brief Places exactly mineCount mines on a prepared board.
     *
     * Existing cell visibility (flags placed before the first reveal) is
     * preserved; only mine positions and adjacency numbers are written.
     * @param seed Reproducible RNG seed.
     * @param safeCell Optional cell excluded from mining (first-move safety).
     * @return True when mines were placed.
     */
    bool placeMines(std::uint32_t seed, const Position* safeCell);

    /**
     * @brief Prepares the board and places mines immediately.
     * @param config Requested dimensions and mine count.
     * @param seed Reproducible RNG seed.
     * @return True when the board is ready to play.
     */
    bool initialize(const BoardConfig& config, std::uint32_t seed);

    /**
     * @brief Prepares the board, guaranteeing the first cell is mine-free.
     * @param config Requested dimensions and mine count.
     * @param seed Reproducible RNG seed.
     * @param safe Position excluded from mining.
     * @return True when the board is ready to play.
     */
    bool initializeSafe(const BoardConfig& config, std::uint32_t seed, Position safe);

    /**
     * @brief Reveals a cell, flood-filling connected empty cells.
     * @param position Cell to reveal.
     * @return True when the board changed; false for invalid, revealed or
     *         flagged cells and for boards with no mines placed yet.
     */
    bool reveal(Position position);

    /**
     * @brief Toggles a flag on a hidden cell.
     * @param position Cell to flag or unflag.
     * @return True when the flag state changed.
     */
    bool toggleFlag(Position position);

    /**
     * @brief Reads one cell for rendering.
     * @param position Cell coordinates.
     * @return Cell pointer, or nullptr outside the board.
     */
    [[nodiscard]] const Cell* getCell(Position position) const;

    /**
     * @brief Checks coordinates against the active board.
     * @param position Cell coordinates.
     * @return True when inside the board.
     */
    [[nodiscard]] bool isValid(Position position) const;

    /**
     * @brief Victory test: every safe cell revealed, flags not required.
     * @return True when revealed == cells - mines.
     */
    [[nodiscard]] bool isVictory() const;

    /**
     * @brief Detonation test: a revealed cell holds a mine.
     * @return True after reveal() hit a mine.
     */
    [[nodiscard]] bool hasDetonated() const;

    /**
     * @brief Locates the detonated mine for the end-of-game reveal.
     * @param out Receives the mine coordinates when found.
     * @return True when a detonation was recorded.
     */
    [[nodiscard]] bool detonatedPosition(Position& out) const;

    /**
     * @brief Counts flagged cells.
     * @return Flags placed, never above the cell count.
     */
    [[nodiscard]] std::uint16_t getFlagCount() const;

    /**
     * @brief Counts revealed cells.
     * @return Cells revealed so far.
     */
    [[nodiscard]] std::uint16_t getRevealedCount() const;

    /**
     * @brief Counts placed mines (test and HUD support).
     * @return Mines on the board, or 0 before placement.
     */
    [[nodiscard]] std::uint16_t getMineCount() const;

    /**
     * @brief Active board geometry.
     * @return Config from the last successful prepare().
     */
    [[nodiscard]] const BoardConfig& getConfig() const;

    /**
     * @brief Placement state.
     * @return True after placeMines() succeeded.
     */
    [[nodiscard]] bool isPlaced() const;

private:
    Cell cells_[MAX_BOARD_CELLS];
    std::uint16_t workBuffer_[MAX_BOARD_CELLS];
    BoardConfig config_;
    std::uint32_t rngState_;
    std::uint16_t revealedCount_;
    std::uint16_t flagCount_;
    bool placed_;
    bool detonated_;
    Position detonatedAt_;

    [[nodiscard]] std::uint16_t cellIndex(std::uint8_t x, std::uint8_t y) const;
    [[nodiscard]] std::uint32_t nextRandom();
    [[nodiscard]] std::uint32_t randomBelow(std::uint32_t bound);
    void computeAdjacency();
};

}  // namespace minesweeper
