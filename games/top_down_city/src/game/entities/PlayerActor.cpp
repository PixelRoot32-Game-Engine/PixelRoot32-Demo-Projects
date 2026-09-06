#include "game/entities/PlayerActor.h"

#include "game/systems/CityCollision.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;
namespace core = pr32::core;
namespace math = pr32::math;

namespace {

constexpr std::int32_t kMaxX =
    static_cast<std::int32_t>(kWorldWidth - kPlayerSpriteW) << kSubPixelShift;
constexpr std::int32_t kMaxY =
    static_cast<std::int32_t>(kWorldHeight - kPlayerSpriteH) << kSubPixelShift;

std::int32_t clampSub(std::int32_t value, std::int32_t maximum) {
    if (value < 0) return 0;
    if (value > maximum) return maximum;
    return value;
}

}  // namespace

// The player's half of the collision model: the box -- only the feet collide,
// see kPlayerBoxOffsetY in CityConstants.h -- and everything that can stop it.
// The tile tests themselves live in CityCollision: cars and pedestrians ask
// the same layers the same questions.
bool PlayerActor::canOccupy(int x, int y, const void* ignore) {
    const int left = x + kPlayerBoxOffsetX;
    const int top  = y + kPlayerBoxOffsetY;

    if (!collision::boxIsFreePerPixel(left, top,
                                      kPlayerBoxWidth, kPlayerBoxHeight)) {
        return false;
    }
    // Parked cars are not in the tile data -- they are actors, and the scene
    // is what knows where they are.
    return !collision::boxIsBlocked(left, top,
                                    kPlayerBoxWidth, kPlayerBoxHeight, ignore);
}

// GENERIC, not ACTOR, and the distinction is not cosmetic. `EntityType::ACTOR`
// is the engine's marker for a CollisionSystem participant: `Scene::addEntity`
// does a `static_cast<Actor*>(entity)` on the strength of it, and every other
// reader of the tag lives in CollisionSystem/SpatialGrid. This demo does not
// derive from `core::Actor` -- its collision is the tile test in CityCollision,
// and `PIXELROOT32_ENABLE_PHYSICS` is 0 because a 128x128 city is thousands of
// obstacles against a 64-entity solver -- so claiming ACTOR would be a downcast
// to a base this object does not have, waiting for the flag to be turned on.
PlayerActor::PlayerActor()
    : core::Entity(math::Vector2::ZERO(), kPlayerSpriteW, kPlayerSpriteH,
                   core::EntityType::GENERIC),
      x_(0),
      y_(0),
      facing_(Facing::Down),
      animTravelSub_(0),
      animPhase_(0),
      moving_(false),
      armed_(false),
      health_(kPlayerHealth),
      vest_(armor::none()),
      impactCooldown_(0) {
    setRenderLayer(1);
}

void PlayerActor::spawnAt(int tileX, int tileY) {
    const int px = tileX * kTilePx + (kTilePx - kPlayerSpriteW) / 2;
    const int py = tileY * kTilePx + (kTilePx - kPlayerSpriteH) / 2;
    x_ = clampSub(static_cast<std::int32_t>(px) << kSubPixelShift, kMaxX);
    y_ = clampSub(static_cast<std::int32_t>(py) << kSubPixelShift, kMaxY);
    facing_ = Facing::Down;
    animTravelSub_ = 0;
    animPhase_ = 0;
    moving_ = false;
    syncEntityPosition();
}

void PlayerActor::placeAt(int px, int py, Facing facing) {
    x_ = clampSub(static_cast<std::int32_t>(px) << kSubPixelShift, kMaxX);
    y_ = clampSub(static_cast<std::int32_t>(py) << kSubPixelShift, kMaxY);
    facing_ = facing;
    animTravelSub_ = 0;
    animPhase_ = 0;
    moving_ = false;
    syncEntityPosition();
}

void PlayerActor::step(bool up, bool down, bool left, bool right, bool run) {
    if (impactCooldown_ > 0) {
        --impactCooldown_;
    }
    const int dirX = (right ? 1 : 0) - (left ? 1 : 0);
    const int dirY = (down ? 1 : 0) - (up ? 1 : 0);

    moving_ = (dirX != 0 || dirY != 0);
    if (!moving_) {
        // Reset to the neutral pose so a stopped player is never caught
        // mid-stride.
        animPhase_ = 0;
        animTravelSub_ = 0;
        return;
    }

    // Facing: the horizontal axis wins a diagonal, which reads better than
    // flipping between two sprites while the player holds two keys.
    if (dirY > 0)      facing_ = Facing::Down;
    else if (dirY < 0) facing_ = Facing::Up;
    if (dirX > 0)      facing_ = Facing::Right;
    else if (dirX < 0) facing_ = Facing::Left;

    std::int32_t speed = run ? kRunSpeedSub : kWalkSpeedSub;
    if (dirX != 0 && dirY != 0) {
        // Without this a diagonal would cover sqrt(2) times more ground per
        // step than a straight line.
        speed = speed * kDiagonalScaleNum / kDiagonalScaleDen;
    }

    const std::int32_t beforeX = x_;
    const std::int32_t beforeY = y_;
    if (dirX != 0) moveAxisX(dirX * speed);
    if (dirY != 0) moveAxisY(dirY * speed);

    // Sum the two axes' magnitudes. Adding the signed deltas would cancel out
    // on an up-right diagonal and freeze the walk cycle.
    const std::int32_t movedX = (x_ > beforeX) ? (x_ - beforeX) : (beforeX - x_);
    const std::int32_t movedY = (y_ > beforeY) ? (y_ - beforeY) : (beforeY - y_);
    animTravelSub_ += static_cast<std::uint32_t>(movedX + movedY);
    const std::uint32_t perFrame =
        static_cast<std::uint32_t>(kAnimPixelsPerFrame) * kSubPixelOne;
    while (animTravelSub_ >= perFrame) {
        animTravelSub_ -= perFrame;
        animPhase_ = static_cast<std::uint8_t>((animPhase_ + 1) % kWalkCycleLength);
    }

    syncEntityPosition();
}

void PlayerActor::moveAxisX(std::int32_t delta) {
    const std::int32_t candidate = clampSub(x_ + delta, kMaxX);
    if (canOccupy(candidate >> kSubPixelShift, y_ >> kSubPixelShift)) {
        x_ = candidate;
    }
}

void PlayerActor::moveAxisY(std::int32_t delta) {
    const std::int32_t candidate = clampSub(y_ + delta, kMaxY);
    if (canOccupy(x_ >> kSubPixelShift, candidate >> kSubPixelShift)) {
        y_ = candidate;
    }
}

void PlayerActor::syncEntityPosition() {
    position = math::Vector2(x_ >> kSubPixelShift, y_ >> kSubPixelShift);
}

void PlayerActor::update(unsigned long deltaTime) {
    (void)deltaTime;
}

void PlayerActor::setArmed(bool armed) {
    armed_ = armed;
}

bool PlayerActor::hitByVehicle() {
    if (impactCooldown_ > 0) {
        // Still picking yourself up. Without this a car that is touching the
        // player is touching them on all 62 steps of the second, which is a
        // death in under two.
        return false;
    }
    impactCooldown_ = static_cast<std::uint8_t>(kCarImpactCooldownSteps);
    hit(kCarImpactDamage);
    return true;
}

bool PlayerActor::hit(std::uint8_t damage) {
    if (health_ == 0) {
        return false;      // already down; the respawn is the scene's job
    }
    // The vest first, and it is spent whether or not the hit gets through it.
    // Before the down check rather than after: a round that the vest stops in
    // full must not be able to reach a player on one hit point.
    damage = armor::absorb(vest_, damage);
    if (damage == 0) {
        return false;
    }
    if (damage >= health_) {
        health_ = 0;
        return true;
    }
    health_ = static_cast<std::uint8_t>(health_ - damage);
    return false;
}

void PlayerActor::wearVest() {
    armor::wear(vest_);
}

void PlayerActor::heal() {
    impactCooldown_ = 0;
    health_ = kPlayerHealth;
    armor::strip(vest_);
}

void PlayerActor::draw(gfx::Renderer& renderer) {
    const std::uint8_t frame = moving_ ? kWalkCycle[animPhase_] : 0;

    // One branch, on the frame set rather than on every case: the armed
    // frames are the unarmed ones with a gun stamped in, so they are indexed
    // identically and nothing about the animation changes with them.
    const gfx::Sprite4bpp* sprite = nullptr;
    switch (facing_) {
        case Facing::Up:
            sprite = armed_ ? &kPlayerArmedUp[frame]   : &kPlayerUp[frame];
            break;
        case Facing::Down:
            sprite = armed_ ? &kPlayerArmedDown[frame] : &kPlayerDown[frame];
            break;
        case Facing::Right:
        case Facing::Left:
            sprite = armed_ ? &kPlayerArmedSide[frame] : &kPlayerSide[frame];
            break;
    }
    if (sprite == nullptr) {
        return;
    }

    // Slot 7, not slot 0: the player's colours are the same as the HUD ink,
    // but only the player's copy follows the day/night tint. See
    // kPlayerPaletteSlot in CityConstants.h.
    renderer.drawSprite(*sprite,
                        x_ >> kSubPixelShift,
                        y_ >> kSubPixelShift,
                        kPlayerPaletteSlot,
                        facing_ == Facing::Left);
}

int PlayerActor::centreX() const {
    return (x_ >> kSubPixelShift) + kPlayerSpriteW / 2;
}

int PlayerActor::centreY() const {
    return (y_ >> kSubPixelShift) + kPlayerSpriteH / 2;
}

std::uint32_t PlayerActor::visualKey() const {
    // 12 bits per axis. The world is 2048 px square today; the assert is what
    // turns a future MAP_W increase into a build error instead of a key that
    // silently wraps and freezes the screen mid-walk.
    static_assert(kWorldWidth <= 4096 && kWorldHeight <= 4096,
                  "visualKey packs each axis into 12 bits");

    const std::uint32_t px = static_cast<std::uint32_t>(x_ >> kSubPixelShift) & 0xFFFu;
    const std::uint32_t py = static_cast<std::uint32_t>(y_ >> kSubPixelShift) & 0xFFFu;
    const std::uint32_t frame = moving_ ? kWalkCycle[animPhase_] : 0u;
    return (px << 20) | (py << 8)
         | (armed_ ? 0x80u : 0u)
         | (static_cast<std::uint32_t>(facing_) << 4)
         | frame;
}

int PlayerActor::tileX() const {
    return centreX() / kTilePx;
}

int PlayerActor::tileY() const {
    // The feet decide which tile the player stands in, not the centre of the
    // sprite -- the same reason the collision box sits at the bottom.
    return ((y_ >> kSubPixelShift) + kPlayerBoxOffsetY + kPlayerBoxHeight / 2)
           / kTilePx;
}

}  // namespace top_down_city
