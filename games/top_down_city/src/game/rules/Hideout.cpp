#include "game/rules/Hideout.h"

namespace top_down_city::hideout {

Burn clear() {
    Burn burn;
    burn.stepsLeft = 0;
    return burn;
}

Burn onEntry(std::uint8_t stars, bool seenAtDoor) {
    Burn burn = clear();
    if (stars > 0 && seenAtDoor) {
        burn.stepsLeft = kBurnSteps;
    }
    return burn;
}

void tick(Burn& burn) {
    if (burn.stepsLeft > 0) {
        --burn.stepsLeft;
    }
}

bool watching(const Burn& burn) {
    return burn.stepsLeft > 0;
}

}  // namespace top_down_city::hideout
