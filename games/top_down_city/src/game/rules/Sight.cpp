#include "game/rules/Sight.h"

namespace top_down_city::sight {

namespace {

/// Floor division, because -1 / 16 is 0 in C++ and the tile left of the origin
/// is -1. Nothing has negative coordinates today, but a ray sampling one tile
/// past an endpoint would silently fold the whole left edge onto column 0.
int floorDiv(int value, int divisor) {
    int quotient = value / divisor;
    if (value % divisor != 0 && (value < 0) != (divisor < 0)) {
        --quotient;
    }
    return quotient;
}

int absOf(int value) {
    return value < 0 ? -value : value;
}

}  // namespace

bool isClear(int fromX, int fromY, int toX, int toY,
             int tileSizePx, SolidFn solid, const void* context) {
    if (solid == nullptr || tileSizePx <= 0) {
        // No map to ask, so nothing hides behind anything. Clear, not
        // blocked: the caller without a tilemap is the scene mid-construction,
        // and an invisible city is a wanted level that never falls.
        return true;
    }

    // Canonical order before sampling anything: this is the whole reason the
    // answer is symmetric rather than nearly symmetric. Samples are struck at
    // `i / steps` along the ray and integer division truncates, so the same
    // line walked backwards lands on a different set of pixels and can disagree
    // with itself about a corner. Sorting means both directions walk the
    // identical ray, by construction rather than by rounding luck.
    if (toY < fromY || (toY == fromY && toX < fromX)) {
        int swap = fromX;
        fromX = toX;
        toX = swap;
        swap = fromY;
        fromY = toY;
        toY = swap;
    }

    const int fromTileX = floorDiv(fromX, tileSizePx);
    const int fromTileY = floorDiv(fromY, tileSizePx);
    const int toTileX   = floorDiv(toX, tileSizePx);
    const int toTileY   = floorDiv(toY, tileSizePx);

    const int dx = toX - fromX;
    const int dy = toY - fromY;
    const int span = absOf(dx) > absOf(dy) ? absOf(dx) : absOf(dy);

    // Half a tile; see the header. Never zero, or the step count below
    // divides by it.
    const int stride = tileSizePx / 2 > 0 ? tileSizePx / 2 : 1;
    const int steps = span / stride;

    for (int i = 1; i < steps; ++i) {
        const int tileX = floorDiv(fromX + dx * i / steps, tileSizePx);
        const int tileY = floorDiv(fromY + dy * i / steps, tileSizePx);
        // Neither end's own tile can block the view -- see the header. Tested
        // by tile, not by sample index: a sample can land in an endpoint's
        // tile without being the endpoint -- two people in adjacent tiles
        // have one sample between them, and it is inside one of the two.
        if ((tileX == fromTileX && tileY == fromTileY)
                || (tileX == toTileX && tileY == toTileY)) {
            continue;
        }
        if (solid(context, tileX, tileY)) {
            return false;
        }
    }
    return true;
}

}  // namespace top_down_city::sight
