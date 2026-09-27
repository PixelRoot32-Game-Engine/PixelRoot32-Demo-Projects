#pragma once

#include <cstdint>

namespace legend_of_clone {

/**
 * @enum Facing
 * @brief The four directions the player sprite can face.
 *
 * Kept apart from PlayerActor.h so the interaction rules can name a direction
 * without including an engine Entity.
 */
enum class Facing : uint8_t { Down = 0, Up, Left, Right };

} // namespace legend_of_clone
