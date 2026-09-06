#pragma once
#include <cstdint>

/**
 * @brief Turning a row of radar cells into the rectangles that draw it.
 *
 * Engine-free like DayNight.h, for the same reason: this part of the overlay
 * can be wrong without LOOKING wrong, so `pio test -e host_test` covers it.
 *
 * The radar used to emit one 1x1 filled rectangle per cell -- 1874 a frame,
 * each paying `isDrawable`, a palette resolve, two virtual dispatches and a
 * dirty-region mark to write one pixel. The city's 16-tile blocks make roads
 * long and buildings solid, so coalescing each row into maximal runs of equal
 * colour draws the IDENTICAL picture, not an approximation: measured over the
 * generated map at 400 sample positions, the two-tone street map collapses
 * about 1625 cells into about 162 runs -- just over 10x, under four runs a
 * row. The pass gets cheaper the more legible the map is, so simplifying the
 * overlay never has to be paid for here.
 */
namespace top_down_city::minimap {

/// A cell with no swatch: outside the island. The radar window is centred on
/// the player and never clamped to the map, so its rows can run past the
/// coast; those cells are skipped rather than painted, and the backing plate
/// showing through is how the overlay draws the sea. 0xFF and not 0 because 0
/// is a legitimate swatch -- `MINIMAP_SWATCH_ITEMS` uses it for "this tile
/// has no item, ask the background layer".
constexpr std::uint8_t kAbsent = 0xFF;

/// One horizontal stretch of a single colour, in cells from the row's left.
struct Run {
    int          start;
    int          length;
    std::uint8_t swatch;
};

/**
 * @brief Decompose one row of the radar into maximal runs of equal swatch.
 *
 * @param swatches One entry per cell, `kAbsent` for cells outside the world.
 * @param count    Cells in the row. Zero is valid and emits nothing.
 * @param out      Receives the runs. Must hold at least `count` entries: a
 *                 chequered row really is one run per cell, and the caller
 *                 does not get to assume the city is tidy.
 * @return How many runs were written.
 *
 * A `kAbsent` cell always ends the run in progress and never starts one, so
 * two stretches of the same colour on either side of a gap stay two runs.
 * Merging them would paint over the coast.
 */
int rowRuns(const std::uint8_t* swatches, int count, Run* out);

}  // namespace top_down_city::minimap
