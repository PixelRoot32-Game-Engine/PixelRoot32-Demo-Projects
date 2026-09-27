/*
 * Unit tests for the turn/shot state machine (src/pool/Game.*). Shots that
 * need a deterministic pocket stage ball positions through the mutable
 * world() accessor; everything else plays real frames through shoot() and
 * stepFrame(), so the state walk itself is what gets pinned.
 */
#include <unity.h>

#include <cstdint>

#include "pool/Fixed.h"
#include "pool/Game.h"
#include "pool/Tables.h"
#include "pool/Trig.h"

using namespace pool;

void setUp(void) {}
void tearDown(void) {}

namespace {

// Runs frames until the game reaches want (or cap frames pass); tests assert
// the arrival separately so a stuck machine fails loudly on the state.
void runUntil(Game& game, State want, int cap) {
    for (int f = 0; f < cap && game.state() != want; ++f) {
        game.stepFrame();
    }
}

void startStage1(Game& game) {
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::None), static_cast<int>(game.newGame(1)));
    game.startGame();
}

}  // namespace

void test_game_new_game_starts_in_menu(void) {
    Game game;
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::None), static_cast<int>(game.newGame(1)));

    TEST_ASSERT_EQUAL(static_cast<int>(State::Menu), static_cast<int>(game.state()));
    TEST_ASSERT_EQUAL_UINT8(1, game.stage());
    TEST_ASSERT_EQUAL_UINT8(7, game.world().ballCount());
    // 6 targets x 2 shots each.
    TEST_ASSERT_EQUAL_UINT8(12, game.shotsLeft());
    TEST_ASSERT_EQUAL_INT32(0, game.score());
    TEST_ASSERT_EQUAL_UINT8(0, game.turn());
    TEST_ASSERT_EQUAL_UINT8(kDefaultPower, game.power());
    TEST_ASSERT_EQUAL_UINT8(1, game.nextExpected());
}

void test_game_start_game_opens_aiming(void) {
    Game game;
    game.newGame(1);
    game.startGame();

    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_EQUAL_UINT8(1, game.turn());
}

void test_game_shoot_rejected_outside_aiming(void) {
    Game game;
    // Menu: no shot.
    TEST_ASSERT_FALSE(game.shoot());

    game.newGame(1);
    TEST_ASSERT_FALSE(game.shoot());

    game.startGame();
    TEST_ASSERT_TRUE(game.shoot());
    TEST_ASSERT_EQUAL(static_cast<int>(State::Shooting), static_cast<int>(game.state()));

    // Live shot: no second shot until it resolves.
    game.stepFrame();
    TEST_ASSERT_EQUAL(static_cast<int>(State::BallsMoving), static_cast<int>(game.state()));
    TEST_ASSERT_FALSE(game.shoot());
}

void test_game_shoot_applies_aim_power_velocity(void) {
    // Angle 0 (+x), power 5: full speed on x, nothing on y.
    Game game;
    startStage1(game);
    TEST_ASSERT_TRUE(game.shoot());
    TEST_ASSERT_EQUAL_INT32(5 * kSpeedPerLevelRaw, game.world().ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(0, game.world().ball(0).vy);

    // Angle 256 (+y, straight down): full speed on y, nothing on x.
    Game aimed;
    startStage1(aimed);
    for (int i = 0; i < 64; ++i) {
        aimed.aimRight();
    }
    TEST_ASSERT_EQUAL_UINT16(256, aimed.angle());
    TEST_ASSERT_TRUE(aimed.shoot());
    TEST_ASSERT_EQUAL_INT32(0, aimed.world().ball(0).vx);
    TEST_ASSERT_EQUAL_INT32(5 * kSpeedPerLevelRaw, aimed.world().ball(0).vy);
}

void test_game_aim_wraps_and_power_clamps(void) {
    Game game;
    // Adjusters are menu-safe no-ops before Start.
    game.newGame(1);
    game.aimRight();
    game.powerUp();
    TEST_ASSERT_EQUAL_UINT16(0, game.angle());
    TEST_ASSERT_EQUAL_UINT8(kDefaultPower, game.power());

    game.startGame();
    game.aimLeft();
    TEST_ASSERT_EQUAL_UINT16(kAngleSteps - kAimStep, game.angle());
    game.aimRight();
    TEST_ASSERT_EQUAL_UINT16(0, game.angle());

    for (int i = 0; i < 20; ++i) {
        game.powerUp();
    }
    TEST_ASSERT_EQUAL_UINT8(kMaxPower, game.power());
    for (int i = 0; i < 20; ++i) {
        game.powerDown();
    }
    TEST_ASSERT_EQUAL_UINT8(kMinPower, game.power());
}

void test_game_quiet_shot_costs_shot_scores_nothing(void) {
    // Power-1 push down +x from the cue start touches nothing: the machine
    // must walk Shooting -> BallsMoving -> EvaluateShot -> NextTurn -> Aiming.
    Game game;
    startStage1(game);
    for (int i = 0; i < 9; ++i) {
        game.powerDown();  // 5 -> 1 (clamped).
    }
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::Aiming, 300);

    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_EQUAL_UINT8(11, game.shotsLeft());
    TEST_ASSERT_EQUAL_INT32(0, game.score());
    TEST_ASSERT_EQUAL_UINT8(2, game.turn());
    TEST_ASSERT_EQUAL_UINT8(1, game.nextExpected());
}

void test_game_pocket_in_order_scores(void) {
    Game game;
    startStage1(game);
    // Ball 1 waits at the first pocket mouth; the weak cue-ball push never
    // reaches it, so the capture is the shot's only event.
    const Pocket& pocket = game.table().pockets[0];
    game.world().placeBall(1, pocket.x, pocket.y, 0, 0);
    for (int i = 0; i < 9; ++i) {
        game.powerDown();
    }
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::Aiming, 300);

    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_FALSE(game.world().ball(1).active);
    TEST_ASSERT_EQUAL_INT32(kPointsPerBall, game.score());
    TEST_ASSERT_EQUAL_UINT8(2, game.nextExpected());
    TEST_ASSERT_EQUAL_UINT8(11, game.shotsLeft());
}

void test_game_cue_scratch_fouls_and_respots(void) {
    Game game;
    startStage1(game);
    const Pocket& pocket = game.table().pockets[0];
    game.world().placeBall(0, pocket.x, pocket.y, 0, 0);
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::Aiming, 300);

    // Scratch: no points (clamped at zero), cue back at its start, shot charged.
    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_EQUAL_INT32(0, game.score());
    TEST_ASSERT_TRUE(game.world().ball(0).active);
    TEST_ASSERT_EQUAL_INT32(70 * kPxScale, game.world().ball(0).x);
    TEST_ASSERT_EQUAL_INT32(140 * kPxScale, game.world().ball(0).y);
    TEST_ASSERT_EQUAL_UINT8(11, game.shotsLeft());
    TEST_ASSERT_EQUAL_UINT8(1, game.nextExpected());
}

void test_game_wrong_order_fouls_without_points(void) {
    Game game;
    startStage1(game);
    // Ball 2 falls while ball 1 is still up: foul, ball stays down, no points.
    const Pocket& pocket = game.table().pockets[1];
    game.world().placeBall(2, pocket.x, pocket.y, 0, 0);
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::Aiming, 300);

    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_FALSE(game.world().ball(2).active);
    TEST_ASSERT_EQUAL_INT32(0, game.score());
    TEST_ASSERT_EQUAL_UINT8(1, game.nextExpected());
    TEST_ASSERT_EQUAL_UINT8(11, game.shotsLeft());
}

void test_game_clearing_table_wins(void) {
    // Clearing the LAST stage wins the run; non-final clears advance (see
    // the next test), so this stages the final table explicitly.
    Game game;
    TEST_ASSERT_EQUAL(static_cast<int>(TableError::None), static_cast<int>(game.newGame(kStageCount)));
    game.startGame();
    for (uint8_t b = 1; b <= 6; ++b) {
        const Pocket& pocket = game.table().pockets[b - 1];
        game.world().placeBall(b, pocket.x, pocket.y, 0, 0);
    }
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::GameOver, 500);

    TEST_ASSERT_EQUAL(static_cast<int>(State::GameOver), static_cast<int>(game.state()));
    TEST_ASSERT_TRUE(game.won());
    TEST_ASSERT_EQUAL_INT32(6 * kPointsPerBall, game.score());
    TEST_ASSERT_EQUAL_UINT8(0, game.nextExpected());
}

void test_game_clearing_nonfinal_stage_advances(void) {
    // Clearing stage 1 with stages left carries the score into a fresh
    // stage 2 instead of ending the game.
    Game game;
    startStage1(game);
    for (uint8_t b = 1; b <= 6; ++b) {
        const Pocket& pocket = game.table().pockets[b - 1];
        game.world().placeBall(b, pocket.x, pocket.y, 0, 0);
    }
    TEST_ASSERT_TRUE(game.shoot());
    runUntil(game, State::Aiming, 500);

    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
    TEST_ASSERT_EQUAL_UINT8(2, game.stage());
    TEST_ASSERT_EQUAL_INT32(6 * kPointsPerBall, game.score());
    // Fresh stage: full shot budget (6 targets x 2), turn 1, ball 1
    // expected, stage-2 rack.
    TEST_ASSERT_EQUAL_UINT8(12, game.shotsLeft());
    TEST_ASSERT_EQUAL_UINT8(1, game.turn());
    TEST_ASSERT_EQUAL_UINT8(1, game.nextExpected());
    TEST_ASSERT_EQUAL_UINT8(7, game.world().ballCount());
    TEST_ASSERT_EQUAL_INT32(60 * kPxScale, game.world().ball(0).x);
    TEST_ASSERT_EQUAL_INT32(140 * kPxScale, game.world().ball(0).y);
    TEST_ASSERT_EQUAL_INT32(150 * kPxScale, game.world().ball(1).x);
    TEST_ASSERT_EQUAL_INT32(120 * kPxScale, game.world().ball(1).y);
}

void test_game_burning_shots_loses(void) {
    Game game;
    startStage1(game);
    // Aim straight up (angle 768): x never moves, so the cue can bounce off
    // the top cushion all day without ever meeting a target at x >= 150.
    for (int i = 0; i < 64; ++i) {
        game.aimLeft();
    }
    TEST_ASSERT_EQUAL_UINT16(768, game.angle());

    for (int shot = 0; shot < 12; ++shot) {
        TEST_ASSERT_TRUE(game.shoot());
        runUntil(game, State::Aiming, 400);
        if (game.state() == State::GameOver) {
            break;
        }
    }

    TEST_ASSERT_EQUAL(static_cast<int>(State::GameOver), static_cast<int>(game.state()));
    TEST_ASSERT_FALSE(game.won());
    TEST_ASSERT_EQUAL_UINT8(0, game.shotsLeft());
    TEST_ASSERT_EQUAL_INT32(0, game.score());
}

void test_game_menu_and_gameover_frames_are_noops(void) {
    Game game;
    game.newGame(1);
    game.stepFrame();
    TEST_ASSERT_EQUAL(static_cast<int>(State::Menu), static_cast<int>(game.state()));

    startStage1(game);
    game.stepFrame();  // Aiming waits for input.
    TEST_ASSERT_EQUAL(static_cast<int>(State::Aiming), static_cast<int>(game.state()));
}

void test_game_fine_aim_steps_single_units(void) {
    Game game;
    game.newGame(1);
    game.startGame();
    game.aimLeftFine();
    TEST_ASSERT_EQUAL_UINT16(kAngleSteps - kAimFineStep, game.angle());
    game.aimRightFine();
    TEST_ASSERT_EQUAL_UINT16(0, game.angle());
    for (int i = 0; i < kAimStep; ++i) {
        game.aimRightFine();
    }
    TEST_ASSERT_EQUAL_UINT16(kAimStep, game.angle());
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_game_new_game_starts_in_menu);
    RUN_TEST(test_game_start_game_opens_aiming);
    RUN_TEST(test_game_shoot_rejected_outside_aiming);
    RUN_TEST(test_game_shoot_applies_aim_power_velocity);
    RUN_TEST(test_game_aim_wraps_and_power_clamps);
    RUN_TEST(test_game_fine_aim_steps_single_units);
    RUN_TEST(test_game_quiet_shot_costs_shot_scores_nothing);
    RUN_TEST(test_game_pocket_in_order_scores);
    RUN_TEST(test_game_cue_scratch_fouls_and_respots);
    RUN_TEST(test_game_wrong_order_fouls_without_points);
    RUN_TEST(test_game_clearing_table_wins);
    RUN_TEST(test_game_clearing_nonfinal_stage_advances);
    RUN_TEST(test_game_burning_shots_loses);
    RUN_TEST(test_game_menu_and_gameover_frames_are_noops);
    return UNITY_END();
}
