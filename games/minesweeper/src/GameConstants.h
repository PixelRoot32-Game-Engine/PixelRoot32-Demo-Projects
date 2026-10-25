#pragma once

#include <platforms/EngineConfig.h>

namespace minesweeper {

/**
 * @file GameConstants.h
 * @brief Layout and scene tuning for the fixed 240x240 composition.
 *
 * Engine-dependent on purpose: only the presentation layer includes it.
 * The rules core (src/game/) and input (src/input/) stay engine-free.
 */

// --- Composition box ----------------------------------------------------------
// The scene is authored in a fixed 240x240 box -- the screen the sprite line was
// drawn for -- while the canvas it draws into may be larger. The CYD target is a
// 240x320 panel used a quarter turn, so its framebuffer is 320x240. Keeping one
// design size and centring it with a viewport offset is what stops every
// coordinate below from having to know about the extra width.
constexpr int DESIGN_WIDTH = 240;
constexpr int DESIGN_HEIGHT = 240;

/**
 * @brief The logical canvas, measured after the engine's rotation.
 *
 * DisplayConfig swaps the logical width and height for a quarter turn, so
 * LOGICAL_WIDTH/LOGICAL_HEIGHT describe the panel as mounted, not the
 * framebuffer the scene draws into: the 240x320 CYD at DISPLAY_ROTATION=3
 * renders into 320x240. Rotation also arrives as 0..3 or as degrees, hence
 * the normalisation to a turn count.
 */
constexpr int QUARTER_TURNS =
    (DISPLAY_ROTATION >= 4 ? DISPLAY_ROTATION / 90 : DISPLAY_ROTATION) % 4;
constexpr int CANVAS_WIDTH = (QUARTER_TURNS % 2 == 0) ? LOGICAL_WIDTH : LOGICAL_HEIGHT;
constexpr int CANVAS_HEIGHT = (QUARTER_TURNS % 2 == 0) ? LOGICAL_HEIGHT : LOGICAL_WIDTH;

// The design box sits in the middle of whatever canvas the target reports: 40 px
// of margin on each side of the 320 px landscape canvas, and (0, 0) on the two
// 240x240 targets, which is why their output is unchanged.
constexpr int VIEW_X = (CANVAS_WIDTH - DESIGN_WIDTH) / 2;
constexpr int VIEW_Y = (CANVAS_HEIGHT - DESIGN_HEIGHT) / 2;

// A composition that does not fit would draw off-panel instead of failing
// loudly, so the design box is checked against the real canvas here.
static_assert(DESIGN_WIDTH <= CANVAS_WIDTH && DESIGN_HEIGHT <= CANVAS_HEIGHT,
              "Minesweeper: the 240x240 composition must fit the logical canvas");

// --- Layout, in design-box pixels --------------------------------------------
// The cell size is the native tile size, not the largest size that fits:
// Sprite4bpp cannot scale, so the board geometry follows the generated art.
//   EASY    9x9  @ 16 px = 144x144
//   MEDIUM 16x16 @ 12 px = 192x192
//   EXPERT 30x16 @  8 px = 240x128
// The board is centred in the area below the header rule (BOARD_TOP ..
// DESIGN_HEIGHT - 10), which leaves the footer strip free for text.
constexpr int HEADER_HEIGHT = 30;
constexpr int HEADER_RULE_Y = 29;
constexpr int BOARD_TOP = 32;
constexpr int FOOTER_Y = 232;
constexpr int BOARD_AREA_HEIGHT = DESIGN_HEIGHT - BOARD_TOP - 10;

// --- Sprite header band -------------------------------------------------------
// The band replaces the old text HUD with generated 8x8 glyphs, so its content
// sits on two 8 px rows and a 16 px face: words at y=4, the three-digit
// counters at y=13, the smiley spanning both. Every x is a left edge; the
// counter rows are centred by the scene on their own label width.
//   MINES 5 glyphs = 40 px at x=4   -> counter x=12
//   TIME  4 glyphs = 32 px at x=204 -> counter x=208
//   face  16 px    = 112..127 (screen centre)
constexpr int HEADER_LABEL_Y = 4;
constexpr int HEADER_COUNTER_Y = 13;
constexpr int HEADER_LEFT_X = 4;
constexpr int HEADER_RIGHT_X = 204;
constexpr int HEADER_SMILEY_X = 112;
constexpr int HEADER_SMILEY_Y = 6;

// --- Framed overlays ----------------------------------------------------------
// Width of one 9-slice piece. Corners and edges are all 8x8, so this is also
// the inner inset every panel keeps between its frame and its content.
constexpr int PANEL_BORDER = 8;

// --- Menu panel ---------------------------------------------------------------
// The panel keeps 12 px to the screen edge and its own 8 px frame; content is
// inset another 4 px so no glyph touches the border art. The three difficulty
// rows are engine UI widgets (UIVerticalLayout + UIButton), so their geometry
// is stated here as the layout rect, the row size and the gap between rows.
constexpr int MENU_PANEL_X = 12;
constexpr int MENU_PANEL_Y = 40;
constexpr int MENU_PANEL_W = 216;
constexpr int MENU_PANEL_H = 168;
constexpr int MENU_CONTENT_X = MENU_PANEL_X + PANEL_BORDER + 4;        // 24
constexpr int MENU_CONTENT_W = MENU_PANEL_W - 2 * (PANEL_BORDER + 4);  // 192
constexpr int MENU_TITLE_Y = MENU_PANEL_Y + PANEL_BORDER;              // 48
constexpr int MENU_TITLE_H = 24;
constexpr int MENU_ROW_FIRST_Y = 84;
constexpr int MENU_ROW_H = 16;
constexpr int MENU_ROW_GAP = 10;
constexpr int MENU_ROW_STEP = MENU_ROW_H + MENU_ROW_GAP;  // 26
constexpr int MENU_ROW_AREA_H = 3 * MENU_ROW_H + 2 * MENU_ROW_GAP;
constexpr int MENU_HINT_FIRST_Y = 164;
constexpr int MENU_HINT_STEP = 14;

// Physical button that activates the selected row: A, in InputConfig order
// (Up, Down, Left, Right, A, B). The scene reads the pad through
// InputController, so the widgets carry the value for their own contract.
constexpr std::uint8_t MENU_ACTIVATE_BUTTON = 4;

// The layout must stay inside the panel, and the hints below it, or the menu
// would draw over its own frame.
static_assert(MENU_ROW_FIRST_Y >= MENU_TITLE_Y + MENU_TITLE_H, "rows must clear the title bar");
static_assert(MENU_ROW_FIRST_Y + MENU_ROW_AREA_H <= MENU_HINT_FIRST_Y, "rows must clear the hints");
static_assert(MENU_HINT_FIRST_Y + 2 * MENU_HINT_STEP <= MENU_PANEL_Y + MENU_PANEL_H,
              "hints must stay inside the panel");

// The band is one strip above the board; asserting the relationship here means
// a later constant edit cannot silently push the header onto the playfield.
static_assert(HEADER_LABEL_Y < HEADER_COUNTER_Y, "the counter row sits under the label row");
static_assert(HEADER_COUNTER_Y < HEADER_RULE_Y, "the counter row must stay inside the band");
static_assert(HEADER_SMILEY_Y + 16 <= HEADER_RULE_Y, "the 16 px smiley must stay inside the band");
static_assert(HEADER_SMILEY_X + 16 <= DESIGN_WIDTH, "the smiley must stay on screen");
static_assert(HEADER_LEFT_X < HEADER_SMILEY_X && HEADER_SMILEY_X + 16 <= HEADER_RIGHT_X,
              "the smiley must sit between the two label groups");
static_assert(HEADER_RULE_Y < BOARD_TOP, "the header must never overlap the board");

// --- Result banner ------------------------------------------------------------
constexpr int RESULT_WIDTH = 200;
constexpr int RESULT_HEIGHT = 72;

// --- Input behaviour ----------------------------------------------------------
constexpr unsigned long GAME_OVER_LOCK_MS = 600;

}  // namespace minesweeper
