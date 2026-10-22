#pragma once

#include <cstdint>

namespace pacman {

/**
 * @enum Direction
 * @brief The four grid directions, plus the resting direction.
 *
 * `None` means "not moving anywhere": it is the direction a freshly placed
 * actor has before it is told to move, and the value a blocked actor keeps
 * when the requested direction cannot be taken.
 */
enum class Direction : std::uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right
};

/**
 * @brief Row offset of a direction on the maze grid.
 *
 * @param direction The direction to inspect.
 * @return -1 for up, +1 for down, 0 for left, right and none.
 */
constexpr int directionRowDelta(Direction direction) {
    return direction == Direction::Up ? -1 : (direction == Direction::Down ? 1 : 0);
}

/**
 * @brief Column offset of a direction on the maze grid.
 *
 * @param direction The direction to inspect.
 * @return -1 for left, +1 for right, 0 for up, down and none.
 */
constexpr int directionColumnDelta(Direction direction) {
    return direction == Direction::Left ? -1 : (direction == Direction::Right ? 1 : 0);
}

/**
 * @brief The direction that points the other way.
 *
 * @param direction The direction to invert.
 * @return The opposite direction, or `Direction::None` for `None`.
 */
constexpr Direction oppositeDirection(Direction direction) {
    switch (direction) {
        case Direction::Up:
            return Direction::Down;
        case Direction::Down:
            return Direction::Up;
        case Direction::Left:
            return Direction::Right;
        case Direction::Right:
            return Direction::Left;
        default:
            return Direction::None;
    }
}

}  // namespace pacman
