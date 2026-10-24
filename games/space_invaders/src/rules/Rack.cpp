#include "rules/Rack.h"

namespace spaceinvaders::rules {

namespace {

constexpr int CellCount = RackRows * RackCols;

/// Step a sweep applies with the given live count (S5): three px for the last
/// alien so the final approach is faster than the formation's own march.
constexpr int stepForAlive(int alive) {
    return alive == 1 ? RackLastStepX : RackStepX;
}

}  // namespace

int16_t rackOriginYForWave(uint16_t wave) {
    // Wave numbers are 1-based, so wave 1 keeps the reference origin and a value
    // below it is treated as the first wave rather than raising the formation.
    const uint32_t waveSteps = wave > 1 ? static_cast<uint32_t>(wave - 1) : 0u;
    const uint32_t origin =
        static_cast<uint32_t>(RackOriginY) + waveSteps * static_cast<uint32_t>(RackWaveDropY);

    // Saturate at the deepest formation that still fits: a wave whose top is any
    // lower would already count as an invasion on its first tick (S13), so this
    // bound is where the accumulation stops describing a reachable wave.
    if (origin >= static_cast<uint32_t>(RackMaxOriginY)) {
        return static_cast<int16_t>(RackMaxOriginY);
    }
    return static_cast<int16_t>(origin);
}

void Rack::spawn(int16_t originX, int16_t originY) {
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            RackCell& target = cells[row][col];
            target.x = static_cast<int16_t>(originX + col * RackPitchX);
            target.y = static_cast<int16_t>(originY + row * RackPitchY);
            target.alive = true;
            target.type = alienTypeForRow(row);
        }
    }
    animFrame = 0;
    cursor = 0;
    holdTicks = 0;
    marchDirection = 1;
}

RackTick Rack::tick() {
    RackTick result;

    if (holdTicks > 0) {
        --holdTicks;
        return result;
    }

    const int alive = aliveCount();
    if (alive == 0) {
        return result;
    }

    // Resume the row-major scan where the previous tick stopped, wrapping so
    // a cursor left past the last live cell still finds one.
    int index = -1;
    for (int offset = 0; offset < CellCount; ++offset) {
        const int candidate = (cursor + offset) % CellCount;
        if (cells[candidate / RackCols][candidate % RackCols].alive) {
            index = candidate;
            break;
        }
    }
    if (index < 0) {
        return result;
    }

    const int step = stepForAlive(alive);
    RackCell& moved = cells[index / RackCols][index % RackCols];
    moved.x = static_cast<int16_t>(moved.x + marchDirection * step);
    result.movedAlien = true;
    cursor = static_cast<uint8_t>((index + 1) % CellCount);

    // The sweep closes on the last live cell in row-major order, not after a
    // fixed tick count, so the interval follows the live count by itself.
    for (int after = index + 1; after < CellCount; ++after) {
        if (cells[after / RackCols][after % RackCols].alive) {
            return result;
        }
    }

    result.sweepCompleted = true;
    result.frameFlipped = true;
    animFrame = static_cast<uint8_t>(animFrame ^ 1u);
    cursor = 0;

    // Bounds use the outermost live sprite, and the next sweep's step, so the
    // reversal lands on the turn tick while every alien is still in range.
    int16_t minX = 0;
    int16_t maxX = 0;
    bool haveLive = false;
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            const RackCell& current = cells[row][col];
            if (!current.alive) {
                continue;
            }
            if (!haveLive || current.x < minX) {
                minX = current.x;
            }
            if (!haveLive || current.x > maxX) {
                maxX = current.x;
            }
            haveLive = true;
        }
    }

    const int nextStep = stepForAlive(alive);
    const bool leavesRight = marchDirection > 0 && maxX + nextStep > RackMaxRight;
    const bool leavesLeft = marchDirection < 0 && minX - nextStep < RackMinLeft;
    if (leavesRight || leavesLeft) {
        marchDirection = static_cast<int8_t>(-marchDirection);
        result.turnedAtEdge = true;
        for (int row = 0; row < RackRows; ++row) {
            for (int col = 0; col < RackCols; ++col) {
                RackCell& current = cells[row][col];
                if (current.alive) {
                    current.y = static_cast<int16_t>(current.y + RackDropY);
                }
            }
        }
    }

    return result;
}

void Rack::hold(uint16_t ticks) {
    holdTicks = ticks;
}

void Rack::kill(int row, int col) {
    if (row < 0 || row >= RackRows || col < 0 || col >= RackCols) {
        return;
    }
    cells[row][col].alive = false;
}

bool Rack::anyAlive() const {
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            if (cells[row][col].alive) {
                return true;
            }
        }
    }
    return false;
}

int Rack::aliveCount() const {
    int alive = 0;
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            if (cells[row][col].alive) {
                ++alive;
            }
        }
    }
    return alive;
}

int Rack::animationFrame() const {
    return animFrame;
}

RackCell& Rack::cell(int row, int col) {
    return cells[row][col];
}

const RackCell& Rack::cell(int row, int col) const {
    return cells[row][col];
}

int16_t Rack::lowestAlienBottom() const {
    int16_t bottom = 0;
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            const RackCell& current = cells[row][col];
            if (!current.alive) {
                continue;
            }
            const int16_t cellBottom =
                static_cast<int16_t>(current.y + alienHeight(current.type));
            if (cellBottom > bottom) {
                bottom = cellBottom;
            }
        }
    }
    return bottom;
}

void Rack::shiftDown(int16_t dy) {
    // Every cell moves, dead ones included, so a stored death position keeps
    // its place in the formation for the explosion the caller draws there.
    for (int row = 0; row < RackRows; ++row) {
        for (int col = 0; col < RackCols; ++col) {
            cells[row][col].y = static_cast<int16_t>(cells[row][col].y + dy);
        }
    }
}

int Rack::direction() const {
    return marchDirection;
}

}  // namespace spaceinvaders::rules
