#pragma once
#include <cstdint>

#include <gameplay/ObjectPool.h>
#include <graphics/Renderer.h>
#include <math/MathUtil.h>

#include "game/CityConstants.h"
#include "game/entities/VehicleActor.h"
#include "game/systems/TrafficDriver.h"

namespace top_down_city {

/// What the traffic did within earshot this step (ADR-15, ADR-18): a value
/// TrafficPool::step returns rather than a cue it plays -- the same shape as
/// PersonHit, for the same reason. This pool must not learn what an
/// AudioDirector is.
struct TrafficNoise {
    bool passedBy = false;   ///< a car crossed INTO earshot this step
    bool passHigh = false;   ///< which of the two pass variants (colour parity)
    bool honked   = false;   ///< an audible car has been wedged long enough
};

/**
 * @brief The cars that are going somewhere: streamed around the camera and
 *        driven by nobody.
 *
 * The parked cars are a fixed array because a parking space does not move.
 * Traffic is the opposite: simulating a plausible number over 128x128 tiles
 * would cost more than the rest of the demo and the player would see six. So
 * six slots, filled from the lane just outside the viewport and emptied once a
 * car is far enough behind -- the same shape as PedestrianPool, whose header
 * explains the ObjectPool storage and the reset-after-Scene::init() contract.
 *
 * What differs is where a spawn may land: on a lane, pointing the way that lane
 * runs, or the car is driving the wrong way up a one-way street from its first
 * frame.
 */
class TrafficPool {
public:
    using Pool = pixelroot32::gameplay::ObjectPool<VehicleActor,
                                                   kTrafficPoolSize>;
    static constexpr std::uint16_t kEnd = Pool::kEnd;

    TrafficPool();

    /// Empty every slot and reseed the spawn noise. Call from the scene's
    /// init(), after Scene::init().
    void reset(std::uint32_t seed);

    /// Fill the visible streets for the first frame. Unlike the crowd's
    /// prime this still refuses to spawn on screen: a car appearing out of
    /// nothing is far more obvious than a pedestrian, and the streets fill
    /// from the edges within a second anyway.
    void prime(int cameraX, int cameraY);

    /**
     * @brief One fixed logic step: retire, admit, and drive.
     * @param driven   The car the player is sitting in, or nullptr. Never
     *                 recycled and never steered -- the pool would otherwise
     *                 fight the player for the wheel, or delete the vehicle
     *                 the camera is following.
     * @param yieldTo  A box the traffic brakes for: the player, on foot.
     * @param chase    Where a police car should be heading. Inactive almost
     *                 always; see wanted::patrolCars.
     * @param maxPolice How many of this pool's slots the force may hold at
     *                 once. Zero for most of a session. A hard-coded one
     *                 flattened the top half of the star ladder: three, four
     *                 and five stars all fielded the same single car.
     * @return What the traffic sounded like this step -- see TrafficNoise.
     */
    TrafficNoise step(int cameraX, int cameraY, const VehicleActor* driven,
                      const traffic::Yield& yieldTo,
                      const traffic::Chase& chase, std::uint8_t maxPolice);

    /// Does any patrol car have the target in reach AND a clear line to it?
    /// The same question the crowd answers, for the same reason: a siren two
    /// streets away with a block between you is not somebody watching you.
    bool anyoneSees(int targetX, int targetY, int rangePx);

    void draw(pixelroot32::graphics::Renderer& renderer,
              int cameraX, int cameraY);

    /// The first traffic car within `range` of this box, or nullptr. This is
    /// what lets the player steal one out of the flow, which is the whole
    /// reason traffic is made of the same actor as everything else.
    VehicleActor* inReachOf(int left, int top, int width, int height,
                            int range);

    /// True when the box overlaps a traffic car other than `ignore`. Part of
    /// the scene's dynamic blocker, so traffic is solid to the player, to the
    /// parked cars, and to itself.
    bool blocks(int left, int top, int width, int height,
                const void* ignore) const;

    /// Fingerprint of every car on screen, for the scene's frame skip.
    /// Cached for the same reason the crowd's is: the scene asks from a const
    /// method and the pool's iteration is not.
    std::uint32_t visualKey() const { return trafficKey_; }

    std::uint16_t liveCount() const { return pool_.size(); }

    /// Ascending iteration over the live cars, for the scene: the crowd has
    /// to be frightened by them and run over by them, and neither belongs in
    /// here.
    std::uint16_t firstLive() const { return pool_.nextLive(0); }
    std::uint16_t nextLive(std::uint16_t after) const {
        return pool_.nextLive(static_cast<std::uint16_t>(after + 1));
    }
    VehicleActor* at(std::uint16_t index) { return pool_.at(index); }

private:
    bool trySpawn(int cameraX, int cameraY, bool police);
    void refreshKey();
    static bool isOnScreen(int spriteX, int spriteY, int cameraX, int cameraY,
                           int margin);

    Pool                      pool_;
    pixelroot32::math::Random rng_;
    std::uint32_t             trafficKey_;
    /// One bit per slot: set means this car drives to the chase point
    /// instead of rolling at junctions. A mask rather than a flag on the
    /// actor, because being police is a property of the ROLE the pool handed
    /// the slot: steal the car and the bit stays with the slot, which is
    /// right -- the livery is a paint job and the driver is what left.
    std::uint8_t              policeMask_;
    /// One bit per slot: set means this car was within `kTrafficAudiblePx`
    /// of the listener as of the LAST step -- the previous reading a rising
    /// edge is measured against (ADR-15 §17.1). Same shape as policeMask_,
    /// covered by the same `static_assert(kTrafficPoolSize <= 8, ...)`.
    std::uint8_t              audibleMask_;
    /// Logic steps this slot's car has been blocked
    /// (`traffic::Decision::go == false`), one byte per slot -- see
    /// `audio_cues::ageStallClock`/`hornDue` (ADR-15). 6 bytes total at
    /// `kTrafficPoolSize == 6`.
    std::uint8_t              stalledSteps_[kTrafficPoolSize];
};

}  // namespace top_down_city
