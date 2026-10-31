#pragma once

#include <cstdint>

namespace doom {
namespace palette {

/// Entries in one PLAYPAL palette, and the bytes that palette occupies: R, G,
/// B per entry, in that order.
constexpr int kEntryCount = 256;
constexpr int kRgb888Bytes = kEntryCount * 3;

/**
 * @brief Pack an RGB565 colour into the byte an 8bpp framebuffer carries.
 *
 * The 8bpp surface is not a palette index: the byte is a colour, RRRGGGBB
 * (RGB332), which the drawer expands back to RGB565 while it presents
 * (`TFT_eSPI_Drawer::buildScaleLUTs()` fills its LUT from `color8to16()`).
 *
 * These are the same shifts TFT_eSPI's own 8bpp `drawPixel()` / `fillRect()`
 * apply (Extensions/Sprite.cpp) and the same ones `Renderer`'s direct blit path
 * applies (`packRgb565ToTftSprite8()` in src/graphics/Renderer.cpp). That helper
 * is file-local there, so the port keeps its own copy; the static_asserts below
 * pin the arithmetic to the wire colours it must produce, so a change on either
 * side fails this build instead of showing the wrong hue on the panel.
 */
constexpr std::uint8_t packRgb332(std::uint16_t rgb565) {
    return static_cast<std::uint8_t>(((rgb565 & 0xE000u) >> 8) |
                                     ((rgb565 & 0x0700u) >> 6) |
                                     ((rgb565 & 0x0018u) >> 3));
}

/**
 * @brief Map one RGB888 triple -- one PLAYPAL entry -- to the same wire byte.
 *
 * Layout, in both directions: RRRGGGBB. Red keeps its top 3 bits in bits 7-5,
 * green its top 3 in bits 4-2 and blue its top 2 in bits 1-0. Integer only: no
 * float, no dithering, no per-pixel work -- a palette index becomes a colour by
 * one shift-and-mask, once per entry, when the table is built.
 *
 * This is the port's only RGB888 -> wire conversion. `buildIndexTable()` is a
 * loop over it, and `packRgb332()` is pinned to it by the static_asserts below,
 * so there is no second mapping that can drift from the panel's.
 */
constexpr std::uint8_t rgb888To332(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint8_t>((r & 0xE0u) | ((g & 0xE0u) >> 3) | (b >> 6u));
}

/**
 * @brief Expand an RGB332 wire byte back to RGB565.
 *
 * Mirrors TFT_eSPI's `color8to16()`: red and green are bit-replicated, blue is
 * a 4-entry lookup (`{0, 11, 21, 31}`), not replicated. Only the desktop
 * mirror needs this -- the panel never expands anything, it blits the byte --
 * but matching the drawer keeps the two screens showing the same colour.
 */
constexpr std::uint16_t expandRgb332(std::uint8_t rgb332) {
    constexpr std::uint8_t kBlue5[4] = {0, 11, 21, 31};
    const std::uint16_t green = static_cast<std::uint16_t>(rgb332 & 0x1Cu);
    return static_cast<std::uint16_t>(((rgb332 & 0xE0u) << 8) |
                                      ((rgb332 & 0xC0u) << 5) |
                                      (green << 6) |
                                      (green << 3) |
                                      kBlue5[rgb332 & 0x03u]);
}

// Known colours, pinned at build time. The source of truth is the layout stated
// above (RRRGGGBB) and the engine's 8bpp packer; these asserts are what turn a
// channel swap into a compile error instead of a wrong hue on the panel.
static_assert(packRgb332(0xFFFF) == 0xFF, "RGB332: white must land on 0xFF");
static_assert(packRgb332(0x0000) == 0x00, "RGB332: black must land on 0x00");
static_assert(packRgb332(0xF800) == 0xE0, "RGB332: red must land on 0xE0");
static_assert(packRgb332(0x07E0) == 0x1C, "RGB332: green must land on 0x1C");
static_assert(packRgb332(0x001F) == 0x03, "RGB332: blue must land on 0x03");
static_assert(packRgb332(0xFFE0) == 0xFC, "RGB332: yellow must land on 0xFC");

static_assert(rgb888To332(255, 255, 255) == 0xFF, "RGB888 -> RGB332: white must land on 0xFF");
static_assert(rgb888To332(0, 0, 0) == 0x00, "RGB888 -> RGB332: black must land on 0x00");
static_assert(rgb888To332(255, 0, 0) == 0xE0, "RGB888 -> RGB332: pure red must land on 0xE0");
static_assert(rgb888To332(0, 255, 0) == 0x1C, "RGB888 -> RGB332: pure green must land on 0x1C");
static_assert(rgb888To332(0, 0, 255) == 0x03, "RGB888 -> RGB332: pure blue must land on 0x03");
static_assert(rgb888To332(128, 128, 128) == 0x92, "RGB888 -> RGB332: mid grey must land on 0x92");

// The palette path and the shell's RGB565 path must produce the same byte, or
// the panel and the desktop mirror would disagree about the same colour.
static_assert(rgb888To332(255, 255, 255) == packRgb332(0xFFFF), "RGB332 paths disagree on white");
static_assert(rgb888To332(255, 0, 0) == packRgb332(0xF800), "RGB332 paths disagree on red");
static_assert(rgb888To332(0, 255, 0) == packRgb332(0x07E0), "RGB332 paths disagree on green");
static_assert(rgb888To332(0, 0, 255) == packRgb332(0x001F), "RGB332 paths disagree on blue");

static_assert(expandRgb332(0xFF) == 0xFFFF, "RGB332 -> RGB565: white must expand to 0xFFFF");
static_assert(expandRgb332(0x00) == 0x0000, "RGB332 -> RGB565: black must expand to 0x0000");
static_assert(expandRgb332(0xE0) == 0xF800, "RGB332 -> RGB565: red must expand to 0xF800");
static_assert(expandRgb332(0x1C) == 0x07E0, "RGB332 -> RGB565: green must expand to 0x07E0");
static_assert(expandRgb332(0x03) == 0x001F, "RGB332 -> RGB565: blue must expand to 0x001F");

/**
 * @brief Convert a whole PLAYPAL palette into the 256 bytes the rasteriser stores.
 *
 * @param rgb888 256 entries of R, G, B -- 768 bytes, exactly the layout the
 *               WAD's `PLAYPAL` lump stores (one 768-byte palette at a time).
 *               Phase 2 feeds the real lump here; until then the port passes
 *               its synthetic palette.
 * @param out    256 bytes, one RRRGGGBB wire byte per palette index. Index `i`
 *               is the colour DOOM's rasteriser writes for palette index `i`.
 *
 * Neither pointer is retained and nothing is allocated.
 */
void buildIndexTable(const std::uint8_t* rgb888, std::uint8_t* out);

/**
 * @brief Build the port's table from the synthetic palette, once.
 *
 * Idempotent: a second call is a no-op. `indexTable()` calls it on first use,
 * so no caller can observe an unbuilt table.
 */
void init();

/**
 * @brief The 256-entry index -> RGB332 table, in palette order.
 *
 * The pointer is stable for the lifetime of the program. DOOM's rasteriser
 * writes `indexTable()[paletteIndex]` straight into the 8bpp framebuffer, one
 * byte per pixel, which is why this table is the whole of Plan B.
 *
 * This is the *synthetic* palette built by `init()`, used by the shell and by
 * the palette contract screen. The shipped game frame uses `playpalTable()`.
 */
const std::uint8_t* indexTable();

/**
 * @brief Installs the table built from the WAD's own PLAYPAL lump (D4.0).
 *
 * The rasteriser writes DOOM palette indices into the engine's 8bpp surface,
 * whose bytes are RGB332. Converting a finished frame in place is one pass of
 * `buffer[i] = table[buffer[i]]`, so this table is the frame path's whole colour
 * contract: entry `i` is the wire byte palette index `i` must become.
 *
 * @param rgb888 768 bytes, palette 0 of the PLAYPAL lump (256 R,G,B triples).
 *               The caller owns it; this copies through `buildIndexTable()`.
 *
 * Idempotent in the same sense as `init()`: the table is built once per boot
 * and a second call is a no-op, so the caller cannot install a second mapping
 * over the one the frame is already using. The pointer `playpalTable()` returns
 * is stable for the lifetime of the program.
 */
void initFromPlaypal(const std::uint8_t* rgb888);

/**
 * @brief The 256-entry PLAYPAL index -> RGB332 table, in palette order.
 *
 * Zero-filled until `initFromPlaypal()` runs, which the D4.0 scene does once,
 * right after the container is open. Unlike `indexTable()` it never falls back
 * to the synthetic palette: a frame converted through an unbuilt table is black,
 * which is a visible failure instead of a wrong-coloured one.
 */
const std::uint8_t* playpalTable();

}  // namespace palette
}  // namespace doom
