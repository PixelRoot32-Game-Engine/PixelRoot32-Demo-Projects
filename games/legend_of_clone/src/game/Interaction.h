#pragma once

#include "game/Facing.h"

#include <cstdint>

namespace legend_of_clone {

/**
 * @enum Interactable
 * @brief What the player can talk to or open by facing it and pressing A.
 *
 * The scene maps tile ids to these, because tile ids are asset-export
 * constants and this header must not include the export. Every object is a
 * solid static tile, so an interaction is a grid lookup, not a sensor: the
 * game builds with PIXELROOT32_ENABLE_PHYSICS=0, and InteractionTracker needs
 * physics.
 */
enum class Interactable : uint8_t { None = 0, Sign, OldMan, Chest, Shopkeeper };

/// A cell in world tile coordinates. May be off the map.
struct GridCell {
    int col;
    int row;
};

/**
 * @brief The world tile under the centre of a player sprite at (pixelX, pixelY).
 *
 * The player moves a pixel at a time, so the centre is what decides which
 * cell they are in: up to half a tile off to either side still counts.
 */
GridCell playerCellAt(int pixelX, int pixelY);

/**
 * @brief The cell one step from `standing` in the `facing` direction.
 *
 * Not clamped. Reading an off-map cell is the tile world's job, and it
 * answers "nothing here".
 */
GridCell facingCell(GridCell standing, Facing facing);

} // namespace legend_of_clone
