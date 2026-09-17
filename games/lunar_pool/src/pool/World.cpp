/*
 * World.cpp - see World.h.
 */
#include "pool/World.h"

namespace pool {

void World::placeBall(uint8_t index, int32_t x, int32_t y, int32_t vx, int32_t vy) {
    Ball& b = balls_[index];
    b.x = x;
    b.y = y;
    b.vx = vx;
    b.vy = vy;
    b.remX = 0;
    b.remY = 0;
    b.number = index;
    b.active = true;
    if (index >= ballCount_) {
        ballCount_ = static_cast<uint8_t>(index + 1);
    }
}

void World::placeFromTable(const Table& table) {
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        balls_[i] = Ball{};
    }
    for (uint8_t i = 0; i < table.ballCount; ++i) {
        Ball& b = balls_[i];
        b.x = table.balls[i].x;
        b.y = table.balls[i].y;
        b.number = table.balls[i].number;
        b.active = true;
    }
    ballCount_ = table.ballCount;
    clearLog();
}

void World::capture(uint8_t index) {
    Ball& b = balls_[index];
    b.active = false;
    b.vx = 0;
    b.vy = 0;
    b.remX = 0;
    b.remY = 0;
    if (log_.count < kMaxBalls) {
        log_.ball[log_.count++] = index;
    }
}

void World::clearLog() {
    log_ = PocketLog{};
}

void World::move() {
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        Ball& b = balls_[i];
        if (!b.active) {
            continue;
        }
        // Drift-free integration: the leftover fractional raw unit is
        // carried into the next substep's total instead of discarded, so
        // accumulated position error stays below 1 raw unit forever.
        const int32_t totalX = b.vx + b.remX;
        const int32_t stepX = totalX / kSubstepsPerSecond;
        b.remX = static_cast<int16_t>(totalX - stepX * kSubstepsPerSecond);
        b.x += stepX;

        const int32_t totalY = b.vy + b.remY;
        const int32_t stepY = totalY / kSubstepsPerSecond;
        b.remY = static_cast<int16_t>(totalY - stepY * kSubstepsPerSecond);
        b.y += stepY;
    }
}

void World::captureNear(const Table& table) {
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        Ball& b = balls_[i];
        if (!b.active) {
            continue;
        }
        for (uint8_t p = 0; p < table.pocketCount; ++p) {
            const Pocket& pocket = table.pockets[p];
            const int64_t dx = int64_t{b.x} - pocket.x;
            const int64_t dy = int64_t{b.y} - pocket.y;
            const int64_t distSq = dx * dx + dy * dy;
            if (distSq < static_cast<int64_t>(kCaptureRadiusRaw) * kCaptureRadiusRaw) {
                capture(i);
                break;  // already captured; skip the remaining pockets.
            }
        }
    }
}

void World::friction() {
    constexpr int64_t kThresholdSq = static_cast<int64_t>(kFrictionPerSubstepRaw) * kFrictionPerSubstepRaw;
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        Ball& b = balls_[i];
        if (!b.active) {
            continue;
        }
        const int64_t speedSq = int64_t{b.vx} * b.vx + int64_t{b.vy} * b.vy;
        if (speedSq <= kThresholdSq) {
            b.vx = 0;
            b.vy = 0;
            b.remX = 0;
            b.remY = 0;
            continue;
        }
        // speedSq > 119^2 > 0 here, so isqrt64's floor is >= 119 (never zero).
        const int64_t speed = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(speedSq)));
        b.vx -= static_cast<int32_t>(divRound(int64_t{b.vx} * kFrictionPerSubstepRaw, speed));
        b.vy -= static_cast<int32_t>(divRound(int64_t{b.vy} * kFrictionPerSubstepRaw, speed));
    }
}

void World::substep(const Table& table) {
    move();
    captureNear(table);
    friction();  // steps 3-4 (ball-ball/cushion) are added by later slices.
}

}  // namespace pool
