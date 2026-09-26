#include "game/StoryRules.h"

namespace legend_of_clone {

pixelroot32::gameplay::LineId oldManFirstLine(const GameState& state) {
    return state.hasSword ? kOldManDirections : kOldManIntro;
}

bool openChest(GameState& state) {
    if (state.chestOpened) return false;
    state.chestOpened = true;
    state.rupees = static_cast<uint16_t>(state.rupees + kChestRupees);
    return true;
}

} // namespace legend_of_clone
