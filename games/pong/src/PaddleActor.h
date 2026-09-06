#pragma once
#include "physics/KinematicActor.h"
#include "GameLayers.h"
#include "graphics/Color.h"

namespace pong {

/**
 * @class PaddleActor
 * @brief Player/AI-controlled paddle with vertical movement
 * 
 * Architecture Pattern:
 * Demonstrates KinematicActor for programmatically-controlled entities.
 * Unlike RigidActor (physics-driven), KinematicActor allows direct position
 * manipulation while maintaining collision detection capabilities.
 * 
 * Dual Control Modes:
 * 1. PLAYER: Direct velocity input from controller
 * 2. AI: Proportional tracking algorithm with target offset
 * 
 * AI Algorithm:
 * Uses proportional control (P-controller) to track ball Y position:
 * - Calculates vertical difference from ball
 * - Applies random offset for "imperfection" (prevents perfect play)
 * - Velocity proportional to distance (faster when ball is far)
 * - Clamped to max speed for fairness
 * 
 * Accumulator Pattern:
 * Movement uses floating-point accumulator for sub-pixel precision.
 * This allows smooth movement even at low speeds where deltaTime
 * would otherwise cause jittery integer-only movement.
 * 
 * Boundary Constraints:
 * Paddles are clamped to play area (not full screen) to prevent
 * blocking the edge completely, maintaining gameplay flow.
 */
class PaddleActor : public pixelroot32::physics::KinematicActor {
public:
    float velocity;      // Current vertical velocity (player mode)
    float accumulator;   // Sub-pixel movement accumulator (AI mode)
    bool isAI;           // Control mode: true=AI, false=player
    pixelroot32::graphics::Color color;

    PaddleActor(pixelroot32::math::Vector2 position, int w, int h, bool ai = false, pixelroot32::graphics::Color c = pixelroot32::graphics::Color::White)
        : pixelroot32::physics::KinematicActor(position, w, h), velocity(0), accumulator(0), isAI(ai), color(c) {
        setRenderLayer(1);
        setCollisionLayer(Layers::PADDLE);
        setCollisionMask(Layers::BALL);
        setShape(pixelroot32::core::CollisionShape::AABB);
    }

    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    pixelroot32::core::Rect getHitBox() override { return {position, width, height}; }

    void onCollision(pixelroot32::core::Actor* other) override;

    /**
     * @brief Sets upper movement boundary
     * @param limit Y coordinate of top boundary
     */
    void setTopLimit(int limit) { topLimit = limit; }
    
    /**
     * @brief Sets lower movement boundary
     * @param limit Y coordinate of bottom boundary
     */
    void setBottomLimit(int limit) { bottomLimit = limit; }

private:
    int topLimit;       // Upper Y boundary (play area top)
    int bottomLimit;    // Lower Y boundary (play area bottom)
};

}
