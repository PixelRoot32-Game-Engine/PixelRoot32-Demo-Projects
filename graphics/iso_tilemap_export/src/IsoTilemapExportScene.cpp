#include "IsoTilemapExportScene.h"

#include <math/Projection.h>
#include <math/Scalar.h>
#include <math/Vector2.h>
#include <graphics/SpanTable.h>

#include "assets/tilemap/isometric_scene_main_scene.h"
#include "assets/sprites/player_palette.h"

namespace gfx = pixelroot32::graphics;
namespace math = pixelroot32::math;
namespace scene_assets = isometric_scene::main_scene;

namespace iso_tilemap_export {

namespace {

/// Where the player starts. Chosen for two reasons, both of which are worth
/// stating because neither is arbitrary:
///
///   - It is LAND. GROUND_INDICES has a tile here; the row is one of the wide
///     ones, so all four neighbours are walkable and every direction can be
///     tried from the first frame.
///   - It projects to (272, 184), well inside the map's drawn extent on every
///     side, so there is room to scroll in every direction from the start.
constexpr int kSpawnCellX = 4;
constexpr int kSpawnCellY = 16;

}  // namespace

void IsoTilemapExportScene::init() {
    pixelroot32::core::Scene::init();

    // Everything the tilemap needs -- geometry, tile indices, tileset, tile
    // count, per-tile foot anchor -- comes from the export. The example adds
    // no asset data of its own, and no corrections either.
    scene_assets::init();

    // Span-limited blit wiring (change iso-perf-blit-fastpath). The exported
    // tileset descriptors are `static const` + PIXELROOT32_SCENE_FLASH_ATTR,
    // so they live in flash on ESP32 and their rowMinX/rowMaxX cannot be
    // written in place (writing flash triggers a LoadStoreError). We copy the
    // two Sprite4bpp DESCRIPTORS into RAM, compute the per-row opaque span
    // with computeSpanTable (which reads pixel data through
    // PIXELROOT32_READ_BYTE_P, so flash reads are safe), and repoint
    // ground.tiles at the RAM copies. The pixel data itself stays in flash;
    // only the 16-byte descriptors move.
    {
        static gfx::Sprite4bpp tilesetRam[scene_assets::TILESET_TILE_COUNT];
        static uint8_t spanMinX[scene_assets::TILESET_TILE_COUNT][32];
        static uint8_t spanMaxX[scene_assets::TILESET_TILE_COUNT][32];
        for (int i = 0; i < scene_assets::TILESET_TILE_COUNT; ++i) {
            tilesetRam[i] = scene_assets::ground.tiles[i];
            gfx::computeSpanTable(tilesetRam[i], spanMinX[i], spanMaxX[i]);
            tilesetRam[i].rowMinX = spanMinX[i];
            tilesetRam[i].rowMaxX = spanMaxX[i];
        }
        scene_assets::ground.tiles = tilesetRam;
    }

    // The player's frames are 4bpp: every pixel in them names a palette INDEX,
    // and the table those indices are resolved against lives in a sprite
    // palette slot, not in the export. Installing it here rather than inside
    // the actor keeps the actor drawing-only, and keeps the one call that has
    // to happen before the first frame in the one place that already runs
    // before the first frame.
    registerPalette();

    // AFTER init(), not before: the spawn is validated against the ground
    // layer's indices, and init() is what points that layer at its data.
    player_.spawn(kSpawnCellX, kSpawnCellY);

    // Same reason -- the call reads the layer's tileset and foot table to
    // work out how far the drawn map really reaches. Once, HERE and never per
    // frame: the box is a property of the export, and the export does not
    // change between frames.
    //
    // The box is taken over the whole cell rectangle, not just the cells the
    // layer fills -- the ground's empty cells do not shrink the map, they just
    // show whatever is behind them.
    gfx::expandProjectedMapBounds(world_, scene_assets::ground,
                                  scene_assets::ISO_PROJECTION);
}

void IsoTilemapExportScene::update(unsigned long deltaTime) {
    pixelroot32::core::Scene::update(deltaTime);

    player_.update(deltaTime);
}

void IsoTilemapExportScene::draw(gfx::Renderer& renderer) {
    pixelroot32::core::Scene::draw(renderer);

    // Aimed here rather than in update() so the viewport is read from the
    // renderer that is about to be drawn into, instead of a copy of the
    // display size kept somewhere else and left to drift.
    const int viewW = renderer.getLogicalWidth();
    const int viewH = renderer.getLogicalHeight();
    camera_.setViewportSize(viewW, viewH);

    // Bounds BEFORE position, and that order is load-bearing rather than
    // tidy: `Camera2D::setPosition` clamps against whatever bounds the camera
    // is already holding. Set the position first and it gets clamped against
    // last frame's range -- and on the very first frame against the {0, 0}
    // both bounds are constructed with, which pins the camera to the origin
    // and makes the map look like it refuses to scroll.
    //
    // `cameraRangeFor` is the conversion, and it is not a rename. `world_` is
    // HALF-OPEN -- `right`/`bottom` are one past the last covered pixel, the
    // same convention a tile blit uses -- while the range it returns is
    // CLOSED, every end of it a camera position the camera may actually sit
    // at. It is also what keeps an axis whose world is SMALLER than the
    // viewport centred rather than jammed against one edge: setPosition
    // applies the min clamp and then the max clamp unconditionally, so an
    // inverted range would resolve to the max end every time, silently.
    const gfx::CameraBounds range = gfx::cameraRangeFor(world_, viewW, viewH);
    camera_.setBounds(math::toScalar(range.minX), math::toScalar(range.maxX));
    camera_.setVerticalBounds(math::toScalar(range.minY), math::toScalar(range.maxY));

    // Centred on the player by hand, deliberately NOT through
    // `followTarget`. followTarget keeps a 30%/70% dead zone the target has to
    // push against before the view moves at all -- a defensible camera, but a
    // different one from the always-centred view this example has always had.
    // Adopting the engine camera is not the moment to quietly change how the
    // example feels.
    camera_.setPosition(math::Vector2(math::toScalar(player_.screenX() - viewW / 2),
                                      math::toScalar(player_.screenY() - viewH / 2)));

    // This is the whole of scrolling. `apply` writes the camera's position,
    // NEGATED, into the renderer's display offset, and every primitive issued
    // afterwards is translated by it -- the tilemap and the player's sprite
    // alike, from one place, with no call site carrying the camera by hand.
    camera_.apply(renderer);

    // There is a single layer, so this one call paints the whole scene.
    // WITHIN it the projected overload relies on row-major iteration already
    // being back to front -- which is what the header's
    // `rowMajorIsPainterOrder` static_assert guarantees for this basis. There
    // is no depth sort anywhere in the path.
    //
    // The origin is ZERO, and it has to be. The camera is already live in the
    // renderer's display offset by this point; ISO_PROJECTION carries the
    // map's own western inset in its origin fields, and the projected overload
    // adds the origin argument to that. Passing the camera here as well would
    // count it twice and scroll the map at double speed. The exported spec is
    // never touched either way, and the culling that reads it follows the
    // offset for free.
    //
    // LayerType::Dynamic because the ground is the ONLY layer: nothing else
    // repaints behind the player, so the cells it walks off have to be
    // repainted by this very map. The dirty grid governs what gets FLUSHED,
    // not what gets drawn: the map repaints over the pixels the player
    // occupied last frame either way, but those pixels never reach the display
    // unless something marks them. Switching this layer to Static would smear
    // the player across the map.
    renderer.drawTileMap(scene_assets::ground, 0, 0,
                         gfx::LayerType::Dynamic,
                         scene_assets::ISO_PROJECTION);

    // Last, so it lands on top of the map. It needs no camera argument and
    // cannot disagree with the map about one: the same display offset the
    // tilemap was drawn under is still in force. Not occluded by tiles in
    // front of it, though -- an isometric game eventually wants that, and it
    // needs a depth comparison this example does not have.
    player_.draw(renderer);
}

void IsoTilemapExportScene::registerPalette() {
    // Tilemaps resolve through the background bank, sprites through the sprite
    // bank. The player's palette goes into its own slot (kPaletteSlot), not
    // slot 0, which would overwrite the global sprite palette.
    gfx::setBackgroundCustomPalette(scene_assets::CUSTOM_PALETTE_DATA);
    gfx::setSpriteCustomPaletteSlot(player_sprites::kPaletteSlot,
                                    player_sprites::PLAYER_SPRITE_PALETTE_RGB565);
}

}  // namespace iso_tilemap_export
