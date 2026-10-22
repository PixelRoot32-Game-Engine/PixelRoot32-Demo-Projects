#include "PacmanPlayer.h"

namespace pacman {

void PacmanPlayer::reset(const PacmanMaze& maze) {
    gameplay::placeAt(motion_, maze.playerStartCol(), maze.playerStartRow());
    direction_ = Direction::None;
    requestedDirection_ = Direction::None;
    animationPhase_ = 0;
}

void PacmanPlayer::requestDirection(Direction direction) {
    // A frame with no direction pressed must not cancel a turn the player
    // asked for earlier: the request stays pending until it can be taken.
    if (direction == Direction::None) {
        return;
    }
    requestedDirection_ = direction;
}

void PacmanPlayer::step(const PacmanMaze& maze) {
    if (gameplay::isMoving(motion_)) {
        const bool arrived = gameplay::tickStep(motion_, kPlayerStepsPerCell);
        if (arrived) {
            ++animationPhase_;
            if (!maze.isInside(motion_.cellX, motion_.cellY)) {
                // The step left the map through a tunnel: the player comes
                // back in on the opposite edge.
                int wrappedCol = 0;
                int wrappedRow = 0;
                if (maze.wrapCell(motion_.cellX, motion_.cellY, wrappedCol, wrappedRow)) {
                    gameplay::placeAt(motion_, wrappedCol, wrappedRow);
                }
            }
        }
    }

    if (!gameplay::isMoving(motion_)) {
        const Direction chosen = chooseDirection(maze);
        if (chosen != Direction::None) {
            const int nextCol = motion_.cellX + directionColumnDelta(chosen);
            const int nextRow = motion_.cellY + directionRowDelta(chosen);
            gameplay::beginStep(motion_, nextCol, nextRow);
            direction_ = chosen;
        } else {
            direction_ = Direction::None;
        }
    }
}

bool PacmanPlayer::mouthIsOpen() const {
    return (animationPhase_ / kMouthAnimationDivisor) % 2 == 0;
}

bool PacmanPlayer::canEnter(const PacmanMaze& maze, Direction direction) const {
    const int nextCol = motion_.cellX + directionColumnDelta(direction);
    const int nextRow = motion_.cellY + directionRowDelta(direction);

    if (!maze.isInside(nextCol, nextRow)) {
        // Leaving the map is only allowed through a tunnel.
        return maze.isTunnel(motion_.cellX, motion_.cellY);
    }
    return maze.isWalkableForPlayer(nextCol, nextRow);
}

Direction PacmanPlayer::chooseDirection(const PacmanMaze& maze) const {
    // The request wins whenever it can be taken, which is what turns a
    // request made before an intersection into a turn at that intersection.
    if (requestedDirection_ != Direction::None && canEnter(maze, requestedDirection_)) {
        return requestedDirection_;
    }
    // Otherwise the player keeps going, if it still can.
    if (direction_ != Direction::None && canEnter(maze, direction_)) {
        return direction_;
    }
    return Direction::None;
}

}  // namespace pacman
