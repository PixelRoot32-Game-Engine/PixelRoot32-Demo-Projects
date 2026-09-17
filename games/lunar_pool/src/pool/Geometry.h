/*
 * Geometry.h - Segment/vertex contact predicates and row-scan fill spans.
 *
 * Shared by table load-time validation and, later, by physics' cushion
 * response and the scene's felt/obstacle rendering, so all three agree on
 * exactly what "touching a cushion" and "inside the table" mean.
 */
#pragma once

#include <cstdint>

#include "pool/TableDef.h"

namespace pool {

/** One border/obstacle edge, precomputed at load time for contact tests. */
struct Segment {
    int32_t ax = 0;
    int32_t ay = 0;
    int16_t ex = 0;
    int16_t ey = 0;
    int32_t lenSqPx = 0;
};

/**
 * @brief True if a circle overlaps this segment's interior span.
 *
 * The circle center's projection onto the edge must land between the edge's
 * two endpoints (the interior test); a circle beyond either endpoint is a
 * job for vertexContact() instead, not this function.
 *
 * @param seg Precomputed segment.
 * @param px, py Circle center, raw units (1/256 px).
 * @param radiusRaw Circle radius, raw units.
 */
[[nodiscard]] bool segmentContact(const Segment& seg, int32_t px, int32_t py, int32_t radiusRaw);

/**
 * @brief True if a circle overlaps the segment's start-point vertex.
 * @param seg Precomputed segment; only its start point (ax, ay) is used.
 * @param px, py Circle center, raw units.
 * @param radiusRaw Circle radius, raw units.
 */
[[nodiscard]] bool vertexContact(const Segment& seg, int32_t px, int32_t py, int32_t radiusRaw);

/**
 * @brief True if a point lies inside a closed polygon (even-odd rule).
 * @param polygon Closed polyline, whole pixels; the edge from the last point
 *   back to the first is implicit.
 * @param px, py Point to test, raw units.
 */
[[nodiscard]] bool pointInPolygon(const Polyline& polygon, int32_t px, int32_t py);

/**
 * @brief Computes one pixel row's horizontal fill spans inside a polygon.
 *
 * Even-odd crossings with a half-open rule on y, so a row that passes
 * exactly through a vertex is counted once, not twice or zero times.
 *
 * @param polygon Closed polyline, whole pixels.
 * @param y Row to scan, whole pixels.
 * @param xs Output buffer for span boundary x-values, ascending; pair them
 *   up as [xs[0],xs[1]), [xs[2],xs[3]), ... to fill the row.
 * @param maxXs Capacity of xs.
 * @return Number of values written to xs (always even for a closed polygon).
 */
[[nodiscard]] uint8_t rowSpans(const Polyline& polygon, int16_t y, int16_t* xs, uint8_t maxXs);

}  // namespace pool
