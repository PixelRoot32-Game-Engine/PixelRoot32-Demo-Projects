#include "game/ScoringSystem.h"

namespace tetris {

namespace {

// Base points per simultaneous clear (spec 6.6 table).
constexpr std::uint16_t BASE_POINTS[5] = {0, 100, 300, 500, 800};

// Gravity curve: level 1 falls every 800 ms, each level sheds 70 ms.
constexpr std::uint32_t BASE_INTERVAL_MS = 800;
constexpr std::uint32_t INTERVAL_STEP_MS = 70;
constexpr std::uint32_t MIN_INTERVAL_MS = 50;

}  // namespace

ScoringSystem::ScoringSystem() : score_(0), lines_(0), level_(1) {}

void ScoringSystem::reset() {
    score_ = 0;
    lines_ = 0;
    level_ = 1;
}

void ScoringSystem::addLines(std::uint8_t count) {
    if (count == 0 || count > 4) {
        return;
    }
    addPoints(static_cast<std::uint32_t>(BASE_POINTS[count]) * level_);
    const std::uint32_t total = static_cast<std::uint32_t>(lines_) + count;
    lines_ = (total > 0xFFFFU) ? 0xFFFFU : static_cast<std::uint16_t>(total);
    const std::uint32_t next = 1U + lines_ / LINES_PER_LEVEL;
    level_ = (next > MAX_LEVEL) ? MAX_LEVEL : static_cast<std::uint8_t>(next);
}

void ScoringSystem::addSoftDropCells(std::uint32_t cells) {
    addPoints(cells);
}

void ScoringSystem::addHardDropCells(std::uint32_t cells) {
    if (cells > 0x7FFFFFFFU / 2U) {
        cells = 0x7FFFFFFFU / 2U;
    }
    addPoints(cells * 2U);
}

std::uint32_t ScoringSystem::score() const {
    return score_;
}

std::uint16_t ScoringSystem::lines() const {
    return lines_;
}

std::uint8_t ScoringSystem::level() const {
    return level_;
}

std::uint32_t ScoringSystem::fallIntervalMs(std::uint8_t level) {
    if (level == 0) {
        level = 1;
    }
    if (level > MAX_LEVEL) {
        level = MAX_LEVEL;
    }
    const std::uint32_t drop =
        static_cast<std::uint32_t>(level - 1U) * INTERVAL_STEP_MS;
    return (BASE_INTERVAL_MS > drop + MIN_INTERVAL_MS)
               ? (BASE_INTERVAL_MS - drop)
               : MIN_INTERVAL_MS;
}

void ScoringSystem::addPoints(std::uint32_t points) {
    if (score_ > 0xFFFFFFFFU - points) {
        score_ = 0xFFFFFFFFU;
        return;
    }
    score_ += points;
}

}  // namespace tetris
