#include "game/systems/StationStaff.h"

#include "game/rules/Threat.h"

#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

StationStaff::StationStaff()
    : pool_(),
      staffKey_(0),
      markMask_(0),
      deployed_(false) {
}

void StationStaff::deployOnce() {
    if (deployed_) {
        return;
    }
    deploy();
}

void StationStaff::deploy() {
    static_assert(station::NUM_OFFICERS <= 8,
                  "markMask_ is one byte, so a fourth-and-more officer would "
                  "be un-markable in silence -- the chapter would point at a "
                  "door and never end");
    // Set here rather than by the caller, the half of this that is easy to
    // leave out: chapter 2 deploys from the city and the player may never have
    // walked into the room, so a `deployOnce` still holding false would fire on
    // the way in and clear the mark. See deployOnce().
    deployed_ = true;
    pool_.reset();
    // The mark goes with the bodies: a per-slot fact must not outlive the
    // person the slot held. `deploy` runs again when chapter 2 is taken, and a
    // mark that survived it would name whoever the pool places there next.
    markMask_ = 0;

    for (std::uint8_t i = 0; i < station::NUM_OFFICERS; ++i) {
        const station::OfficerPost& post = station::OFFICERS[i];
        // A distinct seed per officer, derived from the post rather than from
        // a counter: three people spawned in the same step off one stream
        // wander in lockstep, and a station where everybody turns left at the
        // same moment is worse than a station where nobody moves.
        const std::uint32_t seed =
            0x9E3779B9u ^ (static_cast<std::uint32_t>(post.tileX) << 16)
                        ^ (static_cast<std::uint32_t>(post.tileY) << 8)
                        ^ static_cast<std::uint32_t>(i + 1);
        pool_.acquire(post.tileX * kTilePx, post.tileY * kTilePx,
                      kPoliceTint, seed);
    }
    refreshKey();
}


void StationStaff::mark(std::uint8_t officer) {
    if (officer >= station::NUM_OFFICERS) {
        // Nobody. See the static_assert in CityConstants.h, which is what
        // keeps this branch unreachable rather than merely unlikely: reached,
        // it is a chapter that can never be completed and says nothing.
        return;
    }
    // Post index IS slot index, and that is an argument rather than a hope:
    // `deploy` resets the pool and acquires the posts in order, acquire takes
    // the lowest free slot, and nobody in this room is ever released -- so slot
    // i holds post i for the life of the deployment. Which is also why the mark
    // is cleared by `deploy` and set after it: the identity holds only between
    // one reset and the next.
    markMask_ = static_cast<std::uint8_t>(1u << officer);
}

bool StationStaff::markIsDown() {
    if (markMask_ == 0) {
        return false;
    }
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        if (i >= 8 || (markMask_ & (1u << i)) == 0) {
            continue;
        }
        return pool_.at(i)->isSquashed();
    }
    // Marked, and the slot is not live. Unreachable while nobody is released
    // from this pool -- and false rather than true if that ever changes,
    // because "the mark is gone" must not read as "the mark was shot".
    return false;
}

static bool boxesOverlap(int aL, int aT, int aW, int aH,
                         int bL, int bT, int bW, int bH) {
    return aL < bL + bW && bL < aL + aW
        && aT < bT + bH && bT < aT + aH;
}

void StationStaff::step() {
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        pool_.at(i)->step();
    }
    refreshKey();
}

void StationStaff::draw(gfx::Renderer& renderer) {
    // No viewport cull and no draw order to keep. The room is one screen, so
    // everybody in it is on it, and nothing indoors can be run over -- which
    // is the only reason the street crowd needs two passes.
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        pool_.at(i)->draw(renderer);
    }
}

void StationStaff::refreshKey() {
    std::uint32_t key = 0;
    for (std::uint16_t i = pool_.nextLive(0); i != Pool::kEnd;
         i = pool_.nextLive(static_cast<std::uint16_t>(i + 1))) {
        // Rotate before mixing, so two officers swapping positions is not the
        // same key as neither of them moving.
        key = (key << 7) | (key >> 25);
        key ^= pool_.at(i)->visualKey();
    }
    staffKey_ = key;
}

PersonHit StationStaff::hitBox(int left, int top, int width, int height,
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
        const bool officer = person->isOfficer();
        const bool killed = person->hit(damage);
        refreshKey();
        return PersonHit{true, killed, officer};
    }
    return PersonHit{false, false, false};
}

void StationStaff::hunt(int targetX, int targetY, int rangePx,
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
        const int dx = targetX - person->centreX();
        const int dy = targetY - person->centreY();
        any = person->pursue(dx, dy, rangePx, kOfficerWeaponRangePx,
                             chaseSpeedSub) || any;
    }
    if (any) {
        refreshKey();
    }
}

bool StationStaff::anyoneSees(int targetX, int targetY, int rangePx) {
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

void StationStaff::startleNear(int worldX, int worldY, int radiusPx) {
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
}

void StationStaff::alarmNear(int worldX, int worldY, int radiusPx) {
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

bool StationStaff::returnFire(int targetX, int targetY, WeaponSystem& gun) {
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
