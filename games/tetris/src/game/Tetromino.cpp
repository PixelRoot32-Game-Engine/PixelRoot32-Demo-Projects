#include "game/Tetromino.h"

namespace tetris {

namespace {

// Spawn orientations inside the 4x4 box (rotation 0). I sits on row 1 so
// its vertical states stay inside the box; O sits at columns 1-2 so the
// shared spawnX centers it; the rest use the top-left 3x3 sub-box.
constexpr int8_t SPAWN[TETROMINO_COUNT][BLOCKS_PER_PIECE][2] = {
    // I
    {{0, 1}, {1, 1}, {2, 1}, {3, 1}},
    // O
    {{1, 0}, {2, 0}, {1, 1}, {2, 1}},
    // T
    {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
    // S
    {{1, 0}, {2, 0}, {0, 1}, {1, 1}},
    // Z
    {{0, 0}, {1, 0}, {1, 1}, {2, 1}},
    // J
    {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
    // L
    {{2, 0}, {0, 1}, {1, 1}, {2, 1}},
};

/** @brief One clockwise step inside an NxN box: (x,y) -> (N-1-y, x). */
constexpr BlockOffset rotateCwInBox(BlockOffset block, int8_t boxSize) {
    return BlockOffset{static_cast<int8_t>(boxSize - 1 - block.y), block.x};
}

}  // namespace

BlockOffset Tetromino::spawnBlockAt(TetrominoType type, std::uint8_t index) {
    const std::uint8_t t = static_cast<std::uint8_t>(type);
    if (t >= TETROMINO_COUNT || index >= BLOCKS_PER_PIECE) {
        return BlockOffset{0, 0};
    }
    return BlockOffset{SPAWN[t][index][0], SPAWN[t][index][1]};
}

BlockOffset Tetromino::blockAt(TetrominoType type, std::uint8_t rotation,
                               std::uint8_t index) {
    BlockOffset block = spawnBlockAt(type, index);
    if (type == TetrominoType::O) {
        return block;
    }
    const std::uint8_t steps = static_cast<std::uint8_t>(rotation & 3U);
    const int8_t box = (type == TetrominoType::I) ? 4 : 3;
    for (std::uint8_t i = 0; i < steps; ++i) {
        block = rotateCwInBox(block, box);
    }
    return block;
}

}  // namespace tetris
