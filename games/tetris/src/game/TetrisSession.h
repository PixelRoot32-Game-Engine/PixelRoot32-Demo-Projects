#pragma once

#include <cstdint>

#include "game/Board.h"
#include "game/CollisionSystem.h"
#include "game/LineClearSystem.h"
#include "game/PieceGenerator.h"
#include "game/ScoringSystem.h"
#include "game/TetrisTypes.h"

namespace tetris {

/** Delay before a grounded piece locks (spec section 6.4). */
constexpr std::uint32_t LOCK_DELAY_MS = 500;
/** Soft drop divisor: gravity runs this many times faster while held. */
constexpr std::uint32_t SOFT_DROP_DIVISOR = 20;
/** Fastest gravity while soft-dropping, so level 1 still feels snappy. */
constexpr std::uint32_t SOFT_DROP_MIN_INTERVAL_MS = 50;
/** Cap on delta per update so a long pause cannot multi-drop rows. */
constexpr std::uint32_t MAX_STEP_MS = 250;

/** Outcome of one gravity step. */
struct StepResult {
    bool locked;
    std::uint8_t cleared;
    bool leveledUp;
    bool gameOver;
};

/**
 * @class TetrisSession
 * @brief Playthrough owner: spawn, gravity, lock, clears, score (spec 3.1).
 *
 * The scene feeds it edge commands (tryMove/tryRotate/hardDrop) and calls
 * update() with elapsed time; rendering reads the board read-only. Time
 * comes in as delta milliseconds so pausing is free (no update calls) and
 * tests stay deterministic. No engine dependency.
 */
class TetrisSession {
public:
    TetrisSession();

    /**
     * @brief Starts a playthrough with an empty board and a fresh bag.
     * @param seed RNG seed; equal seeds reproduce equal queues.
     */
    void start(std::uint32_t seed);

    /**
     * @brief Advances gravity and lock timers.
     * @param deltaMs Elapsed milliseconds since the last call.
     * @param softDropHeld True while the soft-drop button is down.
     * @return What happened during this step.
     */
    StepResult update(std::uint32_t deltaMs, bool softDropHeld);

    /**
     * @brief Shifts the active piece horizontally.
     * @param direction -1 left, +1 right (other values rejected).
     * @return True when the piece moved.
     */
    bool tryMove(int direction);

    /** @brief Rotates clockwise with simple wall kicks. @return True on success. */
    bool tryRotateCw();

    /** @brief Rotates counter-clockwise with simple wall kicks. @return True on success. */
    bool tryRotateCcw();

    /**
     * @brief Drops the piece to the ghost position and locks it now.
     * @return Rows cleared by the lock.
     */
    std::uint8_t hardDrop();

    /** @brief Resting row of the ghost piece (hard-drop preview). @return Board y of the ghost box. */
    [[nodiscard]] int ghostY() const;

    /** @brief Locked cells for the renderer. */
    [[nodiscard]] const Board& board() const;
    /** @brief Falling piece. */
    [[nodiscard]] const ActivePiece& active() const;
    /** @brief Preview piece. */
    [[nodiscard]] TetrominoType nextType() const;
    /** @brief Current phase. */
    [[nodiscard]] GameStatus status() const;
    /** @brief Current score. */
    [[nodiscard]] std::uint32_t score() const;
    /** @brief Current level. */
    [[nodiscard]] std::uint8_t level() const;
    /** @brief Cleared lines total. */
    [[nodiscard]] std::uint16_t lines() const;
    /** @brief Best score of this boot (RAM-only record). */
    [[nodiscard]] std::uint32_t bestScore() const;

private:
    Board board_;
    CollisionSystem collision_;
    LineClearSystem lineClear_;
    PieceGenerator generator_;
    ScoringSystem scoring_;
    ActivePiece active_;
    TetrominoType next_;
    GameStatus status_;
    std::uint32_t fallAccumMs_;
    std::uint32_t lockAccumMs_;
    std::uint32_t bestScore_;

    bool spawnNext();
    void lockActive();
    [[nodiscard]] bool grounded() const;
    void resetTimerState();
};

}  // namespace tetris
