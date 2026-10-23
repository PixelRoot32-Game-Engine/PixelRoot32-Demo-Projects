#include "rules/FireTiming.h"

namespace spaceinvaders::rules {

uint8_t enemyReloadTicks(uint16_t scoreHighByte) {
    // The ROM ladder is 48, 16, 11, 8, 7 at 0x02, 0x10, 0x20 and 0x30 (S9).
    // Comparing the highest threshold first keeps each band inclusive of its
    // lower bound and lets the top band cover the rest of the byte range.
    if (scoreHighByte >= 0x30) {
        return 7;
    }
    if (scoreHighByte >= 0x20) {
        return 8;
    }
    if (scoreHighByte >= 0x10) {
        return 11;
    }
    if (scoreHighByte >= 0x02) {
        return 16;
    }
    return 48;
}

}  // namespace spaceinvaders::rules
