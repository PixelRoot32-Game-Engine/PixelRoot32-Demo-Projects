/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 * 
 * PONG Physics Validator
 * Validates PONG-specific physics behavior
 */
#pragma once
#include <cstdint>
#include <cmath>

namespace pong {

/**
 * @struct PongMetrics
 * @brief Real-time metrics for PONG physics validation
 */
struct PongMetrics {
    // Energy tracking
    float initialBallSpeed = 0.0f;
    float currentBallSpeed = 0.0f;
    float maxBallSpeed = 0.0f;
    float minBallSpeed = 0.0f;
    float totalEnergyDeviation = 0.0f;
    uint32_t bounceCount = 0;
    
    // Position tracking
    float maxPositionDriftY = 0.0f;  // Y position drift from expected
    float expectedY = 0.0f;
    
    // Frame tracking
    uint32_t totalFrames = 0;
    uint32_t framesStuck = 0;
    uint32_t framesOutOfBounds = 0;
    
    // Validation
    bool isEnergyConserved() const {
        return std::abs(currentBallSpeed - initialBallSpeed) < (initialBallSpeed * 0.02f);
    }
    
    bool isDeterministic() const {
        // Position should be within 1px of expected
        return maxPositionDriftY < 1.0f;
    }
    
    bool isStable() const {
        return framesStuck == 0 && framesOutOfBounds == 0;
    }
    
    float getEnergyLossPercent() const {
        if (initialBallSpeed == 0.0f) return 0.0f;
        return ((initialBallSpeed - currentBallSpeed) / initialBallSpeed) * 100.0f;
    }
};

/**
 * @class PongPhysicsValidator
 * @brief Validates physics specifically for PONG
 */
class PongPhysicsValidator {
public:
    static PongPhysicsValidator& getInstance();
    
    // Call at game start
    void startTracking(float initialBallSpeed);
    
    // Call every frame with ball state
    void update(float ballX, float ballY, float velX, float velY, bool isActive);
    
    // Call on collision
    void onBounce();
    
    // Call on game reset
    void onReset();
    
    // Get current metrics
    const PongMetrics& getMetrics() const { return metrics; }
    
    // Validation checks
    bool validateEnergyConservation() const;
    bool validateNoSticking() const;
    bool validateBounceConsistency() const;
    
    // Report
    void printReport() const;
    
    // Reset all
    void reset();
    
private:
    PongPhysicsValidator() = default;
    PongMetrics metrics;
    
    float lastBallY = 0.0f;
    uint32_t stuckFrameCount = 0;
    static constexpr float STUCK_THRESHOLD = 0.01f;
    static constexpr float MAX_ENERGY_LOSS_PERCENT = 2.0f; // 2% max
};

}
