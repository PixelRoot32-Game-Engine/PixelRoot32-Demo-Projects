#pragma once

#include "game/DialogScripts.h"
#include "game/GameState.h"

#include <cstdint>

namespace legend_of_clone {

/// What the dungeon chest pays.
inline constexpr uint16_t kChestRupees = 40;

/// The old man's speech before the sword, his directions after it (R4).
pixelroot32::gameplay::LineId oldManFirstLine(const GameState& state);

/**
 * @brief Pays the chest's rupees and marks it open.
 * @return false, changing nothing, when the chest is already open.
 */
bool openChest(GameState& state);

} // namespace legend_of_clone
