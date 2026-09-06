/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 * 
 * BallActor - Flat Solver Demonstration
 * 
 * This file demonstrates the RigidActor pattern for physics-simulated objects.
 * Key features demonstrated:
 * - Perfect elastic collisions (restitution=1.0)
 * - Automatic physics integration (no manual position updates)
 * - Collision callbacks for gameplay logic
 * - "English" physics for paddle control
 */
#include "BallActor.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include "PongPhysicsValidator.h"
#include <cstdlib>
#include <cmath>
#include "platforms/EngineConfig.h"

extern pixelroot32::core::Engine engine;

namespace pong {

namespace pr32 = pixelroot32;

using Color = pr32::graphics::Color;
using Scalar = pr32::math::Scalar;

/**
 * @brief Constructs ball with perfect elastic collision physics
 * 
 * Physics Configuration:
 * - Restitution = 1.0: Perfect energy conservation on bounce
 * - Friction = 0: No velocity decay during flight
 * - Gravity = 0: No vertical acceleration (top-down view)
 * - Shape = CIRCLE: Accurate collision for spherical object
 * - Bounce = true: Required for wall/paddle reflection
 * - Radius: Critical for collision detection, must match visual size
 * 
 * Historical Note: Earlier engine versions required manual bounce logic
 * due to physics solver limitations. Flat Solver's impulse-based
 * velocity solver with proper separation of velocity/position phases
 * enables true elastic collisions without workarounds.
 */
BallActor::BallActor(pixelroot32::math::Vector2 position, float initialSpeed, int radius)
    : pr32::physics::RigidActor(pr32::math::Vector2(position.x - radius, position.y - radius), radius * 2, radius * 2),
      radius(radius),
      isActive(false),
      respawnTimer(0),
      initialSpeed(initialSpeed)
{
    velocity.x = Scalar(0);
    velocity.y = Scalar(0);

    // Flat Solver: Perfect elastic collisions configuration
    setRestitution(Scalar(1.0f));      // No energy loss on bounce
    setFriction(Scalar(0.0f));         // No velocity decay
    setGravityScale(Scalar(0.0f));     // No gravity (top-down)
    setShape(pr32::core::CollisionShape::CIRCLE);
    setRadius(Scalar(radius));          // MUST be set for CIRCLE collisions
    
    // Enable bounce response in collision solver
    setBounce(true);

    setRenderLayer(1);
    setCollisionLayer(Layers::BALL);
    setCollisionMask(Layers::PADDLE | Layers::WALL);
}

/**
 * @brief Resets ball to center court with respawn delay
 * 
 * State Machine Transition:
 * ACTIVE -> [Score Detected] -> INACTIVE (500ms) -> LAUNCH
 * 
 * The respawn delay gives players time to reposition before the next rally,
 * preventing unfair immediate re-engagement.
 */
void BallActor::reset() {
    position.x = Scalar(worldWidth / 2.0f);
    position.y = Scalar(worldHeight / 2.0f);

    velocity.x = Scalar(0);
    velocity.y = Scalar(0);
    respawnTimer = 500;
    isActive = false;   
    isEnabled = true;
}

void BallActor::update(unsigned long deltaTime) {
    if (!isActive) {
        if (respawnTimer > deltaTime) {
            respawnTimer -= deltaTime;
            return;
        } else {
            respawnTimer = 0;
            isActive = true;

            // Random initial direction for variety
            // Horizontal: Random left/right
            // Vertical: Random -0.5 to +0.5 for angled serve
            velocity.x = Scalar((rand() % 2 == 0 ? 1 : -1) * initialSpeed);
            velocity.y = Scalar(((rand() % 100) / 100.0f - 0.5f) * initialSpeed);
        }
    }

    // Flat Solver: Physics system handles all movement
    // No manual position integration needed - velocity is applied by physics solver
    RigidActor::update(deltaTime);
}

void BallActor::draw(pr32::graphics::Renderer& renderer) {
    if(isEnabled) 
        renderer.drawFilledCircle((int)position.x + radius, (int)position.y + radius, radius, Color::White);
}

pixelroot32::core::Rect BallActor::getHitBox() {
    return { {position.x, position.y}, radius * 2, radius * 2 };
}

/**
 * @brief Collision handler with arcade-style physics
 * 
 * PADDLE Collisions - "English" Physics:
 * When ball hits paddle, the impact position relative to paddle center
 * determines the bounce angle. This gives players directional control:
 * 
 * - Hit paddle top: Ball angles upward
 * - Hit paddle center: Ball reflects horizontally  
 * - Hit paddle bottom: Ball angles downward
 * 
 * Mathematical Model:
 * normalizedHit = (ballY - paddleCenter) / (paddleHeight / 2)
 * bounceAngle = normalizedHit * maxAngle (75 degrees)
 * 
 * Additionally, each paddle hit increases speed 5% (max 3x initial),
 * creating escalating rally intensity.
 * 
 * WALL Collisions:
 * No manual velocity inversion needed! Flat Solver's velocity solver
 * automatically reflects velocity when restitution=1.0. We only play sound.
 */
void BallActor::onCollision(pr32::core::Actor* other) {
    // Track bounce for physics validation metrics
    PongPhysicsValidator::getInstance().onBounce();
    
    if (other->isInLayer(Layers::PADDLE)) {
        // Arcade "english" physics: angle based on hit position
        
        // 1. Calculate relative hit position on paddle
        float paddleCenterY = static_cast<float>(other->position.y) + static_cast<float>(other->height) / 2.0f;
        float intersectY = static_cast<float>(position.y) - paddleCenterY;
        float normalizedIntersectY = intersectY / (static_cast<float>(other->height) / 2.0f);
        
        // Clamp to valid range [-1, 1]
        if (normalizedIntersectY > 1.0f) normalizedIntersectY = 1.0f;
        if (normalizedIntersectY < -1.0f) normalizedIntersectY = -1.0f;

        // 2. Calculate bounce angle (max 75 degrees from horizontal)
        float bounceAngle = normalizedIntersectY * (3.14159f * 5.0f / 12.0f);

        // 3. Determine horizontal direction (toward opponent)
        float paddleCenterX = static_cast<float>(other->position.x) + static_cast<float>(other->width) / 2.0f;
        int directionX = (static_cast<float>(position.x) < paddleCenterX) ? -1 : 1;
        
        // 4. Maintain or increase speed
        float currentSpeed = std::sqrt(static_cast<float>(velocity.x * velocity.x + velocity.y * velocity.y));
        if (currentSpeed < initialSpeed) currentSpeed = initialSpeed;
        
        // Escalation: 5% speed increase per hit
        currentSpeed *= 1.05f;
        
        // Cap at 3x initial speed to prevent unplayable velocities
        if (currentSpeed > initialSpeed * 3.0f) currentSpeed = initialSpeed * 3.0f;

        // Apply calculated velocity
        velocity.x = pr32::math::toScalar(currentSpeed * std::cos(bounceAngle) * directionX);
        velocity.y = pr32::math::toScalar(currentSpeed * std::sin(bounceAngle));

        // Audio feedback
        pr32::audio::AudioEvent bounceEv{};
        bounceEv.type = pr32::audio::WaveType::PULSE;
        bounceEv.frequency = 600.0f + (std::rand() % 100);
        bounceEv.duration = 0.05f;
        bounceEv.volume = 0.6f;
        bounceEv.duty = 0.5f;
        engine.getAudioEngine().playEvent(bounceEv);
        
    } else if (other->isInLayer(Layers::WALL)) {
        // Wall bounces are AUTOMATIC with Flat Solver
        // No manual velocity.y *= -1 needed!
        
        pr32::audio::AudioEvent wallEv{};
        wallEv.type = pr32::audio::WaveType::PULSE;
        wallEv.frequency = 400.0f;
        wallEv.duration = 0.05f;
        wallEv.volume = 0.5f;
        wallEv.duty = 0.5f;
        engine.getAudioEngine().playEvent(wallEv);
    }
}

}
