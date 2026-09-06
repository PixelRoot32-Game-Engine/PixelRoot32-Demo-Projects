#pragma once
#include "physics/RigidActor.h"
#include "GameLayers.h"
#include "graphics/Color.h"

namespace pong {

/**
 * @class BallActor
 * @brief Physics-simulated ball with perfect elastic collisions
 * 
 * Architecture Pattern:
 * Demonstrates RigidActor for fully physics-simulated objects. Unlike
 * KinematicActor (controlled programmatically) or StaticActor (immovable),
 * RigidActor integrates velocity automatically through the physics system,
 * responding to forces and collisions without manual position manipulation.
 * 
 * Perfect Elastic Collisions:
 * With restitution=1.0, the ball maintains energy perfectly across bounces,
 * creating predictable arcade-style physics. This required Flat Solver,
 * as previous versions would lose energy or cause sticking.
 * 
 * Paddle Physics (onCollision):
 * When hitting paddles, the ball uses "english" - the impact point relative
 * to paddle center determines bounce angle. This gives players directional
 * control through positioning, a classic Pong gameplay mechanic.
 * 
 * Speed Progression:
 * Each paddle hit increases speed by 5%, up to 3x initial speed.
 * This creates escalating tension as rallies continue.
 * 
 * Lifecycle Management:
 * The ball uses a respawn timer (500ms) after scoring, giving players
 * a moment to prepare before the next rally begins.
 */
class BallActor : public pixelroot32::physics::RigidActor {
public:
    int radius;                           // Visual and collision radius
    bool isActive;                        // False during respawn delay
    unsigned long respawnTimer;           // Accumulator for respawn delay

    BallActor(pixelroot32::math::Vector2 position, float initialSpeed, int radius);

    /**
     * @brief Resets ball to center with respawn delay
     * 
     * Called after scoring. Ball becomes inactive for 500ms,
     * then launches with random direction.
     */
    void reset();
    
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    pixelroot32::core::Rect getHitBox() override;

    /**
     * @brief Handles collision responses with gameplay effects
     * 
     * Collision Types:
     * - PADDLE: Applies "english" physics (angle based on hit position), 
     *           increases speed 5%, plays sound
     * - WALL: Physics system handles bounce (restitution=1.0), plays sound
     * 
     * Design Note: Wall bounces require NO manual velocity inversion.
     * Flat Solver's impulse-based velocity solver handles this
     * automatically when restitution=1.0.
     */
    void onCollision(pixelroot32::core::Actor* other) override;

private:
    float initialSpeed;  // Base speed for resets and speed cap calculations
};

}
