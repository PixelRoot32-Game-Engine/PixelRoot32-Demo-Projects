#pragma once
#include <cstdint>

/**
 * Whether one point in the city can see another -- what holds the star counter
 * up, in place of the timer it used to be. A rule with no owner: a foot
 * officer, a patrol car and the station's duty staff all ask it about the same
 * city, and `SolidFn` is the entire coupling, so this compiles without the
 * engine like the rest of `game/rules/`.
 *
 * Every way it can be wrong is invisible on screen: always "clear" is the old
 * timer with extra steps, always "blocked" is a wanted level with no exit, and
 * an asymmetric ray is worse than both -- right most of the time, so the player
 * keeps trying to learn a rule that does not exist.
 */
namespace top_down_city::sight {

/**
 * @brief The caller's tilemap, as one question. `context` is handed back
 *        untouched from isClear.
 * @param tileX,tileY  MAY be off the map -- a ray between two points inside it
 *                     can still sample a neighbour, and the caller decides what
 *                     the edge of the world is made of.
 */
using SolidFn = bool (*)(const void* context, int tileX, int tileY);

/**
 * @brief Is there a clear line between two world pixels?
 *
 * Sampled rather than swept, in half-tile steps -- the coarsest interval that
 * cannot step over a one-tile wall, the thinnest thing the city is built from.
 *
 * **Both endpoints are excluded, on purpose.** An officer's collision box is
 * smaller than its tile, so one in a doorway or against a corner often stands
 * in a solid tile; blinding them there would blind the police exactly where the
 * player is easiest to corner. Excluding the far end closes the sharper abuse
 * -- shedding a wanted level by stepping into a doorway in plain view of the
 * officer chasing you.
 *
 * Facing is deliberately NOT modelled: a four-way facing that changes every few
 * steps would make being seen depend on which frame an officer was drawn in,
 * unreadable off a 16x16 sprite. "Get a building between us" is actionable.
 *
 * @param tileSizePx  Passed in rather than assumed, so the tests can use a
 *                    grid they can draw on paper.
 * @return true when nothing solid lies strictly between the two points.
 */
bool isClear(int fromX, int fromY, int toX, int toY,
             int tileSizePx, SolidFn solid, const void* context);

}  // namespace top_down_city::sight
