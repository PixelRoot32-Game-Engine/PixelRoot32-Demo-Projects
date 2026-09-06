#pragma once
#include <cstdint>

/**
 * @brief The grammar of the street grid: which tile is a lane, and which way
 *        traffic runs down it.
 *
 * A self-driving car asks two questions on every tile it enters: may I be
 * here, and where may I go next. Answered by a lookup over a handful of band
 * records rather than a per-tile direction map, because a 128x128 map would
 * be 16 KB of flash to say what fourteen numbers already say. The band layout
 * is the generator's, and the `static_assert`s in CityConstants.h keep the
 * two honest:
 *
 *     offset 0   sidewalk
 *     offset 1   lane
 *     offset 2   centre line
 *     offset 3   lane
 *     offset 4   sidewalk
 *
 * Engine-free like the rest of `game/rules/`: every mistake here produces a
 * car that keeps driving -- on the wrong side, into oncoming traffic, or
 * straight through a junction it should have turned at. None crash, and none
 * look like a bad table; they look like the traffic being stupid.
 */
namespace top_down_city::lanes {

/// Sidewalk, lane, centre line, lane, sidewalk.
constexpr int kBandWidthTiles = 5;
constexpr int kNearLaneOffset = 1;
constexpr int kFarLaneOffset  = 3;

/**
 * @brief One street band, clipped to the stretch that is really carriageway.
 *
 * The generator resolves bands over the whole island, paints some as gravel
 * where they cross the park and caps them in pavement where they end; what is
 * emitted here is the run in between. Tile indices, so a row is four bytes.
 */
struct Band {
    std::uint8_t start;       ///< Tile row (horizontal) or column (vertical).
    std::uint8_t first;       ///< First tile ALONG the band, inclusive.
    std::uint8_t last;        ///< Last tile along it, inclusive.
    std::uint8_t horizontal;  ///< 1 when the band runs east-west.
};

/// A direction of travel: one whole step along exactly one axis.
struct Heading {
    std::int8_t dx;
    std::int8_t dy;
};

/// Two bands can cross at a tile, and no more than two: one horizontal, one
/// vertical. A third would have to be parallel to one of them, and parallel
/// bands are 16 tiles apart.
constexpr int kMaxHeadingsPerTile = 2;

/**
 * @brief Every direction a car may leave this tile in.
 * @param out  Filled with up to `kMaxHeadingsPerTile` headings.
 * @return 0 when the tile is not a lane, 1 in a plain lane, 2 in a junction.
 *
 * The result never contains a heading and its opposite, so a car picking
 * freely from it can never be sent back the way it came -- geometry rather
 * than a rule enforced here, and the driver depends on it, so the tests pin
 * it down.
 */
int headingsAt(int tileX, int tileY, const Band* bands, int bandCount,
               Heading* out);

/// Is this tile carriageway a car may drive on? Agrees with `headingsAt`
/// returning anything at all, which the tests check exhaustively.
bool isLane(int tileX, int tileY, const Band* bands, int bandCount);

/**
 * @brief Is this tile road at all -- both lanes and the paint between them?
 *
 * Wider than `isLane`, and the difference is the point: the centre line is
 * not a lane because a car sitting on it is nose to nose with the oncoming
 * side, but it is the middle of the road and somebody standing on it is
 * standing in traffic. Cars ask `isLane`. People ask this.
 */
bool isCarriageway(int tileX, int tileY, const Band* bands, int bandCount);

/// One zebra crossing: a strip of tiles, one tile thick. The generator groups
/// the tiles it REALLY painted into runs; a table derived from where the
/// junctions should be would open a gap in the kerb rule wherever a road did
/// not actually reach that approach.
struct Crossing {
    std::uint8_t x;
    std::uint8_t y;
    std::uint8_t length;      ///< Tiles, along the strip. Never 0.
    std::uint8_t horizontal;  ///< 1 when the strip runs east-west.
};

/// Is this tile part of a marked crossing? This is the one place a pedestrian
/// may step off the kerb, so an off-by-one here is somebody walking into the
/// lane beside the zebra rather than onto it.
bool isCrossing(int tileX, int tileY, const Crossing* crossings, int count);

}  // namespace top_down_city::lanes
