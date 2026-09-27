#pragma once

#include "TopDownScene.h"

#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES

namespace legend_of_clone {

/**
 * @class DungeonScene
 * @brief Four rooms underground, entered from the overworld's cave and left by
 *        the staircase you arrived beside.
 *
 * The point of this scene is that it is almost empty. Rooms, doorways, the
 * scrolling change between them, collision, the player and his walk cycle are
 * all TopDownScene's; a dungeon differs from an overworld in its map, its
 * tiles and what its special tiles do, and that is all this file contains.
 *
 * It also holds the slice's three interactable tiles -- the old man, the chest
 * and the shopkeeper -- and draws the chest open once it has paid out. No
 * enemies and no locked doors yet.
 */
class DungeonScene : public TopDownScene {
protected:
    Setup setup() override;
    void drawStatusBar(pixelroot32::graphics::Renderer& renderer) override;
    void onPlayerSettled() override;
    Interactable interactableAt(int col, int row) const override;
    pixelroot32::graphics::TileMap4bppDrawSpec overlayLayer() const override;

private:
    /// Points at the exported dungeon map. Three pointers, no pixel data.
    TileWorld world_;

    /// The chest's cell, found by scanning the map at setup(); -1 when absent.
    int chestCol_ = -1;
    int chestRow_ = -1;

    /**
     * A one-cell map holding TILE_CHEST_OPEN over the dungeon's own tileset.
     *
     * The exported indices are const flash, so the chest cell cannot be
     * rewritten. This is drawn over it instead, as a dynamic layer, once
     * GameState::chestOpened is set. Collision still reads the export, where
     * the closed chest is solid, and the open one is solid too.
     */
    uint8_t openChestIndex_ = 0;
    pixelroot32::graphics::TileMap4bpp openChest_{};

    void locateChest();
};

} // namespace legend_of_clone

#endif // PIXELROOT32_ENABLE_4BPP_SPRITES
