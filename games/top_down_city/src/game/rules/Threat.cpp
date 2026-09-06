#include "game/rules/Threat.h"

namespace top_down_city::threat {

namespace {

int absInt(int v) {
    return v < 0 ? -v : v;
}

}  // namespace

Bearing awayFrom(int offsetX, int offsetY) {
    const int ax = absInt(offsetX);
    const int ay = absInt(offsetY);

    // >= rather than >, so an exact diagonal resolves horizontally. Either
    // axis would do; what matters is that one of them is always chosen.
    if (ax >= ay) {
        if (offsetX != 0) {
            return Bearing{static_cast<std::int8_t>(offsetX > 0 ? 1 : -1), 0};
        }
        // ax >= ay and offsetX == 0 means both are zero: the threat is on the
        // same pixel. Any direction beats standing on it.
        return Bearing{0, 1};
    }
    return Bearing{0, static_cast<std::int8_t>(offsetY > 0 ? 1 : -1)};
}

Bearing toward(int offsetX, int offsetY) {
    // Identical, deliberately -- see the header. Negating here is the bug the
    // tests exist to catch.
    return awayFrom(offsetX, offsetY);
}

bool hasLineOfFire(int offsetX, int offsetY, int tolerancePx) {
    const int ax = absInt(offsetX);
    const int ay = absInt(offsetY);
    // The bullet travels along the dominant axis, so the one that decides
    // whether it connects is the other one. Same >= tie-break as awayFrom, so
    // the axis considered here is always the axis actually fired along.
    const int minor = (ax >= ay) ? ay : ax;
    return minor <= tolerancePx;
}

Bearing intoLine(int offsetX, int offsetY, int tolerancePx) {
    if (hasLineOfFire(offsetX, offsetY, tolerancePx)) {
        return Bearing{0, 0};
    }
    const int ax = absInt(offsetX);
    const int ay = absInt(offsetY);
    // Same >= tie-break as hasLineOfFire, so the axis stepped along is always
    // the one that function was measuring. Getting these two out of step is a
    // shooter shuffling sideways along the line of fire on every diagonal.
    if (ax >= ay) {
        return Bearing{0, static_cast<std::int8_t>(offsetY > 0 ? 1 : -1)};
    }
    return Bearing{static_cast<std::int8_t>(offsetX > 0 ? 1 : -1), 0};
}

bool within(int offsetX, int offsetY, int radiusPx) {
    if (radiusPx < 0) {
        return false;
    }
    const int ax = absInt(offsetX);
    const int ay = absInt(offsetY);
    // Cheap rejection first. Most of the crowd is nowhere near most threats,
    // and this runs once per pedestrian per shot; it also keeps the squares
    // below well inside 32 bits for anything that gets past it.
    if (ax > radiusPx || ay > radiusPx) {
        return false;
    }
    return ax * ax + ay * ay <= radiusPx * radiusPx;
}

}  // namespace top_down_city::threat
