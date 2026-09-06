#include "game/rules/Lanes.h"

namespace top_down_city::lanes {

namespace {

/**
 * @brief The heading of a lane, from its offset across the band.
 * @return true when the offset names a lane at all.
 *
 * Right-hand traffic, and the whole of what that means is here. Facing east,
 * a driver's right hand points south, so eastbound is the southern lane --
 * the higher row, offset 3; facing south, right points west, so southbound is
 * the western lane -- the lower column, offset 1. The two look inconsistent
 * written down and are one rule; swapping either pair gives a city where
 * every car drives on the left, self-consistent and completely wrong.
 */
bool laneHeading(int offset, bool horizontal, Heading& out) {
    if (horizontal) {
        if (offset == kNearLaneOffset) { out = Heading{-1, 0}; return true; }
        if (offset == kFarLaneOffset)  { out = Heading{ 1, 0}; return true; }
        return false;
    }
    if (offset == kNearLaneOffset) { out = Heading{0,  1}; return true; }
    if (offset == kFarLaneOffset)  { out = Heading{0, -1}; return true; }
    return false;
}

}  // namespace

int headingsAt(int tileX, int tileY, const Band* bands, int bandCount,
               Heading* out) {
    if (bands == nullptr || out == nullptr) {
        return 0;
    }
    // Negative tiles are a normal input, not a corner case: the traffic pool
    // samples a ring around the camera and the camera sits on the map edge
    // often. The table is unsigned, so the comparison must not be.
    if (tileX < 0 || tileY < 0) {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < bandCount && found < kMaxHeadingsPerTile; ++i) {
        const Band& band = bands[i];
        const bool horizontal = band.horizontal != 0;

        // `across` is the offset through the band's width; `along` is the
        // distance down its length. Which coordinate is which is the only
        // thing the orientation changes.
        const int across = horizontal ? tileY - static_cast<int>(band.start)
                                      : tileX - static_cast<int>(band.start);
        const int along  = horizontal ? tileX : tileY;

        if (across < 0 || across >= kBandWidthTiles) {
            continue;
        }
        if (along < static_cast<int>(band.first)
                || along > static_cast<int>(band.last)) {
            continue;
        }
        Heading heading{0, 0};
        if (!laneHeading(across, horizontal, heading)) {
            // The kerb or the painted line. Two of those crossing -- the
            // dead centre of a junction -- is still not a lane.
            continue;
        }
        out[found++] = heading;
    }
    return found;
}

bool isLane(int tileX, int tileY, const Band* bands, int bandCount) {
    Heading scratch[kMaxHeadingsPerTile];
    return headingsAt(tileX, tileY, bands, bandCount, scratch) > 0;
}

bool isCarriageway(int tileX, int tileY, const Band* bands, int bandCount) {
    if (bands == nullptr || tileX < 0 || tileY < 0) {
        return false;
    }
    for (int i = 0; i < bandCount; ++i) {
        const Band& band = bands[i];
        const bool horizontal = band.horizontal != 0;
        const int across = horizontal ? tileY - static_cast<int>(band.start)
                                      : tileX - static_cast<int>(band.start);
        const int along  = horizontal ? tileX : tileY;
        if (across < kNearLaneOffset || across > kFarLaneOffset) {
            continue;   // a kerb, or outside the band entirely
        }
        if (along >= static_cast<int>(band.first)
                && along <= static_cast<int>(band.last)) {
            return true;
        }
    }
    return false;
}

bool isCrossing(int tileX, int tileY, const Crossing* crossings, int count) {
    if (crossings == nullptr || tileX < 0 || tileY < 0) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const Crossing& strip = crossings[i];
        if (strip.length == 0) {
            continue;
        }
        const int along = (strip.horizontal != 0) ? tileX : tileY;
        const int across = (strip.horizontal != 0) ? tileY : tileX;
        const int start = (strip.horizontal != 0) ? strip.x : strip.y;
        const int fixed = (strip.horizontal != 0) ? strip.y : strip.x;
        // One tile thick: a three-tile strip is three tiles, not a square.
        if (across != fixed) {
            continue;
        }
        if (along >= start && along < start + static_cast<int>(strip.length)) {
            return true;
        }
    }
    return false;
}

}  // namespace top_down_city::lanes
