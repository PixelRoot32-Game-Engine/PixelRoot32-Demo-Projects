#include "ProjectileActor.h"
#include "BandColor.h"
#include "GameConstants.h"
#include "assets/ShotSprites.h"
#include "physics/CollisionSystem.h"
#include "rules/RulesConstants.h"

namespace spaceinvaders {

using namespace pixelroot32::math;

ProjectileActor::ProjectileActor(pixelroot32::math::Vector2 position, ProjectileType type)
    : RigidActor(position, PROJECTILE_WIDTH, PROJECTILE_HEIGHT), type(type), active(true), previousPosition(position) {
    
    setRestitution(toScalar(0.0f));
    setFriction(toScalar(0.0f));
    setGravityScale(toScalar(0.0f));
    setShape(pixelroot32::core::CollisionShape::CIRCLE);
    setBounce(false);

    applyTypeBody();
}

void ProjectileActor::applyTypeBody() {
    if (type == ProjectileType::ENEMY_BULLET) {
        // S9: the bolt collides as the 3x7 mask it draws. setRadius() rewrites
        // width and height from the radius, so the box is written after it.
        setRadius(toScalar(ENEMY_SHOT_WIDTH * 0.5f));
        width = ENEMY_SHOT_WIDTH;
        height = ENEMY_SHOT_HEIGHT;
    } else {
        // S8: the player shot's body stays the 1x1 square the 0.5 px radius
        // produced before P4, so its swept test is untouched by this change.
        setRadius(toScalar(PROJECTILE_WIDTH * 0.5f));
    }

    // Neither type is integrated by the solver: the rules tick moves the
    // player shot and the scene moves the enemy bolt (S8, S9).
    setVelocity(toScalar(0), toScalar(0));
}

void ProjectileActor::reset(pixelroot32::math::Vector2 newPosition, ProjectileType newType) {
    // setPosition also clears the base previous position, so a recycled shot
    // cannot drag a stale swept segment from wherever its last life ended.
    setPosition(newPosition);
    previousPosition = newPosition;
    type = newType;
    active = true;
    applyTypeBody();

    if (type == ProjectileType::ENEMY_BULLET) {
        // The descent restarts with the shot: a pooled bolt must not inherit
        // the phase or the frame of the shot whose slot it took (S9).
        enemyMotion.reset(static_cast<int16_t>(newPosition.y));
        enemyFrame = 0;
    }
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

    // An enemy bolt only moves in stepEnemyShot(), which the scene calls after
    // this; the off-screen test still runs every tick so a bolt that fell past
    // the panel is recycled even when no step is due (S9).
    if (position.y < toScalar(-PROJECTILE_HEIGHT) || position.y > toScalar(DISPLAY_HEIGHT)) {
        active = false;
    }
}

void ProjectileActor::stepEnemyShot(bool fast) {
    if (!active || type != ProjectileType::ENEMY_BULLET) {
        return;
    }

    // The rules object owns the step size and the scene owns the clock, so this
    // call is one whole three-tick step, never a single tick of it (S9).
    const int16_t nextY = enemyMotion.step(fast);
    setPosition(Vector2(position.x, toScalar(static_cast<int>(nextY))));

    // Two frames alternating per step is what reads as the bolt wriggling down
    // the lane instead of a static bar (S9).
    enemyFrame = static_cast<uint8_t>(enemyFrame ^ 1u);
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

    // S9: the enemy bolt is the 3x7 zigzag of the reference art, and its two
    // frames alternate on every shot step. The band tint comes from the top row
    // like every other mask, so it greens over the shields (S3).
    const pixelroot32::graphics::Sprite& bolt = enemyFrame ? ENEMY_SHOT_F2 : ENEMY_SHOT_F1;
    renderer.drawSprite(bolt, drawX, drawY, bandColorFor(drawY));
}

void ProjectileActor::onCollision(pixelroot32::core::Actor* other) {
    (void)other;
    active = false;
}

}
