/*
 * Tables.h - The demo's shipped table data.
 *
 * Each table is authoring-time TableDef data (see TableDef.h). tableForStage()
 * is the single place game flow and presentation read a stage's table from,
 * so adding a table later touches only this file, never loadTable() or the
 * scene.
 */
#pragma once

#include <cstdint>

#include "pool/TableDef.h"

namespace pool {

/** Number of stages currently shipped. Grows as more tables are added. */
constexpr uint8_t kStageCount = 1;

/**
 * @brief Returns the TableDef for a 1-based stage number.
 *
 * Only table 1 ships so far, so every stage returns it; stage becomes
 * meaningful once table 2 and 3 exist. Safe to call with any value in the
 * meantime -- there is only one table to hand back.
 * @param stage 1-based stage number.
 */
const TableDef& tableForStage(uint8_t stage);

}  // namespace pool
