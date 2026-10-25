#pragma once

#include <cstdint>

#include <graphics/Renderer.h>

#include "presentation/PresentationMap.h"

namespace minesweeper {

/**
 * @file TileCatalog.h
 * @brief The single table from a presentation tile id and pixel size to a sprite.
 *
 * The scene must never name a generated symbol: TileSprites.h is emitted by
 * tools/generate_sprites.py, so its identifiers may be regrouped or renamed
 * whenever the art changes, and a Sprite4bpp carries no runtime way to be
 * resized. Funnelling every lookup through this header keeps that coupling in
 * one file and turns "every tile exists at every size" into a compile-time
 * assertion instead of a rendering surprise on one difficulty only.
 */

/**
 * @brief Looks up the board sprite for a tile identity at a native pixel size.
 *
 * The size is a hard requirement, not a preference: 4bpp sprites cannot scale,
 * so a 12 px board may only ever draw 12 px art. Nothing falls back to a
 * neighbouring size for that reason -- a smaller tile stretched over the cell
 * would leave a gap, and a larger one would bleed into its neighbours.
 *
 * @param id Tile identity resolved by the presentation layer.
 * @param pixels Cell edge in pixels: 16 (EASY), 12 (MEDIUM) or 8 (EXPERT).
 * @return Sprite to blit, or nullptr when that id/size pair does not exist.
 */
const pixelroot32::graphics::Sprite4bpp* tileSprite(TileId id, std::uint8_t pixels);

/**
 * @brief Looks up one cursor animation frame at a native pixel size.
 *
 * The cursor is drawn over the cell it marks, so it must share the board's
 * pixel size or it would miss the cell edges it is meant to outline.
 *
 * @param phase Animation phase from cursorPhase(): 0 for A, 1 for B.
 * @param pixels Cell edge in pixels: 16 (EASY), 12 (MEDIUM) or 8 (EXPERT).
 * @return Sprite to blit, or nullptr when that phase/size pair does not exist.
 */
const pixelroot32::graphics::Sprite4bpp* cursorSprite(std::uint8_t phase, std::uint8_t pixels);

/**
 * @brief Looks up the 16x16 face for a header smiley identity.
 *
 * Only one size exists: the header band and both overlays are fixed geometry,
 * so unlike the board there is no 12/8 px variant to select.
 *
 * @param id Smiley identity from the presentation layer.
 * @return Sprite to blit, or nullptr when the id is outside the generated set.
 */
const pixelroot32::graphics::Sprite4bpp* smileySprite(SmileyId id);

/**
 * @brief Looks up the 8x8 glyph for an ASCII digit.
 *
 * The header counter is formatted with formatCounter(), which emits only
 * '0'..'9'; anything else is a caller bug and resolves to nullptr instead of
 * silently drawing a neighbouring glyph.
 *
 * @param digit Character in '0'..'9'.
 * @return Sprite to blit, or nullptr when the character is not a digit.
 */
const pixelroot32::graphics::Sprite4bpp* digitSprite(char digit);

/**
 * @brief Looks up the 8x8 glyph for a letter emitted by the asset pipeline.
 *
 * The sheet ships only the letters MINES and TIME are spelled with. A wider
 * run would need more authoring, so a missing letter stays a null rather than
 * borrowing a digit or a blank cell.
 *
 * @param letter One of 'M', 'I', 'N', 'E', 'S', 'T' (uppercase).
 * @return Sprite to blit, or nullptr for any other character.
 */
const pixelroot32::graphics::Sprite4bpp* letterSprite(char letter);

/**
 * @enum PanelPiece
 * @brief Identity of one 8x8 piece of the 9-slice panel frame.
 *
 * Mirrors the generated sheet order, so a missing piece fails the catalog's
 * completeness check at compile time instead of leaving a hole in a border.
 */
enum class PanelPiece : std::uint8_t {
    TopLeft,     /**< Corner: top edge meets left edge. */
    Top,         /**< Top edge, tiled horizontally. */
    TopRight,    /**< Corner: top edge meets right edge. */
    Left,        /**< Left edge, tiled vertically. */
    Right,       /**< Right edge, tiled vertically. */
    BottomLeft,  /**< Corner: bottom edge meets left edge. */
    Bottom,      /**< Bottom edge, tiled horizontally. */
    BottomRight  /**< Corner: bottom edge meets right edge. */
};

/**
 * @brief Looks up one 8x8 piece of the panel frame.
 *
 * The four corners are drawn once; the edges are tiled between them by the
 * caller. There is no centre piece because the centre is a plain fill.
 *
 * @param piece Piece identity.
 * @return Sprite to blit, or nullptr when the piece is outside the enum.
 */
const pixelroot32::graphics::Sprite4bpp* panelPieceSprite(PanelPiece piece);

}  // namespace minesweeper
