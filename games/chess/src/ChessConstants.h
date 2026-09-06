/*
 * ChessConstants.h - Layout and palette for the chess demo.
 *
 * The whole screen is 240x320. The top 240x240 is the board at exactly 30 px
 * per square, which is the largest cell size that divides the panel width
 * evenly; the bottom 80 px is the HUD.
 */
#pragma once

#include <cstdint>

#include <graphics/Color.h>
#include <platforms/EngineConfig.h>

namespace chessdemo {

// --- Board -------------------------------------------------------------------

constexpr int kBoardCells   = 8;
constexpr int kCellSize     = 30;                        ///< 240 / 8, exactly.
constexpr int kBoardX       = 0;
constexpr int kBoardY       = 0;
constexpr int kBoardPixels  = kBoardCells * kCellSize;   ///< 240

/**
 * Pieces are stored at their final 28x28 size, not scaled at draw time: the
 * renderer has no scaled overload for Sprite4bpp. That leaves a 1 px margin
 * inside each cell.
 */
constexpr int kPieceInset = 1;

/**
 * How far above the finger a dragged piece is drawn. Without it the finger
 * covers the piece it is carrying; with it the piece stays visible and the
 * square under the finger - highlighted separately - is what aims the drop.
 */
constexpr int kDragLift = kCellSize / 2;

// --- HUD ---------------------------------------------------------------------

constexpr int kHudY      = kBoardY + kBoardPixels;   ///< 240
constexpr int kHudHeight = 80;

constexpr int kStatusTextY = kHudY + 4;   ///< size-2 text, 16 px tall

/** One tray per side, indexed by the chess::Side that lost the pieces. */
constexpr int kCapturedRowY[2] = { kHudY + 22, kHudY + 37 };
constexpr int kCapturedStride  = 14;      ///< icon width; 16 icons fit in 224 px
constexpr int kCapturedOriginX = 4;
constexpr int kCapturedMaxShown = 16;

constexpr int kButtonY      = kHudY + 53;
constexpr int kButtonHeight = 22;
constexpr int kButtonWidth  = 108;
constexpr int kNewGameX     = 6;
constexpr int kResignX      = kNewGameX + kButtonWidth + 12;

// --- Promotion picker --------------------------------------------------------

/** A four-cell strip centred over the board while a promotion is pending. */
constexpr int kPromoCell   = 44;
constexpr int kPromoWidth  = kPromoCell * 4 + 8;
constexpr int kPromoHeight = kPromoCell + 22;
constexpr int kPromoX      = (kBoardPixels - kPromoWidth) / 2;
constexpr int kPromoY      = (kBoardPixels - kPromoHeight) / 2;
constexpr int kPromoRowY   = kPromoY + 18;

// --- Palette -----------------------------------------------------------------
//
// Only the 16 PR32 slots exist. Two of them behave in ways the names do not
// suggest, and both matter here:
//
//   Color::Black is RGB565 0x0000, which packs to the byte the 8bpp framebuffer
//   reads as transparent. Anything drawn in it survives on the SDL2 build and
//   vanishes on the ESP32, so it is never used for something that must be seen.
//
//   Color::Magenta is #CECECE in this palette, a light grey.
//
// The two square colours below are mirrored in tools/generate_pieces.py as
// LIGHT_SQUARE and DARK_SQUARE, where check_contrast() uses them to reject a
// piece colour that would be invisible against either one. Changing a square
// here without changing it there disables that check silently.

constexpr pixelroot32::graphics::Color kLightSquare    = pixelroot32::graphics::Color::Magenta;    ///< #CECECE
constexpr pixelroot32::graphics::Color kDarkSquare     = pixelroot32::graphics::Color::DarkGreen;  ///< #0E7A0D

constexpr pixelroot32::graphics::Color kSelectedSquare = pixelroot32::graphics::Color::Yellow;
constexpr pixelroot32::graphics::Color kTargetMarker   = pixelroot32::graphics::Color::Blue;
constexpr pixelroot32::graphics::Color kCaptureMarker  = pixelroot32::graphics::Color::LightRed;
constexpr pixelroot32::graphics::Color kLastMoveMarker = pixelroot32::graphics::Color::Cyan;
constexpr pixelroot32::graphics::Color kCheckMarker    = pixelroot32::graphics::Color::Red;

/*
 * Debris thrown up when a piece is taken.
 *
 * The end colour is deliberately not Black. Particles fade from start to end
 * over their life, and a fade that lands on Color::Black lands on the byte the
 * 8bpp framebuffer treats as transparent - so the tail of the burst would be
 * visible under SDL2 and gone on the ESP32. That is also what rules out
 * ParticlePresets::Explosion and ::Smoke here: both end on Color::Black.
 *
 * Both ends are also high-luminance rather than bright-to-dark. Half the board
 * is DarkGreen #0E7A0D, and a fade toward Red #C1121F sinks into it - the tail
 * of the burst reads on the light squares and vanishes on the dark ones. Yellow
 * #FFD500 to Orange #FF9F1C stays visible on both for the whole life.
 */
constexpr pixelroot32::graphics::Color kCaptureSparkStart = pixelroot32::graphics::Color::Yellow;
constexpr pixelroot32::graphics::Color kCaptureSparkEnd   = pixelroot32::graphics::Color::Orange;

constexpr pixelroot32::graphics::Color kHudBackground = pixelroot32::graphics::Color::Navy;
constexpr pixelroot32::graphics::Color kHudText       = pixelroot32::graphics::Color::White;
constexpr pixelroot32::graphics::Color kHudDivider    = pixelroot32::graphics::Color::Gray;
constexpr pixelroot32::graphics::Color kButtonFill    = pixelroot32::graphics::Color::Gray;
constexpr pixelroot32::graphics::Color kButtonText    = pixelroot32::graphics::Color::Navy;
constexpr pixelroot32::graphics::Color kPanelFill     = pixelroot32::graphics::Color::Navy;
constexpr pixelroot32::graphics::Color kPanelBorder   = pixelroot32::graphics::Color::White;

}  // namespace chessdemo
