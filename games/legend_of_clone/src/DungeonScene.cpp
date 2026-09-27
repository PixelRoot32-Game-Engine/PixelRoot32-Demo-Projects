#include "DungeonScene.h"

#ifdef PIXELROOT32_ENABLE_4BPP_SPRITES

#include "core/Engine.h"
#include "graphics/Color.h"

#include "GameSession.h"
#include "Scenes.h"
#include "assets/DungeonTileMap.h"
#include "assets/DungeonRooms.h"
#include "assets/PlayerPalette.h"
#include "assets/TilemapPalette.h"

#include <cstdio>

namespace pr32 = pixelroot32;
extern pr32::core::Engine engine;

namespace legend_of_clone {

namespace gfx = pr32::graphics;

static_assert(dungeon::MAP_WIDTH == kWorldCols, "exported map is not kWorldCols wide");
static_assert(dungeon::MAP_HEIGHT == kWorldRows, "exported map is not kWorldRows tall");
static_assert(dungeon::TILE_SIZE == kTileSize, "exported tiles are not kTileSize");

TopDownScene::Setup DungeonScene::setup() {
    dungeon::init();
    world_.attach(&dungeon::terrain, dungeon::TILE_SOLID, dungeon::TILE_COUNT);
    locateChest();

    Setup config;
    config.backgroundPalette = TILEMAP_PALETTE_DATA;
    config.spritePalette     = PLAYER_SPRITE_PALETTE_RGB565;
    config.roomLayer         = &DUNGEON_ROOM_LAYER;
    config.world             = &world_;
    config.startRoom         = kRoomSouthWest;   // the entrance room, same slot as the overworld's start
    config.startCol          = kDungeonEntryCol;
    config.startRow          = kDungeonEntryRow;
    // Arriving from below, looking into the dungeon.
    config.startFacing       = Facing::Up;
    return config;
}

void DungeonScene::locateChest() {
    chestCol_ = -1;
    chestRow_ = -1;
    // 660 cells, once per init. Finding the chest in the export keeps its
    // position in one place -- the map -- instead of a second copy here.
    for (int row = 0; row < kWorldRows && chestCol_ < 0; ++row) {
        for (int col = 0; col < kWorldCols; ++col) {
            if (world_.tileAt(col, row) == dungeon::TILE_CHEST_CLOSED) {
                chestCol_ = col;
                chestRow_ = row;
                break;
            }
        }
    }

    openChestIndex_         = dungeon::TILE_CHEST_OPEN;
    openChest_.indices      = &openChestIndex_;
    openChest_.width        = 1;
    openChest_.height       = 1;
    openChest_.tiles        = dungeon::terrain.tiles;
    openChest_.tileWidth    = dungeon::TILE_SIZE;
    openChest_.tileHeight   = dungeon::TILE_SIZE;
    openChest_.tileCount    = dungeon::TILE_COUNT;
    openChest_.runtimeMask  = nullptr;
}

gfx::TileMap4bppDrawSpec DungeonScene::overlayLayer() const {
    if (!gameState.chestOpened || chestCol_ < 0) return { nullptr, 0, 0 };
    return { &openChest_, chestCol_ * kTileSize, chestRow_ * kTileSize };
}

Interactable DungeonScene::interactableAt(int col, int row) const {
    switch (world().tileAt(col, row)) {
        case dungeon::TILE_OLD_MAN:      return Interactable::OldMan;
        case dungeon::TILE_CHEST_CLOSED: return Interactable::Chest;
        case dungeon::TILE_SHOPKEEPER:   return Interactable::Shopkeeper;
        default:                         return Interactable::None;
    }
}

void DungeonScene::onPlayerSettled() {
    int col = 0;
    int row = 0;
    playerCell(col, row);

    if (world().tileAt(col, row) != dungeon::TILE_STAIRS) return;

    // The overworld was told where to put the player on the way in, so it
    // already knows to drop them below the cave rather than at the start of the
    // game. Nothing to arrange here beyond the fade.
    engine.triggerTransition(&overworldScene,
                             gfx::TransitionType::Fade,
                             kDoorwayFadeMs);
}

void DungeonScene::drawStatusBar(gfx::Renderer& renderer) {
    TopDownScene::drawStatusBar(renderer);

    const bool oldBypass = renderer.isOffsetBypassEnabled();
    renderer.setOffsetBypass(true);

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "LEVEL 1  ROOM %u",
                  static_cast<unsigned>(currentRoom()));
    renderer.drawText(buffer, 8, kStatusBarY + 12, gfx::Color::White, 1);
    renderer.drawText("STAIRS TO LEAVE", 8, kStatusBarY + 46, gfx::Color::Gray, 1);

    renderer.setOffsetBypass(oldBypass);

    drawRupees(renderer, kStatusBarY + 30);
}

} // namespace legend_of_clone

#endif // PIXELROOT32_ENABLE_4BPP_SPRITES
