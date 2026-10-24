#include "rules/Saucer.h"

namespace spaceinvaders::rules {

namespace {

/// The fifteen mystery-ship values in ROM order; the table is cycled, not
/// drawn at random, so a fixed input script reproduces every award (S11, S20).
constexpr uint16_t ScoreTable[SaucerScoreCount] = {
    100, 50, 50, 100, 150, 100, 100, 50, 300, 100, 100, 100, 50, 150, 100,
};

}  // namespace

void Saucer::reset() {
    timerTicks = 0;
    scoreIndex = 0;
}

void Saucer::tick() {
    // Saturate instead of wrapping: a wrap would silently restart the period
    // while the rack is still too small for a saucer to appear.
    if (timerTicks != 0xFFFFu) {
        ++timerTicks;
    }
}

bool Saucer::spawnDue(uint8_t aliveCount) const {
    return timerTicks >= SaucerPeriodTicks && aliveCount >= SaucerMinAliens;
}

void Saucer::onSpawned() {
    timerTicks = 0;
}

int8_t Saucer::side(uint32_t shotsFired) const {
    // Even shot counts enter from the left, so the side is a function of the
    // run's history rather than another random draw (S11).
    return (shotsFired % 2u == 0u) ? static_cast<int8_t>(1) : static_cast<int8_t>(-1);
}

uint16_t Saucer::currentScore() const {
    return ScoreTable[scoreIndex];
}

uint16_t Saucer::takeScore() {
    const uint16_t awarded = ScoreTable[scoreIndex];
    scoreIndex = static_cast<uint8_t>((scoreIndex + 1u) % SaucerScoreCount);
    return awarded;
}

uint16_t Saucer::timer() const {
    return timerTicks;
}

}  // namespace spaceinvaders::rules
