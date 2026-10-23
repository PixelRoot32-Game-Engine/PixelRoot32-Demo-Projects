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

}  // namespace

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
