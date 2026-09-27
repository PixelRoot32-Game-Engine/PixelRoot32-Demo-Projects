/*
 * TableDef.h - Authoring-time table data structures for the pool physics core.
 *
 * These describe a table as flash-resident, hand-written data (see
 * src/pool/Tables.cpp once it exists): plain points and counts, with no
 * pointers into runtime state. loadTable() (see Table.h) turns one of these
 * into the runtime form the simulation actually uses.
 */
#pragma once

#include <cstdint>

namespace pool {

/** A point in whole logical pixels, screen coordinates (y grows downward). */
struct PointPx {
    int16_t x;
    int16_t y;
};

/**
 * @brief A closed sequence of points describing one border or obstacle outline.
 *
 * The polyline is implicitly closed: an edge runs from points[count-1] back
 * to points[0], so the data never repeats the first point at the end.
 * Winding direction sets which side is the inside of the play area (see
 * Table.h's loadTable() validation order).
 */
struct Polyline {
    const PointPx* points;
    uint8_t count;
};

/**
 * @brief One pocket: its capture center plus the two border-polyline vertex
 * indices that bound its mouth (the gap a ball falls through).
 */
struct PocketDef {
    PointPx center;
    uint8_t mouthA;
    uint8_t mouthB;
};

/** One target ball's fixed number (1-9) and starting position. */
struct TargetDef {
    uint8_t number;
    PointPx pos;
};

/** Authoring-time description of one table: border, obstacles, pockets, cue and targets. */
struct TableDef {
    Polyline border;
    const Polyline* obstacles;
    uint8_t obstacleCount;
    const PocketDef* pockets;
    uint8_t pocketCount;
    PointPx cue;
    const TargetDef* targets;
    uint8_t targetCount;
};

}  // namespace pool
