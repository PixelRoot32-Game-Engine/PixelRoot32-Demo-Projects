#pragma once
#include <cstdint>

namespace top_down_city {

/**
 * Which way a figure is pointing. Its own engine-free header so the
 * host-tested half of the weapon can name a direction without dragging in a
 * Renderer: it used to live in CityConstants.h, which pulls the engine config,
 * the generated tilemaps and three sprite headers behind it. Four values and
 * not eight, even though the player walks in eight directions -- there are
 * three sets of frames plus a mirror, so a diagonal has nothing to draw.
 * Everything that aims -- the walk cycle, the car doors, the gun -- aims along
 * one of these.
 */
enum class Facing : std::uint8_t { Down = 0, Up = 1, Right = 2, Left = 3 };

}  // namespace top_down_city
