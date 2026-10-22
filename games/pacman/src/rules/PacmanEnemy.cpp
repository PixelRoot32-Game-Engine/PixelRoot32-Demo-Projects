#include "PacmanEnemy.h"

namespace pacman {

namespace {

/**
 * @brief The order the hunter considers its options in.
 *
 * The order is fixed so that two hunters that face the same choice always
 * make the same one. Up, left, down, right is the order the arcade original
 * prefers, which is what makes a chaser cut the corner a player expects.
 */
constexpr Direction kDirectionOrder[4] = {
    Direction::Up,
    Direction::Left,
    Direction::Down,
    Direction::Right,
};

}  // namespace

void PacmanEnemy::reset(const PacmanMaze& maze, int index) {
    gameplay::placeAt(motion_, maze.enemyStartCol(index), maze.enemyStartRow(index));
    direction_ = Direction::None;
    state_ = EnemyState::Patrol;
    personality_ = static_cast<EnemyPersonality>(index);

    homeCol_ = maze.enemyStartCol(index);
    homeRow_ = maze.enemyStartRow(index);
    cornerCol_ = kCornerCols[index];
    cornerRow_ = kCornerRows[index];
    scatterTimer_ = kScatterSteps;
    respawnTimer_ = kEnemyReleaseDelaySteps[index];
    reverseRequested_ = false;

    if (respawnTimer_ == 0) {
        // This hunter starts outside the house: it plays from the first step.
        state_ = EnemyState::Chase;
    } else {
        // This hunter starts inside the house and waits for its turn.
        state_ = EnemyState::Respawn;
    }
}

void PacmanEnemy::defeat() {
    state_ = EnemyState::Return;
    reverseRequested_ = false;
}

void PacmanEnemy::step(const PacmanMaze& maze, const PacmanEnemyContext& context) {
    updateState(maze, context);

    if (state_ == EnemyState::Respawn && respawnTimer_ > 0) {
        // The hunter waits in the house for its turn to come.
        --respawnTimer_;
        return;
    }

    // The route field is filled from the cell the hunter is heading for. A
    // fleeing hunter measures against the player instead: it wants the
    // neighbour that is furthest away.
    const int routeTargetCol = (state_ == EnemyState::Flee) ? context.playerCol : behaviourTargetCol(maze, context);
    const int routeTargetRow = (state_ == EnemyState::Flee) ? context.playerRow : behaviourTargetRow(maze, context);

    if (routeField_.targetCol() != routeTargetCol || routeField_.targetRow() != routeTargetRow) {
        routeField_.compute(maze, routeTargetCol, routeTargetRow, true);
    }

    if (gameplay::isMoving(motion_)) {
        gameplay::tickStep(motion_, kEnemyStepsPerCell);
        if (!gameplay::isMoving(motion_)) {
            onArrived(maze);
        }
        return;
    }

    const Direction chosen = chooseDirection(maze, routeTargetCol, routeTargetRow);
    if (chosen != Direction::None) {
        const int nextCol = motion_.cellX + directionColumnDelta(chosen);
        const int nextRow = motion_.cellY + directionRowDelta(chosen);
        gameplay::beginStep(motion_, nextCol, nextRow);
        direction_ = chosen;
    } else {
        direction_ = Direction::None;
    }
}

void PacmanEnemy::updateState(const PacmanMaze& maze, const PacmanEnemyContext& context) {
    // A hunter that has waited in the house and is already standing on the
    // exit cell joins the maze right away.
    if (state_ == EnemyState::Respawn && respawnTimer_ == 0 &&
        motion_.cellX == kHouseExitCol && motion_.cellY == kHouseExitRow) {
        state_ = EnemyState::Chase;
    }

    // The power effect never touches a hunter that is on its way home or
    // waiting to come back: it only changes hunters that are playing.
    if (state_ == EnemyState::Return || state_ == EnemyState::Respawn) {
        return;
    }

    if (context.powerIsActive) {
        if (state_ != EnemyState::Flee) {
            state_ = EnemyState::Flee;
            // A hunter that turns its back on the player does so at the next
            // cell, which is where the choice of direction is made.
            reverseRequested_ = true;
        }
        return;
    }

    if (state_ == EnemyState::Flee) {
        state_ = EnemyState::Chase;
        return;
    }

    if (state_ == EnemyState::Patrol && scatterTimer_ > 0) {
        --scatterTimer_;
        if (scatterTimer_ == 0) {
            state_ = EnemyState::Chase;
        }
    }
}

void PacmanEnemy::onArrived(const PacmanMaze& maze) {
    if (!maze.isInside(motion_.cellX, motion_.cellY)) {
        // The step left the map through a tunnel: the hunter comes back in on
        // the opposite edge.
        int wrappedCol = 0;
        int wrappedRow = 0;
        if (maze.wrapCell(motion_.cellX, motion_.cellY, wrappedCol, wrappedRow)) {
            gameplay::placeAt(motion_, wrappedCol, wrappedRow);
        }
    }

    if (state_ == EnemyState::Return) {
        if (motion_.cellX == homeCol_ && motion_.cellY == homeRow_) {
            state_ = EnemyState::Respawn;
            respawnTimer_ = kEnemyRespawnDelaySteps;
        }
    } else if (state_ == EnemyState::Respawn) {
        if (motion_.cellX == kHouseExitCol && motion_.cellY == kHouseExitRow) {
            state_ = EnemyState::Chase;
        }
    }
}

int PacmanEnemy::behaviourTargetCol(const PacmanMaze& maze, const PacmanEnemyContext& context) const {
    switch (state_) {
        case EnemyState::Return:
            return homeCol_;
        case EnemyState::Respawn:
            return kHouseExitCol;
        case EnemyState::Patrol:
            return cornerCol_;
        case EnemyState::Flee:
        case EnemyState::Chase:
            break;
    }

    switch (personality_) {
        case EnemyPersonality::Chaser:
            return context.playerCol;
        case EnemyPersonality::Ambusher: {
            // The cell four steps ahead of the player. When that cell is
            // inside a wall the hunter falls back on the player's own cell,
            // which keeps the target reachable.
            int col = context.playerCol + directionColumnDelta(context.playerDirection) * kAmbusherLookAhead;
            if (!maze.isWalkableForEnemy(col, context.playerRow)) {
                col = context.playerCol;
            }
            return col;
        }
        case EnemyPersonality::Flanker: {
            // The point past the player, measured from the chaser and
            // doubled, which is the classic flanking rule.
            const int aheadCol = context.playerCol +
                                 directionColumnDelta(context.playerDirection) * kFlankerLookAhead;
            int col = 2 * aheadCol - context.chaserCol;
            if (!maze.isWalkableForEnemy(col, context.playerRow)) {
                col = context.playerCol;
            }
            return col;
        }
        case EnemyPersonality::Timid: {
            const int distance = (cornerCol_ - context.playerCol) * (cornerCol_ - context.playerCol) +
                                 (cornerRow_ - context.playerRow) * (cornerRow_ - context.playerRow);
            if (distance <= kTimidHuntDistance * kTimidHuntDistance) {
                return cornerCol_;
            }
            return context.playerCol;
        }
    }
    return context.playerCol;
}

int PacmanEnemy::behaviourTargetRow(const PacmanMaze& maze, const PacmanEnemyContext& context) const {
    switch (state_) {
        case EnemyState::Return:
            return homeRow_;
        case EnemyState::Respawn:
            return kHouseExitRow;
        case EnemyState::Patrol:
            return cornerRow_;
        case EnemyState::Flee:
        case EnemyState::Chase:
            break;
    }

    switch (personality_) {
        case EnemyPersonality::Chaser:
            return context.playerRow;
        case EnemyPersonality::Ambusher: {
            int row = context.playerRow + directionRowDelta(context.playerDirection) * kAmbusherLookAhead;
            if (!maze.isWalkableForEnemy(context.playerCol, row)) {
                row = context.playerRow;
            }
            return row;
        }
        case EnemyPersonality::Flanker: {
            const int aheadRow = context.playerRow +
                                 directionRowDelta(context.playerDirection) * kFlankerLookAhead;
            int row = 2 * aheadRow - context.chaserRow;
            if (!maze.isWalkableForEnemy(context.playerCol, row)) {
                row = context.playerRow;
            }
            return row;
        }
        case EnemyPersonality::Timid: {
            const int distance = (cornerCol_ - context.playerCol) * (cornerCol_ - context.playerCol) +
                                 (cornerRow_ - context.playerRow) * (cornerRow_ - context.playerRow);
            if (distance <= kTimidHuntDistance * kTimidHuntDistance) {
                return cornerRow_;
            }
            return context.playerRow;
        }
    }
    return context.playerRow;
}

bool PacmanEnemy::canEnter(const PacmanMaze& maze, Direction direction) const {
    const int nextCol = motion_.cellX + directionColumnDelta(direction);
    const int nextRow = motion_.cellY + directionRowDelta(direction);

    if (!maze.isInside(nextCol, nextRow)) {
        // Leaving the map is only allowed through a tunnel.
        return maze.isTunnel(motion_.cellX, motion_.cellY);
    }
    return maze.isWalkableForEnemy(nextCol, nextRow);
}

Direction PacmanEnemy::chooseDirection(const PacmanMaze& maze, int routeTargetCol, int routeTargetRow) {
    // Collect the directions the hunter could take.
    Direction options[4];
    int optionCount = 0;
    for (const Direction candidate : kDirectionOrder) {
        if (canEnter(maze, candidate)) {
            options[optionCount++] = candidate;
        }
    }
    if (optionCount == 0) {
        return Direction::None;
    }

    // A hunter that has just been hit by the power effect turns its back on
    // the player at the first cell it reaches.
    if (reverseRequested_) {
        reverseRequested_ = false;
        const Direction reverse = oppositeDirection(direction_);
        if (canEnter(maze, reverse)) {
            return reverse;
        }
    }

    // A hunter never doubles back while it has another way to go: reversing
    // would leave it wandering in circles.
    const Direction reverse = oppositeDirection(direction_);
    if (optionCount > 1) {
        int kept = 0;
        for (int index = 0; index < optionCount; ++index) {
            if (options[index] != reverse) {
                options[kept++] = options[index];
            }
        }
        optionCount = kept;
    }

    // Pick the option that brings the hunter closest to its target, or --
    // while it is fleeing -- the one that brings it furthest away.
    Direction best = options[0];
    int bestDistance = routeField_.distanceAt(motion_.cellX + directionColumnDelta(best),
                                              motion_.cellY + directionRowDelta(best));

    for (int index = 1; index < optionCount; ++index) {
        const int distance = routeField_.distanceAt(motion_.cellX + directionColumnDelta(options[index]),
                                                    motion_.cellY + directionRowDelta(options[index]));
        if (distance == PacmanRouteField::kUnreachable) {
            continue;
        }
        if (bestDistance == PacmanRouteField::kUnreachable) {
            best = options[index];
            bestDistance = distance;
            continue;
        }
        if (state_ == EnemyState::Flee) {
            if (distance > bestDistance) {
                best = options[index];
                bestDistance = distance;
            }
        } else if (distance < bestDistance) {
            best = options[index];
            bestDistance = distance;
        }
    }

    // When the target cannot be reached at all, the hunter keeps going the
    // way it was going.
    if (bestDistance == PacmanRouteField::kUnreachable) {
        if (canEnter(maze, direction_)) {
            return direction_;
        }
    }

    return best;
}

}  // namespace pacman
