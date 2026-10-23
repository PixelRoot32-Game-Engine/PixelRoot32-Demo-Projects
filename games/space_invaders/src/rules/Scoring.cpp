#include "rules/Scoring.h"

namespace spaceinvaders::rules {

void Scoring::reset() {
    // RED: implemented in T1.3
}

void Scoring::add(uint16_t points) {
    (void)points;
    // RED: implemented in T1.3
}

uint16_t Scoring::score() const {
    return 0;  // RED: implemented in T1.3
}

uint8_t Scoring::highByte() const {
    return 0;  // RED: implemented in T1.3
}

uint8_t Scoring::lives() const {
    return 0;  // RED: implemented in T1.3
}

void Scoring::loseLife() {
    // RED: implemented in T1.3
}

bool Scoring::consumeExtraShip() {
    return false;  // RED: implemented in T1.3
}

bool Scoring::extraShipAwarded() const {
    return false;  // RED: implemented in T1.3
}

uint16_t Scoring::wave() const {
    return 0;  // RED: implemented in T1.3
}

void Scoring::nextWave() {
    // RED: implemented in T1.3
}

uint16_t Scoring::highScore() const {
    return 0;  // RED: implemented in T1.3
}

void Scoring::commitHighScore() {
    // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
