#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

int8_t alienWidth(AlienType t) {
    (void)t;
    return 0;  // RED: implemented in T1.3
}

int8_t alienHeight(AlienType t) {
    (void)t;
    return 0;  // RED: implemented in T1.3
}

uint8_t alienScore(AlienType t) {
    (void)t;
    return 0;  // RED: implemented in T1.3
}

AlienType alienTypeForRow(int row) {
    (void)row;
    return AlienType::Squid;  // RED: implemented in T1.3
}

}  // namespace spaceinvaders::rules
