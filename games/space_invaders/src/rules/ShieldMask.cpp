#include "rules/ShieldMask.h"

namespace spaceinvaders::rules {

void ShieldField::reset() {
    // RED: implemented in T1.3
}

bool ShieldField::solid(int shield, int x, int y) const {
    (void)shield;
    (void)x;
    (void)y;
    return false;  // RED: implemented in T1.3
}

void ShieldField::carve(int shield, int x, int y) {
    (void)shield;
    (void)x;
    (void)y;
    // RED: implemented in T1.3
}

bool ShieldField::destroyed(int shield) const {
    (void)shield;
    return false;  // RED: implemented in T1.3
}

uint32_t ShieldField::rowBits(int shield, int y) const {
    (void)shield;
    (void)y;
    return 0;  // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
