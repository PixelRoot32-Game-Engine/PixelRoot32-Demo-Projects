/*
 * Table.cpp - see Table.h.
 */
#include "pool/Table.h"

namespace pool {
namespace {

constexpr int32_t kMinMouthGapSqPx = 144;  // 12 px, the minimum pocket mouth.

/** Twice the polygon's signed area; sign encodes winding (see TableDef.h). */
int64_t shoelaceTwiceArea(const Polyline& polygon) {
    int64_t sum = 0;
    for (uint8_t i = 0, j = static_cast<uint8_t>(polygon.count - 1); i < polygon.count; j = i++) {
        sum += static_cast<int64_t>(polygon.points[j].x) * polygon.points[i].y -
               static_cast<int64_t>(polygon.points[i].x) * polygon.points[j].y;
    }
    return sum;
}

/** True if any consecutive pair of points (including the closing edge) repeats. */
bool hasZeroLengthEdge(const Polyline& polygon) {
    for (uint8_t i = 0, j = static_cast<uint8_t>(polygon.count - 1); i < polygon.count; j = i++) {
        if (polygon.points[i].x == polygon.points[j].x && polygon.points[i].y == polygon.points[j].y) {
            return true;
        }
    }
    return false;
}

/** Appends one polyline's edges to the table as Segments (capacity already checked). */
void appendSegments(const Polyline& polygon, Table& table) {
    for (uint8_t i = 0, j = static_cast<uint8_t>(polygon.count - 1); i < polygon.count; j = i++) {
        const PointPx& a = polygon.points[j];
        const PointPx& b = polygon.points[i];
        Segment& seg = table.segments[table.segmentCount++];
        seg.ax = static_cast<int32_t>(a.x) * kPxScale;
        seg.ay = static_cast<int32_t>(a.y) * kPxScale;
        seg.ex = static_cast<int16_t>(b.x - a.x);
        seg.ey = static_cast<int16_t>(b.y - a.y);
        seg.lenSqPx = static_cast<int32_t>(seg.ex) * seg.ex + static_cast<int32_t>(seg.ey) * seg.ey;
    }
}

/** True if a ball at (x, y) overlaps any cushion segment or vertex already loaded. */
bool ballOverlapsCushions(const Table& table, int32_t x, int32_t y) {
    for (uint8_t s = 0; s < table.segmentCount; ++s) {
        const Segment& seg = table.segments[s];
        if (segmentContact(seg, x, y, kBallRadiusRaw) || vertexContact(seg, x, y, kBallRadiusRaw)) {
            return true;
        }
    }
    return false;
}

/**
 * True if (x, y) lies inside any obstacle polygon. segmentContact()/
 * vertexContact() only reach kBallRadiusRaw from an edge or vertex, so a
 * ball resting deep inside an obstacle -- farther than the radius from
 * every edge and vertex -- needs this containment check instead, or
 * loadTable() would accept an impossible starting position.
 */
bool ballInsideObstacle(const TableDef& def, int32_t x, int32_t y) {
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        if (pointInPolygon(def.obstacles[o], x, y)) {
            return true;
        }
    }
    return false;
}

}  // namespace

TableError loadTable(const TableDef& def, Table& table) {
    // table is only meaningful on TableError::None (see Table.h); reset here
    // so a failed load never leaks a partial one.
    table = Table{};
    // 1. TooManyBalls
    const uint32_t ballTotal = 1u + def.targetCount;  // +1 for the cue ball.
    if (ballTotal > kMaxBalls) {
        return TableError::TooManyBalls;
    }
    // 2. BadBallNumber -- targets must already be listed 1..9 ascending (see
    // TableDef.h), so index order doubles as play order, no sort needed.
    for (uint8_t i = 0; i < def.targetCount; ++i) {
        if (def.targets[i].number != static_cast<uint8_t>(i + 1)) {
            return TableError::BadBallNumber;
        }
    }
    // 3. TooManySegments
    uint32_t segmentTotal = def.border.count;
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        segmentTotal += def.obstacles[o].count;
    }
    if (segmentTotal > kMaxSegments) {
        return TableError::TooManySegments;
    }
    // 4. TooManyPockets -- checked before any pocket is written so it never overruns Table::pockets.
    if (def.pocketCount > kMaxPockets) {
        return TableError::TooManyPockets;
    }
    // 5. ZeroLengthSegment
    if (hasZeroLengthEdge(def.border)) {
        return TableError::ZeroLengthSegment;
    }
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        if (hasZeroLengthEdge(def.obstacles[o])) {
            return TableError::ZeroLengthSegment;
        }
    }
    // 6. BadWinding -- border clockwise on screen (positive), obstacles
    // counter-clockwise (negative), so N = (-ey, ex) always points inward.
    if (shoelaceTwiceArea(def.border) <= 0) {
        return TableError::BadWinding;
    }
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        if (shoelaceTwiceArea(def.obstacles[o]) >= 0) {
            return TableError::BadWinding;
        }
    }
    // 7. BadMouthIndex
    for (uint8_t p = 0; p < def.pocketCount; ++p) {
        const PocketDef& pocket = def.pockets[p];
        if (pocket.mouthA >= def.border.count || pocket.mouthB >= def.border.count) {
            return TableError::BadMouthIndex;
        }
    }
    // 8. NarrowPocketMouth
    for (uint8_t p = 0; p < def.pocketCount; ++p) {
        const PocketDef& pocket = def.pockets[p];
        const PointPx& a = def.border.points[pocket.mouthA];
        const PointPx& b = def.border.points[pocket.mouthB];
        const int32_t dx = b.x - a.x;
        const int32_t dy = b.y - a.y;
        if (dx * dx + dy * dy < kMinMouthGapSqPx) {
            return TableError::NarrowPocketMouth;
        }
    }
    // The remaining checks need the runtime segment cache and ball list.
    appendSegments(def.border, table);
    for (uint8_t o = 0; o < def.obstacleCount; ++o) {
        appendSegments(def.obstacles[o], table);
    }
    table.balls[table.ballCount].x = static_cast<int32_t>(def.cue.x) * kPxScale;
    table.balls[table.ballCount].y = static_cast<int32_t>(def.cue.y) * kPxScale;
    table.balls[table.ballCount].number = 0;
    ++table.ballCount;
    for (uint8_t i = 0; i < def.targetCount; ++i) {
        BallStart& ball = table.balls[table.ballCount++];
        ball.x = static_cast<int32_t>(def.targets[i].pos.x) * kPxScale;
        ball.y = static_cast<int32_t>(def.targets[i].pos.y) * kPxScale;
        ball.number = def.targets[i].number;
    }
    // 9. BallOutsideTable -- crossing test against the border.
    for (uint8_t b = 0; b < table.ballCount; ++b) {
        if (!pointInPolygon(def.border, table.balls[b].x, table.balls[b].y)) {
            return TableError::BallOutsideTable;
        }
    }
    // 10. BallOverlapsBall
    for (uint8_t i = 0; i < table.ballCount; ++i) {
        for (uint8_t j = static_cast<uint8_t>(i + 1); j < table.ballCount; ++j) {
            const int64_t dx = table.balls[j].x - table.balls[i].x;
            const int64_t dy = table.balls[j].y - table.balls[i].y;
            if (dx * dx + dy * dy < static_cast<int64_t>(kBallContactRaw) * kBallContactRaw) {
                return TableError::BallOverlapsBall;
            }
        }
    }
    // 11. BallOverlapsCushion -- the same edge/vertex predicates physics uses
    // at runtime, plus obstacle containment for a ball resting inside an
    // obstacle's interior, away from any edge or vertex.
    for (uint8_t b = 0; b < table.ballCount; ++b) {
        if (ballOverlapsCushions(table, table.balls[b].x, table.balls[b].y) ||
            ballInsideObstacle(def, table.balls[b].x, table.balls[b].y)) {
            return TableError::BallOverlapsCushion;
        }
    }
    // 12. BallInPocket
    for (uint8_t p = 0; p < def.pocketCount; ++p) {
        const int32_t px = static_cast<int32_t>(def.pockets[p].center.x) * kPxScale;
        const int32_t py = static_cast<int32_t>(def.pockets[p].center.y) * kPxScale;
        table.pockets[table.pocketCount].x = px;
        table.pockets[table.pocketCount].y = py;
        ++table.pocketCount;
        for (uint8_t b = 0; b < table.ballCount; ++b) {
            const int64_t dx = table.balls[b].x - px;
            const int64_t dy = table.balls[b].y - py;
            if (dx * dx + dy * dy < static_cast<int64_t>(kCaptureRadiusRaw) * kCaptureRadiusRaw) {
                return TableError::BallInPocket;
            }
        }
    }
    return TableError::None;
}

}  // namespace pool
