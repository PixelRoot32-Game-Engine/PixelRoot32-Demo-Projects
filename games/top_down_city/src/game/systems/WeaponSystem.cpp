#include "game/systems/WeaponSystem.h"

#include "game/systems/CityCollision.h"

namespace top_down_city {

namespace gfx = pixelroot32::graphics;

// The projectile box lives with the tunnelling check that gives it its value.
using weapons::kProjectileBoxPx;

WeaponSystem::WeaponSystem()
    : spec_(nullptr),
      state_{0, 0},
      shots_(),
      hitFn_(nullptr),
      hitContext_(nullptr),
      flashX_(0),
      flashY_(0),
      flashSteps_(0),
      visualKey_(0) {
}

void WeaponSystem::reset() {
    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        shots_[i].alive = false;
    }
    // Unarmed, deliberately: the demo opens empty-handed with the first pistol
    // five tiles away, in view. WeaponPickups.h has why.
    spec_       = nullptr;
    state_      = weapons::State{0, 0};
    flashSteps_ = 0;
    refreshKey();
}

void WeaponSystem::disarm() {
    spec_  = nullptr;
    state_ = weapons::State{0, 0};
    dropProjectiles();
}

void WeaponSystem::dropProjectiles() {
    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        shots_[i].alive = false;
    }
    flashSteps_ = 0;
    refreshKey();
}

void WeaponSystem::setTargetSink(HitFn fn, void* context) {
    hitFn_      = fn;
    hitContext_ = context;
}

void WeaponSystem::equip(weapons::WeaponId id) {
    spec_  = &weapons::spec(id);
    state_ = weapons::load(*spec_);
}

bool WeaponSystem::fire(int centreX, int centreY, Facing facing) {
    if (!armed() || !weapons::canFire(state_)) {
        return false;
    }

    // The FIRST pellet decides whether the shot happens at all. A shotgun
    // that refused to fire because it could not have all three would be a gun
    // that jams for a reason nothing on screen explains; one that fires two
    // of three at a busy moment is a shotgun with a bad pattern, which reads
    // as the pool being full because it is.
    std::uint8_t slot = kMaxProjectiles;
    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        if (!shots_[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot == kMaxProjectiles) {
        // Every barrel busy. Deliberately NOT charging the cooldown: the
        // player pressed the button and nothing happened, so the gun owes
        // them the next step rather than a punishment for the pool being
        // full. At the shipped fire rate this cannot happen anyway.
        return false;
    }

    const weapons::Aim aim = weapons::aimOf(facing);
    const int muzzleX = centreX + aim.dx * kMuzzleOffsetPx;
    const int muzzleY = centreY + aim.dy * kMuzzleOffsetPx;
    // Perpendicular to the aim, which on a four-way aim is the other axis.
    const int spreadX = aim.dy;
    const int spreadY = aim.dx;

    for (std::uint8_t pellet = 0; pellet < spec_->pellets; ++pellet) {
        if (pellet > 0) {
            slot = kMaxProjectiles;
            for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
                if (!shots_[i].alive) {
                    slot = i;
                    break;
                }
            }
            if (slot == kMaxProjectiles) {
                break;      // fire what fits; see above
            }
        }
        const std::int32_t sideways =
            weapons::pelletOffsetSub(*spec_, pellet);

        Projectile& p = shots_[slot];
        p.x         = muzzleX << kSubPixelShift;
        p.y         = muzzleY << kSubPixelShift;
        p.stepX     = aim.dx * static_cast<std::int32_t>(
                                   spec_->projectileSpeedSub)
                    + spreadX * sideways;
        p.stepY     = aim.dy * static_cast<std::int32_t>(
                                   spec_->projectileSpeedSub)
                    + spreadY * sideways;
        p.stepsLeft = weapons::lifetimeSteps(*spec_);
        p.damage    = spec_->damage;
        p.tracerPx  = spec_->tracerLengthPx;
        p.alive     = true;
    }

    // One shell, however many pellets left the barrel.
    weapons::onFired(state_, *spec_);

    flashX_     = muzzleX;
    flashY_     = muzzleY;
    flashSteps_ = kMuzzleFlashSteps;

    refreshKey();
    return true;
}

bool WeaponSystem::advance(Projectile& p) {
    if (p.stepsLeft == 0) {
        return false;
    }
    --p.stepsLeft;

    p.x += p.stepX;
    p.y += p.stepY;

    // The box is centred on the position, not anchored at it: a bullet tested
    // as a point slips diagonally between two solid tiles that meet at a
    // corner, and does it silently.
    const int left = (p.x >> kSubPixelShift) - kProjectileBoxPx / 2;
    const int top  = (p.y >> kSubPixelShift) - kProjectileBoxPx / 2;

    // A person first, then the world. The order matters where somebody is
    // standing in a doorway: the shot should hit them rather than the frame
    // they are standing in.
    if (hitFn_ != nullptr
        && hitFn_(hitContext_, left, top, kProjectileBoxPx, kProjectileBoxPx,
                  p.damage)) {
        return false;
    }
    // Whole-tile, not per-pixel: the player's eroded per-pixel test exists so
    // a walker slides past a lamp post instead of snagging on it, which is
    // the opposite of what a bullet wants. A bullet that clipped the post
    // should stop at the post.
    if (!collision::boxIsFreeWholeTile(left, top, kProjectileBoxPx,
                                       kProjectileBoxPx)) {
        return false;
    }
    // Cars are actors and not tiles, so the tilemap does not know they are
    // there. Same call the movers make.
    if (collision::boxIsBlocked(left, top, kProjectileBoxPx,
                                kProjectileBoxPx)) {
        return false;
    }
    return true;
}

void WeaponSystem::step() {
    if (armed()) {
        weapons::tick(state_);
    }
    if (flashSteps_ > 0) {
        --flashSteps_;
    }

    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        if (!shots_[i].alive) {
            continue;
        }
        if (!advance(shots_[i])) {
            shots_[i].alive = false;
        }
    }
    refreshKey();
}

void WeaponSystem::draw(gfx::Renderer& renderer) {
    // Through the ink palette, which CityDayNight leaves untinted. That is
    // not an oversight: a muzzle flash is a light source, and a tracer that
    // dimmed with the evening would be a gun that gets harder to see itself
    // fire exactly when the city gets dark.
    if (flashSteps_ > 0) {
        renderer.drawFilledRectangle(flashX_ - kMuzzleFlashPx / 2,
                                     flashY_ - kMuzzleFlashPx / 2,
                                     kMuzzleFlashPx, kMuzzleFlashPx,
                                     hud::kTracer);
    }

    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        const Projectile& p = shots_[i];
        if (!p.alive) {
            continue;
        }
        const int px = p.x >> kSubPixelShift;
        const int py = p.y >> kSubPixelShift;
        // A streak along the axis of travel rather than a dot. A single pixel
        // moving six pixels a step is a dotted line the eye cannot follow;
        // the streak is what makes the shot read as a shot.
        if (p.stepX != 0) {
            const int x = p.stepX > 0 ? px - p.tracerPx : px;
            renderer.drawFilledRectangle(x, py, p.tracerPx, 1, hud::kTracer);
        } else {
            const int y = p.stepY > 0 ? py - p.tracerPx : py;
            renderer.drawFilledRectangle(px, y, 1, p.tracerPx, hud::kTracer);
        }
    }
}

void WeaponSystem::refreshKey() {
    // The flash and every live bullet, position included. Two frames with the
    // same key put the same pixels on the panel, which is the contract
    // CityScene::shouldRedrawFramebuffer() is built on.
    std::uint32_t key = static_cast<std::uint32_t>(flashSteps_);
    for (std::uint8_t i = 0; i < kMaxProjectiles; ++i) {
        key = (key << 3) | (key >> 29);
        if (!shots_[i].alive) {
            continue;
        }
        key ^= static_cast<std::uint32_t>(
                   ((shots_[i].x >> kSubPixelShift) & 0xFFFF) << 16)
             ^ static_cast<std::uint32_t>(
                   (shots_[i].y >> kSubPixelShift) & 0xFFFF);
    }
    visualKey_ = key;
}

}  // namespace top_down_city
