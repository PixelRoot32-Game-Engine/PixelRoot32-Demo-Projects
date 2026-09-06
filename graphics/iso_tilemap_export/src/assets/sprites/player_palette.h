#pragma once
#include "graphics/Color.h"
#include <stdint.h>

namespace player_sprites {

using pixelroot32::graphics::Color;

// Actual RGB565 palette for the sprite (per export: Index 0 Transparent, 1-7 RGB in comments).
// Order: index 0 = transparent, 1 = RGB(31,16,42), 2 = RGB(74,48,82), ...
// Conversion: (R>>3)<<11 | (G>>2)<<5 | (B>>3)
static const uint16_t PLAYER_SPRITE_PALETTE_RGB565[16] = {
    0x0000,  // 0: Transparent
    0x0842,  // 1: RGB(9, 10, 20)
    0x10A3,  // 2: RGB(16, 20, 31)
    0x10E5,  // 3: RGB(21, 29, 40)
    0x10E5,  // 4: RGB(22, 29, 40)
    0x10E5,  // 5: RGB(22, 30, 40)
    0x10E5,  // 6: RGB(22, 30, 41)
    0x18E5,  // 7: RGB(24, 31, 42)
    0x1905,  // 8: RGB(24, 32, 43)
    0x1905,  // 9: RGB(25, 32, 43)
    0x1905,  // 10: RGB(26, 34, 44)
    0x1905,  // 11: RGB(27, 34, 45)
    0x1905,  // 12: RGB(27, 35, 45)
    0x1905,  // 13: RGB(28, 35, 45)
    0x1925,  // 14: RGB(29, 36, 46)
    0x1965   // 15: RGB(30, 46, 42)
};

// Palette mapping for the sprite (1:1 mapping to global palette indices)
static const Color PALETTE_PLAYER_MAPPING[16] = {
    (Color)0, (Color)1, (Color)2, (Color)3,
    (Color)4, (Color)5, (Color)6, (Color)7,
    (Color)8, (Color)9, (Color)10, (Color)11,
    (Color)12, (Color)13, (Color)14, (Color)15,
};

} // namespace metroidvania
