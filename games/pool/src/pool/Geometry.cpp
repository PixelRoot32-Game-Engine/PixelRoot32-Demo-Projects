/*
 * Geometry.cpp - see Geometry.h.
 */
#include "pool/Geometry.h"

#include "pool/Fixed.h"

namespace pool {

bool segmentContact(const Segment& seg, int32_t px, int32_t py, int32_t radiusRaw) {
    const int64_t dx = px - seg.ax;
    const int64_t dy = py - seg.ay;

    // Interior test: the circle's projection onto the edge must land inside
    // the edge's span, not past either endpoint -- a ball beyond a
    // segment's end belongs to vertexContact() instead.
    const int64_t dot = dx * seg.ex + dy * seg.ey;
    const int64_t interiorBound = static_cast<int64_t>(seg.lenSqPx) * kPxScale;
    if (dot < 0 || dot > interiorBound) {
        return false;
    }

    // Perpendicular-distance test with no square root: N = (-ey, ex) points
    // into the play area, and squaring both sides of "distance < radius"
    // keeps every intermediate an exact integer.
    const int64_t sdNum = dx * (-seg.ey) + dy * seg.ex;
    if (sdNum < 0) {
        return false;
    }
    return sdNum * sdNum < static_cast<int64_t>(radiusRaw) * radiusRaw * seg.lenSqPx;
}

bool vertexContact(const Segment& seg, int32_t px, int32_t py, int32_t radiusRaw) {
    const int64_t dx = px - seg.ax;
    const int64_t dy = py - seg.ay;
    return dx * dx + dy * dy < static_cast<int64_t>(radiusRaw) * radiusRaw;
}

bool pointInPolygon(const Polyline& polygon, int32_t px, int32_t py) {
    bool inside = false;
    const uint8_t n = polygon.count;
    for (uint8_t i = 0, j = static_cast<uint8_t>(n - 1); i < n; j = i++) {
        const int32_t yi = static_cast<int32_t>(polygon.points[i].y) * kPxScale;
        const int32_t yj = static_cast<int32_t>(polygon.points[j].y) * kPxScale;
        // Half-open on y: a vertex exactly on the test row belongs to the
        // edge above it, not the edge below, so the ray is never double- or
        // zero-counted at a vertex.
        if ((yi <= py) != (yj <= py)) {
            const int32_t xi = static_cast<int32_t>(polygon.points[i].x) * kPxScale;
            const int32_t xj = static_cast<int32_t>(polygon.points[j].x) * kPxScale;
            const int64_t crossX = xi + static_cast<int64_t>(py - yi) * (xj - xi) / (yj - yi);
            if (px < crossX) {
                inside = !inside;
            }
        }
    }
    return inside;
}

uint8_t rowSpans(const Polyline& polygon, int16_t y, int16_t* xs, uint8_t maxXs) {
    uint8_t count = 0;
    const uint8_t n = polygon.count;
    for (uint8_t i = 0, j = static_cast<uint8_t>(n - 1); i < n; j = i++) {
        const int16_t yi = polygon.points[i].y;
        const int16_t yj = polygon.points[j].y;
        if ((yi <= y) != (yj <= y)) {
            const int16_t xi = polygon.points[i].x;
            const int16_t xj = polygon.points[j].x;
            const int32_t crossX = xi + static_cast<int32_t>(y - yi) * (xj - xi) / (yj - yi);
            if (count < maxXs) {
                xs[count++] = static_cast<int16_t>(crossX);
            }
        }
    }
    // Insertion sort: a table row crosses only a handful of edges, so this
    // beats pulling in a generic sort for a handful of comparisons.
    for (uint8_t i = 1; i < count; ++i) {
        const int16_t key = xs[i];
        uint8_t k = i;
        while (k > 0 && xs[k - 1] > key) {
            xs[k] = xs[k - 1];
            --k;
        }
        xs[k] = key;
    }
    return count;
}

}  // namespace pool
