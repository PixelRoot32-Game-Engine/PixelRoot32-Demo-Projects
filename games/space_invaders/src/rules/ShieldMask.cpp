#include "rules/ShieldMask.h"

namespace spaceinvaders::rules {

namespace {

/// Every pixel of a row set, one bit per column up to ShieldWidth.
constexpr uint32_t RowAllMask = (1u << ShieldWidth) - 1u;

/// The four lowest rows carry the cannon arch (S10).
constexpr int ArchRows = 4;

/// Clears `count` bits starting at `start`, clamped by the caller's range.
constexpr uint32_t clearRange(uint32_t bits, int start, int count) {
    const uint32_t range = count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u);
    return bits & ~(range << start);
}

/**
 * @brief Nearest-pixel offset `delta * step / steps`, rounded away from zero.
 *
 * Integer-only on purpose: the rules layer also builds for targets without an
 * FPU, and the walk must land on the same cells there as in the host tests.
 */
constexpr int roundedOffset(int delta, int step, int steps) {
    if (steps <= 0) {
        return 0;
    }
    const int numerator = delta * step;
    if (numerator >= 0) {
        return (numerator + steps / 2) / steps;
    }
    return -((-numerator + steps / 2) / steps);
}

}  // namespace

bool carveSwept(ShieldField& field, int shield, int fromX, int fromY, int toX, int toY) {
    const int dx = toX - fromX;
    const int dy = toY - fromY;
    const int absDx = dx < 0 ? -dx : dx;
    const int absDy = dy < 0 ? -dy : dy;
    const int steps = absDx > absDy ? absDx : absDy;

    // One sample per pixel along the longer axis: no cell on the path can be
    // stepped over, and every sample is a real mask coordinate. Samples that
    // fall outside the mask report empty, so the walk clips instead of
    // wrapping a negative row onto the bottom of the shield.
    for (int step = 0; step <= steps; ++step) {
        const int x = fromX + roundedOffset(dx, step, steps);
        const int y = fromY + roundedOffset(dy, step, steps);
        if (!field.solid(shield, x, y)) {
            continue;
        }
        // The impact cell is where the shot entered the wall, so the blast is
        // centred on it rather than on the end-of-tick position (S10).
        field.carve(shield, x, y);
        return true;
    }
    return false;
}

void ShieldField::reset() {
    // The intact shape: cut top corners, eleven full rows and the bottom arch
    // the cannon fires through. Rebuilding it per wave restores damage (S10).
    for (int shield = 0; shield < ShieldCount; ++shield) {
        for (int y = 0; y < ShieldHeight; ++y) {
            uint32_t bits = RowAllMask;
            if (y == 0) {
                bits = clearRange(bits, 0, 2);
                bits = clearRange(bits, ShieldWidth - 2, 2);
            } else if (y >= ShieldHeight - ArchRows) {
                const int count = 2 * (y - (ShieldHeight - ArchRows) + 2);
                bits = clearRange(bits, ShieldWidth / 2 - count / 2, count);
            }
            rowMask[shield][y] = bits;
        }
    }
}

bool ShieldField::solid(int shield, int x, int y) const {
    if (shield < 0 || shield >= ShieldCount || x < 0 || x >= ShieldWidth || y < 0 ||
        y >= ShieldHeight) {
        return false;
    }
    return (rowMask[shield][y] & (1u << x)) != 0u;
}

void ShieldField::carve(int shield, int x, int y) {
    if (shield < 0 || shield >= ShieldCount) {
        return;
    }
    // A 3x3 blast clipped to the mask, so an edge hit erodes only the pixels
    // that actually exist instead of spilling onto the next shield.
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int px = x + dx;
            const int py = y + dy;
            if (px < 0 || px >= ShieldWidth || py < 0 || py >= ShieldHeight) {
                continue;
            }
            rowMask[shield][py] &= ~(1u << px);
        }
    }
}

bool ShieldField::destroyed(int shield) const {
    if (shield < 0 || shield >= ShieldCount) {
        return true;
    }
    for (int y = 0; y < ShieldHeight; ++y) {
        if (rowMask[shield][y] != 0u) {
            return false;
        }
    }
    return true;
}

uint32_t ShieldField::rowBits(int shield, int y) const {
    if (shield < 0 || shield >= ShieldCount || y < 0 || y >= ShieldHeight) {
        return 0;
    }
    return rowMask[shield][y];
}

}  // namespace spaceinvaders::rules
