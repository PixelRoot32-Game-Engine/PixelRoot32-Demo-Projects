#include "game/Minefield.h"

namespace minesweeper {

Minefield::Minefield()
    : config_{0, 0, 0},
      rngState_(0x2545F491U),
      revealedCount_(0),
      flagCount_(0),
      placed_(false),
      detonated_(false),
      detonatedAt_{0, 0} {
    // Every cell starts hidden and mine-free so no read ever sees garbage.
    for (std::uint16_t i = 0; i < MAX_BOARD_CELLS; ++i) {
        cells_[i].adjacentMines = 0;
        cells_[i].state = CellState::Hidden;
        cells_[i].hasMine = false;
        workBuffer_[i] = 0;
    }
}

bool Minefield::prepare(const BoardConfig& config) {
    if (!isValidConfig(config)) {
        return false;
    }
    config_ = config;
    revealedCount_ = 0;
    flagCount_ = 0;
    placed_ = false;
    detonated_ = false;
    detonatedAt_ = Position{0, 0};
    const std::uint16_t cells =
        static_cast<std::uint16_t>(config.width) * static_cast<std::uint16_t>(config.height);
    for (std::uint16_t i = 0; i < cells; ++i) {
        cells_[i].adjacentMines = 0;
        cells_[i].state = CellState::Hidden;
        cells_[i].hasMine = false;
    }
    return true;
}

bool Minefield::placeMines(std::uint32_t seed, const Position* safeCell) {
    const std::uint16_t total =
        static_cast<std::uint16_t>(config_.width) * static_cast<std::uint16_t>(config_.height);
    if (total == 0 || placed_) {
        return false;
    }
    if (config_.mineCount >= total) {
        return false;
    }

    // Collect every minable cell index; the safe cell (if any) stays out so
    // the first reveal can never detonate without regenerating the board.
    std::uint16_t excluded = total;
    if (safeCell != nullptr && isValid(*safeCell)) {
        excluded = cellIndex(safeCell->x, safeCell->y);
    }
    std::uint16_t eligible = 0;
    for (std::uint16_t i = 0; i < total; ++i) {
        if (i != excluded) {
            workBuffer_[eligible++] = i;
        }
    }
    if (config_.mineCount > eligible) {
        return false;
    }

    // Partial Fisher-Yates: the first mineCount entries are a uniform sample
    // without replacement, reproducible for a fixed seed.
    rngState_ = (seed != 0) ? seed : 0x2545F491U;
    for (std::uint16_t i = 0; i < config_.mineCount; ++i) {
        const std::uint16_t remaining = static_cast<std::uint16_t>(eligible - i);
        const std::uint16_t pick = static_cast<std::uint16_t>(i + randomBelow(remaining));
        const std::uint16_t chosen = workBuffer_[pick];
        workBuffer_[pick] = workBuffer_[i];
        workBuffer_[i] = chosen;
        cells_[chosen].hasMine = true;
    }

    computeAdjacency();
    placed_ = true;
    return true;
}

bool Minefield::initialize(const BoardConfig& config, std::uint32_t seed) {
    if (!prepare(config)) {
        return false;
    }
    return placeMines(seed, nullptr);
}

bool Minefield::initializeSafe(const BoardConfig& config, std::uint32_t seed, Position safe) {
    if (!prepare(config)) {
        return false;
    }
    return placeMines(seed, &safe);
}

bool Minefield::reveal(Position position) {
    if (!placed_ || !isValid(position)) {
        return false;
    }
    Cell& first = cells_[cellIndex(position.x, position.y)];
    if (first.state != CellState::Hidden) {
        return false;
    }
    if (first.hasMine) {
        // Detonation reveals only the mine that was hit; the full board is
        // exposed by the presentation layer reading every cell.
        first.state = CellState::Revealed;
        ++revealedCount_;
        detonated_ = true;
        detonatedAt_ = position;
        return true;
    }

    // Iterative flood over the preallocated buffer: each cell is marked
    // revealed when pushed, so no position is ever processed twice and the
    // worst case stays O(cells) without recursion depth on the MCU stack.
    std::uint16_t top = 0;
    workBuffer_[top++] = cellIndex(position.x, position.y);
    first.state = CellState::Revealed;
    ++revealedCount_;

    while (top > 0) {
        const std::uint16_t current = workBuffer_[--top];
        if (cells_[current].adjacentMines != 0) {
            continue;
        }
        const std::uint8_t cx = static_cast<std::uint8_t>(current % config_.width);
        const std::uint8_t cy = static_cast<std::uint8_t>(current / config_.width);
        for (std::int8_t dy = -1; dy <= 1; ++dy) {
            for (std::int8_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const std::int16_t nx = static_cast<std::int16_t>(cx) + dx;
                const std::int16_t ny = static_cast<std::int16_t>(cy) + dy;
                if (nx < 0 || ny < 0 || nx >= config_.width || ny >= config_.height) {
                    continue;
                }
                Cell& neighbour =
                    cells_[cellIndex(static_cast<std::uint8_t>(nx), static_cast<std::uint8_t>(ny))];
                if (neighbour.state != CellState::Hidden || neighbour.hasMine) {
                    continue;
                }
                neighbour.state = CellState::Revealed;
                ++revealedCount_;
                workBuffer_[top++] = cellIndex(static_cast<std::uint8_t>(nx),
                                              static_cast<std::uint8_t>(ny));
            }
        }
    }
    return true;
}

bool Minefield::toggleFlag(Position position) {
    if (!isValid(position)) {
        return false;
    }
    Cell& cell = cells_[cellIndex(position.x, position.y)];
    if (cell.state == CellState::Hidden) {
        cell.state = CellState::Flagged;
        ++flagCount_;
        return true;
    }
    if (cell.state == CellState::Flagged) {
        cell.state = CellState::Hidden;
        --flagCount_;
        return true;
    }
    return false;
}

const Cell* Minefield::getCell(Position position) const {
    if (!isValid(position)) {
        return nullptr;
    }
    return &cells_[cellIndex(position.x, position.y)];
}

bool Minefield::isValid(Position position) const {
    return position.x < config_.width && position.y < config_.height;
}

bool Minefield::isVictory() const {
    // A detonated board is lost, never won: the blast cell counts as
    // revealed, so without this guard a one-safe-cell board would report
    // both outcomes at once.
    if (!placed_ || detonated_) {
        return false;
    }
    const std::uint16_t total =
        static_cast<std::uint16_t>(config_.width) * static_cast<std::uint16_t>(config_.height);
    return revealedCount_ == static_cast<std::uint16_t>(total - config_.mineCount);
}

bool Minefield::hasDetonated() const {
    return detonated_;
}

bool Minefield::detonatedPosition(Position& out) const {
    if (!detonated_) {
        return false;
    }
    out = detonatedAt_;
    return true;
}

std::uint16_t Minefield::getFlagCount() const {
    return flagCount_;
}

std::uint16_t Minefield::getRevealedCount() const {
    return revealedCount_;
}

std::uint16_t Minefield::getMineCount() const {
    if (!placed_) {
        return 0;
    }
    const std::uint16_t total =
        static_cast<std::uint16_t>(config_.width) * static_cast<std::uint16_t>(config_.height);
    std::uint16_t mines = 0;
    for (std::uint16_t i = 0; i < total; ++i) {
        if (cells_[i].hasMine) {
            ++mines;
        }
    }
    return mines;
}

const BoardConfig& Minefield::getConfig() const {
    return config_;
}

bool Minefield::isPlaced() const {
    return placed_;
}

std::uint16_t Minefield::cellIndex(std::uint8_t x, std::uint8_t y) const {
    return static_cast<std::uint16_t>(y) * config_.width + x;
}

std::uint32_t Minefield::nextRandom() {
    // xorshift32: small, seedable and good enough for mine sampling.
    std::uint32_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState_ = (x != 0) ? x : 0x2545F491U;
    return rngState_;
}

std::uint32_t Minefield::randomBelow(std::uint32_t bound) {
    return (bound == 0) ? 0 : (nextRandom() % bound);
}

void Minefield::computeAdjacency() {
    const std::uint16_t total =
        static_cast<std::uint16_t>(config_.width) * static_cast<std::uint16_t>(config_.height);
    for (std::uint16_t i = 0; i < total; ++i) {
        if (cells_[i].hasMine) {
            cells_[i].adjacentMines = 0;
            continue;
        }
        const std::uint8_t cx = static_cast<std::uint8_t>(i % config_.width);
        const std::uint8_t cy = static_cast<std::uint8_t>(i / config_.width);
        std::uint8_t count = 0;
        for (std::int8_t dy = -1; dy <= 1; ++dy) {
            for (std::int8_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const std::int16_t nx = static_cast<std::int16_t>(cx) + dx;
                const std::int16_t ny = static_cast<std::int16_t>(cy) + dy;
                if (nx < 0 || ny < 0 || nx >= config_.width || ny >= config_.height) {
                    continue;
                }
                if (cells_[cellIndex(static_cast<std::uint8_t>(nx),
                                     static_cast<std::uint8_t>(ny))]
                        .hasMine) {
                    ++count;
                }
            }
        }
        cells_[i].adjacentMines = count;
    }
}

}  // namespace minesweeper
