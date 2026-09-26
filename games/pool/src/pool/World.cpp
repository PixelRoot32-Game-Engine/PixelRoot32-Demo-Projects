/*
 * World.cpp - see World.h.
 */
#include "pool/World.h"

#include "pool/Geometry.h"

namespace pool {
namespace {

// WHY: ceil-sqrt keeps a pushed ball strictly outside the contact radius. A
// floored norm would under-estimate the target gap and could leave the ball
// close enough to re-trigger on the next substep from rounding alone.
[[nodiscard]] uint32_t isqrtCeil(uint64_t value) {
    const uint32_t floor = isqrt64(value);
    return (static_cast<uint64_t>(floor) * floor == value) ? floor : static_cast<uint32_t>(floor + 1);
}

}  // namespace

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
    cushionHit_ = false;
    ballHit_ = false;
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

void World::resolveCushions(const Table& table) {
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        Ball& ball = balls_[i];
        if (!ball.active) {
            continue;
        }
        // Phase 1: edge interiors. N = (-ey, ex) points into the play area
        // (loadTable's winding contract), so approaching means v.N < 0.
        for (uint8_t s = 0; s < table.segmentCount; ++s) {
            const Segment& seg = table.segments[s];
            if (seg.lenSqPx <= 0) {
                continue;  // Degenerate data; loadTable rejects it, hand-built test tables might not.
            }
            if (!segmentContact(seg, ball.x, ball.y, kBallRadiusRaw)) {
                continue;
            }
            const int64_t dx = static_cast<int64_t>(ball.x) - seg.ax;
            const int64_t dy = static_cast<int64_t>(ball.y) - seg.ay;
            const int64_t nx = -static_cast<int64_t>(seg.ey);
            const int64_t ny = static_cast<int64_t>(seg.ex);
            const int64_t sdNum = dx * nx + dy * ny;  // >= 0 per segmentContact.
            // Positional: push along +N until the gap is radius + slop, so the
            // same predicate reads clear afterwards instead of re-firing.
            const int64_t norm = static_cast<int64_t>(isqrtCeil(static_cast<uint64_t>(seg.lenSqPx)));
            const int64_t target = static_cast<int64_t>(kBallRadiusRaw + kSlopRaw) * norm;
            const int64_t deficit = target - sdNum;
            if (deficit > 0) {
                ball.x += static_cast<int32_t>(divRound(deficit * nx, seg.lenSqPx));
                ball.y += static_cast<int32_t>(divRound(deficit * ny, seg.lenSqPx));
            }
            // Velocity: v' = v - 2(v.N)N/|N|^2 through one rounded division per
            // axis, only when moving into the cushion; a separating ball keeps
            // its velocity so resting contact never injects energy.
            const int64_t vDotN = static_cast<int64_t>(ball.vx) * nx + static_cast<int64_t>(ball.vy) * ny;
            if (vDotN < 0) {
                ball.vx -= static_cast<int32_t>(divRound(2 * vDotN * nx, seg.lenSqPx));
                ball.vy -= static_cast<int32_t>(divRound(2 * vDotN * ny, seg.lenSqPx));
                cushionHit_ = true;
            }
        }
        // Phase 2: vertices. Every polyline vertex is exactly one segment's
        // start point, so one pass covers each corner once, with no double
        // work and no preferred corner.
        for (uint8_t s = 0; s < table.segmentCount; ++s) {
            const Segment& seg = table.segments[s];
            if (!vertexContact(seg, ball.x, ball.y, kBallRadiusRaw)) {
                continue;
            }
            const int64_t dx = static_cast<int64_t>(ball.x) - seg.ax;
            const int64_t dy = static_cast<int64_t>(ball.y) - seg.ay;
            const int64_t distSq = dx * dx + dy * dy;
            if (distSq == 0) {
                // Center exactly on the vertex: no defined normal, so step
                // along +x by the full clearance. Exact and deterministic;
                // the next substep resolves whatever this step leaves behind.
                ball.x += kBallRadiusRaw + kSlopRaw;
                continue;
            }
            const int64_t dist = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(distSq)));
            // Floor-sqrt under-reads the gap, so (target - dist) can only
            // over-push past radius + slop, never leave a fresh contact.
            const int64_t overlap = static_cast<int64_t>(kBallRadiusRaw + kSlopRaw) - dist;
            if (overlap > 0) {
                ball.x += static_cast<int32_t>(divRound(dx * overlap, dist));
                ball.y += static_cast<int32_t>(divRound(dy * overlap, dist));
            }
            const int64_t vDotD = static_cast<int64_t>(ball.vx) * dx + static_cast<int64_t>(ball.vy) * dy;
            if (vDotD < 0) {
                ball.vx -= static_cast<int32_t>(divRound(2 * vDotD * dx, distSq));
                ball.vy -= static_cast<int32_t>(divRound(2 * vDotD * dy, distSq));
                cushionHit_ = true;
            }
        }
    }
}

void World::resolveBalls() {
    // Pairs in index-ascending order, so the outcome never depends on which
    // ball the caller placed first; TableDef's index-order convention needs
    // no extra sort to stay meaningful.
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        Ball& first = balls_[i];
        if (!first.active) {
            continue;
        }
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < kMaxBalls; ++j) {
            Ball& second = balls_[j];
            if (!second.active) {
                continue;
            }
            const int64_t dx = static_cast<int64_t>(second.x) - first.x;
            const int64_t dy = static_cast<int64_t>(second.y) - first.y;
            const int64_t distSq = dx * dx + dy * dy;
            constexpr int64_t kContactSq = static_cast<int64_t>(kBallContactRaw) * kBallContactRaw;
            if (distSq >= kContactSq) {
                continue;  // Strict < matches Table validation and capture.
            }
            if (distSq == 0) {
                // Coincident centers: split along +x and swap the x
                // velocities (head-on elastic along that axis). Unreachable
                // from validated starts; hand-placed tests only.
                constexpr int32_t kTotal = kBallContactRaw + kSlopRaw;
                first.x -= kTotal / 2;
                second.x += kTotal - kTotal / 2;
                const int32_t tmp = first.vx;
                first.vx = second.vx;
                second.vx = tmp;
                ballHit_ = true;
                continue;
            }
            const int64_t dist = static_cast<int64_t>(isqrt64(static_cast<uint64_t>(distSq)));
            // dist >= 1 here; floor-sqrt over-states the overlap, so the
            // split push lands past contact + slop instead of short of it.
            const int64_t overlap = static_cast<int64_t>(kBallContactRaw + kSlopRaw) - dist;
            if (overlap > 0) {
                const int64_t den = 2 * dist;
                first.x -= static_cast<int32_t>(divRound(dx * overlap, den));
                first.y -= static_cast<int32_t>(divRound(dy * overlap, den));
                second.x += static_cast<int32_t>(divRound(dx * overlap, den));
                second.y += static_cast<int32_t>(divRound(dy * overlap, den));
            }
            // Equal-mass elastic exchange of the normal component, computed
            // once as J and applied symmetrically so total momentum stays
            // exact down to the raw unit.
            const int64_t vRelDotD =
                (static_cast<int64_t>(first.vx) - second.vx) * dx + (static_cast<int64_t>(first.vy) - second.vy) * dy;
            if (vRelDotD > 0) {  // Approaching; <= 0 is separating.
                const int32_t jx = static_cast<int32_t>(divRound(vRelDotD * dx, distSq));
                const int32_t jy = static_cast<int32_t>(divRound(vRelDotD * dy, distSq));
                first.vx -= jx;
                first.vy -= jy;
                second.vx += jx;
                second.vy += jy;
                ballHit_ = true;
            }
        }
    }
}

void World::substep(const Table& table) {
    move();
    resolveCushions(table);
    resolveBalls();
    captureNear(table);
    friction();
}

bool World::allBallsAtRest() const {
    for (uint8_t i = 0; i < kMaxBalls; ++i) {
        const Ball& ball = balls_[i];
        if (ball.active && (ball.vx != 0 || ball.vy != 0)) {
            return false;
        }
    }
    return true;
}

}  // namespace pool
