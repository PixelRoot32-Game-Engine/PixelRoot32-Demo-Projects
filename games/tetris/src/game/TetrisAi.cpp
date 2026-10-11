#include "game/TetrisAi.h"

#include "game/CollisionSystem.h"
#include "game/Tetromino.h"

namespace tetris {

namespace {

// El-Tetris weights scaled by 1000 into integers: lines reward minus
// aggregate-height, hole and bumpiness costs. Integer math keeps the
// search deterministic and cheap on targets without an FPU.
constexpr int LINES_WEIGHT = 761;
constexpr int HOLES_WEIGHT = 357;
constexpr int HEIGHT_WEIGHT = 510;
constexpr int BUMPINESS_WEIGHT = 184;

// Search span for the 4x4 box origin: wide enough for kicked rotations,
// narrow enough that rejected columns fail fast inside canPlace.
constexpr int SEARCH_MIN_X = -3;
constexpr int SEARCH_MAX_X = BOARD_WIDTH + 2;

}  // namespace

AiPlacement TetrisAi::findBestPlacement(const Board& board, TetrominoType type) const {
    const CollisionSystem collision;
    AiPlacement best{0, Tetromino::spawnX(type)};
    bool found = false;
    int bestScore = 0;

    for (std::uint8_t rotation = 0; rotation < ROTATION_COUNT; ++rotation) {
        for (int x = SEARCH_MIN_X; x <= SEARCH_MAX_X; ++x) {
            ActivePiece probe{type, static_cast<int8_t>(x), Tetromino::spawnY(),
                              rotation};
            if (!collision.canPlace(board, probe)) {
                continue;
            }
            // Drop the probe until it rests on the stack or the floor.
            while (true) {
                ActivePiece below = probe;
                below.y = static_cast<int8_t>(below.y + 1);
                if (!collision.canPlace(board, below)) {
                    break;
                }
                probe = below;
                if (probe.y > BOARD_HEIGHT) {
                    break;
                }
            }
            // Stamp onto a scratch copy and score the settled stack.
            Board sim = board;
            for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
                const BlockOffset block = Tetromino::blockAt(type, rotation, i);
                const int sx = static_cast<int>(probe.x) + block.x;
                const int sy = static_cast<int>(probe.y) + block.y;
                if (sy >= 0) {
                    sim.set(sx, sy, cellForType(type));
                }
            }
            const int score = evaluate(sim);
            // Strict comparison keeps the first winner, so ties resolve
            // deterministically (leftmost column, lowest rotation).
            if (!found || score > bestScore) {
                best = AiPlacement{rotation, x};
                bestScore = score;
                found = true;
            }
        }
    }
    return best;
}

int TetrisAi::evaluate(const Board& sim) const {
    int heights[BOARD_WIDTH] = {};
    int holes = 0;
    int lines = 0;
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        bool seenBlock = false;
        for (int y = 0; y < BOARD_HEIGHT; ++y) {
            if (sim.isOccupied(x, y)) {
                if (!seenBlock) {
                    // Height measured from the floor to the highest block.
                    heights[x] = BOARD_HEIGHT - y;
                    seenBlock = true;
                }
            } else if (seenBlock) {
                // Empty cell with a block above it: a hard-to-fill hole.
                ++holes;
            }
        }
    }
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        if (sim.isRowFull(y)) {
            ++lines;
        }
    }
    int aggregateHeight = 0;
    int bumpiness = 0;
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        aggregateHeight += heights[x];
        if (x + 1 < BOARD_WIDTH) {
            const int diff = heights[x] - heights[x + 1];
            bumpiness += (diff >= 0) ? diff : -diff;
        }
    }
    return LINES_WEIGHT * lines - HOLES_WEIGHT * holes - HEIGHT_WEIGHT * aggregateHeight -
           BUMPINESS_WEIGHT * bumpiness;
}

}  // namespace tetris
