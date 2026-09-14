#pragma once

#include <cstdint>
#include <type_traits>

namespace legend_of_clone {

/**
 * @struct GameState
 * @brief Story flags and inventory that must outlive a scene change.
 *
 * The engine reruns Scene::init() on every scene swap, so nothing a scene
 * holds survives the cave doorway. This struct lives in GameSession.cpp
 * instead, beside the two scenes rather than inside either.
 *
 * Deliberately a POD with no default member initializers: a future save phase
 * can write and read it as bytes without reshaping it. Value-initialize it
 * (`GameState{}`) to get a new game.
 */
struct GameState {
    bool     hasSword;     ///< Given by the old man.
    bool     chestOpened;  ///< The dungeon chest has paid out.
    bool     hasShield;    ///< Bought once.
    bool     hasKey;       ///< Bought once.
    uint8_t  potions;      ///< Bought any number of times.
    uint16_t rupees;       ///< The only currency.
};

static_assert(std::is_trivial_v<GameState> && std::is_standard_layout_v<GameState>,
              "GameState must stay a POD for the save phase");

} // namespace legend_of_clone
