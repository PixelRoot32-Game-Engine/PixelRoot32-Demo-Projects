#include <unity.h>

#include "rules/PacmanEnemy.h"
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
 * @brief Builds the context a hunter moves in.
 *
 * @param playerCol Column of the player's cell.
 * @param playerRow Row of the player's cell.
 * @param powerActive Whether the power effect is running.
 * @return The context.
 */
PacmanEnemyContext makeContext(int playerCol, int playerRow, bool powerActive) {
    PacmanEnemyContext context;
    context.playerCol = playerCol;
    context.playerRow = playerRow;
    context.playerDirection = Direction::None;
    context.powerIsActive = powerActive;
    context.chaserCol = playerCol;
    context.chaserRow = playerRow;
    return context;
}

/**
 * @brief Runs a number of logic steps on a hunter.
 *
 * @param enemy The hunter to advance.
 * @param maze The maze the hunter moves in.
 * @param context The context the hunter moves in.
 * @param steps How many steps to run.
 */
void runSteps(PacmanEnemy& enemy, const PacmanMaze& maze, const PacmanEnemyContext& context, int steps) {
    for (int index = 0; index < steps; ++index) {
        enemy.step(maze, context);
    }
}

/* ---------------------------------------------------------------------------
 * Starting positions
 * ------------------------------------------------------------------------- */

void test_pacman_enemy_starts_at_its_start_cell(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    TEST_ASSERT_EQUAL_INT(maze.enemyStartCol(0), enemy.cellCol());
    TEST_ASSERT_EQUAL_INT(maze.enemyStartRow(0), enemy.cellRow());
    TEST_ASSERT_EQUAL_INT(EnemyPersonality::Chaser, enemy.personality());
}

void test_pacman_enemy_has_one_personality_per_index(void) {
    PacmanMaze maze;
    TEST_ASSERT_EQUAL_INT(4, maze.enemyCount());

    PacmanEnemy first;
    first.reset(maze, 0);
    PacmanEnemy second;
    second.reset(maze, 1);
    PacmanEnemy third;
    third.reset(maze, 2);
    PacmanEnemy fourth;
    fourth.reset(maze, 3);

    TEST_ASSERT_EQUAL_INT(EnemyPersonality::Chaser, first.personality());
    TEST_ASSERT_EQUAL_INT(EnemyPersonality::Ambusher, second.personality());
    TEST_ASSERT_EQUAL_INT(EnemyPersonality::Flanker, third.personality());
    TEST_ASSERT_EQUAL_INT(EnemyPersonality::Timid, fourth.personality());
}

/* ---------------------------------------------------------------------------
 * Movement
 * ------------------------------------------------------------------------- */

void test_pacman_enemy_never_ends_up_inside_a_wall(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    const PacmanEnemyContext context = makeContext(13, 17, false);
    for (int index = 0; index < 2000; ++index) {
        enemy.step(maze, context);
        TEST_ASSERT_FALSE(maze.isWall(enemy.cellCol(), enemy.cellRow()));
    }
}

void test_pacman_enemy_moves_at_its_own_pace(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    const PacmanEnemyContext context = makeContext(13, 17, false);
    const int startCol = enemy.cellCol();
    const int startRow = enemy.cellRow();

    runSteps(enemy, maze, context, kEnemyStepsPerCell);
    TEST_ASSERT_TRUE(enemy.cellCol() != startCol || enemy.cellRow() != startRow);
}

void test_pacman_enemy_is_deterministic(void) {
    PacmanMaze maze;
    const PacmanEnemyContext context = makeContext(13, 17, false);

    PacmanEnemy first;
    first.reset(maze, 0);
    PacmanEnemy second;
    second.reset(maze, 0);

    for (int index = 0; index < 500; ++index) {
        first.step(maze, context);
        second.step(maze, context);
    }

    TEST_ASSERT_EQUAL_INT(first.cellCol(), second.cellCol());
    TEST_ASSERT_EQUAL_INT(first.cellRow(), second.cellRow());
    TEST_ASSERT_EQUAL_INT(first.direction(), second.direction());
}

/* ---------------------------------------------------------------------------
 * The power effect
 * ------------------------------------------------------------------------- */

void test_pacman_enemy_flees_while_the_power_is_active(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    const PacmanEnemyContext hunting = makeContext(13, 17, false);
    runSteps(enemy, maze, hunting, 10);

    const PacmanEnemyContext powered = makeContext(13, 17, true);
    enemy.step(maze, powered);

    TEST_ASSERT_EQUAL_INT(EnemyState::Flee, enemy.state());
    TEST_ASSERT_TRUE(enemy.isVulnerable());
}

void test_pacman_enemy_returns_to_normal_when_the_power_ends(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    const PacmanEnemyContext hunting = makeContext(13, 17, false);
    runSteps(enemy, maze, hunting, 10);

    const PacmanEnemyContext powered = makeContext(13, 17, true);
    enemy.step(maze, powered);
    TEST_ASSERT_EQUAL_INT(EnemyState::Flee, enemy.state());

    enemy.step(maze, hunting);
    TEST_ASSERT_NOT_EQUAL(EnemyState::Flee, enemy.state());
}

void test_pacman_enemy_leaving_the_house_keeps_its_state_while_the_power_is_active(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 1);

    // The second hunter starts inside the house and is waiting there.
    TEST_ASSERT_EQUAL_INT(EnemyState::Respawn, enemy.state());
    TEST_ASSERT_FALSE(enemy.isPlaying());

    const PacmanEnemyContext powered = makeContext(13, 17, true);
    runSteps(enemy, maze, powered, 10);
    TEST_ASSERT_EQUAL_INT(EnemyState::Respawn, enemy.state());
}

void test_pacman_enemy_leaves_the_house_and_joins_the_maze(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 1);
    TEST_ASSERT_EQUAL_INT(EnemyState::Respawn, enemy.state());

    const PacmanEnemyContext context = makeContext(13, 17, false);
    // Wait out the release delay, then walk to the exit cell.
    runSteps(enemy, maze, context, kEnemyReleaseDelaySteps[1] + kEnemyStepsPerCell * 40);

    TEST_ASSERT_TRUE(enemy.isPlaying());
    TEST_ASSERT_NOT_EQUAL(EnemyState::Respawn, enemy.state());
}

/* ---------------------------------------------------------------------------
 * Defeating a hunter
 * ------------------------------------------------------------------------- */

void test_pacman_enemy_defeated_returns_home_and_comes_back(void) {
    PacmanMaze maze;
    PacmanEnemy enemy;
    enemy.reset(maze, 0);

    const PacmanEnemyContext context = makeContext(13, 17, false);
    runSteps(enemy, maze, context, 10);

    enemy.defeat();
    TEST_ASSERT_EQUAL_INT(EnemyState::Return, enemy.state());
    TEST_ASSERT_TRUE(enemy.isDefeated());
    TEST_ASSERT_FALSE(enemy.isPlaying());

    // It walks home and waits there before it plays again.
    bool reachedHome = false;
    for (int index = 0; index < kEnemyStepsPerCell * 400; ++index) {
        enemy.step(maze, context);
        if (enemy.cellCol() == maze.enemyStartCol(0) && enemy.cellRow() == maze.enemyStartRow(0)) {
            reachedHome = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(reachedHome);
    TEST_ASSERT_EQUAL_INT(EnemyState::Respawn, enemy.state());

    // And it comes back out to play.
    runSteps(enemy, maze, context, kEnemyRespawnDelaySteps + kEnemyStepsPerCell * 10);
    TEST_ASSERT_TRUE(enemy.isPlaying());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_pacman_enemy_starts_at_its_start_cell);
    RUN_TEST(test_pacman_enemy_has_one_personality_per_index);
    RUN_TEST(test_pacman_enemy_never_ends_up_inside_a_wall);
    RUN_TEST(test_pacman_enemy_moves_at_its_own_pace);
    RUN_TEST(test_pacman_enemy_is_deterministic);
    RUN_TEST(test_pacman_enemy_flees_while_the_power_is_active);
    RUN_TEST(test_pacman_enemy_returns_to_normal_when_the_power_ends);
    RUN_TEST(test_pacman_enemy_leaving_the_house_keeps_its_state_while_the_power_is_active);
    RUN_TEST(test_pacman_enemy_leaves_the_house_and_joins_the_maze);
    RUN_TEST(test_pacman_enemy_defeated_returns_home_and_comes_back);

    return UNITY_END();
}
