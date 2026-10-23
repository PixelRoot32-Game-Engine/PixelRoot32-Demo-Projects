#include "rules/Saucer.h"

namespace spaceinvaders::rules {

void Saucer::reset() {
    // RED: implemented in T1.3
}

void Saucer::tick() {
    // RED: implemented in T1.3
}

bool Saucer::spawnDue(uint8_t aliveCount) const {
    (void)aliveCount;
    return false;  // RED: implemented in T1.3
}

void Saucer::onSpawned() {
    // RED: implemented in T1.3
}

int8_t Saucer::side(uint32_t shotsFired) const {
    (void)shotsFired;
    return 0;  // RED: implemented in T1.3
}

uint16_t Saucer::currentScore() const {
    return 0;  // RED: implemented in T1.3
}

uint16_t Saucer::takeScore() {
    return 0;  // RED: implemented in T1.3
}

uint16_t Saucer::timer() const {
    return 0;  // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
