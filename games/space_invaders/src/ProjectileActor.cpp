#include "ProjectileActor.h"
#include "BandColor.h"
#include "GameConstants.h"
#include "physics/CollisionSystem.h"

namespace spaceinvaders {

using namespace pixelroot32::math;

ProjectileActor::ProjectileActor(pixelroot32::math::Vector2 position, ProjectileType type)
    : RigidActor(position, PROJECTILE_WIDTH, PROJECTILE_HEIGHT), type(type), active(true), previousPosition(position) {
    
    setRestitution(toScalar(0.0f));
    setFriction(toScalar(0.0f));
    setGravityScale(toScalar(0.0f));
    setShape(pixelroot32::core::CollisionShape::CIRCLE);
    setRadius(toScalar(PROJECTILE_WIDTH * 0.5f));
    setBounce(false);

    Scalar speed = (type == ProjectileType::PLAYER_BULLET) ? toScalar(-PROJECTILE_SPEED) : toScalar(PROJECTILE_SPEED);
    setVelocity(toScalar(0), speed);
}

void ProjectileActor::reset(pixelroot32::math::Vector2 newPosition, ProjectileType newType) {
    position = newPosition;
    previousPosition = newPosition;
    type = newType;
    active = true;
    Scalar speed = (type == ProjectileType::PLAYER_BULLET) ? toScalar(-PROJECTILE_SPEED) : toScalar(PROJECTILE_SPEED);
    setVelocity(toScalar(0), speed);
}

void ProjectileActor::update(unsigned long deltaTime) {
    if (!active) return;

    previousPosition = position;
    RigidActor::update(deltaTime);

    if (position.y < toScalar(-PROJECTILE_HEIGHT) || position.y > toScalar(DISPLAY_HEIGHT)) {
        active = false;
    }
}

void ProjectileActor::draw(pixelroot32::graphics::Renderer& renderer) {
    if (!active) return;

    const int drawX = static_cast<int>(position.x);
    const int drawY = static_cast<int>(position.y);

    // Both shot types are the same white mask under the overlay (S3): the band
    // the shot is travelling through decides its colour, so the enemy shot is
    // no longer a separate orange and the player shot reddens near the top.
    renderer.drawFilledRectangle(drawX, drawY,
                                 static_cast<int>(width), static_cast<int>(height),
                                 bandColorFor(drawY));
}

void ProjectileActor::onCollision(pixelroot32::core::Actor* other) {
    (void)other;
    active = false;
}

}
