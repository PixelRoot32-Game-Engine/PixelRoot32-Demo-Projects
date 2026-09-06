/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 * 
 * PONG Physics Validator Implementation
 */
#include "PongPhysicsValidator.h"
#include <cstdio>

namespace pong {

PongPhysicsValidator& PongPhysicsValidator::getInstance() {
    static PongPhysicsValidator instance;
    return instance;
}

void PongPhysicsValidator::startTracking(float initialBallSpeed) {
    reset();
    metrics.initialBallSpeed = initialBallSpeed;
    metrics.minBallSpeed = initialBallSpeed;
    metrics.maxBallSpeed = initialBallSpeed;
}

void PongPhysicsValidator::update(float ballX, float ballY, float velX, float velY, bool isActive) {
    if (!isActive) return;
    
    metrics.totalFrames++;
    
    // Calculate current speed
    float speed = std::sqrt(velX * velX + velY * velY);
    metrics.currentBallSpeed = speed;
    
    // Track min/max
    if (speed > metrics.maxBallSpeed) metrics.maxBallSpeed = speed;
    if (speed < metrics.minBallSpeed) metrics.minBallSpeed = speed;
    
    // Calculate energy deviation
    float deviation = std::abs(speed - metrics.initialBallSpeed);
    metrics.totalEnergyDeviation += deviation;
    
    // Detect if ball is stuck (not moving)
    float deltaY = std::abs(ballY - lastBallY);
    if (deltaY < STUCK_THRESHOLD) {
        stuckFrameCount++;
        if (stuckFrameCount > 10) { // Stuck for more than 10 frames
            metrics.framesStuck++;
        }
    } else {
        stuckFrameCount = 0;
    }
    lastBallY = ballY;
    
    // Track position drift from ideal (for determinism check)
    // In an ideal elastic collision system, Y should be predictable
    // For now, just track max deviation from initial trajectory
    float expectedYDrift = std::abs(ballY - metrics.expectedY);
    if (expectedYDrift > metrics.maxPositionDriftY) {
        metrics.maxPositionDriftY = expectedYDrift;
    }
}

void PongPhysicsValidator::onBounce() {
    metrics.bounceCount++;
}

void PongPhysicsValidator::onReset() {
    // Reset tracking but keep metrics for reporting
    lastBallY = 0.0f;
    stuckFrameCount = 0;
}

bool PongPhysicsValidator::validateEnergyConservation() const {
    float lossPercent = metrics.getEnergyLossPercent();
    return std::abs(lossPercent) < MAX_ENERGY_LOSS_PERCENT;
}

bool PongPhysicsValidator::validateNoSticking() const {
    return metrics.framesStuck == 0;
}

bool PongPhysicsValidator::validateBounceConsistency() const {
    if (metrics.bounceCount == 0) return true;
    
    // Check if speed is consistent across bounces
    float avgDeviation = metrics.totalEnergyDeviation / metrics.totalFrames;
    return avgDeviation < (metrics.initialBallSpeed * 0.01f); // Less than 1% avg deviation
}

void PongPhysicsValidator::printReport() const {
    printf("\n=== PONG PHYSICS VALIDATION REPORT ===\n");
    printf("Total Frames: %u\n", metrics.totalFrames);
    printf("Total Bounces: %u\n", metrics.bounceCount);
    printf("\nEnergy Metrics:\n");
    printf("  Initial Speed: %.2f px/s\n", metrics.initialBallSpeed);
    printf("  Current Speed: %.2f px/s\n", metrics.currentBallSpeed);
    printf("  Min Speed: %.2f px/s\n", metrics.minBallSpeed);
    printf("  Max Speed: %.2f px/s\n", metrics.maxBallSpeed);
    printf("  Energy Loss: %.2f%%\n", metrics.getEnergyLossPercent());
    printf("  Energy Conserved: %s\n", validateEnergyConservation() ? "PASS" : "FAIL");
    printf("\nStability Metrics:\n");
    printf("  Max Position Drift: %.2f px\n", metrics.maxPositionDriftY);
    printf("  Frames Stuck: %u\n", metrics.framesStuck);
    printf("  No Sticking: %s\n", validateNoSticking() ? "PASS" : "FAIL");
    printf("\nOverall: %s\n", 
        (validateEnergyConservation() && validateNoSticking()) ? "ALL TESTS PASS" : "TESTS FAILED");
    printf("=====================================\n\n");
}

void PongPhysicsValidator::reset() {
    metrics = PongMetrics();
    lastBallY = 0.0f;
    stuckFrameCount = 0;
}

}
