#include "game/systems/CityCollision.h"

#include <physics/TileAttributes.h>
#include <physics/TilePixelCollision.h>

#include "game/CityConstants.h"
#include "game/rules/Sight.h"
#include "generated/tilemaps/city_scene.h"
#include "generated/tilemaps/corner_shop.h"
#include "generated/tilemaps/police_station.h"

namespace pr32 = pixelroot32;

namespace top_down_city {
namespace collision {

namespace gfx  = pr32::graphics;
namespace phys = pr32::physics;

namespace {

BlockerFn   gBlocker = nullptr;
const void* gBlockerContext = nullptr;

/**
 * @brief Everything the two static tests need about one space.
 *
 * Four pointers and two numbers, resolved once per space rather than branched
 * on per pixel: the per-pixel loop runs thousands of times a frame and a
 * `space == Interior` test inside it would be paid for every one of them.
 */
struct SpaceData {
    const gfx::TileMap4bpp*      items;
    const phys::TileBehaviorLayer* backgroundFlags;
    const phys::TileBehaviorLayer* itemFlags;
    int widthPx;
    int heightPx;
};

const SpaceData kCity = {
    &scene::items,
    &scene::behavior_layers[scene::BEHAVIOR_LAYER_BACKGROUND],
    &scene::behavior_layers[scene::BEHAVIOR_LAYER_ITEMS],
    kWorldWidth,
    kWorldHeight,
};

/// One row per Space, in the enum's order -- a table rather than a chain of
/// comparisons, so a third room is a third line here and nothing else. The
/// static_assert is what makes that true: an enum value with no row would
/// otherwise index past the end and read whatever follows it in .rodata.
const SpaceData kSpaces[] = {
    kCity,
    {
        &station::items,
        &station::behavior_layers[station::BEHAVIOR_LAYER_BACKGROUND],
        &station::behavior_layers[station::BEHAVIOR_LAYER_ITEMS],
        station::MAP_WIDTH  * station::TILE_SIZE,
        station::MAP_HEIGHT * station::TILE_SIZE,
    },
    {
        &shop_room::items,
        &shop_room::behavior_layers[shop_room::BEHAVIOR_LAYER_BACKGROUND],
        &shop_room::behavior_layers[shop_room::BEHAVIOR_LAYER_ITEMS],
        shop_room::MAP_WIDTH  * shop_room::TILE_SIZE,
        shop_room::MAP_HEIGHT * shop_room::TILE_SIZE,
    },
};

static_assert(sizeof(kSpaces) / sizeof(kSpaces[0])
                  == static_cast<std::size_t>(Space::Count),
              "every Space needs a row in kSpaces");

Space           gSpace = Space::City;
const SpaceData* gData = &kSpaces[0];

/// The edge of the map is a wall regardless of tile data: an out-of-bounds
/// read reports TILE_NONE, which would otherwise mean "walkable". Indoors it
/// is the room's own wall doing the work and this is only a backstop -- but a
/// backstop that is wrong by a factor of eight would let a player walk out of
/// a fifteen-tile room into unmapped memory.
bool insideWorld(int left, int top, int right, int bottom) {
    return left >= 0 && top >= 0 &&
           right < gData->widthPx && bottom < gData->heightPx;
}

/// Background carries TILE_SOLID on water outdoors and on the room's walls
/// indoors. Either way the tile is opaque edge to edge, so a whole-tile flag
/// lookup is exact and costs one array read per cell.
bool terrainIsFree(int left, int top, int right, int bottom) {
    for (int ty = top / kTilePx; ty <= bottom / kTilePx; ++ty) {
        for (int tx = left / kTilePx; tx <= right / kTilePx; ++tx) {
            if (phys::getTileFlags(*gData->backgroundFlags, tx, ty)
                    & phys::TILE_SOLID) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

void setSpace(Space space) {
    gSpace = space;
    gData  = &kSpaces[static_cast<std::uint8_t>(space)];
}

Space currentSpace() { return gSpace; }

bool isInterior(Space space) { return space != Space::City; }

int spaceWidthPx()  { return gData->widthPx; }
int spaceHeightPx() { return gData->heightPx; }

// Items carries every prop and building. Those tiles are drawn on a
// transparent surround, which is what lets isWorldPixelSolid decode the 4bpp
// bitmap and block only where the silhouette actually is. With the terrain
// baked into the prop tile -- the single-layer arrangement this demo started
// with -- every tree would be solid across its whole 16x16 cell and this mode
// would be indistinguishable from the whole-tile one.
bool boxIsFreePerPixel(int left, int top, int width, int height) {
    const int right  = left + width - 1;
    const int bottom = top + height - 1;
    if (!insideWorld(left, top, right, bottom)) {
        return false;
    }
    if (!terrainIsFree(left, top, right, bottom)) {
        return false;
    }

    if constexpr (kCollisionMode == CollisionMode::WholeTile) {
        return boxIsFreeWholeTile(left, top, width, height);
    }

    // Erosion is applied to the prop, not to the mover, so the full box
    // slides past thin silhouettes -- lamp posts, fence rails, the ragged
    // edge of a tree canopy -- instead of catching on a single opaque pixel.
    constexpr int erosion =
        (kCollisionMode == CollisionMode::PerPixelEroded) ? kPropErosionPx : 0;

    // isWorldPixelSolid short-circuits on TILE_NONE, so an empty cell costs
    // only the flag lookup and never touches the bitmap.
    for (int py = top; py <= bottom; ++py) {
        for (int px = left; px <= right; ++px) {
            if (phys::isWorldPixelSolid(
                    gData->items,
                    *gData->itemFlags,
                    px / kTilePx, py / kTilePx,
                    px % kTilePx, py % kTilePx,
                    phys::TILE_SOLID,
                    erosion)) {
                return false;
            }
        }
    }
    return true;
}

bool boxIsFreeWholeTile(int left, int top, int width, int height) {
    const int right  = left + width - 1;
    const int bottom = top + height - 1;
    if (!insideWorld(left, top, right, bottom)) {
        return false;
    }
    if (!terrainIsFree(left, top, right, bottom)) {
        return false;
    }
    for (int ty = top / kTilePx; ty <= bottom / kTilePx; ++ty) {
        for (int tx = left / kTilePx; tx <= right / kTilePx; ++tx) {
            if (phys::getTileFlags(*gData->itemFlags, tx, ty)
                    & phys::TILE_SOLID) {
                return false;
            }
        }
    }
    return true;
}

bool tileBlocksSight(int tileX, int tileY) {
    if (tileX < 0 || tileY < 0
            || tileX >= gData->widthPx / kTilePx
            || tileY >= gData->heightPx / kTilePx) {
        return true;
    }
    if (phys::getTileFlags(*gData->itemFlags, tileX, tileY)
            & phys::TILE_SOLID) {
        return true;
    }
    // Background is water outdoors and walls indoors -- see the header. The
    // sea is not cover; a wall is.
    return isInterior(gSpace)
        && (phys::getTileFlags(*gData->backgroundFlags, tileX, tileY)
            & phys::TILE_SOLID) != 0;
}

namespace {

/// tileBlocksSight in the shape sight::isClear asks for. A free function
/// rather than a lambda because sight::SolidFn is a plain function pointer:
/// the rule has no std::function and no capture, on purpose.
bool sightBlocker(const void* /*context*/, int tileX, int tileY) {
    return tileBlocksSight(tileX, tileY);
}

}  // namespace

bool hasLineOfSight(int fromX, int fromY, int toX, int toY) {
    return sight::isClear(fromX, fromY, toX, toY, kTilePx,
                          sightBlocker, nullptr);
}

void setDynamicBlocker(BlockerFn fn, const void* context) {
    gBlocker = fn;
    gBlockerContext = context;
}

bool boxIsBlocked(int left, int top, int width, int height,
                  const void* ignore) {
    if (gBlocker == nullptr) {
        return false;
    }
    return gBlocker(gBlockerContext, left, top, width, height, ignore);
}

}  // namespace collision
}  // namespace top_down_city
