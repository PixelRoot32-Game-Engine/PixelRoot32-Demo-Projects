#include "game/GameSession.h"

namespace minesweeper {

GameSession::GameSession()
    : config_{0, 0, 0}, seed_(0), status_(GameStatus::Ready), startMs_(0), endMs_(0) {
}

bool GameSession::start(const BoardConfig& config, std::uint32_t seed) {
    if (!field_.prepare(config)) {
        return false;
    }
    config_ = config;
    seed_ = seed;
    status_ = GameStatus::Ready;
    startMs_ = 0;
    endMs_ = 0;
    return true;
}

bool GameSession::reveal(Position position, unsigned long nowMs) {
    if (status_ == GameStatus::Won || status_ == GameStatus::Lost) {
        return false;
    }
    if (!field_.isValid(position)) {
        return false;
    }
    // Deferred placement: the board is mined around the first revealed cell,
    // which is why that move can never detonate.
    if (!field_.isPlaced()) {
        if (!field_.placeMines(seed_, &position)) {
            return false;
        }
        beginPlaying(nowMs);
    } else if (status_ == GameStatus::Ready) {
        beginPlaying(nowMs);
    }
    if (!field_.reveal(position)) {
        return false;
    }
    if (field_.hasDetonated()) {
        finish(GameStatus::Lost, nowMs);
    } else if (field_.isVictory()) {
        finish(GameStatus::Won, nowMs);
    }
    return true;
}

bool GameSession::toggleFlag(Position position, unsigned long nowMs) {
    if (status_ == GameStatus::Won || status_ == GameStatus::Lost) {
        return false;
    }
    if (!field_.isValid(position)) {
        return false;
    }
    // A flag starts the clock like any first action, yet placement still
    // waits for the first reveal so the safe-open guarantee holds.
    if (status_ == GameStatus::Ready) {
        beginPlaying(nowMs);
    }
    return field_.toggleFlag(position);
}

bool GameSession::restart(std::uint32_t seed) {
    return start(config_, seed);
}

GameStatus GameSession::getStatus() const {
    return status_;
}

std::uint32_t GameSession::getElapsedSeconds(unsigned long nowMs) const {
    if (status_ == GameStatus::Ready) {
        return 0;
    }
    const unsigned long stop =
        (status_ == GameStatus::Won || status_ == GameStatus::Lost) ? endMs_ : nowMs;
    if (stop < startMs_) {
        return 0;
    }
    return static_cast<std::uint32_t>((stop - startMs_) / 1000U);
}

std::uint16_t GameSession::getRemainingMines() const {
    const std::uint16_t flags = field_.getFlagCount();
    return (flags >= config_.mineCount) ? 0 : static_cast<std::uint16_t>(config_.mineCount - flags);
}

const Minefield& GameSession::getMinefield() const {
    return field_;
}

const BoardConfig& GameSession::getConfig() const {
    return config_;
}

void GameSession::beginPlaying(unsigned long nowMs) {
    status_ = GameStatus::Playing;
    startMs_ = nowMs;
    endMs_ = nowMs;
}

void GameSession::finish(GameStatus terminal, unsigned long nowMs) {
    status_ = terminal;
    endMs_ = nowMs;
}

}  // namespace minesweeper
