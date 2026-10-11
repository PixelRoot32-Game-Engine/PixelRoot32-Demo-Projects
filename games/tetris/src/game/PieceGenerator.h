#pragma once

#include <cstdint>

#include "game/TetrisTypes.h"

namespace tetris {

/**
 * @class PieceGenerator
 * @brief 7-bag randomizer with reproducible seeds (spec section 6.1).
 *
 * A bag holds one of each tetromino shuffled with Fisher-Yates over an
 * internal xorshift32 stream. Equal seeds reproduce equal sequences, which
 * the game-flow tests rely on. No allocation, fixed 7-byte bag.
 */
class PieceGenerator {
public:
    PieceGenerator();

    /**
     * @brief Resets the stream and deals a fresh bag.
     * @param seed Non-zero seed; 0 is replaced with 1.
     */
    void reset(std::uint32_t seed);

    /**
     * @brief Draws the next piece, refilling the bag when empty.
     * @return Next tetromino type.
     */
    TetrominoType next();

    /**
     * @brief Peeks at the piece after the active one without consuming it.
     * @return Upcoming type (the "next piece" preview).
     */
    [[nodiscard]] TetrominoType peekNext() const;

private:
    TetrominoType bag_[TETROMINO_COUNT];
    std::uint8_t position_;
    std::uint32_t rngState_;

    void refill();
    [[nodiscard]] std::uint32_t nextRandom();
    [[nodiscard]] std::uint32_t randomBelow(std::uint32_t bound);
};

}  // namespace tetris
