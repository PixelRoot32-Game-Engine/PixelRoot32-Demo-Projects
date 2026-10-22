#pragma once

#include "graphics/Color.h"

namespace pacman {

/**
 * @file PacmanPalette.h
 * @brief The colours the demo draws with.
 *
 * They live in one place so the whole game reads as one set: the maze is a
 * single blue, the ghost house a single magenta, and every actor keeps its
 * colour across every state.
 */

/**
 * @brief Background colour, also the colour of the maze's corridors.
 */
constexpr pixelroot32::graphics::Color kBackgroundColor = pixelroot32::graphics::Color::Black;

/**
 * @brief Colour of the maze walls.
 */
constexpr pixelroot32::graphics::Color kWallColor = pixelroot32::graphics::Color::Blue;

/**
 * @brief Colour of the ghost house doorway.
 *
 * The engine's `Magenta` entry resolves to a light grey in the default
 * palette, so the pink of the arcade original is taken from `LightRed`,
 * which is the palette's pink.
 */
constexpr pixelroot32::graphics::Color kHouseColor = pixelroot32::graphics::Color::LightRed;

/**
 * @brief Colour of the dots still on the floor.
 */
constexpr pixelroot32::graphics::Color kDotColor = pixelroot32::graphics::Color::White;

/**
 * @brief Colour of the power pellets.
 */
constexpr pixelroot32::graphics::Color kPelletColor = pixelroot32::graphics::Color::Yellow;

/**
 * @brief Colour of the player.
 */
constexpr pixelroot32::graphics::Color kPlayerColor = pixelroot32::graphics::Color::Yellow;

/**
 * @brief Colour of the player's eye.
 *
 * The eye is drawn on top of the yellow head, the way the cabinet draws it.
 */
constexpr pixelroot32::graphics::Color kPlayerEyeColor = pixelroot32::graphics::Color::Black;

/**
 * @brief Colour of the highlight inside the player's eye.
 */
constexpr pixelroot32::graphics::Color kPlayerEyeGlintColor = pixelroot32::graphics::Color::White;

/**
 * @brief Colour of the enemies while they hunt.
 */
constexpr pixelroot32::graphics::Color kEnemyColors[4] = {
    pixelroot32::graphics::Color::Red,
    pixelroot32::graphics::Color::LightRed,
    pixelroot32::graphics::Color::Cyan,
    pixelroot32::graphics::Color::Orange,
};

/**
 * @brief Colour of the enemies while the power effect is active.
 *
 * The maze walls are already blue, so the frightened hunters are drawn in
 * purple instead: they have to read as a different state at a glance, and
 * they have to read differently from the wall they are standing next to.
 */
constexpr pixelroot32::graphics::Color kVulnerableColor = pixelroot32::graphics::Color::Purple;

/**
 * @brief Colour of the status band text.
 */
constexpr pixelroot32::graphics::Color kTextColor = pixelroot32::graphics::Color::White;

/**
 * @brief Colour of the status band text that reports a warning.
 */
constexpr pixelroot32::graphics::Color kAlertColor = pixelroot32::graphics::Color::Red;

}  // namespace pacman
