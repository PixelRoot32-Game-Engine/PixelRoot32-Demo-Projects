#pragma once

#include <cstdint>

#include "PacmanMaze.h"

namespace pacman {

/**
 * @class PacmanRouteField
 * @brief Shortest route distance from every cell of the maze to one target.
 *
 * The enemies choose where to go by asking "which neighbour cell is closest
 * to my target?". Answering that with a search per neighbour would mean four
 * searches per enemy per decision; instead the field is filled once from the
 * target by a breadth-first search over walkable cells, and every cell's
 * distance is then a single lookup.
 *
 * The search runs backwards from the target. The maze's corridors are
 * undirected, so the distance from the target to a cell equals the distance
 * from that cell to the target -- except for the ghost-house doorway, which
 * only enemies may cross, and which is why the actor kind is a parameter.
 *
 * A cell that cannot reach the target keeps the distance `kUnreachable`.
 */
class PacmanRouteField {
public:
    /**
     * @brief Distance stored for a cell that cannot reach the target.
     */
    static constexpr int kUnreachable = -1;

    /**
     * @brief Recomputes the field for one target cell.
     *
     * @param maze The maze to search.
     * @param targetCol Column of the target cell.
     * @param targetRow Row of the target cell.
     * @param forEnemy true when the search may cross the ghost-house
     *        doorway, false when it may not.
     */
    void compute(const PacmanMaze& maze, int targetCol, int targetRow, bool forEnemy);

    /**
     * @brief Route distance from a cell to the target.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return The distance in cells, or `kUnreachable` when the target cannot
     *         be reached from that cell.
     */
    [[nodiscard]] int distanceAt(int col, int row) const;

    /**
     * @brief Tells whether a cell can reach the target.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true when the cell reaches the target.
     */
    [[nodiscard]] bool isReachable(int col, int row) const;

    /**
     * @brief Column of the cell the field was computed for.
     * @return The target column.
     */
    [[nodiscard]] int targetCol() const;

    /**
     * @brief Row of the cell the field was computed for.
     * @return The target row.
     */
    [[nodiscard]] int targetRow() const;

private:
    std::int16_t distance_[PacmanMaze::kCells];  ///< Distance per cell, `kUnreachable` when unreachable.
    int targetCol_ = 0;   ///< Column the field was computed for.
    int targetRow_ = 0;   ///< Row the field was computed for.
    int colCount_ = 0;    ///< Column count of the maze the field was computed for.
    bool valid_ = false;  ///< Whether the field holds a computed result.
};

}  // namespace pacman
