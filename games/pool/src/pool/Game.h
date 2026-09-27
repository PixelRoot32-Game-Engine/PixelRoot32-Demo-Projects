/*
 * Game.h - Engine-free turn/shot state machine over pool::World.
 *
 * Owns the table, the balls and the v1 rules (targets pocketed in ascending
 * order, cue scratch and wrong-order fouls, per-shot budget, win when the
 * table is clear) while the scene keeps input, pacing and drawing. No engine
 * dependency and no float/double, so recorded inputs replay bit-identically
 * on every target.
 */
#pragma once

#include <cstdint>

#include "pool/Table.h"
#include "pool/TableDef.h"
#include "pool/World.h"

namespace pool {

/**
 * @brief Game flow states, in the order one shot walks them.
 *
 * Menu waits for Start; Aiming takes aim/power input; Shooting holds a fired
 * shot before its first simulated frame; BallsMoving simulates until every
 * ball rests; EvaluateShot scores the pocket log; NextTurn charges the shot
 * and routes to Aiming or GameOver.
 */
enum class State : uint8_t {
    Menu = 0,
    Aiming,
    Shooting,
    BallsMoving,
    EvaluateShot,
    NextTurn,
    GameOver,
};

/** Minimum power-meter level accepted by shoot(). */
constexpr uint8_t kMinPower = 1;
/** Maximum power-meter level accepted by shoot(). */
constexpr uint8_t kMaxPower = 10;
/** Power level a new game aims with. */
constexpr uint8_t kDefaultPower = 5;
/** Aim steps (1/1024-revolution units, see Trig.h) per aimLeft/aimRight call. */
constexpr uint16_t kAimStep = 4;
/** Aim micro-step per aimLeftFine/aimRightFine call: one table unit (0.35 deg). */
constexpr uint16_t kAimFineStep = 1;
/** Points awarded per correctly pocketed target ball. */
constexpr int32_t kPointsPerBall = 100;
/** Points lost on a foul shot; the score never drops below zero. */
constexpr int32_t kFoulPenalty = 50;
/** Shots granted per target ball on the loaded table. */
constexpr uint8_t kShotsPerTarget = 2;

/**
 * @class Game
 * @brief Turn-based rules and shot simulation for one stage.
 *
 * The scene drives this with aim/power calls, shoot() and one stepFrame()
 * per 1/60 s tick, then reads score/shots/state for the HUD. Tests stage
 * deterministic shots through the mutable world() accessor.
 */
class Game {
public:
    /**
     * @brief Loads a stage's table and resets score, shots, aim and turn.
     * @param stage 1-based stage number, forwarded to tableForStage().
     * @return TableError::None when the table loaded; otherwise the game
     *   lands in GameOver (lost) and the table must not be used.
     */
    TableError newGame(uint8_t stage);
    /**
     * @brief Leaves the menu and opens aiming for turn 1.
     *
     * No-op unless the state is Menu.
     */
    void startGame();

    /**
     * @brief Turns the aim counter-clockwise by kAimStep angle units.
     *
     * No-op unless aiming.
     */
    void aimLeft();

    /**
     * @brief Turns the aim clockwise by kAimStep angle units.
     *
     * No-op unless aiming.
     */
    void aimRight();

    /**
     * @brief Nudges the aim counter-clockwise by one angle unit.
     *
     * Single-tap precision companion to aimLeft(); no-op unless aiming.
     */
    void aimLeftFine();

    /**
     * @brief Nudges the aim clockwise by one angle unit.
     *
     * Single-tap precision companion to aimRight(); no-op unless aiming.
     */
    void aimRightFine();

    /**
     * @brief Raises the power-meter level by one, clamped to kMaxPower.
     *
     * No-op unless aiming.
     */
    void powerUp();

    /**
     * @brief Lowers the power-meter level by one, clamped to kMinPower.
     *
     * No-op unless aiming.
     */
    void powerDown();

    /**
     * @brief Fires the cue ball along the current aim and power.
     * @return True when a shot was fired (state was Aiming with an active
     *   cue ball); false otherwise, leaving everything untouched.
     */
    bool shoot();

    /**
     * @brief Advances the state machine by one 1/60 s frame.
     *
     * Runs kSubstepsPerFrame physics substeps while a shot is live and walks
     * EvaluateShot/NextTurn forward; a no-op in Menu, Aiming and GameOver.
     */
    void stepFrame();

    /**
     * @brief Smallest active target number, or 0 when none remains.
     * @return Expected ball number for the next valid pocket.
     */
    [[nodiscard]] uint8_t nextExpected() const;

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] uint8_t stage() const { return stage_; }
    [[nodiscard]] uint16_t angle() const { return angle_; }
    [[nodiscard]] uint8_t power() const { return power_; }
    [[nodiscard]] int32_t score() const { return score_; }
    [[nodiscard]] uint8_t shotsLeft() const { return shotsLeft_; }
    [[nodiscard]] uint8_t turn() const { return turn_; }

    /**
     * @brief Whether the finished game was won.
     * @return Meaningful only when state() is GameOver.
     */
    [[nodiscard]] bool won() const { return won_; }

    [[nodiscard]] const Table& table() const { return table_; }
    [[nodiscard]] const World& world() const { return world_; }

    /**
     * @brief Mutable ball state for staging deterministic shots in tests.
     * @return The live simulation world.
     */
    World& world() { return world_; }

private:
    /** Runs one frame worth of physics substeps. */
    void runFrame();

    /** Scores the pocket log and respots a scratched cue ball. */
    void evaluateShot();

    /**
     * @brief Charges the shot and routes to Aiming, the next stage, or GameOver.
     *
     * Clearing the table loads stage + 1 (score carried forward) while stages
     * remain; only clearing the last stage wins the game.
     */
    void advanceTurn();

    /**
     * @brief Loads a stage's table, balls, shot budget, aim and search box.
     *
     * Shared by newGame() and the stage-clear path of advanceTurn(): scoring
     * (score_ kept or reset), turn_ and the resulting state stay with the
     * caller, so this never touches them, nor won_.
     * @param stage 1-based stage number, forwarded to tableForStage().
     * @return TableError::None when the table loaded; otherwise table_ must
     *   not be used.
     */
    TableError loadStage(uint8_t stage);

    /**
     * @brief Returns the cue ball to play after a scratch.
     * @return False when no free cell exists; the game is then lost.
     */
    bool respotCue();

    /**
     * @brief True when a ball center can rest at (x, y) raw units.
     * @param x X position, raw units (1/256 px).
     * @param y Y position, raw units.
     */
    [[nodiscard]] bool isFree(int32_t x, int32_t y) const;

    /** True when at least one target ball is still active. */
    [[nodiscard]] bool anyTargetActive() const { return nextExpected() != 0; }

    Table table_{};
    World world_{};
    const TableDef* def_ = nullptr;
    uint8_t stage_ = 0;
    State state_ = State::Menu;
    uint16_t angle_ = 0;
    uint8_t power_ = kDefaultPower;
    int32_t score_ = 0;
    uint8_t shotsLeft_ = 0;
    uint8_t turn_ = 0;
    uint8_t shotExpected_ = 0;
    bool won_ = false;
    bool respotFailed_ = false;
    int16_t minX_ = 0;
    int16_t maxX_ = 0;
    int16_t minY_ = 0;
    int16_t maxY_ = 0;
};

}  // namespace pool
