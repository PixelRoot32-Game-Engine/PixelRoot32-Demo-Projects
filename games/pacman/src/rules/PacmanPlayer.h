#pragma once

#include <cstdint>

#include "gameplay/GridMotion.h"

#include "PacmanConfig.h"
#include "PacmanDirection.h"
#include "PacmanMaze.h"

namespace pacman {

namespace gameplay = pixelroot32::gameplay;

/**
 * @class PacmanPlayer
 * @brief The player character: a grid-locked actor that may turn before it
 *        reaches an intersection.
 *
 * The player always occupies exactly one logical cell. When it moves, it
 * travels towards the next cell and the gameplay rules keep reading the
 * cell it came from until it arrives -- so no rule ever sees the player
 * straddling two cells.
 *
 * A turn request is remembered rather than acted on immediately. The request
 * is re-examined every time the player reaches a cell, which is what lets a
 * player press "up" a corner early and still take the turn. When the
 * requested direction cannot be taken, the player keeps its current
 * direction and the request stays pending.
 */
class PacmanPlayer {
public:
    /**
     * @brief Places the player at the level's start cell, at rest.
     *
     * @param maze The maze the player is placed in.
     */
    void reset(const PacmanMaze& maze);

    /**
     * @brief Remembers the direction the player is asking for.
     *
     * The request is not acted on here: it is re-examined at every cell the
     * player reaches. A `Direction::None` request is ignored, so a frame in
     * which no direction button was pressed never clears a turn the player
     * asked for earlier.
     *
     * @param direction The requested direction.
     */
    void requestDirection(Direction direction);

    /**
     * @brief Advances the player by one logic step.
     *
     * @param maze The maze the player moves in.
     */
    void step(const PacmanMaze& maze);

    /**
     * @brief Column of the cell the player occupies.
     * @return The player's column.
     */
    [[nodiscard]] int cellCol() const { return motion_.cellX; }

    /**
     * @brief Row of the cell the player occupies.
     * @return The player's row.
     */
    [[nodiscard]] int cellRow() const { return motion_.cellY; }

    /**
     * @brief Column of the cell the player is travelling towards.
     * @return The target column.
     */
    [[nodiscard]] int targetCol() const { return motion_.toX; }

    /**
     * @brief Row of the cell the player is travelling towards.
     * @return The target row.
     */
    [[nodiscard]] int targetRow() const { return motion_.toY; }

    /**
     * @brief How far the player has travelled into the current cell.
     * @return A value from 0 (at rest) to `kPlayerStepsPerCell` - 1.
     */
    [[nodiscard]] int progress() const { return motion_.progress; }

    /**
     * @brief Tells whether the player is between two cells.
     * @return true while a step is in flight.
     */
    [[nodiscard]] bool isMoving() const { return gameplay::isMoving(motion_); }

    /**
     * @brief The direction the player is travelling in.
     * @return The current direction, `Direction::None` while at rest.
     */
    [[nodiscard]] Direction direction() const { return direction_; }

    /**
     * @brief The direction the player last asked for.
     * @return The pending request.
     */
    [[nodiscard]] Direction requestedDirection() const { return requestedDirection_; }

    /**
     * @brief The shape the player's mouth is drawn in.
     *
     * The mouth walks through the cabinet's three shapes -- shut, half open
     * and wide open -- while the player travels, and is shut again while the
     * player stands still. That is what makes the character read as walking
     * rather than sliding.
     *
     * @return 0 for the shut mouth, 1 for the half-open mouth and 2 for the
     *         wide-open mouth.
     */
    [[nodiscard]] int chompFrame() const;

private:
    gameplay::GridMotion motion_;
    Direction direction_ = Direction::None;
    Direction requestedDirection_ = Direction::None;
    int chompStep_ = 0;

    /**
     * @brief Tells whether the player may step into a direction right now.
     *
     * @param maze The maze the player moves in.
     * @param direction The direction to test.
     * @return true when the cell in that direction can be entered.
     */
    [[nodiscard]] bool canEnter(const PacmanMaze& maze, Direction direction) const;

    /**
     * @brief Picks the direction the player travels in from its current cell.
     *
     * @param maze The maze the player moves in.
     * @return The chosen direction, `Direction::None` when the player stops.
     */
    [[nodiscard]] Direction chooseDirection(const PacmanMaze& maze) const;
};

}  // namespace pacman
