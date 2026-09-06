#include "game/entities/VehicleActor.h"

#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;
namespace core = pr32::core;
namespace math = pr32::math;

namespace {

/// Heading order is the generator's: N, E, S, W.
constexpr int kHeadingDx[4] = { 0, 1, 0, -1 };
constexpr int kHeadingDy[4] = { -1, 0, 1, 0 };

constexpr bool headingIsVertical(std::uint8_t heading) {
    return heading == city_scene::kHeadingN || heading == city_scene::kHeadingS;
}

std::int32_t clampSpeed(std::int32_t speed) {
    if (speed > kVehicleMaxSpeedSub)  return kVehicleMaxSpeedSub;
    if (speed < -kVehicleMaxReverseSub) return -kVehicleMaxReverseSub;
    return speed;
}

}  // namespace

// GENERIC rather than ACTOR: see the note in PlayerActor.cpp -- the tag is
// the engine's CollisionSystem marker, and nothing here registers with it.
VehicleActor::VehicleActor()
    : core::Entity(math::Vector2::ZERO(), kVehicleSpriteW, kVehicleSpriteH,
                   core::EntityType::GENERIC),
      x_(0),
      y_(0),
      speedSub_(0),
      heading_(city_scene::kHeadingN),
      color_(0),
      turnCooldown_(0),
      driven_(false),
      crashSpeedSub_(0),
      throttle_(0) {
    setRenderLayer(1);
}

void VehicleActor::spawn(const city_scene::VehicleSpawn& record) {
    // The spawn is a tile, and the sprite is exactly one tile, so the car
    // sits in its cell with no centring arithmetic.
    x_ = static_cast<std::int32_t>(record.tileX * kTilePx) << kSubPixelShift;
    y_ = static_cast<std::int32_t>(record.tileY * kTilePx) << kSubPixelShift;
    heading_ = record.heading;
    color_ = record.color;
    speedSub_ = 0;
    turnCooldown_ = 0;
    driven_ = false;
    crashSpeedSub_ = 0;
    throttle_ = 0;
    syncEntityPosition();
}

void VehicleActor::spawnAt(int tileX, int tileY, std::uint8_t heading,
                           std::uint8_t color) {
    const city_scene::VehicleSpawn record = {
        static_cast<std::uint8_t>(tileX),
        static_cast<std::uint8_t>(tileY),
        heading,
        color,
    };
    spawn(record);
}

void VehicleActor::parkWhere(const VehicleActor& other) {
    x_ = other.x_;
    y_ = other.y_;
    heading_ = other.heading_;
    // setDriven(false) rather than the same five assignments spawn() makes:
    // "genuinely parked" is already written once, and a car that arrives here
    // still holding a throttle would drive itself out of the spot it was just
    // moved into.
    setDriven(false);
    syncEntityPosition();
}

VehicleBox VehicleActor::boxFor(int spriteX, int spriteY,
                                std::uint8_t heading) {
    return VehicleBox{
        spriteX + VEHICLE_BOXES[heading][0],
        spriteY + VEHICLE_BOXES[heading][1],
        VEHICLE_BOXES[heading][2],
        VEHICLE_BOXES[heading][3],
    };
}

VehicleBox VehicleActor::boxFor(const city_scene::VehicleSpawn& record) {
    return boxFor(record.tileX * kTilePx, record.tileY * kTilePx,
                  record.heading);
}

void VehicleActor::setDriven(bool driven) {
    driven_ = driven;
    if (!driven) {
        speedSub_ = 0;
        turnCooldown_ = 0;
        crashSpeedSub_ = 0;
        throttle_ = 0;
    }
}

// From the generated VEHICLE_BOXES, not arithmetic on the heading -- see
// boxFor() in the header.
int VehicleActor::boxLeft() const {
    return spriteX() + VEHICLE_BOXES[heading_][0];
}

int VehicleActor::boxTop() const {
    return spriteY() + VEHICLE_BOXES[heading_][1];
}

int VehicleActor::boxWidth() const {
    return VEHICLE_BOXES[heading_][2];
}

int VehicleActor::boxHeight() const {
    return VEHICLE_BOXES[heading_][3];
}

bool VehicleActor::overlapsBox(int left, int top, int width, int height) const {
    return isWithinRangeOf(left, top, width, height, 0);
}

bool VehicleActor::isWithinRangeOf(int left, int top, int width, int height,
                                   int range) const {
    // One inflated AABB test. Range 0 is plain overlap, which is why the two
    // callers share it: "am I touching this" and "am I close enough to open
    // the door" are the same question with a different margin.
    const int carLeft   = boxLeft() - range;
    const int carTop    = boxTop() - range;
    const int carRight  = boxLeft() + boxWidth() - 1 + range;
    const int carBottom = boxTop() + boxHeight() - 1 + range;
    return !(left + width - 1 < carLeft || left > carRight ||
             top + height - 1 < carTop  || top > carBottom);
}

bool VehicleActor::isLethal() const {
    const std::int32_t speed = speedSub_ < 0 ? -speedSub_ : speedSub_;
    return speed >= kVehicleSquashSpeedSub;
}

std::int32_t VehicleActor::consumeCrash() {
    const std::int32_t speed = crashSpeedSub_;
    crashSpeedSub_ = 0;
    return speed;
}

bool VehicleActor::canFace(std::uint8_t heading) const {
    const VehicleBox box = boxFor(spriteX(), spriteY(), heading);
    return collision::boxIsFreeWholeTile(box.left, box.top,
                                         box.width, box.height) &&
           !collision::boxIsBlocked(box.left, box.top,
                                    box.width, box.height, this);
}

void VehicleActor::turnTo(std::uint8_t heading) {
    if (heading == heading_) {
        return;
    }
    // Turning about the sprite cell, not the box: the cell is square, so the
    // car pivots in place and cannot rotate itself into a wall.
    heading_ = heading;
    speedSub_ = speedSub_ * kVehicleTurnKeepNum / kVehicleTurnKeepDen;
    turnCooldown_ = kVehicleTurnCooldownSteps;
}

void VehicleActor::advance(std::int32_t delta) {
    if (delta == 0) {
        return;
    }
    const bool vertical = headingIsVertical(heading_);
    const std::int32_t candidateX = vertical ? x_ : x_ + delta;
    const std::int32_t candidateY = vertical ? y_ + delta : y_;

    const VehicleBox box = boxFor(candidateX >> kSubPixelShift,
                                  candidateY >> kSubPixelShift, heading_);

    // Whole-tile, not per-pixel: see CityCollision.h. A car that clips a
    // building corner by two pixels has hit the building.
    if (!collision::boxIsFreeWholeTile(box.left, box.top,
                                       box.width, box.height) ||
        collision::boxIsBlocked(box.left, box.top,
                                box.width, box.height, this)) {
        // Into a wall, a tree or another car: stop dead. No bounce, because a
        // bounce off scenery on a 62.5 Hz tick reads as the car being shoved.
        // The speed lost is captured here rather than derived from speedSub_
        // reaching zero, which also happens on an ordinary release of the
        // throttle. consumeCrash() is the only reader, and
        // audio_cues::crashIsAudible turns the magnitude into a yes/no.
        crashSpeedSub_ = speedSub_ < 0 ? -speedSub_ : speedSub_;
        speedSub_ = 0;
        return;
    }

    x_ = candidateX;
    y_ = candidateY;
    syncEntityPosition();
}

void VehicleActor::step(bool up, bool down, bool left, bool right) {
    if (!driven_) {
        // A parked car is the cheapest actor in the demo, and there are two
        // dozen of them.
        return;
    }

    const int dirX = (right ? 1 : 0) - (left ? 1 : 0);
    const int dirY = (down ? 1 : 0) - (up ? 1 : 0);

    if (turnCooldown_ > 0) {
        --turnCooldown_;
    }

    // Throttle is the component of the d-pad along the heading; the
    // perpendicular component steers. Holding up-right in a north-facing car
    // therefore accelerates AND asks for a right turn, which is what a player
    // expects from a car and not what they expect from a walker.
    const int hx = kHeadingDx[heading_];
    const int hy = kHeadingDy[heading_];
    const int throttle = dirX * hx + dirY * hy;
    const int steer = dirX * -hy + dirY * hx;
    // ADR-19: the scene reads this back through throttleState() to derive a
    // pedal EDGE via the pure audio_cues::pedalEventFor. This class stores the
    // value and deliberately not the edge -- see the getter's own comment.
    throttle_ = static_cast<std::int8_t>(throttle);

    if (steer != 0 && turnCooldown_ == 0) {
        // Right of north is east: +1 turns clockwise through the heading
        // enum, -1 anticlockwise.
        const std::uint8_t turned =
            static_cast<std::uint8_t>((heading_ + (steer > 0 ? 1 : 3)) & 3);
        // Refused rather than clipped: a car wedged in an alley keeps the
        // heading it came in with, and reverses out the way it went in.
        if (canFace(turned)) {
            turnTo(turned);
        }
    }

    if (throttle > 0) {
        speedSub_ = clampSpeed(speedSub_ + kVehicleAccelSub);
    } else if (throttle < 0) {
        // The same button brakes and then reverses, as in every top-down
        // driving game: there is no third pedal on a six-button pad.
        speedSub_ = clampSpeed(speedSub_ - kVehicleBrakeSub);
    } else if (speedSub_ > 0) {
        speedSub_ -= kVehicleFrictionSub;
        if (speedSub_ < 0) speedSub_ = 0;
    } else if (speedSub_ < 0) {
        speedSub_ += kVehicleFrictionSub;
        if (speedSub_ > 0) speedSub_ = 0;
    }

    advance(speedSub_ * (headingIsVertical(heading_) ? hy : hx));
}

bool VehicleActor::isTileAligned() const {
    constexpr std::int32_t kTileSub =
        static_cast<std::int32_t>(kTilePx) << kSubPixelShift;
    return (x_ % kTileSub) == 0 && (y_ % kTileSub) == 0;
}

void VehicleActor::driveAutonomous(std::uint8_t desiredHeading, bool go) {
    // Only on a whole tile, and only if the box fits that way round. Turning
    // between tiles is what leaves a car straddling two lanes for the rest of
    // its life -- and unlike the player's turn there is nobody at the wheel
    // to notice and correct it.
    if (desiredHeading != heading_ && isTileAligned()
            && canFace(desiredHeading)) {
        // No speed penalty and no cooldown, unlike turnTo(). Both of those
        // exist to stop a held diagonal flipping the player's car between two
        // headings every step; a driver that asks for one heading per tile
        // has neither problem, and a traffic car that lost a quarter of its
        // speed at every junction would never reach the next one.
        heading_ = desiredHeading;
    }

    speedSub_ = go ? kTrafficCruiseSpeedSub : 0;
    if (!go) {
        return;
    }
    const bool vertical = headingIsVertical(heading_);
    advance(speedSub_ * (vertical ? kHeadingDy[heading_]
                                  : kHeadingDx[heading_]));
}

void VehicleActor::syncEntityPosition() {
    position = math::Vector2(x_ >> kSubPixelShift, y_ >> kSubPixelShift);
}

void VehicleActor::update(unsigned long deltaTime) {
    (void)deltaTime;
}

void VehicleActor::draw(gfx::Renderer& renderer) {
    // Slot 1 of the sprite palette bank holds the car ramp; slot 7 is the
    // player's, and slot 0 is bound to nothing at all -- the HUD is drawn
    // with primitives, which read the BACKGROUND palette instead.
    renderer.drawSprite(kVehicleSprites[color_][heading_],
                        spriteX(), spriteY(),
                        kVehiclePaletteSlot,
                        false);
}

std::uint32_t VehicleActor::visualKey() const {
    static_assert(kWorldWidth <= 4096 && kWorldHeight <= 4096,
                  "visualKey packs each axis into 12 bits");
    const std::uint32_t px = static_cast<std::uint32_t>(spriteX()) & 0xFFFu;
    const std::uint32_t py = static_cast<std::uint32_t>(spriteY()) & 0xFFFu;
    return (px << 20) | (py << 8)
         | (static_cast<std::uint32_t>(heading_) << 4)
         | color_;
}

}  // namespace top_down_city
