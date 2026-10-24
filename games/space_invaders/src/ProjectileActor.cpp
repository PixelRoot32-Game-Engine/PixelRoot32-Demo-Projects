#include "ProjectileActor.h"
#include "BandColor.h"
#include "GameConstants.h"
#include "assets/ShotSprites.h"
#include "physics/CollisionSystem.h"
#include "rules/RulesConstants.h"

namespace spaceinvaders {

using namespace pixelroot32::math;

namespace {

/**
 * @brief Velocity the solver should see for a given projectile type.
 *
 * Player shots are moved by their own fixed tick step (S8), so their velocity
 * must stay zero or the solver would integrate a second, delta-dependent motion
 * on top of it. Enemy shots still ride the solver until P4 rewrites their
 * cadence.
 */
Scalar solverVelocityY(ProjectileType type) {
    return type == ProjectileType::PLAYER_BULLET ? toScalar(0) : toScalar(PROJECTILE_SPEED);
}

}  // namespace

ProjectileActor::ProjectileActor(pixelroot32::math::Vector2 position, ProjectileType type)
    : RigidActor(position, PROJECTILE_WIDTH, PROJECTILE_HEIGHT), type(type), active(true), previousPosition(position) {
    
    setRestitution(toScalar(0.0f));
    setFriction(toScalar(0.0f));
    setGravityScale(toScalar(0.0f));
    setShape(pixelroot32::core::CollisionShape::CIRCLE);
    setRadius(toScalar(PROJECTILE_WIDTH * 0.5f));
    setBounce(false);

    setVelocity(toScalar(0), solverVelocityY(type));
}

void ProjectileActor::reset(pixelroot32::math::Vector2 newPosition, ProjectileType newType) {
    // setPosition also clears the base previous position, so a recycled shot
    // cannot drag a stale swept segment from wherever its last life ended.
    setPosition(newPosition);
    previousPosition = newPosition;
    type = newType;
    active = true;
    setVelocity(toScalar(0), solverVelocityY(type));
}

void ProjectileActor::update(unsigned long deltaTime) {
    if (!active) return;

    // S20: the tick is the clock, exactly like the cannon. The scene calls this
    // once per fixed rules tick, so the shot steps a whole four pixels and no
    // frame delta can stretch or skip it.
    (void)deltaTime;

    // The swept tests read this, so it has to be the position the tick started
    // from — before the step below moves the shot.
    previousPosition = position;

    if (type == ProjectileType::PLAYER_BULLET) {
        // S8: one fixed four-pixel step per tick, with the rigid body velocity
        // pinned to zero so the solver cannot add a second motion on top.
        setPosition(position + Vector2(toScalar(0), toScalar(-rules::PlayerShotStepY)));
    }

    if (position.y < toScalar(-PROJECTILE_HEIGHT) || position.y > toScalar(DISPLAY_HEIGHT)) {
        active = false;
    }
}

void ProjectileActor::draw(pixelroot32::graphics::Renderer& renderer) {
    if (!active) return;

    const int drawX = static_cast<int>(position.x);
    const int drawY = static_cast<int>(position.y);

    if (type == ProjectileType::PLAYER_BULLET) {
        // S2/S3: the 1x4 mask is the arcade missile, and the band the shot is
        // entering decides its colour, so the same white mask reddens near the
        // top instead of carrying a per-band recolour.
        renderer.drawSprite(PLAYER_SHOT_SPRITE, drawX, drawY, bandColorFor(drawY));
        return;
    }

    // P4 replaces this placeholder with the 3x7 two-frame bolt (S9); the enemy
    // shot keeps its old rectangle so this task only changes the player's.
    renderer.drawFilledRectangle(drawX, drawY,
                                 static_cast<int>(width), static_cast<int>(height),
                                 bandColorFor(drawY));
}

void ProjectileActor::onCollision(pixelroot32::core::Actor* other) {
    (void)other;
    active = false;
}

}
