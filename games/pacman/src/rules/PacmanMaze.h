#pragma once

#include <cstdint>

#include "PacmanConfig.h"
#include "PacmanDirection.h"

namespace pacman {

/**
 * @enum CellType
 * @brief The permanent terrain type of one maze cell.
 *
 * The terrain never changes while a game is running. Whether a cell still
 * holds its dot is a separate question, answered by the session's collected
 * mask, so that picking something up can never alter the geometry of the
 * maze.
 */
enum class CellType : std::uint8_t {
    Wall,      ///< Blocks every actor.
    HouseWall, ///< The ghost house's own wall: blocks every actor.
    Path,      ///< Ordinary corridor.
    Tunnel,    ///< Corridor that wraps to the opposite edge of the map.
    HouseDoor, ///< Ghost-house doorway: walkable for the hunters only.
    House      ///< Ghost-house interior: walkable for the hunters only.
};

/**
 * @enum Collectible
 * @brief What a cell holds before it is picked up.
 */
enum class Collectible : std::uint8_t {
    None,        ///< Nothing to collect.
    Dot,         ///< A small dot.
    PowerPellet  ///< A power pellet.
};

/**
 * @struct PacmanLevelConfig
 * @brief Everything a level needs: its layout and its start positions.
 *
 * The layout is an array of row strings, one character per cell, using the
 * legend documented on `kLevelLayout`. Start positions are separate from the
 * layout because they are configuration rather than terrain: the same layout
 * could be played from a different set of start positions.
 */
struct PacmanLevelConfig {
    const char* const* rows;  ///< Row strings, one character per cell.
    int rowCount;             ///< Number of rows.
    int colCount;             ///< Number of columns.
    int playerStartCol;       ///< Column of the player's starting cell.
    int playerStartRow;       ///< Row of the player's starting cell.
    int enemyCount;           ///< Number of enemies.
    int enemyStartCols[kEnemyCount];  ///< Starting columns of the enemies.
    int enemyStartRows[kEnemyCount];  ///< Starting rows of the enemies.
};

/**
 * @class PacmanMaze
 * @brief The level's terrain: cell types, walkability rules, start positions
 *        and the collected-item layout.
 *
 * The terrain is read straight from the level's row strings, so the maze owns
 * no copy of the map: a 28x26 layout costs nothing but the strings
 * themselves, which live in flash.
 *
 * Walkability is asked per actor because the ghost house has two kinds of
 * boundary. The player may never enter the house or its doorway; an enemy may
 * enter both, and that difference is the only thing that separates the two
 * actors' pathfinding.
 */
class PacmanMaze {
public:
    /**
     * @struct HouseBounds
     * @brief The ghost house's rectangle, in cells.
     */
    struct HouseBounds {
        int leftCol = 0;   ///< Leftmost column of the house's wall.
        int rightCol = 0;  ///< Rightmost column of the house's wall.
        int topRow = 0;    ///< Top row of the house's wall.
        int bottomRow = 0; ///< Bottom row of the house's wall.
    };

    /// @brief Columns of the built-in level.
    static constexpr int kCols = kMazeCols;

    /// @brief Rows of the built-in level.
    static constexpr int kRows = kMazeRows;

    /// @brief Cells of the built-in level.
    static constexpr int kCells = kMazeCols * kMazeRows;

    /**
     * @enum ValidationError
     * @brief Why a level layout was rejected.
     */
    enum class ValidationError : std::uint8_t {
        None,             ///< The layout is valid.
        RowLength,        ///< A row is not exactly `colCount` characters long.
        Border,           ///< The outer border is not closed.
        MissingStart,     ///< A start position lies outside the map.
        StartInWall,      ///< A start position lies inside a wall.
        NoCollectibles,   ///< The level holds nothing to collect.
        UnreachableCell   ///< A walkable cell cannot be reached from the player.
    };

    /**
     * @brief Builds the maze from the built-in level.
     */
    PacmanMaze();

    /**
     * @brief Builds the maze from a caller-supplied level.
     *
     * Used by the unit suite to feed the validator deliberately broken
     * layouts. The layout rows must outlive the maze.
     *
     * @param config The level to build from.
     */
    explicit PacmanMaze(const PacmanLevelConfig& config);

    /**
     * @brief Tells whether a cell coordinate lies inside the map.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true when the cell exists.
     */
    [[nodiscard]] bool isInside(int col, int row) const;

    /**
     * @brief Terrain type of a cell.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return The cell's terrain, `CellType::Wall` for coordinates outside
     *         the map.
     */
    [[nodiscard]] CellType cellType(int col, int row) const;

    /**
     * @brief Tells whether a cell is a wall.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true when the cell blocks movement, including every cell
     *         outside the map.
     */
    [[nodiscard]] bool isWall(int col, int row) const;

    /**
     * @brief Tells whether the player may stand on a cell.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true when the cell is a corridor or a tunnel.
     */
    [[nodiscard]] bool isWalkableForPlayer(int col, int row) const;

    /**
     * @brief Tells whether an enemy may stand on a cell.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true for corridors, tunnels, the house doorway and the house.
     */
    [[nodiscard]] bool isWalkableForEnemy(int col, int row) const;

    /**
     * @brief Tells whether a cell wraps around the edge of the map.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true for tunnel cells.
     */
    [[nodiscard]] bool isTunnel(int col, int row) const;

    /**
     * @brief What a cell holds before it is collected.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return The collectible in the cell.
     */
    [[nodiscard]] Collectible collectibleAt(int col, int row) const;

    /**
     * @brief Number of cells that hold a dot or a power pellet.
     *
     * @return The count of mandatory collectibles in this level.
     */
    [[nodiscard]] int collectibleCount() const;

    /**
     * @brief Column of the player's starting cell.
     * @return The starting column.
     */
    [[nodiscard]] int playerStartCol() const;

    /**
     * @brief Row of the player's starting cell.
     * @return The starting row.
     */
    [[nodiscard]] int playerStartRow() const;

    /**
     * @brief Column of an enemy's starting cell.
     *
     * @param index Enemy index, below `enemyCount`.
     * @return The starting column.
     */
    [[nodiscard]] int enemyStartCol(int index) const;

    /**
     * @brief Row of an enemy's starting cell.
     *
     * @param index Enemy index, below `enemyCount`.
     * @return The starting row.
     */
    [[nodiscard]] int enemyStartRow(int index) const;

    /**
     * @brief Number of enemies configured for this level.
     * @return The enemy count.
     */
    [[nodiscard]] int enemyCount() const;

    /**
     * @brief Number of columns in this level.
     * @return The column count.
     */
    [[nodiscard]] int colCount() const;

    /**
     * @brief Number of rows in this level.
     * @return The row count.
     */
    [[nodiscard]] int rowCount() const;

    /**
     * @brief Maps a cell that left the map onto the cell it wraps to.
     *
     * @param col Column of the cell, possibly outside the map.
     * @param row Row of the cell, possibly outside the map.
     * @param outCol Receives the column the actor ends up in.
     * @param outRow Receives the row the actor ends up in.
     * @return true when the cell wrapped; false when it was not a tunnel
     *         leaving the map.
     */
    [[nodiscard]] bool wrapCell(int col, int row, int& outCol, int& outRow) const;

    /**
     * @brief Checks that a level can actually be played.
     *
     * @return The first problem found, or `ValidationError::None`.
     */
    [[nodiscard]] ValidationError validate() const;

    /**
     * @brief Human-readable form of a validation error.
     *
     * @param error The error to describe.
     * @return A short description, in English.
     */
    [[nodiscard]] static const char* describe(ValidationError error);

    /**
     * @brief The ghost house's rectangle, in cells.
     *
     * The house has a wall of its own, the way the cabinet drew it: a closed
     * rectangle rather than a block of the maze's masonry, with a doorway
     * across the top.
     *
     * @return The house's bounds.
     */
    [[nodiscard]] HouseBounds houseBounds() const { return house_; }

    /**
     * @brief The built-in level's layout, one string per row.
     *
     * The legend is: `#` wall, `.` corridor with a dot, `o` corridor with a
     * power pellet, ` ` corridor with nothing, `-` tunnel, `=` house doorway
     * and `_` house interior.
     */
    static const char* const* levelLayout();

    /**
     * @brief The built-in level's configuration.
     * @return A reference to the static level configuration.
     */
    static const PacmanLevelConfig& defaultLevel();

private:
    const PacmanLevelConfig* config_;
    HouseBounds house_;
};

}  // namespace pacman
