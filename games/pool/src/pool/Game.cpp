/*
 * Game.cpp - see Game.h.
 */
#include "pool/Game.h"

#include "pool/Geometry.h"
#include "pool/Tables.h"
#include "pool/Trig.h"

namespace pool {

TableError Game::newGame(uint8_t stage) {
    const TableError err = loadStage(stage);
    if (err != TableError::None) {
        // Broken table data must end the game before a frame runs, never
        // mid-shot; the scene surfaces this same failure in red.
        state_ = State::GameOver;
        won_ = false;
        return err;
    }
    score_ = 0;
    turn_ = 0;
    won_ = false;
    state_ = State::Menu;
    return TableError::None;
}

void Game::startGame() {
    if (state_ != State::Menu) {
        return;
    }
    turn_ = 1;
    world_.clearLog();
    state_ = State::Aiming;
}

void Game::aimLeft() {
    if (state_ != State::Aiming) {
        return;
    }
    // Angles wrap modulo a revolution; going below zero must wrap to the top
    // instead of underflowing the unsigned angle.
    angle_ = static_cast<uint16_t>((angle_ + kAngleSteps - kAimStep) % kAngleSteps);
}

void Game::aimRight() {
    if (state_ != State::Aiming) {
        return;
    }
    angle_ = static_cast<uint16_t>((angle_ + kAimStep) % kAngleSteps);
}

void Game::aimLeftFine() {
    if (state_ != State::Aiming) {
        return;
    }
    angle_ = static_cast<uint16_t>((angle_ + kAngleSteps - kAimFineStep) % kAngleSteps);
}

void Game::aimRightFine() {
    if (state_ != State::Aiming) {
        return;
    }
    angle_ = static_cast<uint16_t>((angle_ + kAimFineStep) % kAngleSteps);
}

void Game::powerUp() {
    if (state_ != State::Aiming || power_ >= kMaxPower) {
        return;
    }
    ++power_;
}

void Game::powerDown() {
    if (state_ != State::Aiming || power_ <= kMinPower) {
        return;
    }
    --power_;
}

bool Game::shoot() {
    if (state_ != State::Aiming || !world_.ball(0).active) {
        return false;
    }
    // Launch speed scales linearly with the power meter; components go
    // through one rounded division each so every target agrees on them.
    const int32_t speed = static_cast<int32_t>(power_) * kSpeedPerLevelRaw;
    const int32_t vx = static_cast<int32_t>(divRound(static_cast<int64_t>(speed) * cosQ14(angle_), kQ14One));
    const int32_t vy = static_cast<int32_t>(divRound(static_cast<int64_t>(speed) * sinQ14(angle_), kQ14One));
    const Ball& cue = world_.ball(0);
    world_.clearLog();
    shotExpected_ = nextExpected();
    world_.placeBall(0, cue.x, cue.y, vx, vy);
    state_ = State::Shooting;
    return true;
}

void Game::stepFrame() {
    switch (state_) {
        case State::Shooting:
            runFrame();
            state_ = world_.allBallsAtRest() ? State::EvaluateShot : State::BallsMoving;
            break;
        case State::BallsMoving:
            runFrame();
            if (world_.allBallsAtRest()) {
                state_ = State::EvaluateShot;
            }
            break;
        case State::EvaluateShot:
            evaluateShot();
            state_ = State::NextTurn;
            break;
        case State::NextTurn:
            advanceTurn();
            break;
        default:
            break;  // Menu, Aiming and GameOver wait for input, never simulate.
    }
}

uint8_t Game::nextExpected() const {
    for (uint8_t i = 1; i < kMaxBalls; ++i) {
        if (world_.ball(i).active) {
            return world_.ball(i).number;
        }
    }
    return 0;
}

void Game::runFrame() {
    for (int32_t i = 0; i < kSubstepsPerFrame; ++i) {
        world_.substep(table_);
    }
}

void Game::evaluateShot() {
    respotFailed_ = false;
    const PocketLog& log = world_.pocketLog();
    bool cueFoul = false;
    bool orderFoul = false;
    uint8_t valid = 0;
    // Target numbers run 1..N contiguously (loadTable's BadBallNumber check),
    // so the expectation simply climbs by one per valid pocket, in log order.
    uint8_t exp = shotExpected_;
    for (uint8_t k = 0; k < log.count; ++k) {
        const uint8_t idx = log.ball[k];
        if (idx == 0) {
            cueFoul = true;
        } else if (idx == exp) {
            ++valid;
            ++exp;
        } else {
            orderFoul = true;
        }
    }
    if (cueFoul || orderFoul) {
        // A fouled shot scores nothing even for balls that fell in order;
        // pocketed balls still stay down (physical fact, and it can win).
        score_ -= kFoulPenalty;
        if (score_ < 0) {
            score_ = 0;
        }
        if (cueFoul && !respotCue()) {
            respotFailed_ = true;
        }
    } else {
        score_ += static_cast<int32_t>(valid) * kPointsPerBall;
    }
}

void Game::advanceTurn() {
    // Every shot costs one, foul or not.
    if (shotsLeft_ > 0) {
        --shotsLeft_;
    }
    // Clearing the table advances while stages remain (score carried
    // forward) and wins on the last stage, even on the last shot or a fouled
    // one; pocketed balls stay down. A table that cannot load is lost
    // instead, the same failure newGame() reports.
    if (!anyTargetActive()) {
        if (stage_ < kStageCount) {
            if (loadStage(static_cast<uint8_t>(stage_ + 1)) != TableError::None) {
                won_ = false;
                state_ = State::GameOver;
                return;
            }
            turn_ = 1;
            state_ = State::Aiming;
            return;
        }
        won_ = true;
        state_ = State::GameOver;
        return;
    }
    if (respotFailed_ || shotsLeft_ == 0) {
        won_ = false;
        state_ = State::GameOver;
        return;
    }
    ++turn_;
    world_.clearLog();
    state_ = State::Aiming;
}

TableError Game::loadStage(uint8_t stage) {
    def_ = &tableForStage(stage);
    stage_ = stage;
    table_ = Table{};
    const TableError err = loadTable(*def_, table_);
    if (err != TableError::None) {
        return err;
    }
    // Border bounding box for the respot scan: whole pixels, read once here
    // so a foul never pays the scan setup cost mid-game.
    minX_ = maxX_ = def_->border.points[0].x;
    minY_ = maxY_ = def_->border.points[0].y;
    for (uint8_t i = 1; i < def_->border.count; ++i) {
        const PointPx& p = def_->border.points[i];
        if (p.x < minX_) minX_ = p.x;
        if (p.x > maxX_) maxX_ = p.x;
        if (p.y < minY_) minY_ = p.y;
        if (p.y > maxY_) maxY_ = p.y;
    }
    world_.placeFromTable(table_);
    shotsLeft_ = table_.ballCount > 0
                     ? static_cast<uint8_t>((table_.ballCount - 1) * kShotsPerTarget)
                     : 0;
    angle_ = 0;
    power_ = kDefaultPower;
    shotExpected_ = 0;
    respotFailed_ = false;
    world_.clearLog();
    return TableError::None;
}

bool Game::respotCue() {
    // The cue start is the fair spot; only when a target sits on it does the
    // deterministic bbox scan (topmost, then leftmost free whole pixel) kick
    // in, so respots never depend on hidden iteration order.
    const int32_t cx = static_cast<int32_t>(def_->cue.x) * kPxScale;
    const int32_t cy = static_cast<int32_t>(def_->cue.y) * kPxScale;
    if (isFree(cx, cy)) {
        world_.placeBall(0, cx, cy, 0, 0);
        return true;
    }
    for (int16_t y = minY_; y <= maxY_; ++y) {
        for (int16_t x = minX_; x <= maxX_; ++x) {
            const int32_t px = static_cast<int32_t>(x) * kPxScale;
            const int32_t py = static_cast<int32_t>(y) * kPxScale;
            if (isFree(px, py)) {
                world_.placeBall(0, px, py, 0, 0);
                return true;
            }
        }
    }
    return false;
}

bool Game::isFree(int32_t x, int32_t y) const {
    if (!pointInPolygon(def_->border, x, y)) {
        return false;
    }
    // Stage 2+ obstacles are solid: the edge/vertex predicates below only
    // reach kBallRadiusRaw from an edge, so a cell deep inside a block needs
    // this containment check, mirroring loadTable()'s BallOverlapsCushion.
    for (uint8_t o = 0; o < def_->obstacleCount; ++o) {
        if (pointInPolygon(def_->obstacles[o], x, y)) {
            return false;
        }
    }
    for (uint8_t p = 0; p < table_.pocketCount; ++p) {
        const int64_t dx = static_cast<int64_t>(x) - table_.pockets[p].x;
        const int64_t dy = static_cast<int64_t>(y) - table_.pockets[p].y;
        if (dx * dx + dy * dy < static_cast<int64_t>(kCaptureRadiusRaw) * kCaptureRadiusRaw) {
            return false;
        }
    }
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        const Ball& ball = world_.ball(i);
        if (!ball.active) {
            continue;
        }
        const int64_t dx = static_cast<int64_t>(x) - ball.x;
        const int64_t dy = static_cast<int64_t>(y) - ball.y;
        if (dx * dx + dy * dy < static_cast<int64_t>(kBallContactRaw) * kBallContactRaw) {
            return false;
        }
    }
    // Same edge/vertex predicates the simulation uses, so a respotted ball
    // never starts its next shot already in cushion contact.
    for (uint8_t s = 0; s < table_.segmentCount; ++s) {
        const Segment& seg = table_.segments[s];
        if (segmentContact(seg, x, y, kBallRadiusRaw) || vertexContact(seg, x, y, kBallRadiusRaw)) {
            return false;
        }
    }
    return true;
}

}  // namespace pool
