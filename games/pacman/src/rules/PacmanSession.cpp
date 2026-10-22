#include "PacmanSession.h"

namespace pacman {

namespace {

/**
 * @brief Index of the bit that records a cell's collected state.
 *
 * @param col Column of the cell.
 * @param row Row of the cell.
 * @param colCount Number of columns in the maze.
 * @return The bit index inside the collected mask.
 */
constexpr int collectedBitIndex(int col, int row, int colCount) {
    return row * colCount + col;
}

}  // namespace

PacmanSession::PacmanSession() : PacmanSession(PacmanMaze::defaultLevel()) {}

PacmanSession::PacmanSession(const PacmanLevelConfig& config) : maze_(config), player_() {
    remainingCollectibles_ = maze_.collectibleCount();
}

void PacmanSession::startNewGame() {
    for (std::uint8_t& byte : collected_) {
        byte = 0;
    }
    score_ = 0;
    lives_ = kStartingLives;
    remainingCollectibles_ = maze_.collectibleCount();
    powerStepsLeft_ = 0;
    player_.reset(maze_);
    state_ = GameState::Playing;
}

void PacmanSession::step(Direction requestedDirection) {
    if (state_ != GameState::Playing) {
        return;
    }

    // 1. Input, then the player moves.
    player_.requestDirection(requestedDirection);
    player_.step(maze_);

    // 2. Collectibles: the player picks up whatever is under it.
    collectAt(player_.cellCol(), player_.cellRow());

    // 3. The power effect ages.
    updatePowerTimer();

    // 4. End conditions: the level is over once nothing is left to collect.
    if (remainingCollectibles_ == 0) {
        state_ = GameState::Victory;
    }
}

void PacmanSession::togglePause() {
    if (state_ == GameState::Playing) {
        state_ = GameState::Paused;
    } else if (state_ == GameState::Paused) {
        state_ = GameState::Playing;
    }
}

Collectible PacmanSession::collectibleAt(int col, int row) const {
    if (!maze_.isInside(col, row)) {
        return Collectible::None;
    }
    const int index = collectedBitIndex(col, row, maze_.colCount());
    if ((collected_[index / 8] & (1u << (index % 8))) != 0) {
        return Collectible::None;
    }
    return maze_.collectibleAt(col, row);
}

bool PacmanSession::isCollected(int col, int row) const {
    if (!maze_.isInside(col, row)) {
        return false;
    }
    const int index = collectedBitIndex(col, row, maze_.colCount());
    return (collected_[index / 8] & (1u << (index % 8))) != 0;
}

void PacmanSession::collectAt(int col, int row) {
    const Collectible collectible = collectibleAt(col, row);
    if (collectible == Collectible::None) {
        return;
    }

    const int index = collectedBitIndex(col, row, maze_.colCount());
    collected_[index / 8] = static_cast<std::uint8_t>(collected_[index / 8] | (1u << (index % 8)));

    --remainingCollectibles_;

    const int scoreBefore = score_;
    if (collectible == Collectible::Dot) {
        score_ += kScoreDot;
    } else {
        score_ += kScorePower;
        // Picking up a pellet while the effect is already running refreshes
        // the timer instead of stacking a second effect.
        powerStepsLeft_ = kPowerDurationSteps;
    }

    // One extra life for every full ten thousand points.
    if (score_ / kScoreExtraLife > scoreBefore / kScoreExtraLife) {
        ++lives_;
    }
}

void PacmanSession::updatePowerTimer() {
    if (powerStepsLeft_ > 0) {
        --powerStepsLeft_;
    }
}

}  // namespace pacman
