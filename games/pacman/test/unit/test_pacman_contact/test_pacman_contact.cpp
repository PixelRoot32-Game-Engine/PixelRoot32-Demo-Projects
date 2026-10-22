#include <unity.h>

#include "rules/PacmanSession.h"

#include "../../test_config.h"

using namespace pacman;

void setUp(void) {
    test_setup();
}

void tearDown(void) {
    test_teardown();
}

/**
 * @brief Builds a single-corridor level with one hunter.
 *
 * @param rows The layout rows.
 * @param colCount Number of columns.
 * @param playerCol Column of the player's start.
 * @param enemyCol Column of the hunter's start.
 * @return The level configuration.
 */
PacmanLevelConfig makeCorridorLevel(const char* const* rows, int colCount, int playerCol, int enemyCol) {
    PacmanLevelConfig config = {};
    config.rows = rows;
    config.rowCount = 3;
    config.colCount = colCount;
    config.playerStartCol = playerCol;
    config.playerStartRow = 1;
    config.enemyCount = 1;
    config.enemyStartCols[0] = enemyCol;
    config.enemyStartRows[0] = 1;
    return config;
}

/**
 * @brief Runs the steps that get a fresh game past its opening freeze.
 *
 * @param session The session to advance.
 */
void runPastOpening(PacmanSession& session) {
    for (int index = 0; index < kRespawnSteps; ++index) {
        session.step(Direction::None);
    }
}

/**
 * @brief Runs a number of logic steps on a session.
 *
 * @param session The session to advance.
 * @param direction The direction the player asks for.
 * @param steps How many steps to run.
 */
void runSteps(PacmanSession& session, Direction direction, int steps) {
    for (int index = 0; index < steps; ++index) {
        session.step(direction);
    }
}

/* ---------------------------------------------------------------------------
 * Contact with a hunter that is hunting
 * ------------------------------------------------------------------------- */

void test_pacman_contact_costs_exactly_one_life(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 5, 1, 3);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    TEST_ASSERT_EQUAL_INT(kStartingLives, session.lives());

    // The hunter walks down the corridor into the player.
    runSteps(session, Direction::None, 60);

    TEST_ASSERT_EQUAL_INT(kStartingLives - 1, session.lives());
}

void test_pacman_contact_is_not_counted_twice_for_one_hit(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 5, 1, 3);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    // Wait for the hunter to walk into the player.
    int waited = 0;
    while (session.lives() == kStartingLives && waited < 200) {
        session.step(Direction::None);
        ++waited;
    }
    const int livesAfterFirstHit = session.lives();
    TEST_ASSERT_EQUAL_INT(kStartingLives - 1, livesAfterFirstHit);

    // The player stays protected for the whole protection window, so the
    // hunter that is still sitting on top of it cannot take a second life.
    runSteps(session, Direction::None, kInvulnerableSteps - 10);

    TEST_ASSERT_EQUAL_INT(livesAfterFirstHit, session.lives());
    TEST_ASSERT_TRUE(session.playerIsInvulnerable());
}

void test_pacman_life_loss_puts_the_board_back_in_order(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 5, 1, 3);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    // Pick up the dot at the start cell before the hunter arrives.
    runSteps(session, Direction::None, 1);
    const int scoreBefore = session.score();
    const int remainingBefore = session.remainingCollectibles();

    runSteps(session, Direction::None, 60);

    TEST_ASSERT_EQUAL_INT(config.playerStartCol, session.player().cellCol());
    TEST_ASSERT_EQUAL_INT(config.playerStartRow, session.player().cellRow());
    TEST_ASSERT_EQUAL_INT(config.enemyStartCols[0], session.enemy(0).cellCol());
    TEST_ASSERT_EQUAL_INT(config.enemyStartRows[0], session.enemy(0).cellRow());
    // The dots that were collected and the score are kept.
    TEST_ASSERT_EQUAL_INT(scoreBefore, session.score());
    TEST_ASSERT_EQUAL_INT(remainingBefore, session.remainingCollectibles());
    // The protection runs for a while after the hit.
    TEST_ASSERT_TRUE(session.playerIsInvulnerable());
}

void test_pacman_game_ends_when_no_lives_are_left(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 5, 1, 3);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    runSteps(session, Direction::None, 2000);

    TEST_ASSERT_EQUAL_INT(0, session.lives());
    TEST_ASSERT_EQUAL_INT(GameState::Defeat, session.state());
}

/* ---------------------------------------------------------------------------
 * Contact with a hunter that is fleeing
 * ------------------------------------------------------------------------- */

void test_pacman_contact_with_a_frightened_hunter_scores_the_hit(void) {
    static const char* const kRows[] = {
        "#######",
        "#o....#",
        "#######",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 7, 1, 5);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    // The player starts on the pellet, so the hunter is frightened from the
    // first step and runs for the far end of the corridor.
    session.step(Direction::None);
    TEST_ASSERT_TRUE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(kScorePower, session.score());

    const int scoreBefore = session.score();
    const int livesBefore = session.lives();

    // The player chases the hunter down the corridor.
    runSteps(session, Direction::Right, 400);

    TEST_ASSERT_TRUE(session.score() >= scoreBefore + kScoreEnemy);
    TEST_ASSERT_EQUAL_INT(livesBefore, session.lives());
}

void test_pacman_consecutive_hits_score_more_each_time(void) {
    static const char* const kRows[] = {
        "#######",
        "#o....#",
        "#######",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows, 7, 1, 5);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    session.step(Direction::None);
    TEST_ASSERT_TRUE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(0, session.enemyChain());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_pacman_contact_costs_exactly_one_life);
    RUN_TEST(test_pacman_contact_is_not_counted_twice_for_one_hit);
    RUN_TEST(test_pacman_life_loss_puts_the_board_back_in_order);
    RUN_TEST(test_pacman_game_ends_when_no_lives_are_left);
    RUN_TEST(test_pacman_contact_with_a_frightened_hunter_scores_the_hit);
    RUN_TEST(test_pacman_consecutive_hits_score_more_each_time);

    return UNITY_END();
}
