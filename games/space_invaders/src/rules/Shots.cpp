#include "rules/Shots.h"

namespace spaceinvaders::rules {

int16_t advancePlayerShot(int16_t y) {
    (void)y;
    return 0;  // RED: implemented in T1.3
}

bool enemyShotIsFast(uint8_t aliveCount) {
    (void)aliveCount;
    return false;  // RED: implemented in T1.3
}

void EnemyShotMotion::reset(int16_t y) {
    (void)y;
    // RED: implemented in T1.3
}

int16_t EnemyShotMotion::advance(bool fast) {
    (void)fast;
    return 0;  // RED: implemented in T1.3
}

int16_t EnemyShotMotion::y() const {
    return 0;  // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
