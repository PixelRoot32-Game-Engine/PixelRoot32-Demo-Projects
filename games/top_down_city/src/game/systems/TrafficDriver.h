#pragma once
#include <cstdint>

#include <math/MathUtil.h>

#include "game/CityConstants.h"
#include "game/entities/VehicleActor.h"

namespace top_down_city {

/**
 * @brief The thing behind the wheel of a car nobody is sitting in.
 *
 * One function, deliberately stateless: a driver that remembered anything would
 * have to keep it on the car, and then every parked car carries memory it will
 * never use. Everything needed is already written down -- where the car is,
 * which way it points, and what the lane table says about its tile.
 *
 * The decision is re-made every step and only acted on when it can be: a turn
 * is refused unless the car is exactly on its tile, so asking early costs
 * nothing and asking late is impossible.
 *
 * `game/systems/` rather than `game/rules/` because it asks the collision layer
 * what is in front of the car, which reads the exported tilemaps. The
 * host-testable half is `game/rules/Lanes.h`.
 */
namespace traffic {

/// A box traffic gives way to on top of the usual obstacles: the player, on
/// foot, standing in the road. Width zero means there is nobody to yield to.
/// Cars stop rather than running them over, the cheaper half of a choice -- the
/// alternative needs damage, an invulnerability window and a reason the player
/// was in the road, and a car braking for somebody crossing reads as traffic
/// either way.
struct Yield {
    int left;
    int top;
    int width;
    int height;
};

/// Somewhere a police car is trying to get to, in world pixels. `active` is
/// false when nobody is being chased, which is almost always -- the star
/// counter has to reach two before wanted::patrolCars puts anybody on the
/// street to fill one in.
struct Chase {
    bool active;
    int  x;
    int  y;
};

/// What the driver would do with the wheel and the pedal this step.
struct Decision {
    std::uint8_t heading;   ///< city_scene::VehicleHeading
    bool         go;
};

/**
 * @brief Read the lane the car is on and decide where it goes next.
 *
 * Straight on where there is no choice; at a junction, straight on
 * `kTrafficStraightOddsIn - 1` times out of `kTrafficStraightOddsIn` and a turn
 * otherwise -- unless `chase` is active, in which case the exit that gets
 * closer wins and the dice are not rolled at all. That is the entire difference
 * between traffic and a police car: the same lane grammar, read with a
 * destination in mind.
 *
 * Options that leave the lane table are discarded first, and a car standing
 * somewhere the table does not cover -- where the player leaves one after
 * stealing it -- is told to hold position until the pool recycles the slot.
 */
Decision decide(const VehicleActor& car,
                pixelroot32::math::Random& rng,
                const Yield& yieldTo,
                const Chase& chase);

}  // namespace traffic
}  // namespace top_down_city
