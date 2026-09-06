#pragma once
#include <cstdint>

namespace top_down_city {

/**
 * @brief The city's collision model, shared by everything that moves.
 *
 * Player, cars and pedestrians read the same exported behaviour layers, so the
 * answers live here instead of in three copies.
 *
 * Two static tests, because the movers do not need the same precision:
 * `boxIsFreePerPixel` decodes the Items tile bitmap and blocks only where the
 * silhouette is, eroded so a thin lamp post does not snag a walker -- the
 * player's test, and the expensive one. `boxIsFreeWholeTile` blocks any cell
 * flagged solid: a car is nearly as wide as its cell, and a dozen pedestrians
 * running the per-pixel loop every 16 ms would not fit the ESP32's budget.
 *
 * Plus a DYNAMIC test, because cars are actors and nothing in the exported
 * scene knows where they are, and a space switch, because an interior is a
 * second tilemap of a different size over the same tileset pools. Both live
 * here rather than in a scene, so no mover has to be told which building it
 * is in.
 */
namespace collision {

/// Which tilemap the static tests read. An enum rather than a handle, and it
/// stayed one when the second interior arrived: a room is a VALUE here plus a
/// SpaceData row in the .cpp, which is the whole mechanism. Order matters in
/// exactly one place -- `isInterior` is a comparison rather than a switch, so
/// City must stay first and every room after it.
enum class Space : std::uint8_t {
    City = 0,
    PoliceStation,
    CornerShop,
    Count
};

/// Is the player in a room rather than out in the city? Asked by the sight
/// test, which reads both layers indoors and only Items outdoors, and by the
/// crowd, which does not exist in either room.
bool isInterior(Space space);

/// Switch the world the static tests read. Everything that moves sees the
/// change on its next step; nothing is copied and nothing is reset.
void setSpace(Space space);

Space currentSpace();

/// The bounds of the current space, in world pixels. The camera needs them,
/// and they are the same numbers `insideWorld` clamps to -- asked for here
/// rather than derived twice.
int spaceWidthPx();
int spaceHeightPx();

/// Solid-tile test with per-pixel Items decoding. Coordinates are the
/// collision box in world pixels, not the sprite cell.
bool boxIsFreePerPixel(int left, int top, int width, int height);

/// Solid-tile test at cell granularity: cheap, and correct for anything as
/// large as the tile it stands on.
bool boxIsFreeWholeTile(int left, int top, int width, int height);

/**
 * A THIRD test, not a synonym for the other two: what stops a bullet or a
 * bonnet is not what stops a look. Water is solid to a walker and transparent
 * to an eye, so counting it as cover would let the player shake a five-star
 * chase by standing in the sea; a building blocks both. Hence the Items layer
 * alone outdoors, both layers indoors, because the room's WALLS live in
 * Background. Off the map reads as opaque, so a ray sampling past the edge
 * cannot fold onto a real tile.
 *
 * Tile coordinates, not pixels -- this is what game/rules/Sight.h asks.
 */
bool tileBlocksSight(int tileX, int tileY);

/**
 * `game/rules/Sight.h` with this city's tilemap plugged into it. The rule is
 * engine-free and takes the map as a callback; this is the one place that
 * callback is written, so the three pools that need the answer ask a question
 * instead of assembling a ray each.
 */
bool hasLineOfSight(int fromX, int fromY, int toX, int toY);

/**
 * Signature of the moving-obstacle test the scene installs: true when the box
 * overlaps an obstacle. `context` is whatever was handed to setDynamicBlocker;
 * `ignore` is an obstacle to skip, or nullptr -- what lets a driver climb out
 * of the very car that would otherwise block every tile beside it.
 */
using BlockerFn = bool (*)(const void* context, int left, int top,
                           int width, int height, const void* ignore);

/// Install (or, with a null function, remove) the moving-obstacle test.
void setDynamicBlocker(BlockerFn fn, const void* context);

/// True when the box overlaps a moving obstacle. Always false until the
/// scene installs one, which is what keeps this header usable on its own.
bool boxIsBlocked(int left, int top, int width, int height,
                  const void* ignore = nullptr);

}  // namespace collision
}  // namespace top_down_city
