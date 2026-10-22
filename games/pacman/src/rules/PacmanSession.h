#pragma once

#include <cstdint>

#include "PacmanConfig.h"
#include "PacmanDirection.h"
#include "PacmanEnemy.h"
#include "PacmanMaze.h"
#include "PacmanPlayer.h"

namespace pacman {

/**
 * @enum GameState
 * @brief The global state of a game.
 *
 * The states are checked in a fixed order once per logic step, never
 * dispatched through a table: there are five of them and exactly one of them
 * does any one-time work.
 */
enum class GameState : std::uint8_t {
    Title,   ///< Waiting for the player to start a game.
    Playing, ///< The simulation is running.
    Paused,  ///< The simulation is frozen.
    Victory, ///< Every mandatory collectible was picked up.
    Defeat   ///< The player ran out of lives.
};

/**
 * @class PacmanSession
 * @brief The rules of one game: the level, the player, the score, the lives
 *        and the power effect.
 *
 * The session owns everything the rules need and nothing the presentation
 * needs. It never draws, never reads an input device and never plays a
 * sound, which is what lets the whole rule set be exercised by a host-side
 * test with no graphics session at all.
 *
 * The order of the rules inside one logic step is fixed and documented, so
 * that a single event is never counted twice: input, player movement,
 * collectibles, power timer, end conditions.
 */
class PacmanSession {
public:
    PacmanSession();

    /**
     * @brief Builds a session on a caller-supplied level.
     *
     * Used by the unit suite to play through a small level. The layout rows
     * must outlive the session.
     *
     * @param config The level to play.
     */
    explicit PacmanSession(const PacmanLevelConfig& config);

    /**
     * @brief Starts a fresh game.
     *
     * Resets the score, the lives, the collected items, the player and the
     * power effect, and moves the game into the playing state.
     */
    void startNewGame();

    /**
     * @brief Advances the rules by one logic step.
     *
     * @param requestedDirection The direction the player is asking for.
     */
    void step(Direction requestedDirection);

    /**
     * @brief Switches between the playing and the paused state.
     *
     * Pausing freezes the simulation; nothing but a pause or a restart
     * changes the state from outside.
     */
    void togglePause();

    /**
     * @brief The global state of the game.
     * @return The current state.
     */
    [[nodiscard]] GameState state() const { return state_; }

    /**
     * @brief The accumulated score.
     * @return The score.
     */
    [[nodiscard]] int score() const { return score_; }

    /**
     * @brief The lives the player has left.
     * @return The remaining lives.
     */
    [[nodiscard]] int lives() const { return lives_; }

    /**
     * @brief The collectibles still waiting on the floor.
     * @return The number of remaining collectibles.
     */
    [[nodiscard]] int remainingCollectibles() const { return remainingCollectibles_; }

    /**
     * @brief How much of the power effect is left.
     * @return The number of logic steps the effect still lasts.
     */
    [[nodiscard]] int powerStepsLeft() const { return powerStepsLeft_; }

    /**
     * @brief Tells whether the power effect is running.
     * @return true while the effect is active.
     */
    [[nodiscard]] bool powerIsActive() const { return powerStepsLeft_ > 0; }

    /**
     * @brief Tells whether the player is protected after losing a life.
     * @return true while the player cannot be hit.
     */
    [[nodiscard]] bool playerIsInvulnerable() const { return invulnerableSteps_ > 0; }

    /**
     * @brief Tells whether the board is frozen for a respawn.
     * @return true while the respawn sequence runs.
     */
    [[nodiscard]] bool isRespawning() const { return respawnSteps_ > 0; }

    /**
     * @brief How many hunters have been defeated in a row.
     * @return The length of the current chain.
     */
    [[nodiscard]] int enemyChain() const { return enemyChain_; }

    /**
     * @brief The player character.
     * @return A reference to the player.
     */
    [[nodiscard]] const PacmanPlayer& player() const { return player_; }

    /**
     * @brief One of the hunters.
     *
     * @param index Hunter index, below the level's enemy count.
     * @return A reference to the hunter.
     */
    [[nodiscard]] const PacmanEnemy& enemy(int index) const { return enemies_[index]; }

    /**
     * @brief How many hunters the level has.
     * @return The enemy count.
     */
    [[nodiscard]] int enemyCount() const { return maze_.enemyCount(); }

    /**
     * @brief The level being played.
     * @return A reference to the maze.
     */
    [[nodiscard]] const PacmanMaze& maze() const { return maze_; }

    /**
     * @brief The collectible still waiting in a cell.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return The collectible, or `Collectible::None` when the cell is empty
     *         or its collectible was already picked up.
     */
    [[nodiscard]] Collectible collectibleAt(int col, int row) const;

    /**
     * @brief Tells whether a cell's collectible was picked up.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     * @return true when the cell held something that has been collected.
     */
    [[nodiscard]] bool isCollected(int col, int row) const;

private:
    PacmanMaze maze_;
    PacmanPlayer player_;

    /// The hunters, one per enemy of the level.
    PacmanEnemy enemies_[kEnemyCount] = {};

    /// One bit per cell: has its collectible been picked up?
    std::uint8_t collected_[PacmanMaze::kCells / 8 + 1] = {};

    int score_ = 0;
    int lives_ = kStartingLives;
    int remainingCollectibles_ = 0;
    int powerStepsLeft_ = 0;
    int invulnerableSteps_ = 0;
    int enemyChain_ = 0;
    int respawnSteps_ = 0;
    GameState state_ = GameState::Title;

    /**
     * @brief Puts the player and the hunters back on their starting cells.
     */
    void resetBoard();

    /**
     * @brief Adds points to the score, handing out an extra life whenever the
     *        score crosses a multiple of the extra-life threshold.
     *
     * @param amount The points to add.
     */
    void addScore(int amount);

    /**
     * @brief Picks up whatever is in a cell.
     *
     * @param col Column of the cell.
     * @param row Row of the cell.
     */
    void collectAt(int col, int row);

    /**
     * @brief Advances every hunter by one logic step.
     */
    void stepEnemies();

    /**
     * @brief Resolves the contact between the player and the hunters.
     */
    void resolveContacts();

    /**
     * @brief Costs the player a life and puts the board back in order.
     */
    void loseLife();

    /**
     * @brief Defeats a hunter and scores the hit.
     *
     * @param index Index of the hunter.
     */
    void defeatEnemy(int index);

    /**
     * @brief Ages the power effect by one logic step.
     */
    void updatePowerTimer();

    /**
     * @brief Ages the player's protection by one logic step.
     */
    void updateInvulnerability();
};

}  // namespace pacman
