/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include "presentation/CourtLayout.h"

/**
 * @file MenuLayout.h
 * @brief The design contract of the title screen.
 *
 * The title screen is one framed panel standing on the court: the title of the
 * screen, the three level rows with a visible selection, and a one-line hint for
 * the controls. Like the court it is authored once on the design box, so the same
 * numbers draw on the square 240 canvas and on the 320 one after its quarter
 * turn; the court simply shows more of itself around the panel as the box grows.
 *
 * The panel is measured against the narrowest canvas the demo builds for, not
 * against the court of the current build. 224 px of panel inside a 240 px box
 * leaves 8 px of court on each side, which is what proves every wider box too,
 * and the checks below run against that 240 width so a single compilation covers
 * both panels.
 *
 * Engine-free on purpose, exactly like the court: the geometry has to be provable
 * without a renderer, so every text width is derived from the glyph metrics in
 * `CourtLayout.h` instead of being measured through the font. The renderer still
 * measures each line when it draws; the counts here are only the widest strings
 * of each group and what they have to fit into.
 *
 * Everything here is in design-box pixels, with the origin at the top-left of the
 * design box.
 */
namespace pong::presentation {

// --- Canvas reference --------------------------------------------------------

/// Narrowest canvas any build produces: the square panel and the native window
/// that mirrors it. The panel is proved against this width, so the 320 build
/// inherits the proof instead of repeating it.
inline constexpr int kNarrowestDesignWidth = 240;

// --- Panel -------------------------------------------------------------------

/// Width of the title panel: 224 px leaves 8 px of court on each side of the
/// narrowest canvas.
inline constexpr int kMenuPanelWidth = 224;

/// Height of the title panel, read top to bottom as 10 px of margin, the 14 px
/// title, a 10 px gap, three 16 px rows with two 6 px gaps (60 px), another
/// 10 px gap, the 14 px hint and 10 px of margin: 128 px in total.
inline constexpr int kMenuPanelHeight = 128;

/// Left edge of the panel, centred on the court.
inline constexpr int kMenuPanelX = (kDesignWidth - kMenuPanelWidth) / 2;

/// Top edge of the panel, centred in the play area below the score band.
inline constexpr int kMenuPanelY = kPlayY + (kPlayHeight - kMenuPanelHeight) / 2;

/// Thickness of the panel border, inset from the panel edge. It is the court
/// frame's thickness, so the title screen wears the same line as the court it
/// stands on.
inline constexpr int kMenuBorder = kFrameThickness;

// --- Title -------------------------------------------------------------------

/// Top edge of the title's first pixel row: 10 px below the panel edge.
inline constexpr int kMenuTitleY = kMenuPanelY + 10;

/// Text size of the title.
inline constexpr int kMenuTitleScale = 2;

// --- Level rows --------------------------------------------------------------
// The rows are the demo's levels in the order `game::Difficulty` declares them:
// row 0 EASY, row 1 MEDIUM, row 2 HARD. The menu draws the selection by index
// and the scene maps that index straight onto the level, so the two orders are
// one contract; it is written here, next to the count, because this header only
// answers to the geometry and never needs the level enum.

/// Number of level rows: one per difficulty the demo offers.
inline constexpr int kMenuRowCount = 3;

/// Height of one row band. The label is 14 px tall at scale 2, so the band keeps
/// one pixel of air above it and one below.
inline constexpr int kMenuRowHeight = 16;

/// Gap between two row bands.
inline constexpr int kMenuRowGap = 6;

/// Vertical distance between two consecutive rows.
inline constexpr int kMenuRowPitch = kMenuRowHeight + kMenuRowGap;

/// Top edge of the first row's text: 10 px below the title's last pixel row.
inline constexpr int kMenuFirstRowY = kMenuPanelY + 34;

/// Left edge of a row band and of its highlight.
inline constexpr int kMenuRowX = kMenuPanelX + 8;

/// Width of a row band: the panel minus the 8 px margin on each side.
inline constexpr int kMenuRowWidth = kMenuPanelWidth - 16;

/// Text size of a level row.
inline constexpr int kMenuRowScale = 2;

/// Height of the selection band: the row plus one pixel at each end, so it reads
/// as a band around the label instead of a tight box behind it.
inline constexpr int kMenuHighlightHeight = kMenuRowHeight + 2;

/// Vertical offset of the selection band from the row's text top. Negative by one
/// pixel, so the band closes over the top of the label and leaves three pixels of
/// band below it; the label itself keeps the row's top edge either way.
inline constexpr int kMenuHighlightYOffset = -1;

// --- Hint --------------------------------------------------------------------

/// Top edge of the hint's first pixel row: 10 px below the last row's gap.
inline constexpr int kMenuHintY = kMenuPanelY + 104;

/// Text size of the hint.
inline constexpr int kMenuHintScale = 2;

// --- Text metrics ------------------------------------------------------------
// Only the character counts live here; the widths are derived from the court's
// glyph metrics so the checks below stay constant expressions. A missing glyph
// cell is invisible where an off-panel one is not, so each group is measured by
// its widest line.

/// Characters in the title, "SELECT LEVEL".
inline constexpr int kMenuTitleCells = 12;

/// Characters in the longest level row, "MEDIUM".
inline constexpr int kMenuRowLongestCells = 6;

/// Characters in the hint row, "UP DOWN A".
inline constexpr int kMenuHintCells = 9;

/**
 * @brief Drawn width of a string of `cells` characters at a text scale.
 * @param cells Character count of the line.
 * @param scale Text size multiplier.
 * @return Width in pixels, with the trailing glyph spacing excluded, which is
 *         the same measurement `FontManager::textWidth()` reports.
 */
[[nodiscard]] constexpr int cellWidth(int cells, int scale) {
    return cells * kGlyphAdvance * scale - kGlyphSpacing * scale;
}

/// Drawn width of the title.
inline constexpr int kMenuTitleWidth = cellWidth(kMenuTitleCells, kMenuTitleScale);

/// Drawn width of the longest level row.
inline constexpr int kMenuRowLongestWidth = cellWidth(kMenuRowLongestCells, kMenuRowScale);

/// Drawn width of the hint.
inline constexpr int kMenuHintWidth = cellWidth(kMenuHintCells, kMenuHintScale);

// --- Contract checks ---------------------------------------------------------
// The title screen replaces the actors rather than sitting beside them, so a
// composition that no longer fits would draw off-panel or over the frame instead
// of failing loudly. Every check runs against the narrowest canvas, which is the
// tightest case: 240 wide and 240 tall on both builds.

static_assert(kNarrowestDesignWidth == 240,
              "MenuLayout: 240 is the narrowest canvas this demo builds for");
static_assert(kDesignWidth >= kNarrowestDesignWidth,
              "MenuLayout: a build must not select a canvas narrower than the proof width");

// Panel horizontally: 8 + 224 + 8 = 240 at the narrowest width, and the same
// centring at 320 only widens the margins to 48.
static_assert((kNarrowestDesignWidth - kMenuPanelWidth) / 2 >= kPlayX,
              "MenuLayout: the panel must keep a margin on the narrowest canvas");
static_assert((kNarrowestDesignWidth - kMenuPanelWidth) / 2 + kMenuPanelWidth <=
                  kNarrowestDesignWidth,
              "MenuLayout: the panel must fit the narrowest canvas");

// Panel vertically: 44 px of play area above and below the 128 px panel inside
// the 216 px play area, so it never touches the score band or the bottom edge.
static_assert(kMenuPanelY >= kPlayY && kMenuPanelY + kMenuPanelHeight <= kPlayBottom,
              "MenuLayout: the panel must fit the play area height");

// Text: each line is centred in a band inset 8 px from the panel on both sides
// (the row band, and the title centred in the same column so it lines up with the
// labels), so a wider line would overhang the frame. The hint is centred in the
// whole panel but is checked against the same inset band, so it never crowds it.
static_assert(kMenuTitleWidth <= kMenuPanelWidth - 2 * 8,
              "MenuLayout: the title must fit the panel's inner width");
static_assert(kMenuRowLongestWidth <= kMenuRowWidth,
              "MenuLayout: the longest level row must fit its band");
static_assert(kMenuHintWidth <= kMenuPanelWidth - 2 * 8,
              "MenuLayout: the hint must fit the panel's inner width");

// Vertical walk inside the panel: title, rows and hint have to arrive in that
// order and stay inside it. The 60 px of rows are 3 * 16 + 2 * 6, and one more
// 6 px gap separates the last row from the hint.
static_assert(kMenuTitleY + kGlyphHeight * kMenuTitleScale <= kMenuFirstRowY,
              "MenuLayout: the title must clear the first row");
static_assert(kMenuRowPitch > kMenuRowHeight,
              "MenuLayout: two rows must keep a gap between them");
static_assert(kMenuFirstRowY + (kMenuRowCount - 1) * kMenuRowPitch + kMenuRowHeight +
                      kMenuRowGap <=
                  kMenuHintY,
              "MenuLayout: the last row and its gap must stay above the hint");
static_assert(kMenuHintY + kGlyphHeight * kMenuHintScale <= kMenuPanelY + kMenuPanelHeight,
              "MenuLayout: the hint must stay inside the panel");

// Selection band: it overhangs the row by one pixel, so it must stay inside the
// panel on both axes -- from the first row's overhang to the last row's bottom.
static_assert(kMenuRowX >= kMenuPanelX &&
                  kMenuRowX + kMenuRowWidth <= kMenuPanelX + kMenuPanelWidth,
              "MenuLayout: the selection band must stay inside the panel horizontally");
static_assert(kMenuFirstRowY + kMenuHighlightYOffset >= kMenuPanelY &&
                  kMenuFirstRowY + (kMenuRowCount - 1) * kMenuRowPitch + kMenuHighlightYOffset +
                          kMenuHighlightHeight <=
                      kMenuPanelY + kMenuPanelHeight,
              "MenuLayout: the selection band must stay inside the panel vertically");

}  // namespace pong::presentation
