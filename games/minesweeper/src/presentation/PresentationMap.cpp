#include "presentation/PresentationMap.h"

#include <cstdio>

namespace minesweeper {

namespace {

/**
 * @brief Maps an adjacent-mine count onto the contiguous Open0..Open8 block.
 * @param adjacentMines Raw count from the cell, possibly above 8.
 * @return Open<clamped count>.
 *
 * The enum keeps Open0..Open8 adjacent on purpose, so the mapping is one
 * offset instead of a nine-arm switch. The clamp is not cosmetic: a 9 can
 * only come from a hand-built board, but falling off the enum would select
 * an unrelated sprite.
 */
TileId openTileFor(std::uint8_t adjacentMines) {
    const std::uint8_t clamped = (adjacentMines > 8) ? 8 : adjacentMines;
    return static_cast<TileId>(static_cast<std::uint8_t>(TileId::Open0) + clamped);
}

/** Cursor animation cycle: two 300 ms phases. */
constexpr unsigned long CURSOR_CYCLE_MS = 600;
constexpr unsigned long CURSOR_PHASE_MS = 300;

}  // namespace

TileId tileFor(const Cell& cell, bool revealMines, bool detonated) {
    // The detonated cell is the one fact the player must read at a glance,
    // so it outranks the end-of-game reveal and the cell's own state.
    if (detonated) {
        return TileId::Explosion;
    }
    // On a loss every mine is exposed, including ones still flagged; showing
    // the mine is what teaches the player they misplaced the flag.
    if (revealMines && cell.hasMine) {
        return TileId::Mine;
    }
    switch (cell.state) {
        case CellState::Flagged:
            return TileId::CoveredFlag;
        case CellState::Revealed:
            // A revealed mine only happens on detonation, which returned
            // above; keeping the branch covers boards restored from a loss.
            return cell.hasMine ? TileId::Mine : openTileFor(cell.adjacentMines);
        case CellState::Hidden:
        default:
            return TileId::Covered;
    }
}

SmileyId smileyFor(GameStatus status) {
    switch (status) {
        case GameStatus::Won:
            return SmileyId::Cool;
        case GameStatus::Lost:
            return SmileyId::Dead;
        // Ready and Playing share the neutral face: the header must not
        // flicker between menu and play, and Surprised is reserved for the
        // future pressed-cell state.
        case GameStatus::Ready:
        case GameStatus::Playing:
        default:
            return SmileyId::Normal;
    }
}

std::uint8_t cellPixelsFor(Difficulty difficulty) {
    // These are the sizes tools/generate_sprites.py emits. 4bpp sprites
    // cannot scale, so this table and the generator must change together.
    switch (difficulty) {
        case Difficulty::Intermediate:
            return 12;
        case Difficulty::Expert:
            return 8;
        case Difficulty::Beginner:
        default:
            return 16;
    }
}

void formatCounter(char out[4], std::uint16_t value) {
    // Clamp instead of truncating: a wrapped 1000 would read "000" and look
    // like a legitimate count. The header band has room for exactly three
    // digit sprites.
    const std::uint16_t clamped = (value > 999) ? 999 : value;
    out[0] = static_cast<char>('0' + (clamped / 100) % 10);
    out[1] = static_cast<char>('0' + (clamped / 10) % 10);
    out[2] = static_cast<char>('0' + (clamped % 10));
    out[3] = '\0';
}

std::uint8_t cursorPhase(unsigned long nowMs) {
    return (nowMs % CURSOR_CYCLE_MS) < CURSOR_PHASE_MS ? 0 : 1;
}

std::size_t formatMenuRow(char* out, std::size_t capacity, const char* name, std::uint8_t width,
                          std::uint8_t height, std::uint16_t mines, std::uint16_t bestSeconds) {
    if (out == nullptr || capacity == 0) {
        return 0;
    }
    out[0] = '\0';

    char slot[4];
    if (bestSeconds == 0) {
        // A missing record keeps the three-character slot without claiming a
        // 0-second best.
        slot[0] = '-';
        slot[1] = '-';
        slot[2] = '-';
        slot[3] = '\0';
    } else {
        const std::uint16_t clamped = (bestSeconds > 999) ? 999 : bestSeconds;
        slot[0] = static_cast<char>('0' + (clamped / 100) % 10);
        slot[1] = static_cast<char>('0' + (clamped / 10) % 10);
        slot[2] = static_cast<char>('0' + (clamped % 10));
        slot[3] = '\0';
    }

    const int written = std::snprintf(out, capacity, "%s %uX%u-%u BEST %s", name,
                                      static_cast<unsigned>(width),
                                      static_cast<unsigned>(height),
                                      static_cast<unsigned>(mines), slot);
    if (written < 0 || static_cast<std::size_t>(written) >= capacity) {
        // A truncated row would silently change the label's length, which the
        // engine's string_view cannot survive. Report failure instead.
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(written);
}

}  // namespace minesweeper
