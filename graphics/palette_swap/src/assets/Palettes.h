/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

/**
 * @file Palettes.h
 * @brief The four custom 16-entry RGB565 palettes this demo cycles through.
 *
 * ## Slot order is the Color enum, not a preference
 *
 * `pixelroot32::graphics::Color` enumerators ARE the palette indices 0-15
 * (see the engine's `include/graphics/Color.h`). Entry @c n of every array
 * below is therefore what `Color::<name>` resolves to. The names in the
 * comments are not decoration: they are the contract.
 *
 * | Index | Enumerator   | Index | Enumerator    |
 * | ----- | ------------ | ----- | ------------- |
 * | 0     | Black        | 8     | Yellow        |
 * | 1     | White        | 9     | Orange        |
 * | 2     | Navy         | 10    | LightRed      |
 * | 3     | Blue         | 11    | Red           |
 * | 4     | Cyan         | 12    | DarkRed       |
 * | 5     | DarkGreen    | 13    | Purple        |
 * | 6     | Green        | 14    | Magenta       |
 * | 7     | LightGreen   | 15    | Gray          |
 *
 * ## Aliases share a slot
 *
 * Several enumerators are aliases of the sixteen above, so editing one entry
 * moves every name that collapses onto it:
 *
 * - 2  Navy      <- DarkBlue
 * - 3  Blue      <- LightBlue, DebugBlue
 * - 4  Cyan      <- Teal
 * - 5  DarkGreen <- Olive
 * - 6  Green     <- DebugGreen
 * - 8  Yellow    <- Gold
 * - 11 Red       <- DebugRed
 * - 12 DarkRed   <- Brown, Maroon
 * - 14 Magenta   <- Pink, LightPurple
 * - 15 Gray      <- MidGray, LightGray, DarkGray, Silver
 *
 * `Color::Silver == Color::Gray` compares true. There is no separate silver to
 * "fix" — the demo draws both words side by side to make that visible.
 *
 * ## Storage duration is a requirement, not a style choice
 *
 * `setCustomPalette()` stores the pointer and never copies the sixteen entries
 * (verified in the engine's `src/graphics/Color.cpp`: it assigns @c palette to
 * @c currentPalette and to the slot-0 banks, and `resolveColor()` dereferences
 * that pointer on every draw). A palette handed to the engine must therefore
 * outlive every frame that uses it. `inline constexpr` at namespace scope gives
 * these arrays static storage duration and puts them in flash/rodata, which is
 * exactly what the engine needs. A palette built on the stack of `init()` would
 * compile, run, and then resolve colours out of dead memory.
 *
 * ## Two entries every palette must keep legible
 *
 * The HUD paints on slot 0 and writes with slot 1, so a palette whose slot 1 is
 * as dark as its slot 0 hides its own name. Each palette below keeps 0 near
 * black and 1 near white for that reason.
 */

namespace palette_swap {

/// Every palette handed to the engine is exactly PALETTE_SIZE (16) entries.
inline constexpr uint8_t kPaletteEntries = 16;

/**
 * @brief Warm fire ramp: the cool half of the enum is pulled into browns.
 *
 * Nothing on screen asks for a brown. `Color::Blue` still means "slot 3" —
 * slot 3 simply is a brown here, and the draw code never learns about it.
 */
inline constexpr uint16_t PALETTE_EMBER[kPaletteEntries] = {
    0x1040,  //  0 Color::Black      #140A05
    0xFF9A,  //  1 Color::White      #FFF3D6
    0x38C2,  //  2 Color::Navy       #3A1B10
    0x6942,  //  3 Color::Blue       #6B2A12
    0xFC47,  //  4 Color::Cyan       #FF8A3C
    0x28A1,  //  5 Color::DarkGreen  #2A1408
    0xC283,  //  6 Color::Green      #C05018
    0xFE2D,  //  7 Color::LightGreen #FFC46A
    0xFF0C,  //  8 Color::Yellow     #FFE066
    0xFC84,  //  9 Color::Orange     #FF9020
    0xFB08,  // 10 Color::LightRed   #FF6040
    0xE102,  // 11 Color::Red        #E02010
    0x8081,  // 12 Color::DarkRed    #801008
    0xA1C5,  // 13 Color::Purple     #A03828
    0xFA92,  // 14 Color::Magenta    #FF5090
    0x8B4A   // 15 Color::Gray       #8A6A52
};

/// Cold high-contrast blues: the mirror image of EMBER over the same slots.
inline constexpr uint16_t PALETTE_SIGNAL[kPaletteEntries] = {
    0x0021,  //  0 Color::Black      #02060E
    0xEFDF,  //  1 Color::White      #E8FBFF
    0x08E7,  //  2 Color::Navy       #0A1E3C
    0x1B5F,  //  3 Color::Blue       #1E6BFF
    0x271F,  //  4 Color::Cyan       #22E0FF
    0x09C6,  //  5 Color::DarkGreen  #0C3A34
    0x1634,  //  6 Color::Green      #16C4A0
    0x8FFC,  //  7 Color::LightGreen #8CFFE0
    0xCF9F,  //  8 Color::Yellow     #C8F0FF
    0x5D5F,  //  9 Color::Orange     #5AA8FF
    0x7C7F,  // 10 Color::LightRed   #7A8CFF
    0x329C,  // 11 Color::Red        #3050E0
    0x1970,  // 12 Color::DarkRed    #182C80
    0x59FA,  // 13 Color::Purple     #5A3CD0
    0xA39F,  // 14 Color::Magenta    #A070FF
    0x6C32   // 15 Color::Gray       #6E8494
};

/// Muted greens and earths: low saturation, so the shapes read as one scene.
inline constexpr uint16_t PALETTE_MOSS[kPaletteEntries] = {
    0x0881,  //  0 Color::Black      #0A120A
    0xF7FD,  //  1 Color::White      #F2FFE8
    0x1143,  //  2 Color::Navy       #16281A
    0x3B46,  //  3 Color::Blue       #3C6B34
    0x9ECD,  //  4 Color::Cyan       #9CD86A
    0x19C2,  //  5 Color::DarkGreen  #1E3A16
    0x4CE6,  //  6 Color::Green      #4E9C36
    0xCF93,  //  7 Color::LightGreen #C8F09A
    0xEEAD,  //  8 Color::Yellow     #E8D46A
    0xC447,  //  9 Color::Orange     #C08A38
    0xC38B,  // 10 Color::LightRed   #C2705A
    0x9A05,  // 11 Color::Red        #9A4028
    0x5943,  // 12 Color::DarkRed    #5A2A18
    0x6AD1,  // 13 Color::Purple     #6A5A8A
    0xD475,  // 14 Color::Magenta    #D08CA8
    0x7C2E   // 15 Color::Gray       #7A8470
};

/**
 * @brief Pure greyscale ramp — the demo's proof.
 *
 * Under this palette every shape keeps its shape and loses its hue, which is
 * only possible because the draw code stores slot numbers rather than colours.
 */
inline constexpr uint16_t PALETTE_MONO[kPaletteEntries] = {
    0x0000,  //  0 Color::Black      #000000
    0xFFFF,  //  1 Color::White      #FFFFFF
    0x18E3,  //  2 Color::Navy       #1C1C1C
    0x3186,  //  3 Color::Blue       #303030
    0xDEDB,  //  4 Color::Cyan       #DADADA
    0x2124,  //  5 Color::DarkGreen  #262626
    0x6B6D,  //  6 Color::Green      #6E6E6E
    0xB5B6,  //  7 Color::LightGreen #B4B4B4
    0xF79E,  //  8 Color::Yellow     #F0F0F0
    0xCE59,  //  9 Color::Orange     #C8C8C8
    0xA514,  // 10 Color::LightRed   #A0A0A0
    0x8410,  // 11 Color::Red        #808080
    0x4A49,  // 12 Color::DarkRed    #484848
    0x5AEB,  // 13 Color::Purple     #5C5C5C
    0x9492,  // 14 Color::Magenta    #909090
    0x7BCF   // 15 Color::Gray       #787878
};

/**
 * @struct PaletteInfo
 * @brief One entry of the cycle: a display name and the sixteen entries.
 *
 * @c entries points at an `inline constexpr` array above, so the pointer stays
 * valid for the whole run — see the storage-duration note at the top of this
 * file.
 */
struct PaletteInfo {
    const char* name;         ///< Shown in the HUD. Kept short: 12 chars fit.
    const uint16_t* entries;  ///< 16 RGB565 values, indexed by Color.
};

/// The cycle order that Left / Right / A walk through.
inline constexpr PaletteInfo kPalettes[] = {
    {"EMBER",  PALETTE_EMBER},
    {"SIGNAL", PALETTE_SIGNAL},
    {"MOSS",   PALETTE_MOSS},
    {"MONO",   PALETTE_MONO}
};

inline constexpr uint8_t kPaletteCount =
    static_cast<uint8_t>(sizeof(kPalettes) / sizeof(kPalettes[0]));

} // namespace palette_swap
