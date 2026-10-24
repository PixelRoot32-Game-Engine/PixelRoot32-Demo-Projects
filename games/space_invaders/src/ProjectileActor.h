#pragma once
#include "physics/RigidActor.h"
#include "graphics/Renderer.h"
#include "math/Scalar.h"

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
 * second, different motion on top of the step. Enemy shots keep their velocity
 * until P4 replaces their cadence.
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
 * - Friction: 0 (no air resistance; enemy shots keep a constant velocity)
 * - Gravity: 0 (no vertical acceleration, straight-line movement)
 * - Shape: Circle (accurate for bullet-like projectiles)
 * - CCD: the scene sweeps previousPosition against the rack and the shields;
 *   a player shot's zero velocity keeps the solver from adding its own motion
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
     * 2. Player shots step rules::PlayerShotStepY on the tick; enemy shots are
     *    still integrated by the solver until P4 owns their motion
     * 3. Deactivate if off-screen (object pool lifecycle)
     *
     * @param deltaTime Frame delta in ms. Unused: the tick is the clock (S20),
     *        so the caller must invoke this exactly once per rules tick.
     */
    void update(unsigned long deltaTime) override;
    
    /**
     * @brief Draws the player shot mask, or the enemy placeholder rectangle.
     *
     * The player shot is the 1x4 arcade missile (S2) tinted by the row it
     * enters (S3). The enemy shot is still a filled rectangle until P4 swaps in
     * the 3x7 two-frame bolt.
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
     * with new position and type.
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
