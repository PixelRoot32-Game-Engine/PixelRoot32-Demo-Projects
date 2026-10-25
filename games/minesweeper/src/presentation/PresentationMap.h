#pragma once

#include <cstddef>
#include <cstdint>

#include "game/MinefieldConfig.h"

namespace minesweeper {

/**
 * @file PresentationMap.h
 * @brief Engine-free mapping from rules state to sprite identity.
 *
 * The drawing layer needs to turn a Cell, a GameStatus or the elapsed time
 * into "which sprite do I blit". That decision is pure arithmetic over the
 * rules types, so it lives here instead of in src/ui/ (which pulls in the
 * renderer) and stays unit-testable on the host without SDL2 or Arduino.
 *
 * This header (and PresentationMap.cpp) must never include an engine, SDL2
 * or Arduino header: the host suite compiles them with no engine present.
 */

/**
 * @enum TileId
 * @brief Sprite identity of one board cell, in generated-sheet order.
 *
 * The opened variants are contiguous so the adjacent-mine count selects one
 * by offset; keep Open0..Open8 in ascending order when editing this enum.
 * CoveredQuestion exists because the generated sheet ships that tile, even
 * though the demo never produces a question-mark state.
 */
enum class TileId : std::uint8_t {
    Covered,         /**< Unrevealed cell. */
    CoveredFlag,     /**< Unrevealed cell carrying a flag. */
    CoveredQuestion, /**< Unrevealed cell with a question mark (unused). */
    Open0,           /**< Revealed cell with no adjacent mine. */
    Open1,           /**< Revealed cell with 1 adjacent mine. */
    Open2,           /**< Revealed cell with 2 adjacent mines. */
    Open3,           /**< Revealed cell with 3 adjacent mines. */
    Open4,           /**< Revealed cell with 4 adjacent mines. */
    Open5,           /**< Revealed cell with 5 adjacent mines. */
    Open6,           /**< Revealed cell with 6 adjacent mines. */
    Open7,           /**< Revealed cell with 7 adjacent mines. */
    Open8,           /**< Revealed cell with 8 (or more) adjacent mines. */
    Mine,            /**< A mine, drawn when the end-of-game reveal is on. */
    Explosion        /**< The mine the player detonated. */
};

/**
 * @brief Resolves the tile to draw for one cell.
 *
 * Precedence is deliberate and order-sensitive: the detonated mine is the
 * moment the player must see, so it beats every other rule; the end-of-game
 * mine reveal comes next so a loss exposes the board; only then does the
 * cell's own visibility decide. Counts above 8 are clamped to Open8 because
 * the sheet has no higher digit tile.
 *
 * @param cell Cell to render, as stored by Minefield.
 * @param revealMines True once the game is lost and every mine must show.
 * @param detonated True only for the cell that was just detonated.
 * @return Tile identity for the cell.
 */
TileId tileFor(const Cell& cell, bool revealMines, bool detonated);

/**
 * @enum SmileyId
 * @brief Header smiley identity, matching the generated smiley sprites.
 */
enum class SmileyId : std::uint8_t {
    Normal,    /**< Ready or playing. */
    Cool,      /**< Victory. */
    Surprised, /**< Reserved for a future pressed state. */
    Dead       /**< Detonation. */
};

/**
 * @brief Resolves the smiley to draw for the current game status.
 * @param status Lifecycle status owned by GameSession.
 * @return Smiley identity; Won maps to Cool, Lost to Dead, everything else
 *         to Normal.
 */
SmileyId smileyFor(GameStatus status);

/**
 * @brief Native cell edge, in pixels, for a difficulty level.
 *
 * 4bpp sprites cannot scale, so the cell size must equal the emitted tile
 * size instead of the largest size that fits: Beginner 16, Intermediate 12,
 * Expert 8. Every board therefore fits the 240x240 logical screen exactly by
 * geometry (9x9 @16, 16x16 @12, 30x16 @8).
 *
 * @param difficulty Selected level.
 * @return Cell edge in pixels.
 */
std::uint8_t cellPixelsFor(Difficulty difficulty);

/**
 * @brief Formats a header counter as exactly three zero-padded digits.
 *
 * The header band draws one digit sprite per character, so the width must
 * never vary; values above 999 clamp to "999" rather than overflow into a
 * fourth glyph.
 *
 * @param out Destination buffer of at least 4 bytes; receives 3 digits plus
 *            the NUL terminator.
 * @param value Counter value to render.
 */
void formatCounter(char out[4], std::uint16_t value);

/**
 * @brief Number of decimal digits a non-negative value needs.
 * @param value Value to measure.
 * @return Digit count, at least 1.
 */
constexpr std::size_t digits10(std::uint16_t value) {
    std::size_t digits = 1;
    while (value >= 10) {
        value /= 10;
        ++digits;
    }
    return digits;
}

/**
 * @brief Character count of a NUL-terminated string, at compile time.
 * @param text Text to measure; must not be nullptr.
 * @return Number of characters before the terminator.
 */
constexpr std::size_t textLength(const char* text) {
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

/**
 * @brief Fixed character width of one difficulty row on the menu.
 *
 * The engine's `UIButton` stores a non-owning `std::string_view` and ships no
 * label setter, so the row text is rewritten in place when a best time
 * appears. That only stays valid while every rewrite has the same character
 * count, so the width is a closed-form function of the board description --
 * the best-time slot is always three characters wide ("042" or "---").
 *
 * @param name Difficulty name shown on the row.
 * @param width Board columns.
 * @param height Board rows.
 * @param mines Mine count.
 * @return Characters in the formatted row, excluding the NUL terminator.
 */
constexpr std::size_t menuRowWidth(const char* name, std::uint8_t width, std::uint8_t height,
                                   std::uint16_t mines) {
    return textLength(name) + 1 + digits10(width) + 1 + digits10(height) + 1 + digits10(mines) +
           textLength(" BEST ") + 3;
}

/**
 * @brief Formats one menu row with a fixed-width best-time slot.
 *
 * Layout is `"<NAME> <W>X<H>-<MINES> BEST <slot>"`, where the slot is three
 * zero-padded digits, or `---` while no best time exists. Values above 999
 * clamp so the slot never grows a fourth digit and the row keeps its width.
 *
 * @param out Destination buffer.
 * @param capacity Capacity of @p out in bytes, including the terminator.
 * @param name Difficulty name shown on the row.
 * @param width Board columns.
 * @param height Board rows.
 * @param mines Mine count.
 * @param bestSeconds Best time, or 0 when the level has no record yet.
 * @return Characters written excluding the terminator, or 0 when the buffer
 *         is too small (in which case @p out is left empty).
 */
std::size_t formatMenuRow(char* out, std::size_t capacity, const char* name, std::uint8_t width,
                          std::uint8_t height, std::uint16_t mines, std::uint16_t bestSeconds);

/**
 * @brief Selects the cursor animation phase for a millisecond timestamp.
 *
 * The sheet ships two dash patterns; alternating them every 300 ms of a
 * 600 ms cycle animates the cursor without extra state or allocation.
 *
 * @param nowMs Milliseconds since boot.
 * @return 0 for the first 300 ms of each cycle, 1 for the second.
 */
std::uint8_t cursorPhase(unsigned long nowMs);

}  // namespace minesweeper
