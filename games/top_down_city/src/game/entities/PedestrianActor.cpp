#include "game/entities/PedestrianActor.h"

#include "game/rules/Lanes.h"
#include "game/rules/Threat.h"

#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;
namespace core = pr32::core;
namespace math = pr32::math;

// GENERIC rather than ACTOR: see the note in PlayerActor.cpp -- the tag is
// the engine's CollisionSystem marker, and nothing here registers with it.
PedestrianActor::PedestrianActor(int px, int py, std::uint8_t tint,
                                 std::uint32_t seed, bool officer)
    : core::Entity(math::Vector2(px, py), kPlayerSpriteW, kPlayerSpriteH,
                   core::EntityType::GENERIC),
      x_(static_cast<std::int32_t>(px) << kSubPixelShift),
      y_(static_cast<std::int32_t>(py) << kSubPixelShift),
      rng_(seed),
      animTravelSub_(0),
      holdSteps_(0),
      lingerSteps_(kPedSquashLingerSteps),
      panicSteps_(0),
      alertSteps_(0),
      chaseSpeedSub_(kOfficerChaseSpeedSub),
      stuckSteps_(0),
      dirX_(0),
      dirY_(0),
      facing_(Facing::Down),
      tint_(tint),
      health_(kPersonHealth),
      animPhase_(0),
      squashed_(false),
      officer_(officer) {
    setRenderLayer(1);
    chooseDirection();
}

void PedestrianActor::chooseDirection(bool allowIdle) {
    // One roll in four is a pause. Without it every pedestrian is always
    // walking, and a pavement of people who never stop reads as a conveyor.
    holdSteps_ = rng_.rand_int(kPedMinHoldSteps, kPedMaxHoldSteps);

    const std::uint32_t roll = rng_.next();
    if (allowIdle && (roll & 3u) == 0u) {
        dirX_ = 0;
        dirY_ = 0;
        return;
    }

    // Four directions, matching the three facings the art has (left is the
    // right-facing frames mirrored).
    switch ((roll >> 2) & 3u) {
        case 0: dirX_ =  0; dirY_ = -1; facing_ = Facing::Up;    break;
        case 1: dirX_ =  1; dirY_ =  0; facing_ = Facing::Right; break;
        case 2: dirX_ =  0; dirY_ =  1; facing_ = Facing::Down;  break;
        default: dirX_ = -1; dirY_ = 0; facing_ = Facing::Left;  break;
    }
}

bool PedestrianActor::canOccupy(int px, int py) const {
    const int left = px + kPlayerBoxOffsetX;
    const int top  = py + kPlayerBoxOffsetY;
    if (!collision::boxIsFreeWholeTile(left, top, boxWidth(), boxHeight())) {
        return false;
    }
    // Cars are solid to people on foot -- including the parked ones, which is
    // what stops a pedestrian from strolling through a bonnet.
    return !collision::boxIsBlocked(left, top, boxWidth(), boxHeight());
}

bool PedestrianActor::isPavement(int spriteX, int spriteY) {
    if (collision::currentSpace() != collision::Space::City) {
        return true;      // indoors; see isPavement's contract in the header
    }
    const int tileX = (spriteX + kPlayerSpriteW / 2) / kTilePx;
    const int tileY = (spriteY + kPlayerSpriteH / 2) / kTilePx;
    if (!lanes::isCarriageway(tileX, tileY, scene::LANE_BANDS,
                              scene::NUM_LANE_BANDS)) {
        return true;
    }
    return lanes::isCrossing(tileX, tileY, scene::CROSSINGS,
                             scene::NUM_CROSSINGS);
}

bool PedestrianActor::respectsKerb(int fromX, int fromY, int toX, int toY) {
    if (isPavement(toX, toY)) {
        return true;
    }
    // Already in the road, so this is a step out of it or along it -- both
    // allowed; see respectsKerb's contract in the header.
    return !isPavement(fromX, fromY);
}

void PedestrianActor::moveAxisX(std::int32_t delta) {
    const std::int32_t candidate = x_ + delta;
    const int fromX = x_ >> kSubPixelShift;
    const int toX   = candidate >> kSubPixelShift;
    const int py    = y_ >> kSubPixelShift;
    // Panic first, and short-circuiting: somebody running from gunfire does
    // not look for the zebra, and the kerb test is two table scans nobody
    // needs to pay for while they are.
    const bool allowed = canOccupy(toX, py)
        && (panicSteps_ > 0 || isChasing() || stuckSteps_ >= kPedKerbGiveUpSteps
            || respectsKerb(fromX, py, toX, py));
    if (allowed) {
        x_ = candidate;
    } else {
        // Blocked: turn now rather than press into the wall for the rest of
        // the hold. This is the whole of the pedestrian's pathfinding, and it
        // is also what turns the kerb rule into behaviour -- somebody who
        // will not step into the road picks another direction on the spot,
        // which reads as walking along the pavement.
        holdSteps_ = 0;
    }
}

void PedestrianActor::moveAxisY(std::int32_t delta) {
    const std::int32_t candidate = y_ + delta;
    const int fromY = y_ >> kSubPixelShift;
    const int toY   = candidate >> kSubPixelShift;
    const int px    = x_ >> kSubPixelShift;
    const bool allowed = canOccupy(px, toY)
        && (panicSteps_ > 0 || isChasing() || stuckSteps_ >= kPedKerbGiveUpSteps
            || respectsKerb(px, fromY, px, toY));
    if (allowed) {
        y_ = candidate;
    } else {
        holdSteps_ = 0;
    }
}

void PedestrianActor::startle(int offsetX, int offsetY) {
    if (squashed_) {
        return;      // a body does not flinch
    }
    const threat::Bearing away = threat::awayFrom(offsetX, offsetY);
    dirX_ = away.dx;
    dirY_ = away.dy;
    if (dirX_ != 0) {
        facing_ = dirX_ > 0 ? Facing::Right : Facing::Left;
    } else {
        facing_ = dirY_ > 0 ? Facing::Down : Facing::Up;
    }
    // Re-armed, not accumulated: a burst of fire keeps the street running
    // without locking somebody into a direction they cannot escape.
    panicSteps_ = kPedPanicSteps;
    // The escape is held for as long as the fright lasts, so the ordinary
    // wander does not roll a new direction on top of it. A block still clears
    // this (see moveAxis*), which is what lets a cornered pedestrian pick a new
    // way out instead of pressing into the wall.
    holdSteps_ = kPedPanicSteps;
}

void PedestrianActor::alarm(int offsetX, int offsetY) {
    if (squashed_) {
        return;
    }
    if (!officer_) {
        startle(offsetX, offsetY);
        return;
    }
    // An officer does not run. They stand still and stay hostile: the wander
    // is stopped rather than redirected, because somebody returning fire
    // while strolling down the pavement reads as indifference.
    alertSteps_ = kOfficerAlertSteps;
    dirX_ = 0;
    dirY_ = 0;
    holdSteps_ = kOfficerAlertSteps;
    const threat::Bearing at = threat::toward(-offsetX, -offsetY);
    if (at.dx != 0) {
        facing_ = at.dx > 0 ? Facing::Right : Facing::Left;
    } else {
        facing_ = at.dy > 0 ? Facing::Down : Facing::Up;
    }
}

bool PedestrianActor::takeAim(int offsetX, int offsetY, int rangePx) {
    if (squashed_ || !officer_ || alertSteps_ <= 0) {
        return false;
    }
    if (!threat::within(offsetX, offsetY, rangePx)) {
        return false;
    }
    // Face the target whether or not the shot is on: turning only at the moment
    // of firing makes every shot look like it came out of nowhere, where an
    // officer tracking the player around a corner is the whole behaviour.
    const threat::Bearing at = threat::toward(offsetX, offsetY);
    if (at.dx != 0) {
        facing_ = at.dx > 0 ? Facing::Right : Facing::Left;
    } else {
        facing_ = at.dy > 0 ? Facing::Down : Facing::Up;
    }
    return threat::hasLineOfFire(offsetX, offsetY, kOfficerAimTolerancePx);
}

bool PedestrianActor::pursue(int offsetX, int offsetY, int rangePx,
                             int weaponRangePx, std::int32_t chaseSpeedSub) {
    if (squashed_ || !officer_) {
        return false;
    }
    if (!threat::within(offsetX, offsetY, rangePx)) {
        return false;
    }
    chaseSpeedSub_ = chaseSpeedSub;
    // Close enough to be chased is close enough to be angry about it. This is
    // what makes a manhunt a manhunt rather than a queue of officers each
    // waiting to be personally shot at first.
    alertSteps_ = kOfficerAlertSteps;
    holdSteps_ = kOfficerAlertSteps;

    const threat::Bearing across =
        threat::intoLine(offsetX, offsetY, kOfficerAimTolerancePx);
    if (across.dx == 0 && across.dy == 0) {
        // Lined up. Stand if the shot is already on, walk down the line if it
        // is not -- takeAim handles the facing either way, and it is called
        // on the same step by returnFire.
        if (threat::within(offsetX, offsetY, weaponRangePx)) {
            dirX_ = 0;
            dirY_ = 0;
            return true;
        }
        const threat::Bearing at = threat::toward(offsetX, offsetY);
        dirX_ = at.dx;
        dirY_ = at.dy;
    } else {
        dirX_ = across.dx;
        dirY_ = across.dy;
    }

    if (dirX_ != 0) {
        facing_ = dirX_ > 0 ? Facing::Right : Facing::Left;
    } else if (dirY_ != 0) {
        facing_ = dirY_ > 0 ? Facing::Down : Facing::Up;
    }
    return true;
}

void PedestrianActor::step() {
    if (squashed_) {
        if (lingerSteps_ > 0) {
            --lingerSteps_;
        }
        return;
    }

    const bool panicking = panicSteps_ > 0;
    if (panicking) {
        --panicSteps_;
    }
    if (alertSteps_ > 0) {
        --alertSteps_;
    }

    if (holdSteps_ <= 0) {
        chooseDirection(!panicking);   // never idle while panicking
    }
    --holdSteps_;

    if (dirX_ == 0 && dirY_ == 0) {
        animPhase_ = 0;
        animTravelSub_ = 0;
        return;
    }

    // Three gaits. A frightened civilian runs, an officer on a chase moves
    // faster than the player walks and slower than they sprint -- so you can
    // outrun the police, but only by running -- and everybody else strolls.
    const std::int32_t speed = panicking ? kPedPanicSpeedSub
                             : isChasing() ? chaseSpeedSub_
                                           : kPedWalkSpeedSub;
    const std::int32_t beforeX = x_;
    const std::int32_t beforeY = y_;
    if (dirX_ != 0) moveAxisX(dirX_ * speed);
    if (dirY_ != 0) moveAxisY(dirY_ * speed);

    const std::int32_t movedX = (x_ > beforeX) ? (x_ - beforeX) : (beforeX - x_);
    const std::int32_t movedY = (y_ > beforeY) ? (y_ - beforeY) : (beforeY - y_);
    // Counted here rather than in moveAxis*, because one axis being refused
    // while the other carried them is not being stuck -- it is a corner.
    if (movedX + movedY == 0) {
        ++stuckSteps_;
    } else {
        stuckSteps_ = 0;
    }
    animTravelSub_ += static_cast<std::uint32_t>(movedX + movedY);
    const std::uint32_t perFrame =
        static_cast<std::uint32_t>(kAnimPixelsPerFrame) * kSubPixelOne;
    while (animTravelSub_ >= perFrame) {
        animTravelSub_ -= perFrame;
        animPhase_ = static_cast<std::uint8_t>((animPhase_ + 1) % kWalkCycleLength);
    }

    syncEntityPosition();
}

bool PedestrianActor::hit(std::uint8_t damage) {
    if (squashed_) {
        return false;      // already down; a body does not absorb rounds
    }
    if (damage >= health_) {
        health_ = 0;
        squash();
        return true;
    }
    health_ = static_cast<std::uint8_t>(health_ - damage);
    return false;
}

void PedestrianActor::squash() {
    if (squashed_) {
        return;
    }
    squashed_ = true;
    lingerSteps_ = kPedSquashLingerSteps;
    dirX_ = 0;
    dirY_ = 0;
}

void PedestrianActor::syncEntityPosition() {
    position = math::Vector2(x_ >> kSubPixelShift, y_ >> kSubPixelShift);
}

void PedestrianActor::update(unsigned long deltaTime) {
    (void)deltaTime;
}

void PedestrianActor::draw(gfx::Renderer& renderer) {
    const std::uint8_t slot =
        static_cast<std::uint8_t>(kPedestrianPaletteSlot + tint_);

    if (squashed_) {
        renderer.drawSprite(kPedestrianSquashed, spriteX(), spriteY(),
                            slot, false);
        return;
    }

    const bool moving = (dirX_ != 0 || dirY_ != 0);
    const std::uint8_t frame = moving ? kWalkCycle[animPhase_] : 0;

    const gfx::Sprite4bpp* sprite = nullptr;
    switch (facing_) {
        case Facing::Up:    sprite = &kPlayerUp[frame];   break;
        case Facing::Down:  sprite = &kPlayerDown[frame]; break;
        case Facing::Right:
        case Facing::Left:  sprite = &kPlayerSide[frame]; break;
    }
    if (sprite == nullptr) {
        return;
    }
    renderer.drawSprite(*sprite, spriteX(), spriteY(), slot,
                        facing_ == Facing::Left);
}

std::uint32_t PedestrianActor::visualKey() const {
    const std::uint32_t px = static_cast<std::uint32_t>(spriteX()) & 0xFFFu;
    const std::uint32_t py = static_cast<std::uint32_t>(spriteY()) & 0xFFFu;
    const std::uint32_t frame =
        (!squashed_ && (dirX_ != 0 || dirY_ != 0)) ? kWalkCycle[animPhase_] : 0u;
    return (px << 20) | (py << 8)
         | (static_cast<std::uint32_t>(facing_) << 5)
         | (squashed_ ? 0x10u : 0u)
         | frame;
}

}  // namespace top_down_city
