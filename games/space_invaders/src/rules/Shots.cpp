#include "rules/Shots.h"

namespace spaceinvaders::rules {

namespace {

/// Under nine aliens the reference tightens the enemy shot to its fast step.
constexpr uint8_t FastFireAliveLimit = 9;

}  // namespace

int16_t advancePlayerShot(int16_t y) {
    // Four pixels per tick is the reference speed (F10); the caller owns the
    // off-screen test, so a position below zero is a legal result.
    return static_cast<int16_t>(y - PlayerShotStepY);
}

bool enemyShotIsFast(uint8_t aliveCount) {
    return aliveCount < FastFireAliveLimit;
}

void EnemyShotMotion::reset(int16_t y) {
    position = y;
    tickPhase = 0;
}

int16_t EnemyShotMotion::advance(bool fast) {
    // The step lands on every third call, which is what makes the enemy shot
    // visibly slower than the player's without a separate timer (S9).
    if (tickPhase + 1u >= EnemyShotTicksPerStep) {
        tickPhase = 0;
        position = static_cast<int16_t>(
            position + (fast ? EnemyShotFastStepY : EnemyShotStepY));
    } else {
        ++tickPhase;
    }
    return position;
}

int16_t EnemyShotMotion::step(bool fast) {
    // One whole shot step: the three per-tick advances the old caller had to
    // make now collapse into one call, so the scene can step every third tick
    // without the object introducing a second divisor on top of that.
    tickPhase = 0;
    position = static_cast<int16_t>(
        position + (fast ? EnemyShotFastStepY : EnemyShotStepY));
    return position;
}

int16_t EnemyShotMotion::y() const {
    return position;
}

}  // namespace spaceinvaders::rules
