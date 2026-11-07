#include "IsoRoomsScene.h"

#include <core/Engine.h>
#include <gameplay/RoomLayout.h>
#include <math/MathUtil.h>
#include <math/Scalar.h>
#include <math/Vector2.h>

namespace pr32 = pixelroot32;
namespace gfx = pixelroot32::graphics;
namespace gameplay = pixelroot32::gameplay;
namespace math = pixelroot32::math;
// Not `map`: Arduino declares a global map() function, so that alias
// would not compile on the hardware target.
namespace scene_map = iso_rooms::map;

extern pr32::core::Engine engine;

namespace {

// Button indices of the InputConfig in platforms/*.h: Up, Down, Left, Right.
constexpr uint8_t BTN_UP = 0;
constexpr uint8_t BTN_DOWN = 1;
constexpr uint8_t BTN_LEFT = 2;
constexpr uint8_t BTN_RIGHT = 3;

}  // namespace

namespace iso_rooms_export {

void IsoRoomsScene::init() {
    pr32::core::Scene::init();

    // Tileset descriptors, room maps and (with the "Install palette" export
    // option) the background palette. Must run before anything is drawn.
    scene_map::init();

    // Each room's rect starts as the screen box of its cell diamonds. Walls
    // rise above their diamonds, so widen every room by what its own map
    // actually draws, measured through the same projection it is drawn with.
    gfx::ScreenBounds art[kMaxRooms];
    for (uint16_t i = 0; i < kMaxRooms; ++i) {
        gfx::expandProjectedMapBounds(
            art[i], *scene_map::ROOM_LAYERS[i],
            gameplay::roomProjection(scene_map::ISO_PROJECTION,
                                     scene_map::ISO_ROOMS_MAP_ROOMS[i]));
    }

    if (gameplay::buildRoomGraph(scene_map::ISO_ROOMS_MAP_ROOM_LAYER, rooms_,
                                 scene_map::ISO_PROJECTION, art) != kMaxRooms) {
        return;  // a malformed room table: draw nothing rather than garbage
    }
    setRoomGraph(&rooms_);
    rooms_.enterRoom(0, &camera_);
    ready_ = true;
}

bool IsoRoomsScene::tryMove(gameplay::RoomDir dir) {
    const gameplay::Room& room = rooms_.getRoom(rooms_.currentRoomIndex());
    const int target = room.connections_[static_cast<int>(dir)];
    if (target == gameplay::Room::INVALID_ROOM) return false;
    rooms_.enterRoom(static_cast<uint16_t>(target), &camera_);
    return true;
}

void IsoRoomsScene::update(unsigned long deltaTime) {
    pr32::core::Scene::update(deltaTime);
    if (!ready_) return;

    auto& input = engine.getInputManager();
    if (input.isButtonPressed(BTN_UP)) {
        tryMove(gameplay::RoomDir::Up);
    } else if (input.isButtonPressed(BTN_DOWN)) {
        tryMove(gameplay::RoomDir::Down);
    } else if (input.isButtonPressed(BTN_LEFT)) {
        tryMove(gameplay::RoomDir::Left);
    } else if (input.isButtonPressed(BTN_RIGHT)) {
        tryMove(gameplay::RoomDir::Right);
    }
}

void IsoRoomsScene::draw(gfx::Renderer& renderer) {
    pr32::core::Scene::draw(renderer);
    if (!ready_) return;

    const uint16_t current = rooms_.currentRoomIndex();
    const gameplay::Room& room = rooms_.getRoom(current);

    // The room rect is the room's EXTENT; cameraRangeFor turns it into the
    // range of camera positions that keep the view inside it (or centred on
    // it when the room is smaller than the screen).
    const int viewW = renderer.getLogicalWidth();
    const int viewH = renderer.getLogicalHeight();
    gfx::ScreenBounds extent;
    extent.left = math::roundToInt(room.cameraMinX);
    extent.top = math::roundToInt(room.cameraMinY);
    extent.right = math::roundToInt(room.cameraMaxX);
    extent.bottom = math::roundToInt(room.cameraMaxY);
    extent.valid = true;
    const gfx::CameraBounds range = gfx::cameraRangeFor(extent, viewW, viewH);

    camera_.setViewportSize(viewW, viewH);
    camera_.setBounds(math::toScalar(range.minX), math::toScalar(range.maxX));
    camera_.setVerticalBounds(math::toScalar(range.minY),
                              math::toScalar(range.maxY));
    camera_.setPosition(math::Vector2(
        math::toScalar((extent.left + extent.right - viewW) / 2),
        math::toScalar((extent.top + extent.bottom - viewH) / 2)));
    camera_.apply(renderer);

    // The room's own map: local cells from (0, 0), placed where they sit in
    // the scene by roomProjection.
    renderer.drawTileMap(*scene_map::ROOM_LAYERS[current], 0, 0,
                         gfx::LayerType::Dynamic,
                         gameplay::roomProjection(scene_map::ISO_PROJECTION,
                                                  scene_map::ISO_ROOMS_MAP_ROOMS[current]));
}

}  // namespace iso_rooms_export
