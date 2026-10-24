#include "rules/Scoring.h"

namespace spaceinvaders::rules {

namespace {

/// Four HUD digits, so the counter saturates instead of rolling over (S12).
constexpr uint16_t MaxScore = 9999;

}  // namespace

void Scoring::reset() {
    // The high score belongs to the session, so a new run leaves it alone.
    scorePoints = 0;
    shipLives = StartLives;
    waveNumber = 1;
    extraShipGiven = false;
}

void Scoring::add(uint16_t points) {
    const uint32_t total = static_cast<uint32_t>(scorePoints) + points;
    scorePoints = total > MaxScore ? MaxScore : static_cast<uint16_t>(total);
}

uint16_t Scoring::score() const {
    return scorePoints;
}

uint8_t Scoring::scoreBand() const {
    return static_cast<uint8_t>(scorePoints >> 8);
}

uint8_t Scoring::lives() const {
    return shipLives;
}

void Scoring::loseLife() {
    if (shipLives > 0) {
        --shipLives;
    }
}

bool Scoring::consumeExtraShip() {
    // One award per run: the latch is what makes later calls false even after
    // the score crosses the threshold again.
    if (extraShipGiven || scorePoints < ExtraShipScore) {
        return false;
    }
    extraShipGiven = true;
    ++shipLives;
    return true;
}

bool Scoring::extraShipAwarded() const {
    return extraShipGiven;
}

uint16_t Scoring::wave() const {
    return waveNumber;
}

void Scoring::nextWave() {
    ++waveNumber;
}

uint16_t Scoring::highScore() const {
    return highScorePoints;
}

void Scoring::commitHighScore() {
    if (scorePoints > highScorePoints) {
        highScorePoints = scorePoints;
    }
}

}  // namespace spaceinvaders::rules
