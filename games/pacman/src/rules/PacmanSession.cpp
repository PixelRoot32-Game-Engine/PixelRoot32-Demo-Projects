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
    resetBoard();
}

void PacmanSession::resetBoard() {
    player_.reset(maze_);
    for (int index = 0; index < maze_.enemyCount(); ++index) {
        enemies_[index].reset(maze_, index);
    }
}

void PacmanSession::startNewGame() {
    for (std::uint8_t& byte : collected_) {
        byte = 0;
    }
    score_ = 0;
    lives_ = kStartingLives;
    remainingCollectibles_ = maze_.collectibleCount();
    powerStepsLeft_ = 0;
    invulnerableSteps_ = 0;
    enemyChain_ = 0;
    respawnSteps_ = kRespawnSteps;
    resetBoard();
    state_ = GameState::Playing;
}

void PacmanSession::step(Direction requestedDirection) {
    if (state_ != GameState::Playing) {
        return;
    }

    // 0. The respawn sequence freezes the board. Nothing moves and no
    // contact is resolved until it is over.
    if (respawnSteps_ > 0) {
        --respawnSteps_;
        return;
    }

    // 1. Input, then the player moves.
    player_.requestDirection(requestedDirection);
    player_.step(maze_);

    // 2. Collectibles: the player picks up whatever is under it.
    collectAt(player_.cellCol(), player_.cellRow());

    // 3. The hunters move.
    stepEnemies();

    // 4. The power effect ages.
    updatePowerTimer();

    // 5. Contacts: the player and a hunter share a cell.
    resolveContacts();

    // 6. Timers: the player's protection runs out.
    updateInvulnerability();

    // 7. End conditions: the level is over once nothing is left to collect.
    if (remainingCollectibles_ == 0) {
        state_ = GameState::Victory;
        events_ = static_cast<std::uint8_t>(events_ | kEventLevelCompleted);
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

    if (collectible == Collectible::Dot) {
        addScore(kScoreDot);
        events_ = static_cast<std::uint8_t>(events_ | kEventDot);
    } else {
        addScore(kScorePower);
        events_ = static_cast<std::uint8_t>(events_ | kEventPowerPellet);
        // Picking up a pellet while the effect is already running refreshes
        // the timer instead of stacking a second effect.
        powerStepsLeft_ = kPowerDurationSteps;
        // A new pellet starts a new chain of defeats.
        enemyChain_ = 0;
    }
}

void PacmanSession::addScore(int amount) {
    const int scoreBefore = score_;
    score_ += amount;

    // One extra life for every full ten thousand points.
    if (score_ / kScoreExtraLife > scoreBefore / kScoreExtraLife) {
        ++lives_;
        events_ = static_cast<std::uint8_t>(events_ | kEventExtraLife);
    }
}

void PacmanSession::updatePowerTimer() {
    if (powerStepsLeft_ > 0) {
        --powerStepsLeft_;
        if (powerStepsLeft_ == 0) {
            // The effect is over: the chain of defeats starts again.
            enemyChain_ = 0;
        }
    }
}

void PacmanSession::updateInvulnerability() {
    if (invulnerableSteps_ > 0) {
        --invulnerableSteps_;
    }
}

void PacmanSession::resolveContacts() {
    const int playerCol = player_.cellCol();
    const int playerRow = player_.cellRow();

    for (int index = 0; index < maze_.enemyCount(); ++index) {
        const PacmanEnemy& enemy = enemies_[index];
        if (enemy.cellCol() != playerCol || enemy.cellRow() != playerRow) {
            continue;
        }

        if (enemy.isVulnerable()) {
            defeatEnemy(index);
            continue;
        }

        // A hunter that is heading home or waiting to come back is harmless.
        if (enemy.isDefeated()) {
            continue;
        }

        if (invulnerableSteps_ > 0) {
            // The player is still protected from the last life.
            continue;
        }

        loseLife();
        return;
    }
}

void PacmanSession::defeatEnemy(int index) {
    enemies_[index].defeat();
    events_ = static_cast<std::uint8_t>(events_ | kEventEnemyDefeated);

    // Consecutive defeats inside one power effect are worth more each time.
    const int multiplier = 1 << enemyChain_;
    addScore(kScoreEnemy * multiplier);
    if (enemyChain_ < kEnemyChainLength - 1) {
        ++enemyChain_;
    }
}

void PacmanSession::loseLife() {
    --lives_;
    events_ = static_cast<std::uint8_t>(events_ | kEventLifeLost);

    if (lives_ <= 0) {
        state_ = GameState::Defeat;
        return;
    }

    // The board goes back to its starting shape. The dots that were already
    // collected and the score are kept: only the positions and the effects
    // are reset.
    resetBoard();
    powerStepsLeft_ = 0;
    enemyChain_ = 0;
    invulnerableSteps_ = kInvulnerableSteps;
    respawnSteps_ = kRespawnSteps;
}

void PacmanSession::stepEnemies() {
    PacmanEnemyContext context;
    context.playerCol = player_.cellCol();
    context.playerRow = player_.cellRow();
    context.playerDirection = player_.direction();
    context.powerIsActive = powerStepsLeft_ > 0;
    // The chasing hunter is the one the flanking hunter measures from. Its
    // position is read before any hunter moves, so the flanking rule sees a
    // single, stable snapshot of the board.
    context.chaserCol = enemies_[0].cellCol();
    context.chaserRow = enemies_[0].cellRow();

    for (int index = 0; index < maze_.enemyCount(); ++index) {
        enemies_[index].step(maze_, context);
    }
}

}  // namespace pacman
