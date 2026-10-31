#include "DoomScene.h"

#include "doom_link_probe.h"
#include "doomc/DoomConsole.h"

#if defined(PIXELROOT32_DOOM_WAD_PROBE)
#include "doomc/WhdSource.h"
#endif

#if defined(PIXELROOT32_DOOM_ZONE_PROBE)
#include "z_zone.h"
#endif

#if defined(PIXELROOT32_DOOM_FLASH_PROFILE)
#include "doomc/FlashProfile.h"
#endif

#if defined(PIXELROOT32_DOOM_FRAME_PROBE)
#include "doomc/FrameDriver.h"
#endif

#include "palette/DoomPalette.h"

#include <cstddef>
#include <cstdio>

#if defined(PLATFORM_NATIVE)
#include <chrono>
#else
#include <Arduino.h>

#include "time/Esp32TicSource.h"

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
#include <esp_heap_caps.h>

// Bounds of the static DRAM image, provided by the ESP32 linker script. The
// census reports what the image already owns before it talks about what a
// second frame buffer would cost.
extern "C" {
extern std::uint8_t _data_start;
extern std::uint8_t _data_end;
extern std::uint8_t _bss_start;
extern std::uint8_t _bss_end;
}
#endif
#endif

#include <core/Engine.h>
#include <graphics/Color.h>
#include <graphics/DrawSurface.h>
#include <graphics/Renderer.h>

// Declared once by the platform header of whichever target is being built, the
// same way PongScene borrows it. The scene needs exactly two things from the
// engine: the draw surface's framebuffer, and the input manager.
extern pixelroot32::core::Engine engine;

namespace doom {

namespace {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::DrawSurface;
using pixelroot32::graphics::Renderer;

// --- Input contract ---------------------------------------------------------

/// Buttons the board exposes, in the engine's `InputConfig` order.
///
/// The order is the whole contract: index 0 is up, 5 is B, and this scene both
/// paints its slots and packs its bits in that same order (see the class
/// comment in DoomScene.h).
constexpr int kButtonCount = 6;

// --- Shell geometry ---------------------------------------------------------

/// Side of a corner orientation marker, in logical pixels.
constexpr int kMarkerSize = 16;

/// Button slot size and spacing, in logical pixels.
///
/// The six slots are centred, so the row is 184 px wide inside the 320 px
/// surface and clears the 16 px corner markers on both sides.
constexpr int kSlotWidth = 24;
constexpr int kSlotHeight = 8;
constexpr int kSlotGap = 8;

/// Width of the whole six-slot row.
constexpr int kSlotRowWidth = kButtonCount * kSlotWidth + (kButtonCount - 1) * kSlotGap;

// --- Palette ----------------------------------------------------------------

/**
 * @brief What the shell paints, in the shell's own vocabulary.
 *
 * The enumerators index `kInkColors`, which maps each one to an engine palette
 * entry; nothing here hardcodes an RGB value, so the shell follows whichever
 * palette the engine has active, exactly as the placeholder frame did when it
 * asked `Renderer` for `Color::Red`.
 */
enum class Ink : std::uint8_t {
    Background = 0,    ///< Whole surface, so the shell owns every pixel.
    Frame,             ///< The 1 px outline on the outermost logical pixels.
    Slot,              ///< Idle button slot, an outline only.
    Pressed,           ///< Held button, filled solid.
    MarkerTopLeft,     ///< Red: the first pixel the rasteriser will write.
    MarkerTopRight,    ///< Green.
    MarkerBottomLeft,  ///< Blue.
    MarkerBottomRight, ///< Yellow.
    Count
};

/// One palette entry per `Ink`, in enumerator order.
constexpr Color kInkColors[] = {
    Color::Navy,   // Background
    Color::White,  // Frame
    Color::Gray,   // Slot
    Color::White,  // Pressed
    Color::Red,    // MarkerTopLeft
    Color::Green,  // MarkerTopRight
    Color::Blue,   // MarkerBottomLeft
    Color::Yellow, // MarkerBottomRight
};

static_assert(sizeof(kInkColors) / sizeof(kInkColors[0]) == static_cast<std::size_t>(Ink::Count),
              "DoomScene: kInkColors must keep one entry per Ink, in enumerator order");

// --- Pixel formats ----------------------------------------------------------

// The colour formats live in palette/DoomPalette.h: the RGB332 layout the 8bpp
// surface stores, the palette table DOOM's rasteriser indexes, and the
// static_asserts that pin both to the wire colours they must produce. The shell
// writes through `palette::packRgb332()` for the same reason -- one conversion
// in the port, not two that can drift.

// --- Console ----------------------------------------------------------------

/**
 * @brief Write one already-formatted line to the engine's console.
 *
 * Delegates to `doomc::writeLine()`, the port's own console symbol, which calls
 * `pixelroot32::core::logging::platformPrint()`: the Arduino core's Serial on
 * the device, stdout on the simulator. It is always compiled in, unlike
 * `logging::log()`, whose output is gated behind `PIXELROOT32_DEBUG_MODE`;
 * these lines are part of the shell contract on every build.
 *
 * The delegation is not decoration. The C <-> C++ bridge writes its `LINK,...`
 * record through the same symbol (`src/doomc/DoomLinkBridge.cpp`), so the one
 * line produced from C has exactly the path the lines produced here have.
 */
void writeConsoleLine(const char* line) {
    doomc::writeLine(line);
}

/**
 * @brief Report the framebuffer contract DOOM's renderer has to satisfy.
 *
 * One line, at boot, named fields only: the surface the renderer may assume,
 * the stride it must use, the byte size of the buffer the shell actually got,
 * and the palette the drawer presents it in.
 *
 * `bytes` also says which path is live, so a maintainer reading the log learns
 * as much as the code does: 64,000 B for the 8bpp panel buffer, 128,000 B for
 * the 16bpp desktop one, 0 for a driver with neither.
 *
 * Suppressed by the frame-log build: that build's stream is pure `FRAME,...`
 * CSV, and the one boot line would be the only record without a frame index.
 */
#if !defined(PIXELROOT32_DOOM_FRAME_LOG)
void reportFrameContract(Renderer& renderer) {
    DrawSurface& surface = renderer.getDrawSurface();
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();

    // Both drivers keep the frame at logical resolution: the TFT_eSPI sprite is
    // created at logical size and the SDL2 buffer is allocated at logical size
    // (`SDL2_Drawer::init()`), so the stride is the logical width on both, not
    // the physical one. The platform headers static_assert the pair.
    const int stride = width;

    const bool has8bpp = surface.getSpriteBuffer() != nullptr;
    const bool hasRgb565 = surface.getPixelBuffer() != nullptr;
    const int bytesPerPixel = has8bpp ? 1 : (hasRgb565 ? 2 : 0);

    // "rgb565" names the palette the drawer presents, not the byte packing: an
    // 8bpp buffer's bytes are colours by default (RGB332, expanded by the
    // drawer) or, under PIXELROOT32_DOOM_SHOW_E1M1, DOOM palette indices
    // expanded through the PLAYPAL table installed with setPalette8(). Either
    // way the panel receives RGB565. A driver with neither buffer has no palette
    // to name.
    const char* palette = bytesPerPixel != 0 ? "rgb565" : "none";

    char line[128];
    std::snprintf(line, sizeof line, "SHELL,surface=%dx%d,stride=%d,bytes=%d,palette=%s\n",
                  width, height, stride, stride * height * bytesPerPixel, palette);
    writeConsoleLine(line);
}
#endif  // !PIXELROOT32_DOOM_FRAME_LOG

// --- Memory census (D4.0c) --------------------------------------------------

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

/**
 * @brief `dram0_0_seg`'s length in this build's `firmware.map`.
 *
 * The ESP32 linker carves the static image (`.dram0.data` + `.dram0.bss`) out of
 * this region; the heap starts at `_bss_end` and continues in the DRAM regions
 * the framework leaves outside the segment. The number is the map's
 * `0x3ffbdb5c + 0x1e6a4`, stated here so the serial record names the pool the
 * static allocations come from instead of the 327,680 B PlatformIO prints for
 * the whole chip. If the image outgrows this region the link fails, so a stale
 * constant is a report error, never a silent overflow.
 */
constexpr std::uint32_t kDramImageRegionBytes = 124580u;

/**
 * @brief Prints the DRAM/heap census once, before the first game frame.
 *
 * Two lines, both `SHOW,census,...`, because they answer two different
 * questions and a single 400-character line is not a record anyone reads:
 *
 *  - `pool`: the static image's share of `dram0_0_seg` and the heap the rest of
 *    the frame path is allocated from, split into the general internal pool and
 *    the DMA-capable one the line buffers must come from. `heap_largest` is the
 *    one a second 320x200 frame buffer has to fit inside.
 *  - `frame`: every allocation the engine's present path owns, with the pool it
 *    comes from named in the field. The sizes are the driver's literals, not
 *    estimates: the sprite is `width*height + 1` because TFT_eSprite adds one
 *    off-screen guard pixel, the line buffers are two blocks of
 *    `physicalWidth * 2 B * linesPerBlock`, and the three LUTs are fixed.
 *
 * The port's own 512 B RGB565 table (palette::playpal565Table()) is a static
 * array, not heap; it is named so the total is not silently larger than the
 * engine's row.
 */
void reportMemoryCensus(Renderer& renderer) {
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();

    // The driver runs 1:1 here (physical == logical), which is what makes the
    // line-buffer arithmetic below exact for this environment.
    const std::uint32_t spriteBytes = static_cast<std::uint32_t>(width) * height + 1u;
    const std::uint32_t dmaLineBytes =
        static_cast<std::uint32_t>(width) * 2u * PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK;
    const std::uint32_t dmaLineTotal = dmaLineBytes * 2u;
    const std::uint32_t paletteLutBytes = 256u * 2u;
    const std::uint32_t xLutBytes = static_cast<std::uint32_t>(width) * 2u;
    const std::uint32_t yLutBytes = static_cast<std::uint32_t>(height) * 2u;
    const std::uint32_t engineFrameBytes =
        spriteBytes + dmaLineTotal + paletteLutBytes + xLutBytes + yLutBytes;

#if defined(PLATFORM_NATIVE)
    const std::uint32_t staticData = 0u;
    const std::uint32_t staticBss = 0u;
    const std::uint32_t heapTotal = 0u;
    const std::uint32_t heapUsed = 0u;
    const std::uint32_t heapFree = 0u;
    const std::uint32_t heapLargest = 0u;
    const std::uint32_t dmaTotal = 0u;
    const std::uint32_t dmaFree = 0u;
    const std::uint32_t dmaLargest = 0u;
    const char* twinVerdict = "unknown";
#else
    const std::uint32_t staticData =
        static_cast<std::uint32_t>(&_data_end - &_data_start);
    const std::uint32_t staticBss =
        static_cast<std::uint32_t>(&_bss_end - &_bss_start);
    const std::uint32_t heapTotal =
        static_cast<std::uint32_t>(heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t heapFree =
        static_cast<std::uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t heapUsed = heapTotal >= heapFree ? heapTotal - heapFree : 0u;
    const std::uint32_t heapLargest = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    const std::uint32_t dmaTotal = static_cast<std::uint32_t>(
        heap_caps_get_total_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    const std::uint32_t dmaFree = static_cast<std::uint32_t>(
        heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    const std::uint32_t dmaLargest = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    // The one allocation a twin-framebuffer design needs, asked for instead of
    // inferred: a second 320x200 8-bit frame must be ONE contiguous block. It
    // is taken and returned at once, so the probe leaves the heap exactly as it
    // found it; `heap_largest` alone cannot prove the answer because a smaller
    // hole can sit next to a larger one without being coalesced.
    void* twinProbe = heap_caps_malloc(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const char* twinVerdict = twinProbe != nullptr ? "fits" : "no_fit";
    if (twinProbe != nullptr) {
        heap_caps_free(twinProbe);
    }
#endif

    const std::uint32_t staticUsed = staticData + staticBss;
    const std::uint32_t segFree =
        kDramImageRegionBytes > staticUsed ? kDramImageRegionBytes - staticUsed : 0u;

    char line[384];
    std::snprintf(line, sizeof line,
                  "SHOW,census,pool,dram_seg=%u,static_data=%u,static_bss=%u,"
                  "static_used=%u,seg_free=%u,heap_total=%u,heap_used=%u,"
                  "heap_free=%u,heap_largest=%u,dma_total=%u,dma_free=%u,"
                  "dma_largest=%u,twin64=%s\n",
                  static_cast<unsigned>(kDramImageRegionBytes),
                  static_cast<unsigned>(staticData), static_cast<unsigned>(staticBss),
                  static_cast<unsigned>(staticUsed), static_cast<unsigned>(segFree),
                  static_cast<unsigned>(heapTotal), static_cast<unsigned>(heapUsed),
                  static_cast<unsigned>(heapFree), static_cast<unsigned>(heapLargest),
                  static_cast<unsigned>(dmaTotal), static_cast<unsigned>(dmaFree),
                  static_cast<unsigned>(dmaLargest), twinVerdict);
    writeConsoleLine(line);

    std::snprintf(line, sizeof line,
                  "SHOW,census,frame,sprite=%u,dma_line=%u,dma_lines=%u,"
                  "palette_lut=%u,xlut=%u,ylut=%u,engine_frame=%u,port_lut=512,"
                  "lines_per_block=%d\n",
                  static_cast<unsigned>(spriteBytes), static_cast<unsigned>(dmaLineBytes),
                  static_cast<unsigned>(dmaLineTotal),
                  static_cast<unsigned>(paletteLutBytes), static_cast<unsigned>(xLutBytes),
                  static_cast<unsigned>(yLutBytes),
                  static_cast<unsigned>(engineFrameBytes),
                  static_cast<int>(PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK));
    writeConsoleLine(line);
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1

// --- Tic clock ---------------------------------------------------------------

/**
 * @brief One microsecond sample, in the source's own 32-bit range.
 *
 * `TicClock` is platform-free; this is the only place the sample is taken. The
 * board reads the Arduino core's `micros()` through the adapter, which is
 * 32-bit and wraps every ~71 minutes. The desktop mirror produces the same
 * shape from `steady_clock`, truncated on purpose: the host then exercises the
 * same wrap extension as the panel instead of a 64-bit shortcut only one target
 * has. The cast is not an optimisation -- it is the contract the clock expects.
 */
std::uint32_t sampleMicros() {
#if defined(PLATFORM_NATIVE)
    using SteadyClock = std::chrono::steady_clock;
    const auto sinceEpoch = SteadyClock::now().time_since_epoch();
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(sinceEpoch).count());
#else
    return time::esp32Micros();
#endif
}

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
/**
 * @brief Free heap bytes for the frame record, sampled at the end of `draw()`.
 *
 * The panel build reads the Arduino core's heap counter, so the value belongs
 * to the frame that just finished. The desktop mirror has no ESP32 heap: it
 * reports 0, a sentinel that says "not measurable here", not a fake reading.
 *
 * Only the frame-log build pays for this call; the shipped environments never
 * compile it.
 */
std::uint32_t sampleFreeHeap() {
#if defined(PLATFORM_NATIVE)
    return 0;
#else
    return static_cast<std::uint32_t>(ESP.getFreeHeap());
#endif
}
#endif  // PIXELROOT32_DOOM_FRAME_LOG

// --- Pattern ----------------------------------------------------------------

/**
 * @brief Paint the whole shell into a logical-sized framebuffer.
 *
 * Geometry only: every pixel is handed to `store` with the colour it must have,
 * so the identical layout reaches the 8bpp panel buffer and the 16bpp desktop
 * one. Nothing here reads a frame counter or an animation phase -- the pattern
 * is a pure function of `inks`, the surface size and the pressed set.
 *
 * @param inks    RGB565 palette, indexed by `Ink`.
 * @param width   Surface width; also the stride.
 * @param height  Surface height.
 * @param pressed Button bits, packed as `INPUT` reports them.
 * @param store   Callable (int x, int y, std::uint16_t rgb565) that writes one
 *                pixel in the target format. It must not clip: every rectangle
 *                below is inside the surface by construction.
 */
template <typename Store>
void paintShell(const std::uint16_t* inks, int width, int height, std::uint8_t pressed,
                Store store) {
    const auto fill = [&](int x, int y, int w, int h, Ink ink) {
        const std::uint16_t rgb565 = inks[static_cast<std::size_t>(ink)];
        for (int row = 0; row < h; ++row) {
            for (int col = 0; col < w; ++col) {
                store(x + col, y + row, rgb565);
            }
        }
    };

    // Background first: the shell owns every pixel of the surface, so no stale
    // frame can survive underneath the pattern.
    fill(0, 0, width, height, Ink::Background);

    // The 1 px frame sits on the outermost logical pixels, not inset by one: a
    // present path that crops instead of scaling loses a side of it at once,
    // and on a narrower panel it also proves the downscale is a skip, not a
    // nearest-neighbour upscale of a cropped surface.
    fill(0, 0, width, 1, Ink::Frame);
    fill(0, height - 1, width, 1, Ink::Frame);
    fill(0, 0, 1, height, Ink::Frame);
    fill(width - 1, 0, 1, height, Ink::Frame);

    // Orientation markers, in the order the rasteriser fills them. Red is the
    // first thing DOOM writes (column 0, row 0); if the panel shows red
    // anywhere else, the rotation or the LUT direction is wrong.
    fill(0, 0, kMarkerSize, kMarkerSize, Ink::MarkerTopLeft);
    fill(width - kMarkerSize, 0, kMarkerSize, kMarkerSize, Ink::MarkerTopRight);
    fill(0, height - kMarkerSize, kMarkerSize, kMarkerSize, Ink::MarkerBottomLeft);
    fill(width - kMarkerSize, height - kMarkerSize, kMarkerSize, kMarkerSize,
         Ink::MarkerBottomRight);

    // Six button slots along the bottom edge, left to right in the engine's
    // order: up, down, left, right, A, B. The row is centred between the corner
    // markers and sits directly above the frame line, so it never covers a
    // marker whose colour is the orientation check.
    const int firstX = (width - kSlotRowWidth) / 2;
    const int rowY = height - 1 - kSlotHeight;
    for (int index = 0; index < kButtonCount; ++index) {
        const int x = firstX + index * (kSlotWidth + kSlotGap);
        const bool held = (pressed & (1u << index)) != 0;

        if (!held) {
            // Idle slot: an outline only, so the order stays readable when
            // nothing is held. The pressed fill below covers it whole.
            fill(x, rowY, kSlotWidth, 1, Ink::Slot);
            fill(x, rowY + kSlotHeight - 1, kSlotWidth, 1, Ink::Slot);
            fill(x, rowY + 1, 1, kSlotHeight - 2, Ink::Slot);
            fill(x + kSlotWidth - 1, rowY + 1, 1, kSlotHeight - 2, Ink::Slot);
        } else {
            fill(x, rowY, kSlotWidth, kSlotHeight, Ink::Pressed);
        }
    }
}

#if !defined(PIXELROOT32_DOOM_PALETTE_TEST)

/**
 * @brief Paint the shipped shell into whichever framebuffer the driver offers.
 *
 * Split out of `draw()` so the palette-test build can replace it whole instead
 * of editing it: with the test flag off, this is the frame the port has always
 * shown, unchanged.
 */
void paintShellFrame(Renderer& renderer, std::uint8_t pressed) {
    DrawSurface& surface = renderer.getDrawSurface();
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();

    // paintShell() indexes `y * width + x` with no clipping, which is only
    // inside the buffer while the surface has a size. Renderer asserts both are
    // positive; this guard is what keeps a broken configuration from becoming a
    // wild write instead of an empty frame.
    if (width <= 0 || height <= 0) {
        return;
    }

    // Resolved once per frame rather than per pixel: the pattern is a handful of
    // colours, and the engine palette can be swapped by any code that runs
    // between frames.
    constexpr int kInkCount = static_cast<int>(Ink::Count);
    std::uint16_t inks[kInkCount];
    for (int index = 0; index < kInkCount; ++index) {
        inks[index] = pixelroot32::graphics::resolveColor(kInkColors[index]);
    }

    // The panel path, and the one DOOM's renderer will use: the 8bpp logical
    // framebuffer, one byte per pixel, stride = logical width, top-left origin.
    // Writing through this pointer is the whole point of the shell -- it is
    // what a column-indexed rasteriser can fill without a single virtual call.
    if (std::uint8_t* framebuffer = surface.getSpriteBuffer()) {
        paintShell(inks, width, height, pressed,
                   [framebuffer, width](int x, int y, std::uint16_t rgb565) {
                       framebuffer[y * width + x] = palette::packRgb332(rgb565);
                   });
        return;
    }

    // The desktop mirror: SDL2 has no 8bpp surface, but its RGB565 buffer is
    // logical-sized and indexed the same way, so the same pattern (and the same
    // button state) reaches the window.
    if (std::uint16_t* framebuffer = surface.getPixelBuffer()) {
        paintShell(inks, width, height, pressed,
                   [framebuffer, width](int x, int y, std::uint16_t rgb565) {
                       framebuffer[y * width + x] = rgb565;
                   });
        return;
    }

    // A driver with neither buffer has no frame to write. init() has already
    // reported bytes=0,palette=none, so there is nothing left to say here.
}

#endif  // !PIXELROOT32_DOOM_PALETTE_TEST

#if defined(PIXELROOT32_DOOM_PALETTE_TEST)

/// Palette order, as PLAYPAL stores it: 16 rows of 16 entries.
constexpr int kPaletteColumns = 16;
constexpr int kPaletteRows = 16;

/// Levels in the bottom greyscale ramp.
constexpr int kGreySteps = 32;

/**
 * @brief Paint the palette contract: the whole index table, then grey banding.
 *
 * Two things and nothing else:
 *
 * 1. A 16x16 grid of 20x12 blocks in palette order -- row 0 is indices 0-15
 *    and the last block is index 255 -- so a swapped channel, an off-by-one in
 *    the table or a shuffled PLAYPAL read is visible at a glance. The synthetic
 *    palette starts with its grey row, then 15 hue ramps, which is why the grid
 *    reads as a grey top row followed by a wheel from red to magenta.
 * 2. A 32-step greyscale ramp along the bottom strip, built through the same
 *    RGB888 -> RGB332 conversion the table builder uses. RGB332 has eight
 *    distinct greys, so 32 source levels must collapse onto 8 flat bands; a
 *    smooth ramp means the strip bypassed the wire format.
 *
 * @param width   Surface width; also the stride.
 * @param height  Surface height.
 * @param store   Callable (int x, int y, std::uint8_t wire) that writes one
 *                RGB332 byte. It must not clip: every rectangle below is inside
 *                the surface by construction.
 */
template <typename Store>
void paintPaletteTest(int width, int height, Store store) {
    const std::uint8_t* table = palette::indexTable();

    // Clear first. At 320x200 the blocks and the strip tile the surface exactly,
    // but a different logical size must not leave a stale pixel behind.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            store(x, y, table[0]);
        }
    }

    const int blockWidth = width / kPaletteColumns;
    const int blockHeight = height / kPaletteRows;
    for (int row = 0; row < kPaletteRows; ++row) {
        for (int column = 0; column < kPaletteColumns; ++column) {
            const std::uint8_t wire = table[row * kPaletteColumns + column];
            for (int pixelY = 0; pixelY < blockHeight; ++pixelY) {
                for (int pixelX = 0; pixelX < blockWidth; ++pixelX) {
                    store(column * blockWidth + pixelX, row * blockHeight + pixelY, wire);
                }
            }
        }
    }

    // The strip is whatever the grid left over, so the two never overlap.
    const int stripY = kPaletteRows * blockHeight;
    const int stripHeight = height - stripY;
    const int stepWidth = width / kGreySteps;
    for (int step = 0; step < kGreySteps; ++step) {
        const std::uint8_t grey = static_cast<std::uint8_t>(step * 255 / (kGreySteps - 1));
        const std::uint8_t wire = palette::rgb888To332(grey, grey, grey);
        for (int pixelY = 0; pixelY < stripHeight; ++pixelY) {
            for (int pixelX = 0; pixelX < stepWidth; ++pixelX) {
                store(step * stepWidth + pixelX, stripY + pixelY, wire);
            }
        }
    }
}

/**
 * @brief Paint the palette test into whichever framebuffer the driver offers.
 *
 * The panel path writes the table byte straight into the 8bpp surface -- the
 * same store DOOM's rasteriser performs -- so the panel shows exactly what
 * Phase 2 will draw. The desktop mirror expands that byte the way TFT_eSPI
 * does, so both screens agree instead of showing the expanded source colour.
 */
void paintPaletteTestFrame(Renderer& renderer) {
    DrawSurface& surface = renderer.getDrawSurface();
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();
    if (width <= 0 || height <= 0) {
        return;
    }

    if (std::uint8_t* framebuffer = surface.getSpriteBuffer()) {
        paintPaletteTest(width, height, [framebuffer, width](int x, int y, std::uint8_t wire) {
            framebuffer[y * width + x] = wire;
        });
        return;
    }

    if (std::uint16_t* framebuffer = surface.getPixelBuffer()) {
        paintPaletteTest(width, height, [framebuffer, width](int x, int y, std::uint8_t wire) {
            framebuffer[y * width + x] = palette::expandRgb332(wire);
        });
    }
}

#endif  // PIXELROOT32_DOOM_PALETTE_TEST

// --- Container probe (D2.3) -------------------------------------------------

#if defined(PIXELROOT32_DOOM_WAD_PROBE)

/**
 * @brief The lumps the probe resolves, in the order it reports them.
 *
 * The six are one of each kind the port will need: the palette and the colour
 * map (Phase 2's first read), ENDOOM (the fixed 4000 B screen), E1M1 (a map
 * marker with no bytes of its own), DEMO1 (the largest demo, a real DEFLATE
 * payload) and TEXTURE1 (a composite), so a directory that accepted the image
 * but shifted its names is caught here.
 *
 * NOSUCHXX is the control: it is not in DOOM1.WAD, so the line it produces must
 * be `index=-1`. A lookup that resolves everything -- a comparison that always
 * matches, an index read from the wrong field -- fails on that line alone.
 */
const char* const kProbeLumpNames[] = {
    "PLAYPAL", "COLORMAP", "ENDOOM", "E1M1", "DEMO1", "TEXTURE1", "NOSUCHXX",
};

/**
 * @brief Reports the storage chain, once, at boot: mapping, lumps, names, sizes.
 *
 * One `WAD,...` line says whether the `wad` partition mapped and the container
 * inside it was accepted, then one `LUMP,...` line per name says what the
 * directory answers. No payload is decoded: the largest lump of DOOM1.WAD is
 * 68,168 B and the board's task stack is a fraction of that, so a decode here
 * would be a stack overflow waiting for a build flag. The size each line prints
 * is the number a later layer must allocate for -- which is the fact this probe
 * exists to establish.
 *
 * The mapping is released before returning. Nothing below needs it, and a probe
 * that left 2 MB of flash mapped would be a state its caller never asked for.
 */
void reportContainerProbe() {
    whd_state_t state;
    const int openCode = doom_whd_source_open(&state);

    // Longest line this function writes is a resolved lump with a 10-digit size,
    // about 52 bytes in the worst case; the buffer leaves room for two more
    // decimal fields before snprintf could ever truncate.
    char line[80];

    if (openCode != DOOM_WHD_SOURCE_OK) {
        std::snprintf(line, sizeof line, "WAD,status=fail,code=%d\n", openCode);
        writeConsoleLine(line);
        return;
    }

    std::snprintf(line, sizeof line, "WAD,status=ok,lumps=%u,container=%u\n",
                  static_cast<unsigned>(doom_whd_source_lump_count(&state)),
                  static_cast<unsigned>(doom_whd_source_mapped_size()));
    writeConsoleLine(line);

    for (std::size_t index = 0; index < sizeof kProbeLumpNames / sizeof kProbeLumpNames[0];
         ++index) {
        const char* const name = kProbeLumpNames[index];
        const int lump = doom_whd_source_lump_index(&state, name);

        if (lump < 0) {
            std::snprintf(line, sizeof line, "LUMP,name=%s,index=-1\n", name);
        } else {
            std::snprintf(line, sizeof line, "LUMP,name=%s,index=%u,size=%u\n", name,
                          static_cast<unsigned>(lump),
                          static_cast<unsigned>(doom_whd_source_lump_size(&state, lump)));
        }
        writeConsoleLine(line);
    }

    doom_whd_source_close();
}

#endif  // PIXELROOT32_DOOM_WAD_PROBE

// --- Zone probe -------------------------------------------------------------

#if defined(PIXELROOT32_DOOM_ZONE_PROBE)

/**
 * @brief The probe's arena: 64 KB of static DRAM, a smoke test only.
 *
 * The real level working set is not allocated here. This array exists so the
 * boot log can state three facts about the allocator on the board itself: the
 * arena's accounting overhead, the largest single free block, and that the
 * container's 68,168 B HELP1 lump is refused cleanly by a 64 KB zone -- the
 * refusal the port will handle outside the zone.
 */
alignas(ZONE_ALIGNMENT) std::uint8_t kZoneArena[65536];

/**
 * @brief Creates one zone over the static arena and prints one `ZONE,...` line.
 *
 * No block survives this function and nothing is freed: the state is local and
 * the arena is left as the allocator wrote it. The line is a boot-time smoke
 * record, not a frame path, and the whole function is compiled out of every
 * environment that does not define PIXELROOT32_DOOM_ZONE_PROBE.
 */
void reportZoneProbe() {
    zone_state_t zone;

    if (z_init(&zone, kZoneArena, sizeof kZoneArena) != ZONE_ERR_OK) {
        writeConsoleLine("ZONE,arena=0,overhead=0,largest_free=0,alloc_68168=init_failed\n");
        return;
    }

    const zone_stats_t stats = z_stats(&zone);
    zone_error_t error = ZONE_ERR_OK;
    const void* help1 = z_alloc(&zone, 68168u, PU_LEVEL, &error);

    // The three decimal fields reach at most ten digits each; 128 bytes leaves
    // room for the labels and the two possible outcome words.
    char line[128];
    std::snprintf(line, sizeof line,
                  "ZONE,arena=%u,overhead=%u,largest_free=%u,alloc_68168=%s\n",
                  static_cast<unsigned>(stats.arenaSize),
                  static_cast<unsigned>(stats.overhead),
                  static_cast<unsigned>(stats.largestFree),
                  help1 != nullptr ? "ok" : "fail");
    writeConsoleLine(line);
}

#endif  // PIXELROOT32_DOOM_ZONE_PROBE

}  // namespace

void DoomScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first. This scene adds no entities.
    Scene::init();

    // Nothing is held when the shell comes up; the first change edge is what
    // prints the INPUT line.
    buttons_ = 0;

    // Builds the index -> RGB332 table once. It is the port's whole palette
    // story: Phase 2 replaces the synthetic source with the WAD's PLAYPAL lump
    // and nothing else in the frame path changes.
    palette::init();

#if !defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The C <-> C++ link probe: one C translation unit calls a C++ bridge that
    // calls this port's palette and the engine's console, and the value it
    // sends across comes back verified. It prints exactly one
    // `LINK,status=...,checksum=0x...` line, through the same console as the
    // lines above and below, so a maintainer reading the boot log sees whether
    // the DOOM core's build model links before the core exists.
    //
    // Runs after palette::init() because the probe reads the palette table, and
    // it is compiled out of the frame-log build for the reason stated there:
    // that environment's stream is pure `FRAME,...` CSV with one writer.
    doom_link_probe_run();
#endif  // !PIXELROOT32_DOOM_FRAME_LOG

#if defined(PIXELROOT32_DOOM_WAD_PROBE)
    // D2.3's storage probe, compiled only into env:esp32dev_ILI9341_wad: opens
    // the `wad` partition through the container adapter and prints one
    // `WAD,status=...` line plus one `LUMP,...` line per resolved name. It runs
    // after palette::init() and inside init()'s once-per-scene contract, so the
    // boot log carries exactly one record of the chain and no frame repeats it.
    //
    // No frame-log guard here: the two flags never meet. env:esp32dev_ILI9341_wad
    // exists to read these lines, and the frame-log environment exists to keep
    // its stream pure CSV.
    reportContainerProbe();
#endif  // PIXELROOT32_DOOM_WAD_PROBE

#if defined(PIXELROOT32_DOOM_ZONE_PROBE)
    // The zone probe: one `ZONE,arena=...,overhead=...,largest_free=...,
    // alloc_68168=...` line over a static 64 KB arena, stating that the
    // allocator accounts for the whole arena and refuses the real container's
    // largest lump. It allocates no level memory; the working set arrives when
    // the level loader does.
    reportZoneProbe();
#endif  // PIXELROOT32_DOOM_ZONE_PROBE

#if defined(PIXELROOT32_DOOM_FLASH_PROFILE)
    // D2.5's flash-read profile, compiled only into
    // env:esp32dev_ILI9341_flashprof: opens the `wad` partition, decodes E1M1's
    // whole working set out of it once (cold) and immediately again (warm), and
    // prints the two summaries, the phase lines, the warm/cold ratio and the
    // per-frame budget. It takes ~68 KB of static decode buffer, so it is not a
    // line a shipped build pays for -- the buffer and the probe are both inside
    // that flag.
    //
    // No frame-log guard here: the two flags never meet. The profile exists to be
    // read at boot, and the frame-log environment exists to keep its stream pure
    // CSV.
    doomc::flashProfileRunProbe();
#endif  // PIXELROOT32_DOOM_FLASH_PROFILE

#if defined(PIXELROOT32_DOOM_FRAME_PROBE)
    // D3.6's frame driver, compiled only into env:esp32dev_ILI9341_frame: opens
    // the `wad` partition, loads E1M1's renderer working set, renders the 20
    // sampled viewpoints and prints the per-phase table, the three-view
    // breakdown, the summary with the Phase 3 gate verdict, the RAM line and the
    // hash of every frame.
    //
    // The surface is the engine's own 8bpp logical framebuffer, passed in rather
    // than fetched by the driver so the driver carries no engine dependency. It
    // receives DOOM palette indices, not RGB332: that is visually wrong and
    // timing-correct, and the probe's `FRAMEPROF,mem` line says so. Nothing is
    // presented from here; the probe measures renderer CPU and nothing else.
    //
    // No frame-log guard here: the two flags never meet. The probe exists to be
    // read at boot, and the frame-log environment exists to keep its stream pure
    // CSV.
    {
        pixelroot32::graphics::DrawSurface& surface =
            engine.getRenderer().getDrawSurface();
        doomc::frameProbeRun(surface.getSpriteBuffer(), engine.getRenderer().getLogicalWidth(),
                             engine.getRenderer().getLogicalHeight());
    }
#endif  // PIXELROOT32_DOOM_FRAME_PROBE

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // D4.0's game frame: one open level for the life of the scene. It reads the
    // `wad` partition, so this is the only environment that can render the game;
    // when it fails the reason is already on the serial line and draw() keeps
    // painting the shell. The measurement counters start here so the first
    // `SHOW,...` line belongs to the first frame this scene drew.
    if (!sceneRenderer_.open()) {
        writeConsoleLine("SHOW,setup,status=fail,reason=scene\n");
    } else {
        // The 8bpp surface holds DOOM palette indices now, so hand the engine
        // the PLAYPAL -> RGB565 table once: the driver expands each index as it
        // presents, one lookup per pixel and no per-frame pass. The call is a
        // no-op on a surface without an 8-bit buffer (DrawSurface's default), so
        // the desktop mirror keeps its own conversion below.
        engine.getRenderer().getDrawSurface().setPalette8(palette::playpal565Table());
    }

    // D4.0c's census: what the image owns in dram0_0_seg, what the heap holds,
    // the exact size of every allocation the frame path made, and the one
    // question the overlap design depends on -- whether a second 320x200 8-bit
    // frame fits in ONE contiguous block. On this board it does not: the heap
    // has a single ~110 KB block at boot and the sprite already owns it, so
    // `twin64=no_fit` is the honest answer and the serialized present stays.
    reportMemoryCensus(engine.getRenderer());

    showFrame_ = 0;
    showPendingReport_ = false;
    showRenderMicros_ = 0;
    showConvertMicros_ = 0;
    showHashMicros_ = 0;
    showUpdateMicros_ = 0;
    showUpdateEntry_ = 0;
    showHash_[0] = '\0';
    showLastReportUs_ = sampleMicros();
#endif  // PIXELROOT32_DOOM_SHOW_E1M1

    // Anchors the tic clock at the scene's first microsecond sample, which is
    // tic 0. The sentinel makes the first update() report it, so the serial
    // stream starts with a TIC line instead of hiding tic 0 behind the filter.
    ticClock_.reset(sampleMicros());
    reportedTic_ = kNoTic;

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The recorder writes the same console the shipped shell did. Under the
    // flag it is the only writer: the SHELL, TIC and INPUT lines are compiled
    // out so the stream is pure CSV.
    frameLog_.setSink(&writeConsoleLine);
#else
    reportFrameContract(engine.getRenderer());
#endif
}

void DoomScene::pollTicClock() {
    // One sample, one tic: the sample that produced the tic is the one the line
    // prints, so no second reading can already belong to the next tic.
    const std::uint32_t sample = sampleMicros();
    const std::uint32_t tic = ticClock_.update(sample);
    if (tic == reportedTic_) {
        return;
    }
    reportedTic_ = tic;

    // One line per tic, never per frame. A tic that elapses while update() is
    // not running is never a line either -- there was no sample to report it.
    //
    // Under PIXELROOT32_DOOM_FRAME_LOG the tic is still derived and cached in
    // ticClock_, because the FRAME record reports it; only the serial line is
    // gone, so the CSV stream has exactly one writer.
#if !defined(PIXELROOT32_DOOM_FRAME_LOG)
    char line[48];
    std::snprintf(line, sizeof line, "TIC,n=%u,us=%lu\n", static_cast<unsigned>(tic),
                  static_cast<unsigned long>(sample));
    writeConsoleLine(line);
#endif
}

void DoomScene::update(unsigned long deltaTime) {
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The frame measurement needs the scene's own update time: the engine calls
    // update() before draw(), and its present() runs after draw(), so this sample
    // is what separates the two from the loop period.
    const std::uint32_t updateEntry = sampleMicros();
#endif
#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    // Close the previous frame at the first instant of this update. The line is
    // written inside endFrame(); the start sample below is taken after that
    // write, so the recorder's own serial cost is bracketed out of both the
    // previous frame's period and this frame's render interval.
    frameLog_.endFrame(sampleMicros());
    const std::uint32_t frameStartMicros = sampleMicros();
#endif

    // Entities first, as everywhere else in the repository; this scene has
    // none, so the call is the base contract rather than work.
    Scene::update(deltaTime);

    // The 35 Hz tic advances on every frame, input or not, so it is sampled
    // before the input early-return below.
    pollTicClock();

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The frame's start timestamp is from the top of update(); the tic is the
    // one pollTicClock() just derived from this frame's sample. beginFrame() is
    // called here only because the tic is not known earlier; it stores the
    // timestamp without taking another reading.
    frameLog_.beginFrame(frameStartMicros, ticClock_.tic());
#endif

    const pixelroot32::input::InputManager& input = engine.getInputManager();

    // One sample per frame, in index order: `isButtonDown` is the held state,
    // which is what the on-screen slots and the bit field both mean. Taking the
    // sample once here also keeps a press from being reported and drawn from
    // two different readings.
    std::uint8_t buttons = 0;
    for (int index = 0; index < kButtonCount; ++index) {
        if (input.isButtonDown(static_cast<std::uint8_t>(index))) {
            buttons = static_cast<std::uint8_t>(buttons | (1u << index));
        }
    }

    if (buttons == buttons_) {
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
        showUpdateEntry_ = updateEntry;
        showUpdateMicros_ = sampleMicros() - updateEntry;
#endif
        return;
    }
    buttons_ = buttons;

    // The bit field is the same number as the index order: bit 0 up ... bit 5
    // B. One line per change, nothing per frame.
    //
    // The frame-log build suppresses this line: its stream is pure CSV, and a
    // per-frame recorder cannot share the port with an event line.
#if !defined(PIXELROOT32_DOOM_FRAME_LOG)
    char line[32];
    std::snprintf(line, sizeof line, "INPUT,buttons=0x%02X\n", static_cast<unsigned>(buttons));
    writeConsoleLine(line);
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    showUpdateEntry_ = updateEntry;
    showUpdateMicros_ = sampleMicros() - updateEntry;
#endif
}

void DoomScene::draw(Renderer& renderer) {
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // D4.0's game frame: E1M1's spawn viewpoint, presented by the engine with
    // the PLAYPAL expansion the surface was given in init(). It takes precedence
    // over the palette test and the shell, but the flags never meet: this
    // environment defines only this one.
    drawSceneFrame(renderer);
#elif defined(PIXELROOT32_DOOM_PALETTE_TEST)
    // A separate build (env:esp32dev_ILI9341_palette) shows the palette
    // contract instead of the shell. The shipped environments never define the
    // flag, so their frame is the one above, untouched.
    paintPaletteTestFrame(renderer);
#else
    paintShellFrame(renderer, buttons_);
#endif

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The end of the scene's own work, not the end of the engine's present
    // path: Engine::draw() still runs the transition effect (off here) and the
    // debug overlay (off here) after this returns, then run() calls present().
    // Both land in present_us, as the FrameLog header states.
    frameLog_.endDraw(sampleMicros(), sampleFreeHeap());
#endif
}

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

void DoomScene::drawSceneFrame(Renderer& renderer) {
    DrawSurface& surface = renderer.getDrawSurface();
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();

    // This call happens after the engine's update() and before its present().
    // `arrival` closes the measurement of the frame that just finished; the
    // draw start below is taken *after* the optional serial write, so a reported
    // frame's own line is not charged to any frame period.
    const std::uint32_t arrival = sampleMicros();

    if (showFrame_ > 0u && showPendingReport_) {
        // present_us: the gap between the previous draw's last instruction and
        // the update() that followed it. The engine presents outside draw()
        // (`drawer->present()` after the scene returns, then
        // flushPendingTransfers() under PIXELROOT32_DRAIN_PRESENT), so this gap
        // is the tightest measurement the scene can take; it is the whole wire
        // transfer plus the Arduino yield() between loop iterations. `other_us`
        // is what is left of the period: the engine's input sample, event pump
        // and loop bookkeeping, which the scene does not own.
        const std::uint32_t presentUs = showUpdateEntry_ - showDrawEnd_;
        // The reporting hash is a diagnostic, not part of the game's frame: it is
        // one software SHA-256 over 64 KB that only a reported frame pays.
        // Subtract it from the period so `frame_us` and `fps_x10` describe every
        // frame, and keep its cost visible as `hash_us`.
        const std::uint32_t rawInterval = arrival - showDrawStart_;
        const std::uint32_t frameUs =
            rawInterval > showHashMicros_ ? rawInterval - showHashMicros_ : rawInterval;
        const std::uint32_t accounted =
            showRenderMicros_ + showConvertMicros_ + presentUs + showUpdateMicros_;
        const std::uint32_t otherUs = frameUs > accounted ? frameUs - accounted : 0u;
        const std::uint32_t fpsX10 = frameUs != 0u ? 10000000u / frameUs : 0u;

        char line[256];
        std::snprintf(line, sizeof line,
                      "SHOW,frame=%u,render_us=%u,convert_us=%u,present_us=%u,"
                      "frame_us=%u,fps_x10=%u,update_us=%u,hash_us=%u,other_us=%u,"
                      "hash=%s\n",
                      static_cast<unsigned>(showFrame_ - 1u),
                      static_cast<unsigned>(showRenderMicros_),
                      static_cast<unsigned>(showConvertMicros_),
                      static_cast<unsigned>(presentUs),
                      static_cast<unsigned>(frameUs),
                      static_cast<unsigned>(fpsX10),
                      static_cast<unsigned>(showUpdateMicros_),
                      static_cast<unsigned>(showHashMicros_),
                      static_cast<unsigned>(otherUs), showHash_);
        writeConsoleLine(line);
    }

    // Decide whether this frame takes a hash and gets a line at the next draw.
    // Bounded two ways: the first frames every frame, then one per second, and
    // nothing at all after kShowReportFrameLimit -- the scene keeps drawing, so
    // the panel does not freeze when reporting stops.
    showPendingReport_ = false;
    if (showFrame_ < kShowReportFrameLimit
        && (showFrame_ < kShowReportFirstFrames
            || (arrival - showLastReportUs_) >= kShowReportIntervalMicros)) {
        showPendingReport_ = true;
        showLastReportUs_ = arrival;
    }

    const std::uint32_t drawStart = sampleMicros();
    bool rendered = false;

    // The panel path: one byte per pixel, and the byte is a DOOM palette index.
    // init() installed the PLAYPAL -> RGB565 table on this surface through
    // setPalette8(), so the driver expands each index while it presents. There
    // is no conversion pass here: the buffer the hash covers is the exact buffer
    // the panel draws from, and the frame costs one lookup per pixel inside the
    // present it already paid for.
    if (std::uint8_t* pixels = surface.getSpriteBuffer()) {
        if (sceneRenderer_.isOpen() && width == kLogicalWidth && height == kLogicalHeight) {
            doomc::SceneFrameMeasurements measured;
            if (sceneRenderer_.render(pixels, width, height, showPendingReport_, &measured)) {
                showRenderMicros_ = measured.renderMicros;
                showHashMicros_ = measured.hashMicros;
                if (measured.hashed) {
                    for (int index = 0; index < 65; ++index) {
                        showHash_[index] = measured.hash[index];
                    }
                } else {
                    showHash_[0] = '\0';
                }

                // Nothing to convert: the surface's expansion table owns it.
                showConvertMicros_ = 0;
                rendered = true;
            }
        }
    }
#if defined(PLATFORM_NATIVE)
    // The desktop mirror: SDL2 has no 8bpp surface, so the indices land in a
    // scratch plane and are expanded to RGB565 exactly the way the panel's LUT
    // expands them, which keeps the window showing the panel's colours instead of
    // a second, slightly different conversion. Guarded by PLATFORM_NATIVE because
    // on the board the scratch would cost 64,000 B of DRAM for a branch the TFT
    // driver never takes.
    else if (std::uint16_t* pixels = surface.getPixelBuffer()) {
        static std::uint8_t indices[static_cast<std::size_t>(kLogicalWidth)
                                    * static_cast<std::size_t>(kLogicalHeight)];
        if (sceneRenderer_.isOpen() && width == kLogicalWidth && height == kLogicalHeight) {
            doomc::SceneFrameMeasurements measured;
            if (sceneRenderer_.render(indices, width, height, showPendingReport_, &measured)) {
                showRenderMicros_ = measured.renderMicros;
                showHashMicros_ = measured.hashMicros;
                if (measured.hashed) {
                    for (int index = 0; index < 65; ++index) {
                        showHash_[index] = measured.hash[index];
                    }
                } else {
                    showHash_[0] = '\0';
                }

                // This surface stores colours, not indices, so the expansion is
                // necessarily a pass here. It reads the same PLAYPAL -> RGB565
                // table the panel's driver uses, which keeps the window and the
                // panel on one palette instead of two conversions.
                const std::uint16_t* table = palette::playpal565Table();
                const std::size_t count =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                const std::uint32_t convertStart = sampleMicros();
                for (std::size_t index = 0; index < count; ++index) {
                    pixels[index] = table[indices[index]];
                }
                showConvertMicros_ = sampleMicros() - convertStart;
                rendered = true;
            }
        }
    }
#endif

    if (!rendered) {
        // A closed session (missing partition, rejected container) or a surface
        // the renderer cannot use: keep the shell instead of freezing the last
        // frame, and drop the pending line so it cannot describe a shell frame.
        paintShellFrame(renderer, buttons_);
        showPendingReport_ = false;
    }

    showDrawStart_ = drawStart;
    showDrawEnd_ = sampleMicros();
    ++showFrame_;
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1

}  // namespace doom
