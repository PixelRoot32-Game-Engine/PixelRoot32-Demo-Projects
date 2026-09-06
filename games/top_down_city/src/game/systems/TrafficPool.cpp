#include "game/systems/TrafficPool.h"

#include "game/rules/AudioCues.h"
#include "game/rules/Lanes.h"
#include "game/rules/Threat.h"
#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

namespace {

/// The heading the lane at this tile runs, or false when it is not a lane.
bool laneHeadingAt(int tileX, int tileY, std::uint8_t& heading) {
    lanes::Heading out[lanes::kMaxHeadingsPerTile];
    const int count = lanes::headingsAt(tileX, tileY,
                                        scene::LANE_BANDS,
                                        scene::NUM_LANE_BANDS, out);
    if (count == 0) {
        return false;
    }
    // A junction tile offers two; take the first. Which one hardly matters --
    // the car is about to be asked again on the very next step, and it will
    // pick properly then.
    const lanes::Heading& h = out[0];
    if (h.dy < 0)      heading = scene::kHeadingN;
    else if (h.dx > 0) heading = scene::kHeadingE;
    else if (h.dy > 0) heading = scene::kHeadingS;
    else               heading = scene::kHeadingW;
    return true;
}

/// Would a car fit here, facing this way, with nothing already in it?
bool spotIsFree(int spriteX, int spriteY, std::uint8_t heading) {
    const VehicleBox box = VehicleActor::boxFor(spriteX, spriteY, heading);
    if (!collision::boxIsFreeWholeTile(box.left, box.top,
                                       box.width, box.height)) {
        return false;
    }
    // The clearance, not a plain overlap: a car dropped one pixel behind a
    // parked one is a car that brakes on its first step and never moves.
    return !collision::boxIsBlocked(box.left - kTrafficSpawnClearPx,
                                    box.top - kTrafficSpawnClearPx,
                                    box.width + 2 * kTrafficSpawnClearPx,
                                    box.height + 2 * kTrafficSpawnClearPx);
}

}  // namespace

TrafficPool::TrafficPool()
    : pool_(),
      rng_(0x7A11C0DEu),
      trafficKey_(0),
      policeMask_(0),
      audibleMask_(0),
      stalledSteps_{} {
    static_assert(kTrafficPoolSize <= 8,
                  "the police set is an eight-bit mask");
}

void TrafficPool::reset(std::uint32_t seed) {
    pool_.reset();
    rng_ = pr32::math::Random(seed);
    trafficKey_ = 0;
    policeMask_ = 0;
    audibleMask_ = 0;
    for (std::uint16_t i = 0; i < kTrafficPoolSize; ++i) {
        stalledSteps_[i] = 0;
    }
}

bool TrafficPool::isOnScreen(int spriteX, int spriteY,
                             int cameraX, int cameraY, int margin) {
    return spriteX + kVehicleSpriteW > cameraX - margin &&
           spriteX < cameraX + DISPLAY_WIDTH + margin &&
           spriteY + kVehicleSpriteH > cameraY - margin &&
           spriteY < cameraY + DISPLAY_HEIGHT + margin;
}

bool TrafficPool::trySpawn(int cameraX, int cameraY, bool police) {
    if (pool_.isFull() || scene::NUM_LANE_BANDS == 0) {
        return false;
    }

    for (int attempt = 0; attempt < kTrafficSpawnAttempts; ++attempt) {
        // Sample the lane table, not the map. Most of a city is roof and
        // water, so picking a pixel and asking whether it is a lane fails
        // almost always; picking a band and a distance down it starts from
        // somewhere already legal and only has to check the surroundings.
        const lanes::Band& band =
            scene::LANE_BANDS[rng_.rand_int(0, scene::NUM_LANE_BANDS - 1)];
        const int along = rng_.rand_int(band.first, band.last);
        const int across = static_cast<int>(band.start)
                         + (rng_.rand_int(0, 1) == 0 ? lanes::kNearLaneOffset
                                                     : lanes::kFarLaneOffset);
        const int tileX = (band.horizontal != 0) ? along : across;
        const int tileY = (band.horizontal != 0) ? across : along;

        std::uint8_t heading = scene::kHeadingN;
        if (!laneHeadingAt(tileX, tileY, heading)) {
            continue;
        }

        const int px = tileX * kTilePx;
        const int py = tileY * kTilePx;

        // Inside the ring, and never in view: a car materialising in the
        // middle of the street is the one artefact that gives streamed
        // traffic away, and a car is sixteen pixels of it.
        if (!isOnScreen(px, py, cameraX, cameraY, kTrafficSpawnMarginPx)) {
            continue;
        }
        if (isOnScreen(px, py, cameraX, cameraY, 0)) {
            continue;
        }
        if (!spotIsFree(px, py, heading)) {
            continue;
        }

        VehicleActor* car = pool_.acquire();
        if (car == nullptr) {
            return false;
        }
        // A police car is spawned in the livery, not painted into it: the
        // colour is the same column the ordinary spread already draws from,
        // and the difference the player sees is that this one turns after
        // them at the junction.
        car->spawnAt(tileX, tileY, heading,
                     police ? kPoliceCarColor
                            : static_cast<std::uint8_t>(
                                  rng_.rand_int(0, kVehicleColorCount - 1)));
        const std::uint16_t slot = pool_.indexOf(car);
        if (slot != Pool::kEnd && slot < 8) {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << slot);
            policeMask_ = police
                ? static_cast<std::uint8_t>(policeMask_ | bit)
                : static_cast<std::uint8_t>(policeMask_ & ~bit);
        }
        return true;
    }
    return false;
}

void TrafficPool::prime(int cameraX, int cameraY) {
    for (std::uint16_t i = 0; i < kTrafficPoolSize; ++i) {
        // Never police on the first fill. The scene primes at zero stars, and
        // a patrol car waiting at the spawn for somebody who has done nothing
        // is the game accusing the player before they have moved.
        trySpawn(cameraX, cameraY, false);
    }
    refreshKey();
}

TrafficNoise TrafficPool::step(int cameraX, int cameraY,
                               const VehicleActor* driven,
                               const traffic::Yield& yieldTo,
                               const traffic::Chase& chase,
                               std::uint8_t maxPolice) {
    TrafficNoise noise{};
    // The listener is the screen centre -- the camera is hard-centred, so
    // this is the same point CityScene's own cameraOriginX/Y arithmetic
    // targets, just derived here rather than threaded through as a third
    // parameter (ADR-15 §17.1).
    const int listenerX = cameraX + DISPLAY_WIDTH / 2;
    const int listenerY = cameraY + DISPLAY_HEIGHT / 2;

    std::uint8_t policeLive = 0;
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        VehicleActor* car = pool_.at(i);
        if (car == driven) {
            // The player is at the wheel. Not steered, and above all not
            // recycled: releasing the slot under them would delete the thing
            // the camera is following.
            continue;
        }
        if (!isOnScreen(car->spriteX(), car->spriteY(),
                        cameraX, cameraY, kTrafficDespawnMarginPx)) {
            pool_.releaseAt(i);
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << (i & 7u));
            policeMask_ = static_cast<std::uint8_t>(policeMask_ & ~bit);
            // A released slot's next occupant must not inherit "already
            // audible" or "already stalled" from whoever was just here --
            // the same reasoning that clears policeMask_ above.
            audibleMask_ = static_cast<std::uint8_t>(audibleMask_ & ~bit);
            if (i < kTrafficPoolSize) {
                stalledSteps_[i] = 0;
            }
            continue;
        }
        const bool police = i < 8
            && (policeMask_ & (1u << i)) != 0
            && chase.active;
        if (police) {
            ++policeLive;
        }
        const traffic::Chase mine =
            police ? chase : traffic::Chase{false, 0, 0};
        const traffic::Decision decision =
            traffic::decide(*car, rng_, yieldTo, mine);
        car->driveAutonomous(decision.heading, decision.go);

        // Audibility edge, stall clock and horn -- ADR-15. i < 8 mirrors the
        // police mask's own guard; static_assert(kTrafficPoolSize <= 8, ...)
        // in the constructor already makes it unconditionally true, kept for
        // the same defensive-symmetry reason that guard is.
        if (i < 8 && i < kTrafficPoolSize) {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
            const int dx = listenerX - car->centreX();
            const int dy = listenerY - car->centreY();
            const bool nowAudible =
                threat::within(dx, dy, audio_cues::kTrafficAudiblePx);
            const bool wasAudible = (audibleMask_ & bit) != 0;
            if (nowAudible && !wasAudible) {
                noise.passedBy = true;
                // Colour parity, not a new roll: "simple engine variations"
                // (ADR-15 §17.2), picked from data the pool already knows.
                noise.passHigh = (car->color() & 1u) != 0;
            }
            audibleMask_ = nowAudible
                ? static_cast<std::uint8_t>(audibleMask_ | bit)
                : static_cast<std::uint8_t>(audibleMask_ & ~bit);

            stalledSteps_[i] =
                audio_cues::ageStallClock(stalledSteps_[i], decision.go);
            // Gated on audibility: a car wedged across the island must not
            // honk in the player's ear.
            if (nowAudible && audio_cues::hornDue(stalledSteps_[i])) {
                noise.honked = true;
            }
        }
    }

    // One admission per step, like the crowd: six slots refill in a tenth of a
    // second and the spawn test is the most expensive thing in this file.
    // Police only as far as the star level has earned, never all six -- six
    // cars converging on a grid where every route shortens the distance is a
    // closing box the player cannot see the sides of rather than a chase, and
    // this pool is the ordinary traffic too, which would stop existing.
    // wanted::patrolCars keeps the count strictly under the pool size and a
    // host test holds it there.
    trySpawn(cameraX, cameraY, chase.active && policeLive < maxPolice);
    refreshKey();
    return noise;
}

bool TrafficPool::anyoneSees(int targetX, int targetY, int rangePx) {
    if (rangePx <= 0) {
        return false;
    }
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        if (i >= 8 || (policeMask_ & (1u << i)) == 0) {
            continue;
        }
        VehicleActor* car = pool_.at(i);
        const int dx = targetX - car->centreX();
        const int dy = targetY - car->centreY();
        if (!threat::within(dx, dy, rangePx)) {
            continue;
        }
        if (collision::hasLineOfSight(car->centreX(), car->centreY(),
                                      targetX, targetY)) {
            return true;
        }
    }
    return false;
}

void TrafficPool::draw(gfx::Renderer& renderer, int cameraX, int cameraY) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        VehicleActor* car = pool_.at(i);
        if (isOnScreen(car->spriteX(), car->spriteY(), cameraX, cameraY, 0)) {
            car->draw(renderer);
        }
    }
}

VehicleActor* TrafficPool::inReachOf(int left, int top, int width, int height,
                                     int range) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        VehicleActor* car = pool_.at(i);
        if (car->isWithinRangeOf(left, top, width, height, range)) {
            return car;
        }
    }
    return nullptr;
}

bool TrafficPool::blocks(int left, int top, int width, int height,
                         const void* ignore) const {
    // The engine's ObjectPool has a const nextLive() and a non-const at(),
    // so a read-only sweep needs this one cast. Nothing below writes.
    Pool& pool = const_cast<Pool&>(pool_);
    for (std::uint16_t i = pool.nextLive(0); i != Pool::kEnd;
         i = pool.nextLive(static_cast<std::uint16_t>(i + 1))) {
        const VehicleActor* car = pool.at(i);
        if (car == ignore) {
            continue;
        }
        if (car->overlapsBox(left, top, width, height)) {
            return true;
        }
    }
    return false;
}

void TrafficPool::refreshKey() {
    // Order-independent, like the crowd's: a slot freed in the middle shifts
    // nothing, so the key only changes when what is on screen does.
    std::uint32_t key = pool_.size();
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        key += pool_.at(i)->visualKey();
        key ^= key << 5;
    }
    trafficKey_ = key;
}

}  // namespace top_down_city
