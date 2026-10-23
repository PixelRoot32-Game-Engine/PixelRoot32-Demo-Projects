#include "rules/FireTiming.h"

namespace spaceinvaders::rules {

uint8_t enemyReloadTicks(uint16_t scoreHighByte) {
    (void)scoreHighByte;
    return 0;  // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
