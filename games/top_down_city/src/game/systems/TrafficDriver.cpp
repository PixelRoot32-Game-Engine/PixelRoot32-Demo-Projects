#include "game/systems/TrafficDriver.h"

#include "game/rules/Lanes.h"
#include "game/systems/CityCollision.h"

namespace top_down_city {
namespace traffic {

namespace {

// The generator's heading order, and the assert is what keeps this file from
// silently disagreeing with it.
static_assert(scene::kHeadingN == 0 && scene::kHeadingE == 1
              && scene::kHeadingS == 2 && scene::kHeadingW == 3,
              "the heading table below is in the generator's order");
constexpr lanes::Heading kHeadingDelta[4] = {
    { 0, -1},   // N
    { 1,  0},   // E
    { 0,  1},   // S
    {-1,  0},   // W
};

std::uint8_t headingIndexOf(const lanes::Heading& h) {
    if (h.dy < 0) return scene::kHeadingN;
    if (h.dx > 0) return scene::kHeadingE;
    if (h.dy > 0) return scene::kHeadingS;
    return scene::kHeadingW;
}

bool boxesOverlap(int aL, int aT, int aW, int aH,
                  int bL, int bT, int bW, int bH) {
    if (aW <= 0 || bW <= 0) {
        return false;
    }
    return aL < bL + bW && bL < aL + aW
        && aT < bT + bH && bT < aT + aH;
}

/// Is the strip immediately in front of the car clear?
bool roadAhead(const VehicleActor& car, int dx, int dy, const Yield& yieldTo) {
    // The car's own box, pushed forward. It still overlaps the tile the car
    // stands on, which is free by definition -- so at whole-tile granularity
    // this asks about the cell in front and nothing else.
    const int left = car.boxLeft() + dx * kTrafficLookaheadPx;
    const int top  = car.boxTop()  + dy * kTrafficLookaheadPx;
    const int width  = car.boxWidth();
    const int height = car.boxHeight();

    if (!collision::boxIsFreeWholeTile(left, top, width, height)) {
        return false;
    }
    if (collision::boxIsBlocked(left, top, width, height, &car)) {
        return false;
    }
    return !boxesOverlap(left, top, width, height,
                         yieldTo.left, yieldTo.top,
                         yieldTo.width, yieldTo.height);
}

}  // namespace

Decision decide(const VehicleActor& car,
                pixelroot32::math::Random& rng,
                const Yield& yieldTo,
                const Chase& chase) {
    const int tileX = car.tileX();
    const int tileY = car.tileY();

    lanes::Heading options[lanes::kMaxHeadingsPerTile];
    const int count = lanes::headingsAt(tileX, tileY,
                                        scene::LANE_BANDS,
                                        scene::NUM_LANE_BANDS,
                                        options);
    if (count == 0) {
        // Off the network. The player parks a stolen car on a pavement and
        // walks off; this is that car, and standing still is the only honest
        // thing it can do -- there is no lane to rejoin from here.
        return Decision{car.heading(), false};
    }

    // Discard whatever leads nowhere BEFORE choosing, not after. A car that
    // picks a dead end and then discovers it has to stop is a car parked
    // across the end of a street; one that never picks it turns at the last
    // junction like everybody else.
    lanes::Heading open[lanes::kMaxHeadingsPerTile];
    int openCount = 0;
    for (int i = 0; i < count; ++i) {
        if (lanes::isLane(tileX + options[i].dx, tileY + options[i].dy,
                          scene::LANE_BANDS, scene::NUM_LANE_BANDS)) {
            open[openCount++] = options[i];
        }
    }
    if (openCount == 0) {
        // Every way out of this tile leaves the network: the last tile of a
        // clipped band, at the shore or the edge of the park. Hold, and let
        // the pool recycle the slot once the player has moved on.
        return Decision{car.heading(), false};
    }

    lanes::Heading chosen = open[0];
    if (openCount > 1 && chase.active) {
        // A police car. No dice: of the exits this junction offers, take the
        // one that ends up nearer. On a grid where every street meets every
        // other that is a route rather than a heuristic, and it costs two
        // subtractions instead of a pathfinder. One tile of lookahead is enough
        // because the decision is remade at the next junction, and a chase that
        // re-plans every block is what a chase looks like from the outside.
        long best = -1;
        for (int i = 0; i < openCount; ++i) {
            const long dx = (tileX + open[i].dx) * kTilePx + kTilePx / 2
                          - chase.x;
            const long dy = (tileY + open[i].dy) * kTilePx + kTilePx / 2
                          - chase.y;
            const long distance = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
            if (best < 0 || distance < best) {
                best = distance;
                chosen = open[i];
            }
        }
    } else if (openCount > 1) {
        // Ordinary traffic. Carrying straight on is the common case on
        // purpose: traffic that takes every turn it is offered never travels
        // anywhere and reads as a fairground ride rather than a city.
        int straight = -1;
        for (int i = 0; i < openCount; ++i) {
            if (headingIndexOf(open[i]) == car.heading()) {
                straight = i;
            }
        }
        const bool keepGoing =
            straight >= 0 && rng.rand_int(0, kTrafficStraightOddsIn - 1) != 0;
        chosen = keepGoing ? open[straight]
                           : open[rng.rand_int(0, openCount - 1)];
    }

    std::uint8_t heading = headingIndexOf(chosen);
    int dx = chosen.dx;
    int dy = chosen.dy;

    // Look along the heading the car will really have this step, not the one
    // it asked for. A turn only lands on a whole tile and only if the box
    // fits, so between tiles the car is still going the way it was -- and
    // braking for the obstacle around a corner it has not turned yet is how
    // traffic ends up stopping in the middle of a junction.
    if (heading != car.heading()
            && !(car.isTileAligned() && car.canFace(heading))) {
        heading = car.heading();
        // From the heading itself, not from the lane options: a car the
        // player abandoned facing across a street has a heading the table
        // does not list, and looking ahead along somebody else's lane would
        // have it brake for whatever is beside it.
        dx = kHeadingDelta[heading & 3].dx;
        dy = kHeadingDelta[heading & 3].dy;
    }

    return Decision{heading, roadAhead(car, dx, dy, yieldTo)};
}

}  // namespace traffic
}  // namespace top_down_city
