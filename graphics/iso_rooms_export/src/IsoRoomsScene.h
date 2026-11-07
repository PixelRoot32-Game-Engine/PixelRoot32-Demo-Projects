#pragma once

#include <core/Scene.h>
#include <gameplay/RoomGraph.h>
#include <graphics/Camera2D.h>
#include <graphics/ProjectedMapBounds.h>
#include <graphics/Renderer.h>

#include "assets/tilemap/iso_rooms_map.h"

namespace iso_rooms_export {

/**
 * @class IsoRoomsScene
 * @brief Walks the rooms of an isometric Tilemap Editor export.
 *
 * Everything here comes from the export: the room table and its connections
 * (ISO_ROOMS_MAP_ROOM_LAYER), one map per room (ROOM_LAYERS[]) and the scene
 * projection (ISO_PROJECTION). The scene builds the graph once with the
 * projected buildRoomGraph overload, then draws only the current room through
 * roomProjection(), and an arrow key moves to the room connected on that side.
 */
class IsoRoomsScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    static constexpr uint16_t kMaxRooms = iso_rooms::map::NUM_ROOM_LAYERS;

    /// Follows the current room's connection in `dir`; false at a wall.
    bool tryMove(pixelroot32::gameplay::RoomDir dir);

    pixelroot32::gameplay::RoomGraph<kMaxRooms> rooms_;
    pixelroot32::graphics::Camera2D camera_{1, 1};
    bool ready_ = false;
};

}  // namespace iso_rooms_export
