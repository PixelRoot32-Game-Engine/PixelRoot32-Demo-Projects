#pragma once
#include "physics/RigidActor.h"
#include "graphics/Renderer.h"
#include "math/Scalar.h"
#include "rules/Shots.h"

namespace spaceinvaders {

/**
 * @enum ProjectileType
 * @brief Distinguishes between player and enemy projectiles
 * 
 * Used for collision filtering and visual differentiation.
 * PLAYER_BULLET moves upward (negative Y), ENEMY_BULLET moves downward (positive Y).
 */
enum class ProjectileType {
    PLAYER_BULLET,
    ENEMY_BULLET
};

/**
 * @class ProjectileActor
 * @brief High-speed projectile with Continuous Collision Detection (CCD)
 * 
 * Architecture Pattern:
 * Demonstrates the RigidActor usage pattern for physics-simulated entities: it
 * keeps a rigid body (velocity, friction, bounce, collision layers) so the
 * solver and the scene's swept tests still find it, while the rules tick owns
 * the motion that S8 fixes in pixels.
 * 
 * Player shot motion (S8):
 * The player shot does not ride the solver. It steps a fixed four pixels per
 * rules tick, because the arcade advances it on the tick and a velocity would
 * make its speed depend on whatever delta the physics scheduler happened to
 * hand out. Its rigid body velocity stays zero so the solver cannot add a
 * second, different motion on top of the step.
 *
 * Enemy shot motion (S9):
 * The enemy bolt carries no velocity either. The scene owns the three-tick
 * clock, and every third tick it calls stepEnemyShot() on all live bolts, so
 * they all fall one step (four pixels, five once fewer than nine aliens
 * remain) together and flip their animation frame together. Keeping the clock
 * in the scene is what stops the fire rate from following the rack sweep.
 *
 * Continuous Collision Detection (CCD):
 * A four-pixel tick can carry the shot across a small alien between two frames,
 * so collision needs swept tests. This class records previousPosition before
 * the step to feed swept-circle tests against AlienActors.
 * 
 * Pooling Pattern:
 * Projectiles use object pooling (managed by SpaceInvadersScene) rather than
 * dynamic allocation. The reset() method allows reuse without reconstruction,
 * critical for performance on microcontroller targets like ESP32.
 * 
 * Physics Configuration:
 * - Restitution: 0 (no bounce, projectiles should not ricochet)
 * - Friction: 0 (no air resistance)
 * - Gravity: 0 (no vertical acceleration, straight-line movement)
 * - Shape: Circle (accurate for bullet-like projectiles)
 * - CCD: the scene sweeps previousPosition against the rack and the shields;
 *   a zero velocity keeps the solver from adding its own motion
 */
class ProjectileActor : public pixelroot32::physics::RigidActor {
public:
    /**
     * @brief Constructs and immediately activates projectile
     * @param position Initial spawn position (typically player/alien position)
     * @param type Determines direction and color (player = white, enemy = red)
     */
    ProjectileActor(pixelroot32::math::Vector2 position, ProjectileType type);
    
    /**
     * @brief Advances the projectile by one fixed rules tick.
     *
     * System Flow:
     * 1. Store previous position, before the move, for swept collision tests
     * 2. Player shots step rules::PlayerShotStepY on the tick; enemy shots do
     *    not move here, because the scene hands them one whole shot step every
     *    third tick through stepEnemyShot() (S9)
     * 3. Deactivate if off-screen (object pool lifecycle)
     *
     * @param deltaTime Frame delta in ms. Unused: the tick is the clock (S20),
     *        so the caller must invoke this exactly once per rules tick.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Lands one enemy shot step: motion, position and animation frame.
     *
     * Records nothing itself. The pre-step position the swept tests need was
     * already captured by update() at the top of the tick, so this can run
     * after the rest of the tick's rules without losing the segment.
     *
     * A player shot ignores the call: its motion belongs to update().
     *
     * @param fast true to use the five-pixel step (fewer than nine aliens).
     */
    void stepEnemyShot(bool fast);
    
    /**
     * @brief Draws the player shot mask, or the enemy bolt's current frame.
     *
     * Both masks are tinted by the row they enter (S3): the player shot is the
     * 1x4 arcade missile (S2), and the enemy bolt is the 3x7 zigzag whose two
     * frames alternate on every shot step (S9).
     *
     * @param renderer Renderer to draw into.
     */
    void draw(pixelroot32::graphics::Renderer& renderer);
    
    /**
     * @brief Resets projectile for reuse from object pool
     * 
     * Design Pattern: Object Pooling
     * Instead of constructing/destructing projectiles (expensive), the Scene
     * maintains a fixed pool. reset() prepares a pooled projectile for reuse
     * with new position and type, including the enemy bolt's descent phase and
     * animation frame, so a recycled slot cannot inherit the previous life.
     */
    void reset(pixelroot32::math::Vector2 position, ProjectileType type);
    
    /**
     * @brief Collision callback - marks projectile for deactivation
     * 
     * Design Note: Projectiles always deactivate on collision regardless of
     * what they hit. Specific collision logic (damage, scoring) is handled
     * by SpaceInvadersScene to maintain separation of concerns.
     */
    void onCollision(pixelroot32::core::Actor* other) override;
    
    /**
     * @brief Returns true if projectile is active and should be processed
     */
    bool isActive() const { return active; }
    
    /**
     * @brief Marks projectile for deactivation and pool recycling
     */
    void deactivate() { active = false; }
    
    /**
     * @brief Returns projectile type for collision filtering
     */
    ProjectileType getType() const { return type; }
    
    /**
     * @brief Returns X coordinate from previous frame
     * 
     * Used by SpaceInvadersScene::handleCollisions() for swept-circle
     * collision tests against AlienActors. This provides sub-frame collision
     * accuracy essential for hitting fast-moving or small targets.
     */
    pixelroot32::math::Scalar getPreviousX() const { return previousPosition.x; }
    
    /**
     * @brief Returns Y coordinate from previous frame
     * 
     * Paired with getPreviousX() for complete swept test data.
     */
    pixelroot32::math::Scalar getPreviousY() const { return previousPosition.y; }

private:
    ProjectileType type;             // Player or enemy projectile
    bool active;                     // Pool lifecycle state
    rules::EnemyShotMotion enemyMotion; ///< Enemy descent: one call per shot step
    uint8_t enemyFrame = 0;          ///< Which of the two 3x7 enemy frames to draw

    /**
     * @brief Applies the body box a projectile type needs.
     *
     * The enemy bolt's box is its whole 3x7 mask (S9), so a hit is judged on
     * the shape the player sees. The player shot keeps the 1x1 box the
     * constructor's radius derived, so its swept test stays what it was.
     */
    void applyTypeBody();

    /**
     * Pre-step position for the swept tests in SpaceInvadersScene.
     *
     * This deliberately shadows PhysicsActor::previousPosition: the collision
     * system rewrites the base field after the entity updates, which would
     * erase the pre-step point a player shot needs after moving itself.
     */
    pixelroot32::math::Vector2 previousPosition;
};

}
