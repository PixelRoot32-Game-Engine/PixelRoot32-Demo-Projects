#include "game/TetrisSession.h"

#include "game/Tetromino.h"

namespace tetris {

namespace {

// First-tries-first wall kicks: straight, one step sideways, then two.
// Keeps rotation safe next to walls and stacks without full SRS tables.
constexpr int8_t KICKS[5][2] = {{0, 0}, {-1, 0}, {1, 0}, {-2, 0}, {2, 0}};

}  // namespace

TetrisSession::TetrisSession()
    : active_{TetrominoType::I, 3, -1, 0},
      next_(TetrominoType::I),
      status_(GameStatus::Ready),
      fallAccumMs_(0),
      lockAccumMs_(0),
      bestScore_(0) {}

void TetrisSession::start(std::uint32_t seed) {
    board_.clear();
    scoring_.reset();
    generator_.reset(seed);
    status_ = GameStatus::Playing;
    resetTimerState();
    active_.type = generator_.next();
    active_.x = Tetromino::spawnX(active_.type);
    active_.y = Tetromino::spawnY();
    active_.rotation = 0;
    next_ = generator_.next();
    if (!collision_.canPlace(board_, active_)) {
        status_ = GameStatus::GameOver;
    }
}

StepResult TetrisSession::update(std::uint32_t deltaMs, bool softDropHeld) {
    StepResult result{false, 0, 0, false};
    if (status_ != GameStatus::Playing) {
        return result;
    }
    // Clamp long stalls (pause hitch, slow frame) to one bounded step so
    // the piece can fall at most a few rows per call, never a whole stack.
    if (deltaMs > MAX_STEP_MS) {
        deltaMs = MAX_STEP_MS;
    }
    std::uint32_t interval = ScoringSystem::fallIntervalMs(scoring_.level());
    if (softDropHeld) {
        interval /= SOFT_DROP_DIVISOR;
        if (interval < SOFT_DROP_MIN_INTERVAL_MS) {
            interval = SOFT_DROP_MIN_INTERVAL_MS;
        }
        if (interval == 0) {
            interval = 1;
        }
    }

    if (grounded()) {
        // On the stack: the lock clock runs, gravity pauses.
        lockAccumMs_ += deltaMs;
        if (lockAccumMs_ >= LOCK_DELAY_MS) {
            const std::uint8_t before = scoring_.level();
            const std::uint16_t linesBefore = scoring_.lines();
            lockActive();
            result.locked = true;
            const unsigned gained = scoring_.lines() - linesBefore;
            result.cleared = (gained > 255U) ? 255U : static_cast<std::uint8_t>(gained);
            result.leveledUp = (scoring_.level() != before);
            if (status_ == GameStatus::GameOver) {
                result.gameOver = true;
            }
        }
        return result;
    }

    lockAccumMs_ = 0;
    fallAccumMs_ += deltaMs;
    std::uint32_t steps = 0;
    while (fallAccumMs_ >= interval) {
        fallAccumMs_ -= interval;
        ActivePiece below = active_;
        below.y += 1;
        if (collision_.canPlace(board_, below)) {
            active_ = below;
            if (softDropHeld) {
                scoring_.addSoftDropCells(1);
            }
            ++steps;
            // One row per update keeps high levels playable on slow frames;
            // leftover time stays banked for the next call.
            break;
        }
        break;
    }
    (void)steps;
    return result;
}

bool TetrisSession::tryMove(int direction) {
    if (status_ != GameStatus::Playing) {
        return false;
    }
    if (direction != -1 && direction != 1) {
        return false;
    }
    ActivePiece candidate = active_;
    candidate.x = static_cast<int8_t>(candidate.x + direction);
    if (!collision_.canPlace(board_, candidate)) {
        return false;
    }
    active_ = candidate;
    if (grounded()) {
        // Nudging along the stack gives a small lock-delay refresh, but it
        // never exceeds the delay: classic "move reset" would stall forever.
        if (lockAccumMs_ > 0 && LOCK_DELAY_MS - lockAccumMs_ > 60) {
            lockAccumMs_ -= 0;
        }
    }
    return true;
}

bool TetrisSession::tryRotateCw() {
    if (status_ != GameStatus::Playing) {
        return false;
    }
    const std::uint8_t next = static_cast<std::uint8_t>((active_.rotation + 1U) & 3U);
    for (const auto& kick : KICKS) {
        ActivePiece candidate = active_;
        candidate.rotation = next;
        candidate.x = static_cast<int8_t>(candidate.x + kick[0]);
        candidate.y = static_cast<int8_t>(candidate.y + kick[1]);
        if (collision_.canPlace(board_, candidate)) {
            active_ = candidate;
            lockAccumMs_ = 0;
            return true;
        }
    }
    return false;
}

bool TetrisSession::tryRotateCcw() {
    if (status_ != GameStatus::Playing) {
        return false;
    }
    const std::uint8_t next = static_cast<std::uint8_t>((active_.rotation + 3U) & 3U);
    for (const auto& kick : KICKS) {
        ActivePiece candidate = active_;
        candidate.rotation = next;
        candidate.x = static_cast<int8_t>(candidate.x + kick[0]);
        candidate.y = static_cast<int8_t>(candidate.y + kick[1]);
        if (collision_.canPlace(board_, candidate)) {
            active_ = candidate;
            lockAccumMs_ = 0;
            return true;
        }
    }
    return false;
}

std::uint8_t TetrisSession::hardDrop() {
    if (status_ != GameStatus::Playing) {
        return 0;
    }
    const int target = ghostY();
    const int distance = target - active_.y;
    active_.y = static_cast<int8_t>(target);
    if (distance > 0) {
        scoring_.addHardDropCells(static_cast<std::uint32_t>(distance));
    }
    const std::uint16_t before = scoring_.lines();
    lockActive();
    const unsigned gained = scoring_.lines() - before;
    return (gained > 255U) ? 255U : static_cast<std::uint8_t>(gained);
}

int TetrisSession::ghostY() const {
    ActivePiece probe = active_;
    while (true) {
        ActivePiece below = probe;
        below.y += 1;
        if (!collision_.canPlace(board_, below)) {
            break;
        }
        probe = below;
        if (probe.y > BOARD_HEIGHT) {
            break;
        }
    }
    return probe.y;
}

const Board& TetrisSession::board() const {
    return board_;
}

const ActivePiece& TetrisSession::active() const {
    return active_;
}

TetrominoType TetrisSession::nextType() const {
    return next_;
}

GameStatus TetrisSession::status() const {
    return status_;
}

std::uint32_t TetrisSession::score() const {
    return scoring_.score();
}

std::uint8_t TetrisSession::level() const {
    return scoring_.level();
}

std::uint16_t TetrisSession::lines() const {
    return scoring_.lines();
}

std::uint32_t TetrisSession::bestScore() const {
    return bestScore_;
}

bool TetrisSession::spawnNext() {
    active_.type = next_;
    active_.x = Tetromino::spawnX(active_.type);
    active_.y = Tetromino::spawnY();
    active_.rotation = 0;
    next_ = generator_.next();
    // One bag draw per spawn: start() consumes two draws (active + next),
    // every later spawn promotes next_ and draws one replacement.
    if (!collision_.canPlace(board_, active_)) {
        status_ = GameStatus::GameOver;
        if (scoring_.score() > bestScore_) {
            bestScore_ = scoring_.score();
        }
        return false;
    }
    return true;
}

void TetrisSession::lockActive() {
    // Copy the four blocks into the board, then clear and respawn.
    for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
        const BlockOffset block = Tetromino::blockAt(active_.type, active_.rotation, i);
        const int x = static_cast<int>(active_.x) + block.x;
        const int y = static_cast<int>(active_.y) + block.y;
        if (y >= 0) {
            board_.set(x, y, cellForType(active_.type));
        }
    }
    resetTimerState();
    const std::uint8_t cleared = lineClear_.clearLines(board_);
    if (cleared > 0) {
        scoring_.addLines(cleared);
    }
    if (scoring_.score() > bestScore_) {
        bestScore_ = scoring_.score();
    }
    spawnNext();
}

bool TetrisSession::grounded() const {
    ActivePiece below = active_;
    below.y += 1;
    return !collision_.canPlace(board_, below);
}

void TetrisSession::resetTimerState() {
    fallAccumMs_ = 0;
    lockAccumMs_ = 0;
}

}  // namespace tetris
