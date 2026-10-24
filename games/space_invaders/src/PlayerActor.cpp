#include "PlayerActor.h"
#include "platforms/EngineConfig.h"
#include "BandColor.h"
#include "GameConstants.h"
#include "core/Engine.h"
#include "rules/RulesConstants.h"
#include "assets/PlayerSprites.h"

namespace pr32 = pixelroot32;
extern pr32::core::Engine engine;

namespace spaceinvaders {

PlayerActor::PlayerActor(pixelroot32::math::Vector2 position)
    : KinematicActor(position, static_cast<int>(PLAYER_WIDTH), static_cast<int>(PLAYER_HEIGHT)),
      isAlive(true) {

    setShape(pr32::core::CollisionShape::AABB);

    // S7: the field edge is the only limit, so a spawn is clamped and nothing
    // else. Snapping to a lane here is what made respawns land somewhere the
    // old grid could not describe (F2). The actor keeps screen coordinates like
    // every other entity, so the clamp is applied in playfield space and the
    // origin is added back before storing it.
    const int startX = FIELD_X + rules::clampCannonX(static_cast<int>(position.x) - FIELD_X);
    setPosition(pr32::math::Vector2(pr32::math::toScalar(startX), position.y));
}

void PlayerActor::update(unsigned long deltaTime) {
    // S20: the tick is the clock. The scene calls this exactly once per fixed
    // rules tick, so one tick is one pixel and no frame rate can stretch it.
    (void)deltaTime;

    auto& input = engine.getInputManager();
    const bool leftDown = input.isButtonDown(BTN_LEFT);
    const bool rightDown = input.isButtonDown(BTN_RIGHT);

    // Holding both cancels out instead of letting the last sampled button win:
    // a two-button press must never move the cannon by accident.
    if (leftDown == rightDown) {
        return;
    }

    // The clamp lives in playfield pixels (0..CannonMaxX) because that is the
    // rule; the actor stores the screen column every other entity uses, so the
    // origin is subtracted, clamped and added back in one place.
    const int step = rightDown ? rules::CannonStepX : -rules::CannonStepX;
    const int fieldX = rules::clampCannonX(static_cast<int>(position.x) - FIELD_X + step);
    setPosition(pr32::math::Vector2(pr32::math::toScalar(FIELD_X + fieldX), position.y));
}

bool PlayerActor::isFireDown() const {
    auto& input = engine.getInputManager();
    return input.isButtonDown(BTN_FIRE);
}

bool PlayerActor::wantsToShoot() const {
    auto& input = engine.getInputManager();
    return input.isButtonPressed(BTN_FIRE);
}

void PlayerActor::draw(pr32::graphics::Renderer& renderer) {
    if (!isAlive) return;

    // Screen column, like every other entity the scene collides with.
    const int drawX = static_cast<int>(position.x);
    const int drawY = static_cast<int>(position.y);

    // 1:1 draw: the arcade screen model has no scale factor, so the sprite is
    // the same 13x8 the rules talk about. The cannon sits in the green band
    // under the overlay, so its white mask reads green (S3).
    renderer.drawSprite(PLAYER_SHIP_SPRITE,
                        drawX,
                        drawY,
                        bandColorFor(drawY));
}

void PlayerActor::onCollision(pr32::core::Actor* other) {
    (void)other;
}

}  // namespace spaceinvaders
