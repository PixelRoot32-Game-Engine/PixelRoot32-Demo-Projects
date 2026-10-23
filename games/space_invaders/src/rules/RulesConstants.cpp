#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

int8_t alienWidth(AlienType t) {
    // The three reference masks are 8, 11 and 12 pixels wide (S2); the edge
    // check needs the true width, so it must never be a single shared value.
    switch (t) {
        case AlienType::Squid:
            return 8;
        case AlienType::Crab:
            return 11;
        case AlienType::Octopus:
            return 12;
    }
    return 8;
}

int8_t alienHeight(AlienType t) {
    // All three variants are eight pixels tall, but the accessor keeps the
    // constant out of the callers so revised art touches one place only.
    (void)t;
    return 8;
}

uint8_t alienScore(AlienType t) {
    switch (t) {
        case AlienType::Squid:
            return 30;
        case AlienType::Crab:
            return 20;
        case AlienType::Octopus:
            return 10;
    }
    return 10;
}

AlienType alienTypeForRow(int row) {
    // Row 0 is the squid, rows 1-2 the crab and rows 3-4 the octopus (S4).
    // Out-of-range rows clamp to the nearest band instead of reading past the
    // table, so a caller with a bad index still gets a drawable variant.
    if (row <= 0) {
        return AlienType::Squid;
    }
    if (row <= 2) {
        return AlienType::Crab;
    }
    return AlienType::Octopus;
}

}  // namespace spaceinvaders::rules
