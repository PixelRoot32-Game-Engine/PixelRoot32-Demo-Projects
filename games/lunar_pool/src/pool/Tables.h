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
constexpr uint8_t kStageCount = 3;

/**
 * @brief Returns the TableDef for a 1-based stage number.
 *
 * Out-of-range stages clamp to the shipped range (below 1 gives table 1,
 * above kStageCount gives the last table), so game flow and the scene never
 * read past the shipped data.
 * @param stage 1-based stage number.
 */
const TableDef& tableForStage(uint8_t stage);

}  // namespace pool
