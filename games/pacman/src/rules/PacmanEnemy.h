#pragma once

#include <cstdint>

#include "gameplay/GridMotion.h"

#include "PacmanConfig.h"
#include "PacmanDirection.h"
#include "PacmanMaze.h"
#include "PacmanRouteField.h"

namespace pacman {

namespace gameplay = pixelroot32::gameplay;

/**
 * @enum EnemyState
 * @brief What a hunter is currently doing.
 *
 * The states are explicit so that the rules never have to infer an enemy's
 * behaviour from a combination of position and timers.
 */
enum class EnemyState : std::uint8_t {
    Patrol,   ///< VIGILAR: heading for its own corner.
    Chase,    ///< PERSEGUIR: hunting the player.
    Flee,     ///< HUIR: running from the player while the power is active.
    Return,   ///< REGRESAR: defeated and heading back to the house.
    Respawn   ///< REAPARECER: waiting in the house before it plays again.
};

/**
 * @enum EnemyPersonality
 * @brief How a hunter picks the cell it heads for.
 */
enum class EnemyPersonality : std::uint8_t {
    Chaser,   ///< Heads straight for the player.
    Ambusher, ///< Heads for the cell four steps ahead of the player.
    Flanker,  ///< Heads for the point past the player, measured from the chaser.
    Timid     ///< Hunts from far away and retreats when the player is close.
};

/**
 * @struct PacmanEnemyContext
 * @brief What a hunter needs to know about the rest of the game.
 */
struct PacmanEnemyContext {
    int playerCol = 0;              ///< Column of the player's cell.
    int playerRow = 0;              ///< Row of the player's cell.
    Direction playerDirection = Direction::None;  ///< Direction the player is travelling in.
    bool powerIsActive = false;     ///< Whether the power effect is running.
    int chaserCol = 0;              ///< Column of the hunter that always chases.
    int chaserRow = 0;              ///< Row of the hunter that always chases.
};

/**
 * @class PacmanEnemy
 * @brief One hunter: its position on the grid, its behaviour state and the
 *        route it follows.
 *
 * The hunter moves one sub-step per logic step, exactly like the player, but
 * it needs more steps to cross a cell, so it is slower. At every cell it
 * picks the direction that brings it closest to its target -- or, while it
 * is fleeing, the direction that brings it furthest away -- by asking a
 * breadth-first route field for the distance to each neighbour.
 *
 * The choice is deterministic: the same level, the same player and the same
 * power state always produce the same route.
 */
class PacmanEnemy {
public:
    /**
     * @brief Places the hunter at its start position.
     *
     * @param maze The maze the hunter lives in.
     * @param index Which hunter this is; it picks the personality, the start
     *        cell and the corner from that index.
     */
    void reset(const PacmanMaze& maze, int index);

    /**
     * @brief Advances the hunter by one logic step.
     *
     * @param maze The maze the hunter moves in.
     * @param context What the hunter knows about the rest of the game.
     */
    void step(const PacmanMaze& maze, const PacmanEnemyContext& context);

    /**
     * @brief Column of the cell the hunter occupies.
     * @return The hunter's column.
     */
    [[nodiscard]] int cellCol() const { return motion_.cellX; }

    /**
     * @brief Row of the cell the hunter occupies.
     * @return The hunter's row.
     */
    [[nodiscard]] int cellRow() const { return motion_.cellY; }

    /**
     * @brief Column of the cell the hunter is travelling towards.
     * @return The target column.
     */
    [[nodiscard]] int targetCol() const { return motion_.toX; }

    /**
     * @brief Row of the cell the hunter is travelling towards.
     * @return The target row.
     */
    [[nodiscard]] int targetRow() const { return motion_.toY; }

    /**
     * @brief How far the hunter has travelled into the current cell.
     * @return A value from 0 (at rest) to `kEnemyStepsPerCell` - 1.
     */
    [[nodiscard]] int progress() const { return motion_.progress; }

    /**
     * @brief The direction the hunter is travelling in.
     * @return The current direction.
     */
    [[nodiscard]] Direction direction() const { return direction_; }

    /**
     * @brief What the hunter is currently doing.
     * @return The behaviour state.
     */
    [[nodiscard]] EnemyState state() const { return state_; }

    /**
     * @brief How the hunter picks its target.
     * @return The personality.
     */
    [[nodiscard]] EnemyPersonality personality() const { return personality_; }

    /**
     * @brief Tells whether the hunter can be defeated right now.
     * @return true while the hunter is fleeing.
     */
    [[nodiscard]] bool isVulnerable() const { return state_ == EnemyState::Flee; }

    /**
     * @brief Tells whether the hunter has been defeated.
     * @return true while the hunter is heading home or waiting to come back.
     */
    [[nodiscard]] bool isDefeated() const { return state_ == EnemyState::Return || state_ == EnemyState::Respawn; }

    /**
     * @brief Tells whether the hunter is on its way home as a pair of eyes.
     *
     * A hunter that is waiting inside the house is not defeated: it is still
     * a whole hunter, and the cabinet drew it that way.
     *
     * @return true while the hunter is heading home.
     */
    [[nodiscard]] bool isEaten() const { return state_ == EnemyState::Return; }

    /**
     * @brief Tells whether the hunter is out of the house and on the board.
     * @return true while the hunter is playing.
     */
    [[nodiscard]] bool isPlaying() const {
        return state_ != EnemyState::Return && state_ != EnemyState::Respawn;
    }

    /**
     * @brief Marks the hunter as defeated and sends it home.
     */
    void defeat();

private:
    gameplay::GridMotion motion_;
    Direction direction_ = Direction::None;
    EnemyState state_ = EnemyState::Patrol;
    EnemyPersonality personality_ = EnemyPersonality::Chaser;

    int homeCol_ = 0;         ///< Column of the hunter's home cell.
    int homeRow_ = 0;         ///< Row of the hunter's home cell.
    int cornerCol_ = 0;       ///< Column of the hunter's own corner.
    int cornerRow_ = 0;       ///< Row of the hunter's own corner.
    int scatterTimer_ = 0;    ///< Steps left before the hunter starts hunting.
    int respawnTimer_ = 0;    ///< Steps the hunter waits in the house.
    bool reverseRequested_ = false;  ///< Set when the power effect makes it turn back.

    /// Route distances toward the hunter's current target.
    PacmanRouteField routeField_;

    [[nodiscard]] bool canEnter(const PacmanMaze& maze, Direction direction) const;
    [[nodiscard]] int behaviourTargetCol(const PacmanMaze& maze, const PacmanEnemyContext& context) const;
    [[nodiscard]] int behaviourTargetRow(const PacmanMaze& maze, const PacmanEnemyContext& context) const;
    [[nodiscard]] Direction chooseDirection(const PacmanMaze& maze, int routeTargetCol, int routeTargetRow);
    void updateState(const PacmanMaze& maze, const PacmanEnemyContext& context);
    void onArrived(const PacmanMaze& maze);
};

}  // namespace pacman
