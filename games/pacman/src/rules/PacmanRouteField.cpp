#include "PacmanRouteField.h"

namespace pacman {

namespace {

/**
 * @brief Breadth-first search queue, shared by every route field.
 *
 * The queue is scratch space: it only matters while a search is running, and
 * the game runs its searches one after another on the main thread. Keeping it
 * out of the field saves one buffer per enemy, which matters on the ESP32.
 */
int gSearchQueue[PacmanMaze::kCells];

}  // namespace

void PacmanRouteField::compute(const PacmanMaze& maze, int targetCol, int targetRow, bool forEnemy) {
    valid_ = false;
    targetCol_ = targetCol;
    targetRow_ = targetRow;
    colCount_ = maze.colCount();

    for (int index = 0; index < PacmanMaze::kCells; ++index) {
        distance_[index] = kUnreachable;
    }

    if (!maze.isInside(targetCol, targetRow)) {
        return;
    }

    const auto walkable = [&maze, forEnemy](int col, int row) {
        return forEnemy ? maze.isWalkableForEnemy(col, row) : maze.isWalkableForPlayer(col, row);
    };

    int head = 0;
    int tail = 0;

    const int startIndex = targetRow * colCount_ + targetCol;
    distance_[startIndex] = 0;
    gSearchQueue[tail++] = startIndex;

    const int deltas[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

    while (head < tail) {
        const int index = gSearchQueue[head++];
        const int row = index / colCount_;
        const int col = index % colCount_;
        const std::int16_t nextDistance = static_cast<std::int16_t>(distance_[index] + 1);

        for (const auto& delta : deltas) {
            int nextCol = col + delta[1];
            int nextRow = row + delta[0];

            if (!maze.isInside(nextCol, nextRow)) {
                // A tunnel edge wraps to the opposite side of the map.
                int wrappedCol = 0;
                int wrappedRow = 0;
                if (!maze.wrapCell(nextCol, nextRow, wrappedCol, wrappedRow)) {
                    continue;
                }
                nextCol = wrappedCol;
                nextRow = wrappedRow;
            }

            if (!walkable(nextCol, nextRow)) {
                continue;
            }

            const int nextIndex = nextRow * colCount_ + nextCol;
            if (distance_[nextIndex] != kUnreachable) {
                continue;
            }
            distance_[nextIndex] = nextDistance;
            gSearchQueue[tail++] = nextIndex;
        }
    }

    valid_ = true;
}

int PacmanRouteField::distanceAt(int col, int row) const {
    if (!valid_ || col < 0 || col >= colCount_ || row < 0 || row >= PacmanMaze::kRows) {
        return kUnreachable;
    }
    return distance_[row * colCount_ + col];
}

bool PacmanRouteField::isReachable(int col, int row) const {
    return distanceAt(col, row) != kUnreachable;
}

int PacmanRouteField::targetCol() const {
    return targetCol_;
}

int PacmanRouteField::targetRow() const {
    return targetRow_;
}

}  // namespace pacman
