#include "game/PieceGenerator.h"

namespace tetris {

PieceGenerator::PieceGenerator() : position_(TETROMINO_COUNT), rngState_(1) {
    reset(1);
}

void PieceGenerator::reset(std::uint32_t seed) {
    rngState_ = (seed != 0) ? seed : 1U;
    position_ = TETROMINO_COUNT;
    refill();
}

TetrominoType PieceGenerator::next() {
    if (position_ >= TETROMINO_COUNT) {
        refill();
    }
    return bag_[position_++];
}

TetrominoType PieceGenerator::peekNext() const {
    if (position_ < TETROMINO_COUNT) {
        return bag_[position_];
    }
    // Bag exhausted: the refill order is deterministic from the current
    // RNG state but must not advance it, so peek replays the shuffle on a
    // copy. Peeking is rare (once per frame for the preview panel).
    std::uint32_t state = rngState_;
    TetrominoType order[TETROMINO_COUNT];
    for (std::uint8_t i = 0; i < TETROMINO_COUNT; ++i) {
        order[i] = static_cast<TetrominoType>(i);
    }
    for (std::uint8_t i = TETROMINO_COUNT - 1; i > 0; --i) {
        auto nextRand = [&state]() {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        };
        const std::uint8_t j = static_cast<std::uint8_t>(nextRand() % (i + 1U));
        const TetrominoType tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
    return order[0];
}

void PieceGenerator::refill() {
    for (std::uint8_t i = 0; i < TETROMINO_COUNT; ++i) {
        bag_[i] = static_cast<TetrominoType>(i);
    }
    // Fisher-Yates: every permutation is equally likely, so long droughts
    // of one piece type cannot happen inside a bag.
    for (std::uint8_t i = TETROMINO_COUNT - 1; i > 0; --i) {
        const std::uint8_t j = static_cast<std::uint8_t>(randomBelow(i + 1U));
        const TetrominoType tmp = bag_[i];
        bag_[i] = bag_[j];
        bag_[j] = tmp;
    }
    position_ = 0;
}

std::uint32_t PieceGenerator::nextRandom() {
    // xorshift32: tiny, fast and fully reproducible from the seed.
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 17;
    rngState_ ^= rngState_ << 5;
    return rngState_;
}

std::uint32_t PieceGenerator::randomBelow(std::uint32_t bound) {
    if (bound == 0) {
        return 0;
    }
    return nextRandom() % bound;
}

}  // namespace tetris
