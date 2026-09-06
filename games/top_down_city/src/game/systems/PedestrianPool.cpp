#include "game/systems/PedestrianPool.h"

#include "game/rules/Threat.h"

#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

namespace {

/// Somewhere a pedestrian could stand: free ground, not inside a car, and
/// not in the middle of a lane.
bool spotIsFree(int spriteX, int spriteY) {
    const int left = spriteX + kPlayerBoxOffsetX;
    const int top  = spriteY + kPlayerBoxOffsetY;
    if (!collision::boxIsFreeWholeTile(left, top,
                                       kPlayerBoxWidth, kPlayerBoxHeight)) {
        return false;
    }
    // The same rule that stops them walking into the road stops them being
    // put there. Without this the kerb discipline would look like a lottery:
    // most of the crowd on the pavement, and whoever happened to spawn on
    // the asphalt standing in it until a car arrived.
    if (!PedestrianActor::isPavement(spriteX, spriteY)) {
        return false;
    }
    return !collision::boxIsBlocked(left, top,
                                    kPlayerBoxWidth, kPlayerBoxHeight);
}

}  // namespace

PedestrianPool::PedestrianPool()
    : pool_(),
      rng_(0x5EED1234u),
      crowdKey_(0),
      officerChanceIn_(kOfficerSpawnChanceIn) {
}

void PedestrianPool::setOfficerChanceIn(std::uint8_t chanceIn) {
    // Clamped rather than trusted: this feeds a rand_int(0, N - 1), and an N
    // of zero is an empty range. wanted::officerChanceIn already guarantees
    // it is at least one, and this is the second lock on the same door.
    officerChanceIn_ = chanceIn > 0 ? chanceIn : 1;
}

void PedestrianPool::reset(std::uint32_t seed) {
    pool_.reset();
    rng_ = pr32::math::Random(seed);
    crowdKey_ = 0;
    officerChanceIn_ = kOfficerSpawnChanceIn;
}

bool PedestrianPool::isOnScreen(int spriteX, int spriteY,
                                int cameraX, int cameraY, int margin) {
    return spriteX + kPlayerSpriteW > cameraX - margin &&
           spriteX < cameraX + DISPLAY_WIDTH + margin &&
           spriteY + kPlayerSpriteH > cameraY - margin &&
           spriteY < cameraY + DISPLAY_HEIGHT + margin;
}

bool PedestrianPool::trySpawn(int cameraX, int cameraY, bool allowOnScreen) {
    if (pool_.isFull()) {
        return false;
    }

    // rand_int rejection-samples, so the ring is sampled without the modulo
    // bias that would otherwise crowd its left and top edges.
    const int minX = cameraX - kPedSpawnMarginPx;
    const int maxX = cameraX + DISPLAY_WIDTH + kPedSpawnMarginPx;
    const int minY = cameraY - kPedSpawnMarginPx;
    const int maxY = cameraY + DISPLAY_HEIGHT + kPedSpawnMarginPx;

    // Rejection sampling, bounded: most of the city is roof, water or hedge,
    // so a spawn attempt usually fails and the honest fix is to give up for
    // this step rather than to search harder. The crowd fills in over the
    // next few frames instead, which nobody can see happening.
    for (int attempt = 0; attempt < kPedSpawnAttempts; ++attempt) {
        const int px = rng_.rand_int(minX, maxX);
        const int py = rng_.rand_int(minY, maxY);

        // Nobody may appear in view: a pedestrian materialising twenty pixels
        // from the player is the one artefact that gives a streamed crowd
        // away.
        if (!allowOnScreen && isOnScreen(px, py, cameraX, cameraY, 0)) {
            continue;
        }
        if (allowOnScreen) {
            // The first fill may use the visible street, but not the middle
            // of it: that is where the player is standing, and a pedestrian
            // constructed on top of them would walk out through their chest.
            const int dx = px - (cameraX + DISPLAY_WIDTH / 2);
            const int dy = py - (cameraY + DISPLAY_HEIGHT / 2);
            if (dx * dx + dy * dy < kPedPrimeClearPx * kPedPrimeClearPx) {
                continue;
            }
        }
        if (!spotIsFree(px, py)) {
            continue;
        }

        // The beat. One in kOfficerSpawnChanceIn is police, and this is a
        // separate roll on purpose -- kPoliceTint is deliberately NOT one of
        // the tints the line below picks from, so the random crowd can never
        // put somebody in a uniform by accident.
        const bool officer =
            rng_.rand_int(0, officerChanceIn_ - 1) == 0;
        const std::uint8_t tint =
            officer ? kPoliceTint
                    : static_cast<std::uint8_t>(
                          rng_.rand_int(0, kPedestrianTintCount - 1));
        pool_.acquire(px, py, tint, rng_.next(), officer);
        return true;
    }
    return false;
}

void PedestrianPool::prime(int cameraX, int cameraY) {
    // Fill what is visible at scene start. The cap is the pool itself; a
    // couple of failures just leave the street quieter for a second.
    for (std::uint16_t i = 0; i < kPedestrianPoolSize; ++i) {
        trySpawn(cameraX, cameraY, true);
    }
    refreshKey();
}

void PedestrianPool::step(int cameraX, int cameraY) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* pedestrian = pool_.at(i);

        const bool gone = !isOnScreen(pedestrian->spriteX(),
                                      pedestrian->spriteY(),
                                      cameraX, cameraY, kPedDespawnMarginPx);
        if (gone || pedestrian->hasFaded()) {
            pool_.releaseAt(i);
            continue;
        }
        pedestrian->step();
    }

    // One admission per step at most: twelve slots refill in a fifth of a
    // second, and the spawn test is the most expensive thing in this file.
    trySpawn(cameraX, cameraY, false);
    refreshKey();
}


static bool boxesOverlap(int aL, int aT, int aW, int aH,
                         int bL, int bT, int bW, int bH) {
    return aL < bL + bW && bL < aL + aW
        && aT < bT + bH && bT < aT + aH;
}

PersonHit PedestrianPool::runOverBy(const VehicleActor& car) {
    PersonHit hit{false, false, false};
    if (!car.isLethal()) {
        return hit;
    }
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* pedestrian = pool_.at(i);
        if (pedestrian->isSquashed()) {
            continue;
        }
        if (car.overlapsBox(pedestrian->boxLeft(), pedestrian->boxTop(),
                            PedestrianActor::boxWidth(),
                            PedestrianActor::boxHeight())) {
            // Accumulated rather than returned early: a car at 156 px/s can
            // take two people out in one step, and the caller wants the worse
            // of them rather than the first.
            hit.any = true;
            hit.killed = true;
            hit.officer = hit.officer || pedestrian->isOfficer();
            pedestrian->squash();
        }
    }
    if (hit.any) {
        refreshKey();
    }
    return hit;
}

void PedestrianPool::drawGround(gfx::Renderer& renderer,
                                int cameraX, int cameraY) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* pedestrian = pool_.at(i);
        if (!pedestrian->isSquashed()) {
            continue;
        }
        if (isOnScreen(pedestrian->spriteX(), pedestrian->spriteY(),
                       cameraX, cameraY, 0)) {
            pedestrian->draw(renderer);
        }
    }
}

void PedestrianPool::drawWalking(gfx::Renderer& renderer,
                                 int cameraX, int cameraY) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* pedestrian = pool_.at(i);
        if (pedestrian->isSquashed()) {
            continue;
        }
        if (isOnScreen(pedestrian->spriteX(), pedestrian->spriteY(),
                       cameraX, cameraY, 0)) {
            pedestrian->draw(renderer);
        }
    }
}

void PedestrianPool::refreshKey() {
    // Order-independent on purpose: a slot freed in the middle of the pool
    // shifts nothing, so the key only changes when what is ON SCREEN does.
    std::uint32_t key = pool_.size();
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        key += pool_.at(i)->visualKey();
        key ^= key << 7;
    }
    crowdKey_ = key;
}

PersonHit PedestrianPool::hitBox(int left, int top, int width, int height,
                                 std::uint8_t damage) {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (person->isSquashed()) {
            // A body is scenery. Letting it stop rounds would put an
            // invisible shield wherever somebody had already fallen.
            continue;
        }
        if (!boxesOverlap(left, top, width, height,
                          person->boxLeft(), person->boxTop(),
                          PedestrianActor::boxWidth(),
                          PedestrianActor::boxHeight())) {
            continue;
        }
        // Read the uniform BEFORE the hit: a squashed officer is still an
        // officer, but nothing about the actor after it goes down is worth
        // depending on for something the city bills the player for.
        const bool officer = person->isOfficer();
        const bool killed = person->hit(damage);
        refreshKey();
        return PersonHit{true, killed, officer};
    }
    return PersonHit{false, false, false};
}

void PedestrianPool::hunt(int targetX, int targetY, int rangePx,
                          std::int32_t chaseSpeedSub) {
    if (rangePx <= 0) {
        return;
    }
    bool any = false;
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (!person->isOfficer() || person->isSquashed()) {
            continue;
        }
        // FROM the officer TO the player, which is the convention takeAim and
        // pursue share -- and the opposite of alarmNear's, because that one is
        // telling people which way to run.
        const int dx = targetX - person->centreX();
        const int dy = targetY - person->centreY();
        any = person->pursue(dx, dy, rangePx, kOfficerWeaponRangePx,
                             chaseSpeedSub) || any;
    }
    if (any) {
        refreshKey();
    }
}

bool PedestrianPool::anyoneSees(int targetX, int targetY, int rangePx) {
    if (rangePx <= 0) {
        return false;
    }
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (!person->isOfficer() || person->isSquashed()) {
            continue;
        }
        const int dx = targetX - person->centreX();
        const int dy = targetY - person->centreY();
        // Range first: it is two multiplies, and the ray is half a dozen
        // tile lookups. Almost nobody in the crowd is in range.
        if (!threat::within(dx, dy, rangePx)) {
            continue;
        }
        if (collision::hasLineOfSight(person->centreX(), person->centreY(),
                                      targetX, targetY)) {
            return true;
        }
    }
    return false;
}

bool PedestrianPool::startleNear(int worldX, int worldY, int radiusPx) {
    bool any = false;
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (person->isSquashed()) {
            continue;
        }
        // Centre to centre, and the offset runs FROM the threat TO them,
        // which is the direction they need to keep going.
        const int dx = person->centreX() - worldX;
        const int dy = person->centreY() - worldY;
        if (!threat::within(dx, dy, radiusPx)) {
            continue;
        }
        person->startle(dx, dy);
        any = true;
    }
    if (any) {
        refreshKey();
    }
    return any;
}

void PedestrianPool::alarmNear(int worldX, int worldY, int radiusPx) {
    bool any = false;
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (person->isSquashed()) {
            continue;
        }
        const int dx = person->centreX() - worldX;
        const int dy = person->centreY() - worldY;
        if (!threat::within(dx, dy, radiusPx)) {
            continue;
        }
        person->alarm(dx, dy);
        any = true;
    }
    if (any) {
        refreshKey();
    }
}

bool PedestrianPool::returnFire(int targetX, int targetY, WeaponSystem& gun) {
    if (!gun.ready()) {
        // One shared weapon means one shared cooldown, which is also the rate
        // limit on the whole force: three officers cannot put out three times
        // the fire. A feature at this scale -- a demo where standing near four
        // police is instant death is not one anybody plays twice.
        return false;
    }
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        PedestrianActor* person = pool_.at(i);
        if (!person->isOfficer() || !person->isAlert() || person->isSquashed()) {
            continue;
        }
        const int dx = targetX - person->centreX();
        const int dy = targetY - person->centreY();
        if (!person->takeAim(dx, dy, gun.spec().rangePx)) {
            continue;
        }
        if (gun.fire(person->centreX(), person->centreY(), person->facing())) {
            refreshKey();   // takeAim turned them; the frame must show it
            return true;    // the shared cooldown is spent
        }
    }
    refreshKey();
    return false;
}

}  // namespace top_down_city
