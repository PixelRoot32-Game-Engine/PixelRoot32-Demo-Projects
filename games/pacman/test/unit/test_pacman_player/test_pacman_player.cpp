#include <unity.h>

#include "rules/PacmanMaze.h"
#include "rules/PacmanPlayer.h"

#include "../../test_config.h"

using namespace pacman;

void setUp(void) {
    test_setup();
}

void tearDown(void) {
    test_teardown();
}

/**
 * @brief Runs a number of logic steps on a player.
 *
 * @param player The player to advance.
 * @param maze The maze the player moves in.
 * @param steps How many steps to run.
 */
void runSteps(PacmanPlayer& player, const PacmanMaze& maze, int steps) {
    for (int index = 0; index < steps; ++index) {
        player.step(maze);
    }
}

/* ---------------------------------------------------------------------------
 * Starting position
 * ------------------------------------------------------------------------- */

void test_pacman_player_starts_at_the_level_start_at_rest(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);

    TEST_ASSERT_EQUAL_INT(maze.playerStartCol(), player.cellCol());
    TEST_ASSERT_EQUAL_INT(maze.playerStartRow(), player.cellRow());
    TEST_ASSERT_FALSE(player.isMoving());
    TEST_ASSERT_EQUAL_INT(Direction::None, player.direction());
}

/* ---------------------------------------------------------------------------
 * Moving
 * ------------------------------------------------------------------------- */

void test_pacman_player_crosses_a_cell_in_the_configured_number_of_steps(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);
    player.requestDirection(Direction::Left);

    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() - 1, player.cellCol());
    TEST_ASSERT_EQUAL_INT(maze.playerStartRow(), player.cellRow());

    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() - 2, player.cellCol());
}

void test_pacman_player_does_not_move_into_a_wall(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);

    // The cell above the player's start is the ghost house, which the player
    // may not enter.
    TEST_ASSERT_FALSE(maze.isWalkableForPlayer(maze.playerStartCol(), maze.playerStartRow() - 1));

    player.requestDirection(Direction::Up);
    runSteps(player, maze, kPlayerStepsPerCell * 2);

    TEST_ASSERT_EQUAL_INT(maze.playerStartCol(), player.cellCol());
    TEST_ASSERT_EQUAL_INT(maze.playerStartRow(), player.cellRow());
    TEST_ASSERT_EQUAL_INT(Direction::None, player.direction());
}

void test_pacman_player_keeps_its_direction_when_the_request_cannot_be_taken(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);

    player.requestDirection(Direction::Left);
    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() - 1, player.cellCol());

    // Up is not possible from the cell the player just reached, so the
    // player carries on to the left.
    player.requestDirection(Direction::Up);
    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() - 2, player.cellCol());
    TEST_ASSERT_EQUAL_INT(Direction::Left, player.direction());
}

void test_pacman_player_keeps_a_pending_turn_until_it_can_be_taken(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);

    player.requestDirection(Direction::Left);
    runSteps(player, maze, kPlayerStepsPerCell);

    // Ask for a turn that is not possible yet, then let the player walk on.
    player.requestDirection(Direction::Up);
    player.requestDirection(Direction::None);
    runSteps(player, maze, kPlayerStepsPerCell * 3);

    // The player has reached the column where the turn upwards is possible
    // and has taken it.
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() - 4, player.cellCol());
    TEST_ASSERT_EQUAL_INT(Direction::Up, player.direction());
    TEST_ASSERT_EQUAL_INT(Direction::Up, player.requestedDirection());
}

void test_pacman_player_reports_its_travel_direction(void) {
    PacmanMaze maze;
    PacmanPlayer player;
    player.reset(maze);

    player.requestDirection(Direction::Right);
    player.step(maze);
    TEST_ASSERT_EQUAL_INT(Direction::Right, player.direction());
    TEST_ASSERT_TRUE(player.isMoving());
    TEST_ASSERT_EQUAL_INT(maze.playerStartCol() + 1, player.targetCol());
}

/* ---------------------------------------------------------------------------
 * Tunnels
 * ------------------------------------------------------------------------- */

void test_pacman_player_wraps_around_a_tunnel(void) {
    static const char* const kRows[] = {
        "#####",
        "-...-",
        "#####",
    };
    PacmanLevelConfig config = {};
    config.rows = kRows;
    config.rowCount = 3;
    config.colCount = 5;
    config.playerStartCol = 1;
    config.playerStartRow = 1;
    config.enemyCount = 0;

    const PacmanMaze maze(config);
    PacmanPlayer player;
    player.reset(maze);

    player.requestDirection(Direction::Left);
    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(0, player.cellCol());

    // The next step to the left leaves the map and comes back on the right.
    runSteps(player, maze, kPlayerStepsPerCell);
    TEST_ASSERT_EQUAL_INT(4, player.cellCol());
    TEST_ASSERT_EQUAL_INT(1, player.cellRow());
}

void test_pacman_player_leaves_the_map_only_through_a_tunnel(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    PacmanLevelConfig config = {};
    config.rows = kRows;
    config.rowCount = 3;
    config.colCount = 5;
    config.playerStartCol = 1;
    config.playerStartRow = 1;
    config.enemyCount = 0;

    const PacmanMaze maze(config);
    PacmanPlayer player;
    player.reset(maze);

    player.requestDirection(Direction::Left);
    runSteps(player, maze, kPlayerStepsPerCell * 2);
    TEST_ASSERT_EQUAL_INT(1, player.cellCol());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_pacman_player_starts_at_the_level_start_at_rest);
    RUN_TEST(test_pacman_player_crosses_a_cell_in_the_configured_number_of_steps);
    RUN_TEST(test_pacman_player_does_not_move_into_a_wall);
    RUN_TEST(test_pacman_player_keeps_its_direction_when_the_request_cannot_be_taken);
    RUN_TEST(test_pacman_player_keeps_a_pending_turn_until_it_can_be_taken);
    RUN_TEST(test_pacman_player_reports_its_travel_direction);
    RUN_TEST(test_pacman_player_wraps_around_a_tunnel);
    RUN_TEST(test_pacman_player_leaves_the_map_only_through_a_tunnel);

    return UNITY_END();
}
