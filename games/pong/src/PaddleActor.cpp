#include "PaddleActor.h"
#include "PongScene.h"  
#include "core/Engine.h"
#include <cmath>
#include "GameLayers.h"
#include "GameConstants.h"

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace pong {

using Color = pr32::graphics::Color;

/**
 * @brief Updates paddle position based on control mode
 * 
 * Control Modes:
 * 
 * PLAYER MODE:
 * Direct velocity application. Input system sets velocity, this method
 * applies it scaled by deltaTime for frame-rate independent movement.
 * 
 * AI MODE - Proportional Control Algorithm:
 * The AI uses a P-controller (proportional controller) to track ball position:
 * 
 * 1. Calculate error: diff = ballY - paddleCenterY
 * 2. Add random offset for "imperfection" (prevents perfect unbeatable AI)
 * 3. Calculate proportional velocity: speed = diff * 1.5
 * 4. Clamp to max speed: speed = clamp(speed, -MAX, +MAX)
 * 5. Apply only if error exceeds threshold (prevents micro-jitter)
 * 
 * The accumulator pattern provides sub-pixel precision for smooth movement
 * even at low speeds or when deltaTime is small.
 * 
 * Boundary Enforcement:
 * Both modes clamp position to play area boundaries, resetting accumulator
 * when hitting limits to prevent "stuck" movement state.
 */
void PaddleActor::update(unsigned long deltaTime) {
    float dt = deltaTime / 1000.0f;
    
    if (isAI) {
        // AI Mode: Proportional tracking with imperfection
        PongScene* pongScene = static_cast<PongScene*>(engine.getCurrentScene().value_or(nullptr));

        if (pongScene) {
            float ballX = pongScene->getBallX();
            float ballY = pongScene->getBallY();

            float paddleCenterY = static_cast<float>(position.y) + height / 2.0f;
            float diff = ballY - paddleCenterY;

            // AI Imperfection: Random offset prevents perfect play
            // Seed varies with ball position and time for unpredictability
            static uint32_t aiOffsetSeed = 0;
            if (AI_TARGET_OFFSET > 0.0f) {
                if (aiOffsetSeed == 0) aiOffsetSeed = millis();
                float offset = ((float)((aiOffsetSeed + (uint32_t)(ballX*5) + (uint32_t)(millis()/150)) % 200) / 200.0f - 0.5f) * AI_TARGET_OFFSET;
                diff += offset;
                aiOffsetSeed++;
            }

            // Proportional control: faster when ball is far
            float proportionalSpeed = diff * 1.5f;
            if (proportionalSpeed > AI_MAX_SPEED) proportionalSpeed = AI_MAX_SPEED;
            else if (proportionalSpeed < -AI_MAX_SPEED) proportionalSpeed = -AI_MAX_SPEED;

            // Only move if difference exceeds threshold (prevent jitter)
            if (std::abs(diff) > AI_MOVEMENT_THRESHOLD) accumulator += proportionalSpeed * dt;

            // Apply accumulated sub-pixel movement when >= 1 pixel
            if (accumulator >= 1.0f) { 
                int m = (int)accumulator; 
                position.y += pixelroot32::math::toScalar(m); 
                accumulator -= m; 
            }
            else if (accumulator <= -1.0f) { 
                int m = (int)accumulator; 
                position.y += pixelroot32::math::toScalar(m); 
                accumulator -= m; 
            }
        }

    } else {
        // Player Mode: Direct velocity application
        position.y += pixelroot32::math::toScalar(velocity * dt);
    }

    // Enforce play area boundaries
    if (position.y < pixelroot32::math::toScalar(topLimit)) { 
        position.y = pixelroot32::math::toScalar(topLimit); 
        accumulator = 0; 
    }
    if (position.y + pixelroot32::math::toScalar(height) > pixelroot32::math::toScalar(bottomLimit)) { 
        position.y = pixelroot32::math::toScalar(bottomLimit - height); 
        accumulator = 0; 
    }

    // Update kinematic actor state (spatial grid, collision bounds, etc.)
    KinematicActor::update(deltaTime);
}

void PaddleActor::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle((int)position.x, (int)position.y, width, height, color);
}

void PaddleActor::onCollision(pr32::core::Actor* other) {
    (void)other;
    // Collision response handled by BallActor (english physics)
    // Paddle just needs to exist as collision target
}

}
