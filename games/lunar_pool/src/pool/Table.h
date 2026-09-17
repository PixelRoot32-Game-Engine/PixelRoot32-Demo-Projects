/*
 * Table.h - Runtime table representation and load-time validation.
 *
 * Turns an authoring-time TableDef (see TableDef.h) into the runtime form
 * the simulation and rendering use, and rejects any table data that would
 * put the simulation in an inconsistent state (overlapping balls, a ball
 * starting inside a pocket, a degenerate cushion, ...) before a single
 * frame runs, so a broken table is caught at load time, not mid-game.
 */
#pragma once

#include <cstdint>

#include "pool/Fixed.h"
#include "pool/Geometry.h"
#include "pool/TableDef.h"

namespace pool {

/** Runtime cap on cushion/obstacle segments a loaded table can hold. */
constexpr uint8_t kMaxSegments = 48;
/** Runtime cap on pockets a loaded table can hold (every shipped table uses 6). */
constexpr uint8_t kMaxPockets = 8;
/** Runtime cap on balls: index 0 is the cue, 1..9 are targets. */
constexpr uint8_t kMaxBalls = 10;

/**
 * @brief Every way loadTable() can reject a TableDef, in check order.
 *
 * loadTable() returns the FIRST failure it finds, checked in this fixed
 * order, so a table with several problems always reports the same one
 * until that one is fixed.
 */
enum class TableError : uint8_t {
    None = 0,
    TooManyBalls,
    BadBallNumber,
    TooManySegments,
    TooManyPockets,
    ZeroLengthSegment,
    BadWinding,
    BadMouthIndex,
    NarrowPocketMouth,
    BallOutsideTable,
    BallOverlapsBall,
    BallOverlapsCushion,
    BallInPocket,
};

/** One pocket's capture center after loading, raw units (1/256 px). */
struct Pocket {
    int32_t x = 0;
    int32_t y = 0;
};

/**
 * @brief One ball's starting position and, for a target, its fixed number.
 *
 * Populated straight from the TableDef; a future World-owning task reads
 * these to place its balls, so loadTable() has no World dependency.
 */
struct BallStart {
    int32_t x = 0;
    int32_t y = 0;
    uint8_t number = 0;  // 0 = cue ball.
};

/** Runtime form of a loaded table: cushion segments, pockets, ball starts. */
struct Table {
    Segment segments[kMaxSegments]{};
    uint8_t segmentCount = 0;
    Pocket pockets[kMaxPockets]{};
    uint8_t pocketCount = 0;
    BallStart balls[kMaxBalls]{};
    uint8_t ballCount = 0;
};

/**
 * @brief Validates a TableDef and, on success, fills the runtime Table.
 *
 * Checks run in the fixed order documented on TableError and stop at the
 * first failure; table is left partially written on failure, so callers
 * must not use it unless the return value is TableError::None.
 *
 * @param def Authoring-time table description.
 * @param table Output; only meaningful when the return value is None.
 * @return The first validation failure found, or TableError::None.
 */
[[nodiscard]] TableError loadTable(const TableDef& def, Table& table);

}  // namespace pool
