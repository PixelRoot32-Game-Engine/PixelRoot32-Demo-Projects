#pragma once

#include <cstdint>

#include "gameplay/Projection.h"
#include "graphics/Color.h"

#include "assets/tilemap/iso_dungeon_rooms.h"

/**
 * @file IsoDungeonConstants.h
 * @brief Hero tuning, colours, and the game-side names for the geometry the
 *        tilemap export owns.
 *
 * The whole isometric view of this game is six integers. There is no isometric
 * mode, no diamond-specific renderer and no coordinate class: a
 * `ProjectionSpec` says where cell (0, 0) lands and how far one step along each
 * cell axis moves on screen, and that is the entire model.
 *
 * Those six integers, the room's extent in tiles and the hero's start cell are
 * not authored here. They come from assets/tilemap/iso_dungeon_rooms.h, which
 * the Tilemap Editor exports from the project in editor/, and this file only
 * gives them the names the gameplay code already uses. That direction is
 * deliberate: an export is the single source of truth for a map's geometry,
 * and duplicating it here would let the two drift with nothing to catch it.
 * Re-export, and every static_assert below runs against the new value.
 */

namespace iso_dungeon {

namespace gameplay = pixelroot32::gameplay;

/// Display size in pixels (square 240x240).
inline constexpr int kDisplaySize = 240;

/// Button indices matching the InputConfig order (Up, Down, Left, Right, A, B).
inline constexpr std::uint8_t BTN_UP    = 0;
inline constexpr std::uint8_t BTN_DOWN  = 1;
inline constexpr std::uint8_t BTN_LEFT  = 2;
inline constexpr std::uint8_t BTN_RIGHT = 3;

// --- Geometry ---------------------------------------------------------------

/// Every exported room (rooms::, the export's namespace: three 7x7 rooms side
/// by side in one scene) has the same square extent as room 0.
///
/// The layouts in RoomCatalog.h are sized by one constant, so the editor's
/// rooms have to agree before the game can use a single name for them. The
/// scene itself is wider than a room (MAP_WIDTH is all three side by side);
/// what the game sizes by is a ROOM, which is what this checks.
constexpr bool exportedRoomsAreSquareAndAlike() {
    const auto& first = rooms::ISO_DUNGEON_ROOMS_ROOMS[0];
    for (const auto& room : rooms::ISO_DUNGEON_ROOMS_ROOMS) {
        if (room.cols != room.rows || room.cols != first.cols) {
            return false;
        }
    }
    return true;
}
static_assert(exportedRoomsAreSquareAndAlike(),
              "This game assumes square rooms of one size. Give every room in "
              "the editor project the same NxN rect, or give RoomCatalog.h "
              "separate width and height constants.");

/// Room extent in tiles, both axes. Owned by the export; named here.
inline constexpr int kRoomTiles = rooms::ISO_DUNGEON_ROOMS_ROOMS[0].cols;

/**
 * @brief Tile grid: +1 tileX steps down-right, +1 tileY steps down-left.
 *
 * Owned by the export as ISO_PROJECTION; named here because every gameplay
 * call site -- hero interpolation, prop placement, screen-to-cell picking --
 * already reads `kTileProjection`, and none of them care that the value now
 * arrives from a generated file.
 *
 * One room is one screen, and every room is drawn with this same projection.
 * The export's room maps (ROOM_LAYERS) are cropped to their room with local
 * cells from (0, 0), so the projection's origin -- authored in the editor as
 * (120, 88), where room 0's cell (0, 0) sits -- puts ANY room's local (0, 0)
 * there and centres it on the 240x240 display. The export's own comment
 * suggests drawing room r through gameplay::roomProjection(), which places it
 * where it sits in the scene (rooms 1 and 2 are laid out to the right of room
 * 0). This game does not want that -- it never shows two rooms at once -- and
 * that helper is also newer than the engine release this example pins.
 *
 * 32x16 is the classic 2:1 isometric tile, and at 7 tiles per side it is not
 * an arbitrary choice: the room's diamond is (7 + 7) * 16 = 224 px wide, the
 * largest 2:1 tile that keeps the whole room inside a 240 px display without
 * scrolling. A 48x24 tile would need 336 px.
 *
 * The origin is the diamond CENTRE rather than a sprite's top-left corner
 * because every sprite in this example is anchored by a FOOT_Y measured to
 * that same point. One anchor convention, one subtraction, no per-sprite fudge
 * factors.
 */
inline constexpr gameplay::ProjectionSpec kTileProjection = rooms::ISO_PROJECTION;

// The export asserts its own validity and its paint order at its declaration
// site. What it cannot know is the room size the game will use it at, which is
// what this re-check covers.
static_assert(gameplay::projectionSpecIsValid(kTileProjection, kRoomTiles, kRoomTiles),
              "kTileProjection exceeds Scalar's fixed-point range.");
static_assert(gameplay::projectionDet(kTileProjection) == 256,
              "kTileProjection's determinant is no longer a power of two; "
              "screenToCell would stop strength-reducing to a shift.");

/**
 * @brief Whether plain row-major cell iteration is a correct back-to-front
 *        paint order under `kTileProjection` -- `RoomRenderer::drawTiles`
 *        has no depth sort at all and relies on it.
 *
 * The predicate and its full proof now live in the engine, at
 * `math::rowMajorIsPainterOrder()` (include/math/Projection.h), since a
 * shipped renderer code path depends on the same property. See that header
 * for the two-line proof, the strictness argument, and why `false` here
 * would not mean this spec is invalid.
 */
static_assert(gameplay::rowMajorIsPainterOrder(kTileProjection),
              "kTileProjection no longer makes row-major iteration a back-to-front "
              "paint order. RoomRenderer::drawTiles has no depth sort and relies "
              "on it; see math::rowMajorIsPainterOrder in include/math/Projection.h.");

// --- Room layout ------------------------------------------------------------

/// Room extent is fixed for every room in the dungeon, so a single constant
/// sizes every layout array and every bounds check. The layouts themselves,
/// and the doors between them, live in RoomCatalog.h -- this header stays the
/// geometry and hero tuning it started as.
///
/// Tile roles, indexed `layout[tileY][tileX]`:
///
/// | Char | Meaning                                    | Walkable |
/// |------|--------------------------------------------|----------|
/// | `W`  | stone wall                                 | no       |
/// | `D`  | doorway in the wall running along +x       | yes      |
/// | `E`  | doorway in the wall running along +y       | yes      |
/// | `.`  | plain floor                                | yes      |
/// | `a`  | accent floor (the ritual square)           | yes      |
/// | `A`  | altar, standing on accent floor            | no       |
/// | `P`  | pillar                                     | no       |
///
/// The two back edges (tileY == 0 and tileX == 0) are walls; the two front
/// edges are open, with the black backdrop reading as the drop beyond them.
/// That framing is not decoration -- see RoomCatalog.h for why the front edges
/// can never carry a wall, and what that costs the dungeon's topology.
///
/// The doorways are two chars rather than one because which face a doorway is
/// carved into depends on which wall it sits in, and inferring that from the
/// coordinates would silently do the wrong thing the first time a wall moves.
///
/// Doorways are walkable: standing on one is what triggers the room change.

/// Where the hero starts, in tiles, in room 0: the editor project's player
/// spawn. The export states it in SCENE cells; room 0 sits at the scene's
/// origin, so they are room-0 cells too, which is what the assert pins.
static_assert(rooms::ISO_DUNGEON_ROOMS_ROOMS[0].originCol == 0 &&
                  rooms::ISO_DUNGEON_ROOMS_ROOMS[0].originRow == 0,
              "The player spawn is exported in scene cells; this game reads it "
              "as room-0 cells, which only holds while room 0 is at (0, 0).");
inline constexpr int kSpawnTileX = rooms::SPAWN_TILE_X;
inline constexpr int kSpawnTileY = rooms::SPAWN_TILE_Y;

// --- Hero -------------------------------------------------------------------

/// Length of one logic step in milliseconds.
///
/// Movement runs on a fixed clock rather than the render frame so a slow frame
/// stretches nothing: `gameplay::tickStep` advances by exactly one, and the
/// hero covers a tile in the same wall-clock time on a 240 MHz ESP32 as it
/// does on a desktop that renders four times faster.
inline constexpr unsigned long kLogicStepMs = 16;

/// Logic steps one tile of travel takes (~256 ms per tile).
///
/// This is the number that makes the movement read as a board rather than as
/// a walk: the hero is always either at rest on a tile or travelling between
/// two named tiles, never anywhere else.
inline constexpr int kStepsPerCell = 16;

// --- Colours ----------------------------------------------------------------

/// The backdrop beyond the room.
///
/// Under this example's custom palette a `graphics::Color` is an INDEX, and its
/// name is meaningless: `Color::Black` is index 0, which the renderer treats as
/// transparent. Index 1 -- spelled `Color::White` -- is where the SPRITE
/// palette (DungeonSpritePalette.h) puts pure black. Naming the intent here
/// keeps that trap in one place.
///
/// Sprite palette only. The background bank holds the tilemap export's own
/// palette, whose index 1 is the stone outline, so a fill resolved in the
/// background context (as anything drawn on render layer 0 is) would not be
/// black. Nothing paints the backdrop today -- see RoomRenderer::draw().
inline constexpr pixelroot32::graphics::Color kVoidColor =
    pixelroot32::graphics::Color::White;

// --- Cell helpers -----------------------------------------------------------

/// True when a tile coordinate pair is inside the room.
constexpr bool containsTile(int tileX, int tileY) {
    return tileX >= 0 && tileY >= 0 && tileX < kRoomTiles && tileY < kRoomTiles;
}

}  // namespace iso_dungeon
