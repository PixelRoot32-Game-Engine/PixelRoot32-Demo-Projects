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
 * @brief Builds a small single-row level.
 *
 * @param rows The layout rows.
 * @return A level configuration for that layout.
 */
PacmanLevelConfig makeCorridorLevel(const char* const* rows) {
    PacmanLevelConfig config = {};
    config.rows = rows;
    config.rowCount = 3;
    config.colCount = 5;
    config.playerStartCol = 1;
    config.playerStartRow = 1;
    config.enemyCount = 0;
    return config;
}

/**
 * @brief Runs the steps that get a fresh game past its opening freeze.
 *
 * A game starts with the board frozen for the length of the respawn
 * sequence: that is the "ready" beat before the player is allowed to move.
 *
 * @param session The session to advance.
 */
void runPastOpening(PacmanSession& session) {
    for (int index = 0; index < kRespawnSteps; ++index) {
        session.step(Direction::None);
    }
}

/* ---------------------------------------------------------------------------
 * Starting a game
 * ------------------------------------------------------------------------- */

void test_pacman_session_starts_in_the_title_state(void) {
    PacmanSession session;
    TEST_ASSERT_EQUAL_INT(GameState::Title, session.state());
}

void test_pacman_session_new_game_resets_score_lives_and_collectibles(void) {
    PacmanSession session;
    session.startNewGame();
    runPastOpening(session);

    TEST_ASSERT_EQUAL_INT(GameState::Playing, session.state());
    TEST_ASSERT_EQUAL_INT(0, session.score());
    TEST_ASSERT_EQUAL_INT(kStartingLives, session.lives());
    TEST_ASSERT_EQUAL_INT(session.maze().collectibleCount(), session.remainingCollectibles());
    TEST_ASSERT_FALSE(session.powerIsActive());
}

/* ---------------------------------------------------------------------------
 * Collectibles and scoring
 * ------------------------------------------------------------------------- */

void test_pacman_session_collects_a_dot_and_updates_the_counter(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    const int before = session.remainingCollectibles();
    session.step(Direction::None);

    TEST_ASSERT_EQUAL_INT(before - 1, session.remainingCollectibles());
    TEST_ASSERT_EQUAL_INT(kScoreDot, session.score());
}

void test_pacman_session_never_collects_the_same_dot_twice(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    session.step(Direction::None);
    const int scoreAfterFirst = session.score();
    const int remainingAfterFirst = session.remainingCollectibles();

    // Stand still on the same cell for a while.
    for (int index = 0; index < kPlayerStepsPerCell * 3; ++index) {
        session.step(Direction::None);
    }

    TEST_ASSERT_EQUAL_INT(scoreAfterFirst, session.score());
    TEST_ASSERT_EQUAL_INT(remainingAfterFirst, session.remainingCollectibles());
}

void test_pacman_session_counter_never_goes_negative(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    for (int index = 0; index < kPlayerStepsPerCell * 10; ++index) {
        session.step(Direction::Right);
    }

    TEST_ASSERT_TRUE(session.remainingCollectibles() >= 0);
}

void test_pacman_session_collecting_does_not_change_the_terrain(void) {
    PacmanSession session;
    session.startNewGame();
    runPastOpening(session);

    TEST_ASSERT_EQUAL_INT(Collectible::Dot, session.collectibleAt(13, 17));

    session.step(Direction::None);
    TEST_ASSERT_EQUAL_INT(Collectible::None, session.collectibleAt(13, 17));

    // The maze still describes what the cell holds; the session keeps track
    // of what is still lying on the floor.
    TEST_ASSERT_EQUAL_INT(Collectible::Dot, session.maze().collectibleAt(13, 17));
}

/* ---------------------------------------------------------------------------
 * The power effect
 * ------------------------------------------------------------------------- */

void test_pacman_session_power_pellet_starts_the_effect(void) {
    static const char* const kRows[] = {
        "#####",
        "#o..#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    session.step(Direction::None);

    TEST_ASSERT_TRUE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(kScorePower, session.score());
}

void test_pacman_session_power_effect_ends_when_its_timer_runs_out(void) {
    static const char* const kRows[] = {
        "#####",
        "#o..#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    // The player stays on the pellet's cell, so the level never ends and
    // the timer is free to run all the way down.
    session.step(Direction::None);
    TEST_ASSERT_TRUE(session.powerIsActive());

    for (int index = 0; index < kPowerDurationSteps; ++index) {
        session.step(Direction::None);
    }
    TEST_ASSERT_FALSE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(0, session.powerStepsLeft());
}

void test_pacman_session_second_pellet_refreshes_the_timer(void) {
    static const char* const kRows[] = {
        "#####",
        "#o.o#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    session.step(Direction::Right);
    TEST_ASSERT_EQUAL_INT(kPowerDurationSteps - 1, session.powerStepsLeft());

    // Walk to the second pellet.
    for (int index = 0; index < kPlayerStepsPerCell * 2; ++index) {
        session.step(Direction::Right);
    }
    TEST_ASSERT_TRUE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(kPowerDurationSteps - 1, session.powerStepsLeft());
}

/* ---------------------------------------------------------------------------
 * End conditions and pausing
 * ------------------------------------------------------------------------- */

void test_pacman_session_finishes_the_level_when_nothing_is_left(void) {
    static const char* const kRows[] = {
        "#####",
        "#...#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    TEST_ASSERT_EQUAL_INT(3, session.remainingCollectibles());

    session.step(Direction::Right);  // collects the dot at the start cell
    for (int index = 0; index < kPlayerStepsPerCell; ++index) {
        session.step(Direction::Right);  // collects the second dot
    }
    for (int index = 0; index < kPlayerStepsPerCell; ++index) {
        session.step(Direction::Right);  // collects the last dot
    }

    TEST_ASSERT_EQUAL_INT(0, session.remainingCollectibles());
    TEST_ASSERT_EQUAL_INT(GameState::Victory, session.state());
}

void test_pacman_session_opening_freezes_the_board(void) {
    PacmanSession session;
    session.startNewGame();

    TEST_ASSERT_TRUE(session.isRespawning());

    // The board does not move while the opening freeze lasts.
    const int startCol = session.player().cellCol();
    session.step(Direction::Right);
    TEST_ASSERT_EQUAL_INT(startCol, session.player().cellCol());

    runPastOpening(session);
    TEST_ASSERT_FALSE(session.isRespawning());

    // And it moves once the freeze is over: the player takes a whole cell to
    // cross it, so the step count is the one the rules use.
    for (int index = 0; index < kPlayerStepsPerCell; ++index) {
        session.step(Direction::Right);
    }
    TEST_ASSERT_NOT_EQUAL(startCol, session.player().cellCol());
}

void test_pacman_session_pause_freezes_the_simulation(void) {    PacmanSession session;
    session.startNewGame();
    runPastOpening(session);

    session.togglePause();
    TEST_ASSERT_EQUAL_INT(GameState::Paused, session.state());

    const int scoreBefore = session.score();
    session.step(Direction::Right);
    TEST_ASSERT_EQUAL_INT(scoreBefore, session.score());

    session.togglePause();
    TEST_ASSERT_EQUAL_INT(GameState::Playing, session.state());
}

void test_pacman_session_restart_clears_the_previous_game(void) {
    static const char* const kRows[] = {
        "#####",
        "#o..#",
        "#####",
    };
    const PacmanLevelConfig config = makeCorridorLevel(kRows);
    PacmanSession session(config);
    session.startNewGame();
    runPastOpening(session);

    session.step(Direction::Right);
    TEST_ASSERT_TRUE(session.score() > 0);
    TEST_ASSERT_TRUE(session.powerIsActive());

    session.startNewGame();
    runPastOpening(session);

    TEST_ASSERT_EQUAL_INT(0, session.score());
    TEST_ASSERT_EQUAL_INT(kStartingLives, session.lives());
    TEST_ASSERT_EQUAL_INT(session.maze().collectibleCount(), session.remainingCollectibles());
    TEST_ASSERT_FALSE(session.powerIsActive());
    TEST_ASSERT_EQUAL_INT(GameState::Playing, session.state());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_pacman_session_starts_in_the_title_state);
    RUN_TEST(test_pacman_session_new_game_resets_score_lives_and_collectibles);
    RUN_TEST(test_pacman_session_collects_a_dot_and_updates_the_counter);
    RUN_TEST(test_pacman_session_never_collects_the_same_dot_twice);
    RUN_TEST(test_pacman_session_counter_never_goes_negative);
    RUN_TEST(test_pacman_session_collecting_does_not_change_the_terrain);
    RUN_TEST(test_pacman_session_power_pellet_starts_the_effect);
    RUN_TEST(test_pacman_session_power_effect_ends_when_its_timer_runs_out);
    RUN_TEST(test_pacman_session_second_pellet_refreshes_the_timer);
    RUN_TEST(test_pacman_session_finishes_the_level_when_nothing_is_left);
    RUN_TEST(test_pacman_session_opening_freezes_the_board);
    RUN_TEST(test_pacman_session_pause_freezes_the_simulation);
    RUN_TEST(test_pacman_session_restart_clears_the_previous_game);

    return UNITY_END();
}
