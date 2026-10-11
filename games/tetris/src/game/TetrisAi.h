#pragma once

#include <cstdint>

#include "game/Board.h"
#include "game/TetrisTypes.h"

namespace tetris {

/**
 * @struct AiPlacement
 * @brief Target pose for the falling piece: orientation plus the X column
 * of the 4x4 bounding box top-left corner.
 */
struct AiPlacement {
    std::uint8_t rotation;
    int x;
};

/**
 * @class TetrisAi
 * @brief Single-ply heuristic brain for the attract-mode demo player.
 *
 * Every placement (rotation x column) is simulated: the piece is dropped
 * onto a scratch copy of the board and the resulting stack is scored with
 * an El-Tetris-style linear evaluation (cleared lines minus aggregate
 * height, holes and bumpiness, all in integer math so it stays cheap and
 * deterministic on FPU-less targets). The best-scoring pose wins; ties
 * keep the first candidate so repeated queries agree with each other.
 *
 * Stateless and engine-free: the scene re-plans from the live board each
 * AI tick and steers the session toward the pose with tryRotate/tryMove,
 * so wall-kick nudges or blocked paths self-correct on the next tick.
 * No allocation, no engine dependency.
 */
class TetrisAi {
public:
    /**
     * @brief Finds the best-scoring resting pose for a piece.
     * @param board Locked cells to simulate on (never mutated).
     * @param type Falling piece type.
     * @return Rotation and box X of the winning pose. Always valid when
     * at least one spawn pose fits; rotation 0 and spawn column otherwise.
     */
    [[nodiscard]] AiPlacement findBestPlacement(const Board& board,
                                                TetrominoType type) const;

private:
    /**
     * @brief Scores a simulated settled stack (higher is better).
     * @param sim Board copy with the candidate piece already stamped.
     * @return Heuristic value: lines reward minus height/hole/bumpiness cost.
     */
    [[nodiscard]] int evaluate(const Board& sim) const;
};

}  // namespace tetris
