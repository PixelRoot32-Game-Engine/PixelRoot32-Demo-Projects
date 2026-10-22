#include <cstring>

#include <unity.h>

#include "rules/PacmanMaze.h"
#include "rules/PacmanRouteField.h"

#include "../../test_config.h"

using namespace pacman;

void setUp(void) {
    test_setup();
}

void tearDown(void) {
    test_teardown();
}

/* ---------------------------------------------------------------------------
 * The shipped level
 * ------------------------------------------------------------------------- */

void test_pacman_maze_shipped_level_is_valid(void) {
    PacmanMaze maze;
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::None, maze.validate());
}

void test_pacman_maze_shipped_level_rows_have_the_declared_width(void) {
    const PacmanLevelConfig& level = PacmanMaze::defaultLevel();
    for (int row = 0; row < level.rowCount; ++row) {
        TEST_ASSERT_EQUAL_INT(kMazeCols, static_cast<int>(std::strlen(level.rows[row])));
    }
}

void test_pacman_maze_shipped_level_holds_the_expected_collectibles(void) {
    PacmanMaze maze;
    // 256 dots and 4 power pellets.
    TEST_ASSERT_EQUAL_INT(260, maze.collectibleCount());

    int dots = 0;
    int pellets = 0;
    for (int row = 0; row < maze.rowCount(); ++row) {
        for (int col = 0; col < maze.colCount(); ++col) {
            const Collectible collectible = maze.collectibleAt(col, row);
            if (collectible == Collectible::Dot) {
                ++dots;
            } else if (collectible == Collectible::PowerPellet) {
                ++pellets;
            }
        }
    }
    TEST_ASSERT_EQUAL_INT(256, dots);
    TEST_ASSERT_EQUAL_INT(4, pellets);
}

void test_pacman_maze_player_start_is_walkable_and_outside_the_house(void) {
    PacmanMaze maze;
    const int col = maze.playerStartCol();
    const int row = maze.playerStartRow();
    TEST_ASSERT_TRUE(maze.isWalkableForPlayer(col, row));
    TEST_ASSERT_NOT_EQUAL(CellType::House, maze.cellType(col, row));
    TEST_ASSERT_NOT_EQUAL(CellType::HouseDoor, maze.cellType(col, row));
}

void test_pacman_maze_enemy_starts_are_walkable(void) {
    PacmanMaze maze;
    for (int index = 0; index < maze.enemyCount(); ++index) {
        TEST_ASSERT_TRUE(maze.isWalkableForEnemy(maze.enemyStartCol(index), maze.enemyStartRow(index)));
    }
}

void test_pacman_maze_player_cannot_enter_the_ghost_house(void) {
    PacmanMaze maze;
    // The doorway and the house interior are the only cells the player is
    // kept out of.
    for (int row = 0; row < maze.rowCount(); ++row) {
        for (int col = 0; col < maze.colCount(); ++col) {
            const CellType type = maze.cellType(col, row);
            if (type == CellType::House || type == CellType::HouseDoor) {
                TEST_ASSERT_FALSE(maze.isWalkableForPlayer(col, row));
                TEST_ASSERT_TRUE(maze.isWalkableForEnemy(col, row));
            }
        }
    }
}

void test_pacman_maze_tunnel_row_wraps_around_the_map(void) {
    PacmanMaze maze;
    TEST_ASSERT_TRUE(maze.isTunnel(0, kTunnelRow));
    TEST_ASSERT_TRUE(maze.isTunnel(maze.colCount() - 1, kTunnelRow));

    int wrappedCol = -1;
    int wrappedRow = -1;
    TEST_ASSERT_TRUE(maze.wrapCell(-1, kTunnelRow, wrappedCol, wrappedRow));
    TEST_ASSERT_EQUAL_INT(maze.colCount() - 1, wrappedCol);
    TEST_ASSERT_EQUAL_INT(kTunnelRow, wrappedRow);

    TEST_ASSERT_TRUE(maze.wrapCell(maze.colCount(), kTunnelRow, wrappedCol, wrappedRow));
    TEST_ASSERT_EQUAL_INT(0, wrappedCol);
}

void test_pacman_maze_rows_without_tunnels_do_not_wrap(void) {
    PacmanMaze maze;
    int wrappedCol = -1;
    int wrappedRow = -1;
    TEST_ASSERT_FALSE(maze.wrapCell(-1, 0, wrappedCol, wrappedRow));
    TEST_ASSERT_FALSE(maze.wrapCell(maze.colCount(), 5, wrappedCol, wrappedRow));
}

/* ---------------------------------------------------------------------------
 * The validator
 * ------------------------------------------------------------------------- */

/**
 * @brief Builds a level configuration around a caller-supplied layout.
 *
 * @param rows The layout rows.
 * @param rowCount Number of rows.
 * @param colCount Number of columns.
 * @param playerStartCol Column of the player's start.
 * @param playerStartRow Row of the player's start.
 * @return The assembled configuration.
 */
PacmanLevelConfig makeLevel(const char* const* rows, int rowCount, int colCount,
                             int playerStartCol, int playerStartRow) {
    PacmanLevelConfig config = {};
    config.rows = rows;
    config.rowCount = rowCount;
    config.colCount = colCount;
    config.playerStartCol = playerStartCol;
    config.playerStartRow = playerStartRow;
    config.enemyCount = 0;
    return config;
}

void test_pacman_maze_validator_rejects_a_row_of_the_wrong_width(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 1, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::RowLength, maze.validate());
}

void test_pacman_maze_validator_rejects_an_open_border(void) {
    static const char* const kRows[] = {
        "##.",
        "#.#",
        "###",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 3, 1, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::Border, maze.validate());
}

void test_pacman_maze_validator_rejects_a_start_inside_a_wall(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 0, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::StartInWall, maze.validate());
}

void test_pacman_maze_validator_rejects_a_start_outside_the_map(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 9, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::MissingStart, maze.validate());
}

void test_pacman_maze_validator_rejects_a_level_without_collectibles(void) {
    static const char* const kRows[] = {
        "#####",
        "#   #",
        "#####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 1, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::NoCollectibles, maze.validate());
}

void test_pacman_maze_validator_rejects_an_unreachable_cell(void) {
    static const char* const kRows[] = {
        "#####",
        "#.#.#",
        "#####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 1, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::UnreachableCell, maze.validate());
}

void test_pacman_maze_validator_accepts_a_small_valid_level(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeLevel(kRows, 3, 5, 1, 1);
    const PacmanMaze maze(config);
    TEST_ASSERT_EQUAL_INT(PacmanMaze::ValidationError::None, maze.validate());
}

/* ---------------------------------------------------------------------------
 * Route distances
 * ------------------------------------------------------------------------- */

void test_pacman_route_field_measures_the_shortest_route(void) {
    PacmanMaze maze;
    PacmanRouteField field;

    field.compute(maze, maze.playerStartCol(), maze.playerStartRow(), false);
    TEST_ASSERT_EQUAL_INT(0, field.distanceAt(maze.playerStartCol(), maze.playerStartRow()));
    // The cell next to the start along the corridor is one step away.
    TEST_ASSERT_EQUAL_INT(1, field.distanceAt(maze.playerStartCol() - 1, maze.playerStartRow()));
    TEST_ASSERT_EQUAL_INT(1, field.distanceAt(maze.playerStartCol() + 1, maze.playerStartRow()));
}

void test_pacman_route_field_reports_unreachable_cells(void) {
    PacmanMaze maze;
    PacmanRouteField field;

    field.compute(maze, maze.playerStartCol(), maze.playerStartRow(), false);

    // The ghost house is out of the player's reach.
    TEST_ASSERT_EQUAL_INT(PacmanRouteField::kUnreachable, field.distanceAt(13, 14));

    // The same cell is reachable for an enemy, which may use the doorway.
    field.compute(maze, 13, 14, true);
    TEST_ASSERT_TRUE(field.distanceAt(maze.playerStartCol(), maze.playerStartRow()) >= 0);
}

void test_pacman_route_field_crosses_the_tunnel(void) {
    PacmanMaze maze;
    PacmanRouteField field;

    // A target on the far side of the left tunnel is reachable by wrapping.
    field.compute(maze, maze.colCount() - 1, kTunnelRow, false);
    TEST_ASSERT_TRUE(field.isReachable(0, kTunnelRow));
    TEST_ASSERT_EQUAL_INT(1, field.distanceAt(0, kTunnelRow));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_pacman_maze_shipped_level_is_valid);
    RUN_TEST(test_pacman_maze_shipped_level_rows_have_the_declared_width);
    RUN_TEST(test_pacman_maze_shipped_level_holds_the_expected_collectibles);
    RUN_TEST(test_pacman_maze_player_start_is_walkable_and_outside_the_house);
    RUN_TEST(test_pacman_maze_enemy_starts_are_walkable);
    RUN_TEST(test_pacman_maze_player_cannot_enter_the_ghost_house);
    RUN_TEST(test_pacman_maze_tunnel_row_wraps_around_the_map);
    RUN_TEST(test_pacman_maze_rows_without_tunnels_do_not_wrap);
    RUN_TEST(test_pacman_maze_validator_rejects_a_row_of_the_wrong_width);
    RUN_TEST(test_pacman_maze_validator_rejects_an_open_border);
    RUN_TEST(test_pacman_maze_validator_rejects_a_start_inside_a_wall);
    RUN_TEST(test_pacman_maze_validator_rejects_a_start_outside_the_map);
    RUN_TEST(test_pacman_maze_validator_rejects_a_level_without_collectibles);
    RUN_TEST(test_pacman_maze_validator_rejects_an_unreachable_cell);
    RUN_TEST(test_pacman_maze_validator_accepts_a_small_valid_level);
    RUN_TEST(test_pacman_route_field_measures_the_shortest_route);
    RUN_TEST(test_pacman_route_field_reports_unreachable_cells);
    RUN_TEST(test_pacman_route_field_crosses_the_tunnel);

    return UNITY_END();
}
