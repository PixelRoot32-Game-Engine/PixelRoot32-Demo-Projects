#include "DoomScene.h"

#include "doom_link_probe.h"
#include "doomc/DoomConsole.h"
#include "doomc/AudioTelemetry.h"

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

#if defined(DOOM_FRAME_DUMP)
// The dump's button combination; `_hud` pulls InputMap in through its HUD path
// and `_show` does not, so the include is the dump's own.
#include "doomc/InputMap.h"
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
#include "doomc/BootScreen.h"
#endif

#include "palette/DoomPalette.h"
#if defined(DOOM_AUDIO)
#include "audio/AudioEngine.h"  /* AudioEngine::setMasterVolume */
#include "doomc/DoomAudio.h"
#include "doomc/SfxBank.h"
#include "s_sound.h"
#include "sounds.h"
#endif

#if defined(DOOM_MENU) && !defined(PIXELROOT32_DOOM_SHOW_E1M1)
#error "DOOM_MENU belongs to the game build: define PIXELROOT32_DOOM_SHOW_E1M1 too"
#endif

#if defined(DOOM_PALFLASH) && !defined(DOOM_MENU)
#error "DOOM_PALFLASH needs DOOM_MENU: it composes with the menu's gamma"
#endif

#if DOOM_PERSISTENCE && !defined(DOOM_MENU)
#error "DOOM_PERSISTENCE is wired to the menu's LOAD/SAVE screens: define DOOM_MENU too"
#endif

#if defined(DOOM_SCREENSHOT) && !defined(PLATFORM_NATIVE)
#error "DOOM_SCREENSHOT writes the finished frame to a host file: it is native-only"
#endif

#if defined(DOOM_MENU) || defined(DOOM_HUD)
#include "doomc/InputMap.h"  /* the button bits the menu and the automap read */
#endif
#if defined(DOOM_MENU)
#include "doomc/AudioLevels.h"  /* the volume/mute -> audio level mapping */
#include "doomc/SceneRenderer.h"
#include "doomc/SettingsStore.h"
#if DOOM_PERSISTENCE
#include "doomc/DoomPersistence.h"
#elif defined(ARDUINO)
#include "platforms/PreferencesStore.h"
#endif
#endif
#if defined(DOOM_HUD)
#include "am_map.h"  /* AM_PAN_* and AM_ZOOM_*: the automap's controls */
#endif

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(DOOM_SCREENSHOT)
#include <cstdlib>  /* std::exit: the capture's own reason to stop */
#include <filesystem>
#include <system_error>
#endif

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

/// The engine's `InputConfig` index of Start (GPIO 34 on the CYD): after the six
/// game buttons, so the shell, the INPUT line and the on-screen slots never see it.
/// Only the menu reads it.
constexpr std::uint8_t kStartIndex = 6;

/// The engine's `InputConfig` index of Select (GPIO 35 on the CYD): the automap's
/// toggle. Like Start it is not a game button: it is sampled on its own,
/// never packed into the world's bits, so `kButtonCount` stays 6.
constexpr std::uint8_t kSelectIndex = 7;

/// The scripted-input build's Select bit. The engine never sets bit 7 of the
/// button byte (the six game bits are 0..5), and the menu's Start is 0x40, so a
/// diagnostic script reaches `runMap()` through the same byte the real sample
/// fills without the menu reading it as Start.
constexpr std::uint8_t kMapScriptSelect = 0x80u;

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
 * palette the engine has active.
 */
enum class Ink : std::uint8_t {
    Background = 0,    ///< Whole surface, so the shell owns every pixel.
    Frame,             ///< The 1 px outline on the outermost logical pixels.
    Slot,              ///< Idle button slot, an outline only.
    Pressed,           ///< Held button, filled solid.
    MarkerTopLeft,     ///< Red: the first pixel the rasteriser writes.
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
 * `bytes` also says which path is live: 64,000 B for the 8bpp panel buffer,
 * 128,000 B for the 16bpp desktop one, 0 for a driver with neither.
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

// --- Memory census -----------------------------------------------------------

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
 *    `physicalWidth * 2 B * linesPerBlock`, and the three LUTs are fixed. The
 *    line-buffer term is a TFT_eSPI board fact, so on PLATFORM_NATIVE it -- and
 *    `lines_per_block` -- is reported as 0 rather than a made-up block size.
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
#if defined(PLATFORM_NATIVE)
    // The host presents through SDL2 and owns no TFT_eSPI DMA block, so the DMA
    // term is not a host number to invent: `linesPerBlock` and the two line
    // buffers are reported as 0, the same sentinel the pool figures below use.
    const std::uint32_t dmaLineBytes = 0u;
    const int linesPerBlock = 0;
#else
    // Two DMA line buffers of `physicalWidth * 2 B * linesPerBlock`: this is
    // the board's TFT_eSPI layout, read from the macro the board build defines.
    const std::uint32_t dmaLineBytes =
        static_cast<std::uint32_t>(width) * 2u * PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK;
    const int linesPerBlock = PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK;
#endif
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
                  linesPerBlock);
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
 * of editing it.
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

    // The panel path, and the one DOOM's renderer uses: the 8bpp logical
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
 * same store DOOM's rasteriser performs -- so the panel shows exactly what the
 * renderer draws. The desktop mirror expands that byte the way TFT_eSPI
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

// --- Container probe ---------------------------------------------------------

#if defined(PIXELROOT32_DOOM_WAD_PROBE)

/**
 * @brief The lumps the probe resolves, in the order it reports them.
 *
 * The six are one of each kind the port needs: the palette and the colour
 * map (the renderer's first read), ENDOOM (the fixed 4000 B screen), E1M1 (a map
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
 * refusal the port handles outside the zone.
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


#if defined(DOOM_MENU)

/// The store the settings live in: the board's NVS, or RAM on the desktop mirror.
/// The game code only ever sees the doomc::SettingsStore seam.
#if DOOM_PERSISTENCE
/// The engine's persistence layer owns both media -- the settings in
/// `nvs` (the same namespace and key the `Preferences` store used, so the record
/// survives byte for byte) and the six slots in the `saves` partition.
doomc::DoomPersistence gPersistence;

doomc::SettingsStore& settingsStore() { return gPersistence.settingsStore(); }
#else
#if defined(ARDUINO)
platforms::PreferencesStore gSettingsStore;
#else
doomc::MemorySettingsStore gSettingsStore;
#endif

doomc::SettingsStore& settingsStore() { return gSettingsStore; }
#endif

static_assert(M_BTN_UP == doomc::kButtonUp && M_BTN_DOWN == doomc::kButtonDown
                  && M_BTN_LEFT == doomc::kButtonLeft && M_BTN_RIGHT == doomc::kButtonRight
                  && M_BTN_A == doomc::kButtonA && M_BTN_B == doomc::kButtonB,
              "the menu reads InputMap.h's button bits");

#if defined(DOOM_MENU_SCRIPT)
/*
 * DOOM_MENU_SCRIPT (env esp32dev_ILI9341_hud_menutest): the board has no buttons
 * this build can press, so the diagnostic plays a fixed input script into the same
 * path a real press takes -- `m_menu_buttons()`, ORed onto the sampled buttons --
 * and the serial log shows what the menu did. Each step holds `buttons` for
 * `frames` frames; a key press is one frame down and two up so every press is its
 * own rising edge.
 *
 * What the script does, in order, and why each part is there:
 *   1. wait, then Start: the Start edge opens the main menu (menu-open fps window)
 *   2. DOWN, A: OPTIONS. Skill is driven to 0 then to 2, volume to 0 then to 5,
 *      gamma to 0 then to 3 and mute to OFF then ON (the clamps are exercised, and
 *      the end values are the same whatever the stored ones were, so a second boot
 *      can be compared).
 *   3. B, B: back to main, then close: the settings are written to NVS
 *   4. NEW GAME at skills 0, 1, 2, 3, 4 and 3 again: each is a Start press, A, a cursor
 *      move from the saved skill and A, then a wait so the reload and the first
 *      frames of the new world are in the log. The last one leaves skill 3. The main
 *      screen remembers its cursor (upstream's lastOn), which step 2 left on OPTIONS,
 *      so the first one moves it back to NEW GAME with one UP.
 *   5. SAVE GAME on row 0, then LOAD GAME on the same row, so the
 *      boot log carries one `SHOW,save` and one `SHOW,load` through the real menu.
 * End state: skill 3, volume 5, gamma 3, mute ON, menu closed, slot 0 holding a
 * fresh New Game's world.
 */
struct ScriptStep {
    std::uint8_t buttons;
    std::uint8_t frames;
};

#define TAP(button) {(button), 1}, {0, 2}
#define OPEN_MENU {M_BTN_START, 1}, {0, 3}  /* the Start press that opens the menu */
#define WAIT(count) {0, (count)}
#define NEWGAME_AT(downs) OPEN_MENU, TAP(M_BTN_A), downs, TAP(M_BTN_A), WAIT(60)
#define NEWGAME_FIRST(downs) OPEN_MENU, TAP(M_BTN_UP), TAP(M_BTN_A), downs, TAP(M_BTN_A), WAIT(60)
#define DOWN3 TAP(M_BTN_DOWN), TAP(M_BTN_DOWN), TAP(M_BTN_DOWN)
#define LEFT5 TAP(M_BTN_LEFT), TAP(M_BTN_LEFT), TAP(M_BTN_LEFT), TAP(M_BTN_LEFT), TAP(M_BTN_LEFT)
#define RIGHT3 TAP(M_BTN_RIGHT), TAP(M_BTN_RIGHT), TAP(M_BTN_RIGHT)
#define LEFT16 LEFT5, LEFT5, LEFT5, TAP(M_BTN_LEFT)

const ScriptStep kMenuScript[] = {
    WAIT(40),
    OPEN_MENU,
    WAIT(60),                                  // main menu open
    TAP(M_BTN_DOWN), TAP(M_BTN_A),             // OPTIONS
    WAIT(60),                                  // setup-lite open
    LEFT5, RIGHT3 /* skill 0, then 3 */,
    TAP(M_BTN_LEFT),                           // skill 2
    TAP(M_BTN_DOWN), LEFT16, RIGHT3, TAP(M_BTN_RIGHT), TAP(M_BTN_RIGHT),  // volume 0, then 5
    TAP(M_BTN_DOWN), LEFT5, RIGHT3,            // gamma 0, then 3
    TAP(M_BTN_DOWN), TAP(M_BTN_LEFT), TAP(M_BTN_RIGHT),  // mute OFF (clamp), then ON
    WAIT(40),
    TAP(M_BTN_B), WAIT(10), TAP(M_BTN_B),      // main, then closed: the write happens
    WAIT(50),
    // New game. The cursor starts on the saved skill (2 after the options above).
    NEWGAME_FIRST(DOWN3),                      // 2 -> 0 (wraps: down, down, down)
    NEWGAME_AT(TAP(M_BTN_DOWN)),               // 0 -> 1
    NEWGAME_AT(TAP(M_BTN_DOWN)),               // 1 -> 2
    NEWGAME_AT(TAP(M_BTN_DOWN)),               // 2 -> 3
    NEWGAME_AT(TAP(M_BTN_DOWN)),               // 3 -> 4
    NEWGAME_AT(TAP(M_BTN_UP)),                 // 4 -> 3
    WAIT(120),
#if DOOM_PERSISTENCE
    /*
     * The two slot screens driven through the real menu path.
     *
     * The New Games leave MAIN's cursor on NEW GAME, so three DOWNs reach SAVE
     * GAME (NEW GAME, OPTIONS, LOAD GAME, SAVE GAME). A opens the six-row screen,
     * A again saves over row 0 -- EMPTY on a fresh partition, and this port has no
     * confirm dialog. The save closes the menu; OPEN_MENU, UP, A, A then loads the
     * same row back. The `SHOW,save` and `SHOW,load` lines are the record; a
     * partition that is not there reports `status=unavailable` and the script
     * still finishes, which is the honest shape on a table without `saves`.
     */
    OPEN_MENU, DOWN3, TAP(M_BTN_A), WAIT(40), TAP(M_BTN_A), WAIT(60),
    OPEN_MENU, TAP(M_BTN_UP), TAP(M_BTN_A), WAIT(40), TAP(M_BTN_A), WAIT(120),
    /*
     * A second used row, so the slot screen proves "more than one slot"
     * rather than only the row the power-cycle proof uses. The load above left
     * MAIN's cursor on LOAD GAME; DOWN reaches SAVE GAME, A opens it on row 0,
     * DOWN moves to row 1, A writes it. A save changes no world state, so the
     * forced `SHOW,frame` the load produced is still the captured "before".
     */
    OPEN_MENU, TAP(M_BTN_DOWN), TAP(M_BTN_A), WAIT(40),
    TAP(M_BTN_DOWN), TAP(M_BTN_A), WAIT(80),
#endif
};

#if DOOM_PERSISTENCE
/*
 * The second boot's script: slot 0 already holds the record the first boot
 * wrote, so this boot only opens the menu and loads it. MAIN starts on NEW GAME,
 * so two DOWNs reach LOAD GAME and two A presses open row 0 and load it. This is
 * the same image as `kMenuScript`, chosen at boot from slot 0's label, so no
 * reflash sits between the save and the load: the reset is the only event.
 */
const ScriptStep kMenuLoadScript[] = {
    WAIT(40),
    OPEN_MENU,
    TAP(M_BTN_DOWN), TAP(M_BTN_DOWN), TAP(M_BTN_A), WAIT(40),
    TAP(M_BTN_A), WAIT(120),
};
#endif

/// The buttons the script holds on @p frame, or 0 once it has ended.
/// @p loadOnly selects the second-boot script (slot 0 already used).
std::uint8_t menuScriptButtons(std::uint32_t frame, bool loadOnly) {
#if DOOM_PERSISTENCE
    const ScriptStep* script = loadOnly ? kMenuLoadScript : kMenuScript;
    const std::size_t count = loadOnly ? (sizeof kMenuLoadScript / sizeof kMenuLoadScript[0])
                                       : (sizeof kMenuScript / sizeof kMenuScript[0]);
#else
    (void)loadOnly;
    const ScriptStep* script = kMenuScript;
    const std::size_t count = sizeof kMenuScript / sizeof kMenuScript[0];
#endif
    std::uint32_t at = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const ScriptStep& step = script[index];
        if (frame < at + step.frames) {
            return step.buttons;
        }
        at += step.frames;
    }
    return 0;
}
#endif  // DOOM_MENU_SCRIPT

#endif  // DOOM_MENU

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD) && defined(DOOM_MAP_SCRIPT)
/*
 * DOOM_MAP_SCRIPT (env esp32dev_ILI9341_hud_mapcheck): the board's buttons cannot
 * be pressed from the build host, so this diagnostic plays a fixed input script
 * into the same path a real press takes -- the sampled buttons plus the Select
 * bit, both read by `runMap()` -- and the serial log shows what the automap did.
 * Each step holds `buttons` for `frames` frames; a tap is one frame down and two
 * up, so every press is its own rising edge (Select's toggle is edge-triggered).
 *
 * What it does, in order, and why each part is there:
 *   1. Select: the map opens at the entry scale, following the player, nothing
 *      mapped yet -- the state tools/am_check.py pins as `spawn`.
 *   2. A held: zoom in (one 1.02x step per tic). B held: back out.
 *   3. RIGHT then UP held: pan, which cancels the follow; the window stops where
 *      the level's edge clamps it.
 *   4. Select: the map closes and the world frame comes back.
 *   5. UP held: the player walks forward, so the wall pass marks more lines.
 *   6. Select: the map opens again (the follow stays off, as upstream's static
 *      does), with the lines walked in step 5 drawn.
 *   7. B held: zoom out to the whole level (the min_scale_mtof clamp).
 *   8. Select: closed, and the log goes back to SHOW,frame lines.
 *
 * The grid is not in the script: upstream toggles it with 'g', the board has no
 * button for it, and the build assigns it none.
 */
struct MapScriptStep {
    std::uint8_t buttons;
    std::uint8_t frames;
};

#define MTAP(button) {(button), 1}, {0, 2}
#define MHOLD(button, count) {(button), (count)}, {0, 4}
#define MWAIT(count) {0, (count)}

const MapScriptStep kMapScript[] = {
    MWAIT(40),
    MTAP(kMapScriptSelect),                 // the map opens, following, full scale
    MWAIT(30),
    MHOLD(doomc::kButtonA, 24),             // zoom in
    MWAIT(6),
    MHOLD(doomc::kButtonB, 24),             // back out
    MWAIT(6),
    MHOLD(doomc::kButtonRight, 30),         // pan right: the follow is cancelled
    MWAIT(6),
    MHOLD(doomc::kButtonUp, 20),            // pan up
    MWAIT(12),
    MTAP(kMapScriptSelect),                 // the map closes
    MWAIT(20),
    MHOLD(doomc::kButtonUp, 30),            // the player walks
    MWAIT(20),
    MTAP(kMapScriptSelect),                 // the map opens again, panned
    MWAIT(30),
    MHOLD(doomc::kButtonB, 40),             // zoom out to the whole level
    MWAIT(16),
    MTAP(kMapScriptSelect),                 // closed again
    MWAIT(20),
};

/// The buttons the script holds on @p frame, or 0 once it has ended.
std::uint8_t mapScriptButtons(std::uint32_t frame) {
    std::uint32_t at = 0;
    for (const MapScriptStep& step : kMapScript) {
        if (frame < at + step.frames) {
            return step.buttons;
        }
        at += step.frames;
    }
    return 0;
}
#endif  // SHOW && DOOM_HUD && DOOM_MAP_SCRIPT

}  // namespace

void DoomScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first. This scene adds no entities.
    Scene::init();

    // Nothing is held when the shell comes up; the first change edge is what
    // prints the INPUT line.
    buttons_ = 0;

#if defined(DOOM_FULL_PROFILE)
    // The twin index planes are scene storage, so the boot log states the cost
    // the profile pays once instead of per frame. `SHOW,census,pool`'s
    // `twin64` field is a different fact -- the board's single-block heap probe
    // -- and keeps its own meaning and values.
    {
        char twinLine[64];
        std::snprintf(twinLine, sizeof twinLine, "SHOW,twin,planes=2,bytes=%u\n",
                      static_cast<unsigned>(2u * kIndexPlaneBytes));
        writeConsoleLine(twinLine);
    }
#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(PLATFORM_NATIVE)
    // The melt borrows the twin planes rather than owning screen copies, so
    // its state is scene storage too: one idle state, reused by every level
    // transition, holding only the column fronts and the random position.
    wipe_Init(&wipe_);
#endif
#endif

#if defined(DOOM_MENU)
    // A closed menu with the defaults, so a session that never opens (no
    // partition, rejected container) still has a well-formed one. The real
    // settings replace it below, once the level is open.
    m_menu_init(&menu_, nullptr);
#if DOOM_PERSISTENCE
    // The two media before anything that reads the menu's settings,
    // so the load below never runs against an uninitialised store. A failure is
    // printed once here and the game continues: settings default, slots answer
    // unavailable.
    (void)gPersistence.init();
#if defined(DOOM_MENU_SCRIPT)
    // One image, two boots. A used slot 0 means the first boot already
    // wrote the record this boot must load; an empty one means this is the first
    // boot and the full script (settings, New Games, SAVE, LOAD) runs. Choosing
    // from the medium is what keeps a reflash out of the save-to-load gap.
    {
        char labels[M_SLOT_COUNT][M_SLOT_LABEL_SIZE];
        gPersistence.readSlotLabels(labels);
        menuScriptLoadOnly_ = std::strcmp(labels[0], M_SLOT_EMPTY) != 0;
    }
#endif
#endif
#endif

    // Builds the index -> RGB332 table once; the shell, the palette test and
    // the link probe below all read it.
    palette::init();

#if defined(DOOM_AUDIO)
    // The S_* layer first, so a level load's S_Start() has a seam to talk to.
    // `S_Init` takes upstream's volumes (0..127) and the layer runs at full
    // scale; the menu's 0..15 volume is applied as the engine's master gain.
    // `mode=legacy` is a literal on purpose: this port never sends
    // SET_SOUND_PROFILE.
    doomc::doomAudioInit(engine.getAudioEngine(), engine.getMusicPlayer());
    S_Init(S_MAX_VOLUME, S_MAX_VOLUME);
    {
        char audioLine[72];
        std::snprintf(audioLine, sizeof audioLine,
                      "SHOW,audio,init,status=ok,bank=%u,mode=legacy\n",
                      static_cast<unsigned>(doomc::sfxBankSize()));
        writeConsoleLine(audioLine);
    }
    // One line before any level is loaded: the bank and the transport, so a
    // quiet boot is visibly "no track" rather than a missing log.
    doomc::doomMusicReport("init");
    audioLastReportUs_ = sampleMicros();
#if defined(DOOM_AUDIO_SCRIPT)
    audioScriptLastUs_ = sampleMicros();
#endif
#endif

#if !defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The C <-> C++ link probe: one C translation unit calls a C++ bridge that
    // calls this port's palette and the engine's console, and the value it
    // sends across comes back verified. It prints exactly one
    // `LINK,status=...,checksum=0x...` line, through the same console as the
    // lines above and below, so the boot log shows whether the DOOM core's
    // build model links.
    //
    // Runs after palette::init() because the probe reads the palette table, and
    // it is compiled out of the frame-log build for the reason stated there:
    // that environment's stream is pure `FRAME,...` CSV with one writer.
    doom_link_probe_run();
#endif  // !PIXELROOT32_DOOM_FRAME_LOG

#if defined(PIXELROOT32_DOOM_WAD_PROBE)
    // The storage probe, compiled only into env:esp32dev_ILI9341_wad: opens
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
    // The flash-read profile, compiled only into
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
    // The frame driver, compiled only into env:esp32dev_ILI9341_frame: opens
    // the `wad` partition, loads E1M1's renderer working set, renders the 20
    // sampled viewpoints and prints the per-phase table, the three-view
    // breakdown, the summary with the gate verdict, the RAM line and the
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
    // The game frame: one open level for the life of the scene. It reads the
    // `wad` partition, so this is the only environment that can render the game;
    // when it fails the reason is already on the serial line and draw() keeps
    // painting the shell. The measurement counters start here so the first
    // `SHOW,...` line belongs to the first frame this scene drew.
    bootFailed_ = false;
    bootFailure_ = doomc::LevelFailure{};

#if !defined(PLATFORM_NATIVE)
    // The loading screen, painted and presented before anything that can
    // fail runs. This is the one moment the surface is still in its default
    // RGB332 regime, so the painter's bytes are exactly what the panel shows.
    // `present()` + `flushPendingTransfers()` is the pair Engine::run() uses: the
    // driver may leave the last block in flight, and this closes it before the
    // level loader claims the bus, the heap and the CPU. The desktop mirror skips
    // this whole block -- it must not swap a window during init.
    {
        pixelroot32::graphics::DrawSurface& surface = engine.getRenderer().getDrawSurface();
        std::uint8_t* pixels = surface.getSpriteBuffer();
        const int width = engine.getRenderer().getLogicalWidth();
        const int height = engine.getRenderer().getLogicalHeight();
        if (pixels != nullptr && width > 0 && height > 0) {
            const doomc::BootScreen loading = doomc::bootScreenLoading("E1M1");
            doomc::bootScreenPaint(pixels, width, height, loading);
            surface.present();
            surface.flushPendingTransfers();
            writeConsoleLine("SHOW,boot,screen=loading,level=E1M1\n");
        }
    }
#endif

    const std::uint32_t bootStart = sampleMicros();
    if (!sceneRenderer_.open()) {
        // The session classified the failure as it printed its line; the same
        // record becomes the panel screen, so a board on a shelf says why it is
        // not playing without a serial cable attached.
        bootFailed_ = true;
        bootFailure_ = sceneRenderer_.failure();
        char bootLine[160];
        std::snprintf(bootLine, sizeof bootLine,
                      "SHOW,boot,screen=error,reason=%s,code=%d,detail=%s\n",
                      doomc::levelFailureName(bootFailure_.reason), bootFailure_.code,
                      bootFailure_.detail);
        writeConsoleLine(bootLine);
        writeConsoleLine("SHOW,setup,status=fail,reason=scene\n");
    } else {
        const std::uint32_t openMs = (sampleMicros() - bootStart) / 1000u;
        char bootLine[96];
        std::snprintf(bootLine, sizeof bootLine,
                      "SHOW,boot,screen=game,level=E1M1,open_ms=%u\n",
                      static_cast<unsigned>(openMs));
        writeConsoleLine(bootLine);
        // The 8bpp surface holds DOOM palette indices now, so hand the engine
        // the PLAYPAL -> RGB565 table once: the driver expands each index as it
        // presents, one lookup per pixel and no per-frame pass. The call is a
        // no-op on a surface without an 8-bit buffer (DrawSurface's default), so
        // the desktop mirror keeps its own conversion below.
#if defined(DOOM_MENU)
        // The stored settings, or the defaults when the store has no valid
        // record. The gamma goes into the PLAYPAL tables before the panel is
        // handed them, so the first frame already shows it. Boot continues at
        // medium skill in E1M1 as always: the stored skill is the cursor's start
        // in New Game, not the first level's skill.
        {
            m_settings_t stored;
            const bool fromStore = doomc::loadSettings(settingsStore(), &stored);
            m_menu_init(&menu_, &stored);
            settingsDirty_ = false;
            menuScriptFrame_ = 0;
            gammaApplied_ = menu_.settings.gamma;
            palette::applyGamma(sceneRenderer_.playpal(), gammaApplied_);
            sceneRenderer_.setMenu(&menu_);
#if defined(DOOM_AUDIO)
            // The stored volume and mute reach the APU before the first
            // frame. The level is DOOM's 0..127; see doomc/AudioLevels.h for
            // why it is applied once, at the engine's master gain.
            applyAudioLevels();
#endif
            char line[128];
            std::snprintf(line, sizeof line,
                          "SHOW,menu,settings=%s,skill=%d,volume=%d,gamma=%d,mute=%d,level=%d\n",
                          fromStore ? "nvs" : "default", static_cast<int>(menu_.settings.skill),
                          static_cast<int>(menu_.settings.volume),
                          static_cast<int>(menu_.settings.gamma),
                          static_cast<int>(menu_.settings.muted),
                          doomc::audioLevel(menu_.settings.volume, menu_.settings.muted != 0));
            writeConsoleLine(line);
        }
#endif
        engine.getRenderer().getDrawSurface().setPalette8(palette::playpal565Table());

        // Byte-identity proof: render the host harness's frame 00 through
        // the same `frameLevelRenderView()` the game frame uses, hash it, and
        // print the number. `50fb3d9c...` is the host's frame-00 hash, so a boot
        // log that prints anything else says the shared view path diverged.
        //
        // The panel surface is the buffer this writes into; the first game frame
        // overwrites it a moment later, so nothing is presented from here.
        std::uint8_t* pixels = engine.getRenderer().getDrawSurface().getSpriteBuffer();
        if (pixels != nullptr) {
            (void)sceneRenderer_.verifySpawnFrame(pixels, engine.getRenderer().getLogicalWidth(),
                                                 engine.getRenderer().getLogicalHeight());
        }
#if defined(DOOM_SAVE_TEST) && DOOM_PERSISTENCE
        // The codec self-test, after the world exists (the valid record is
        // the live E1M1) and before any menu or input can change it. The two
        // slots it touches are 4 and 5, never the power-cycle record in slot 0.
        gPersistence.runSaveTest();
#endif
    }

    // The census: what the image owns in dram0_0_seg, what the heap holds,
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
    // The world starts at tic 0 and has run nothing: the level was just loaded
    // and this init() has not returned to update() yet. `worldTic_` is the
    // high-water mark the tic clock drives forward from here.
    worldTic_ = 0;
    showPlayer_ = doomc::ScenePlayerState{};
#endif  // PIXELROOT32_DOOM_SHOW_E1M1

    // Anchors the tic clock at the scene's first microsecond sample, which is
    // tic 0. The sentinel makes the first update() report it, so the serial
    // stream starts with a TIC line instead of hiding tic 0 behind the filter.
    ticClock_.reset(sampleMicros());
    reportedTic_ = kNoTic;

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    // The recorder writes the same console the shell uses. Under the
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
    //
    // The sample is taken before the world tick below, because the world needs
    // this frame's buttons and the tick must not be skipped by the change
    // filter: a held button is exactly the case where the bit field does not
    // change and the simulation must still run.
    std::uint8_t buttons = 0;
    for (int index = 0; index < kButtonCount; ++index) {
        if (input.isButtonDown(static_cast<std::uint8_t>(index))) {
            buttons = static_cast<std::uint8_t>(buttons | (1u << index));
        }
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD) && defined(DOOM_MAP_SCRIPT)
    // The map diagnostic build: a fixed script plays Select and the map's keys
    // through the same byte the real sample fills, because no button on this board
    // can be pressed from the build host. Its Select bit is kMapScriptSelect, which
    // `runMap()` strips before anything reaches the world.
    buttons = static_cast<std::uint8_t>(buttons | mapScriptButtons(mapScriptFrame_++));
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_LEVEL_TEST)
#if defined(DOOM_LEVEL_WALK)
    // The viewing walk. Each level gets a dwell, then the fixture stands on
    // its own exit and B (the world's USE) is held for three frames; E1M3 takes
    // its secret line 51 so E1M9 is on the route, and E1M8 takes the victory
    // branch. Every tic goes through the real input path, and the driver's own
    // SHOW,level lines are the log of what the panel showed.
    if (!walkDone_) {
        const std::uint8_t level = doomc::frameLevelNumber();
#if defined(PLATFORM_NATIVE)
        // The desktop loop draws several hundred frames a second, so the
        // board's 45-frame dwell is a tenth of a second -- less than one 35 Hz
        // tic -- and its three-frame press falls between two tics and never
        // reaches a ticcmd. The same walk is therefore paced in the clock the
        // world itself runs on: 105 tics is the board's ~3 s, and the press is
        // latched for four tics, the window `levelTestPressTic_` above already
        // proves the world samples.
        constexpr std::uint32_t kNativeDwellTics = 105u;
        constexpr std::uint32_t kNativeUseTics = 4u;
        constexpr std::uint32_t kNativeRetryTics = 21u;
        const std::uint32_t tic = ticClock_.tic();

        if (!walkStopRead_) {
            // DOOM_WALK_STOP ends the run on one chosen level, after its dwell,
            // so a capture's tail is that level's audio alone. Unset (or not
            // 1..9) keeps the board's tour of all nine. The choice is printed
            // once so the run's log names it instead of the environment.
            walkStopRead_ = true;
            const char* stop = std::getenv("DOOM_WALK_STOP");
            if (stop != nullptr) {
                const long value = std::strtol(stop, nullptr, 10);
                if (value >= 1 && value <= 9) {
                    walkStopLevel_ = static_cast<std::uint8_t>(value);
                }
            }
            char line[48];
            std::snprintf(line, sizeof line, "SHOW,walk,stop=%u\n",
                          static_cast<unsigned>(walkStopLevel_));
            writeConsoleLine(line);
        }

        if (level != walkLastLevel_) {
            walkLastLevel_ = level;
            walkArmTic_ = tic + kNativeDwellTics;
            walkPressEndTic_ = kNoTic;
            char line[64];
            std::snprintf(line, sizeof line, "SHOW,walk,level=%u,tic=%u\n",
                          static_cast<unsigned>(level), static_cast<unsigned>(tic));
            writeConsoleLine(line);
        }

        if (walkPressEndTic_ != kNoTic && tic <= walkPressEndTic_) {
            buttons = static_cast<std::uint8_t>(buttons | doomc::kButtonB);
        } else if (tic >= walkArmTic_) {
            if (walkStopLevel_ != 0 && level == walkStopLevel_) {
                // The requested level has had its dwell: end the run here, with
                // the success code `main()` returns, so the host's capture is
                // this level's theme and nothing after it.
                char line[64];
                std::snprintf(line, sizeof line, "SHOW,walk,exit=level=%u,tic=%u\n",
                              static_cast<unsigned>(level), static_cast<unsigned>(tic));
                writeConsoleLine(line);
                std::exit(0);
            } else if (level == 8u) {
                walkDone_ = true;
#if DOOM_PERSISTENCE
                (void)doomc::frameLevelVictoryScript();
#endif
            } else {
                const int special = (level == 3u) ? 51 : 11;
                if (!doomc::frameLevelExitScript(special)) {
                    walkDone_ = true;
                } else {
                    walkPressEndTic_ = tic + kNativeUseTics;
                    // A press that does not move the level retries after the
                    // same cooldown the board's 40 frames give it.
                    walkArmTic_ = tic + kNativeUseTics + kNativeRetryTics;
                }
            }
        }
#else
        if (level != walkLastLevel_) {
            walkLastLevel_ = level;
            walkTimer_ = 45;  // ~3 s at the shipped frame rate, fade included
            walkUse_ = 0;
        }
        if (walkUse_ > 0u) {
            buttons = static_cast<std::uint8_t>(buttons | doomc::kButtonB);
            --walkUse_;
        } else if (walkTimer_ > 0u) {
            --walkTimer_;
        } else if (level == 8u) {
            walkDone_ = true;
#if DOOM_PERSISTENCE
            (void)doomc::frameLevelVictoryScript();
#endif
        } else {
            const int special = (level == 3u) ? 51 : 11;
            if (!doomc::frameLevelExitScript(special)) {
                walkDone_ = true;
            } else {
                walkUse_ = 3u;
            }
            walkTimer_ = 40;
        }
#endif
    }
#else
    // The level-exit board proof (env esp32dev_ILI9341_hud_leveltest): at frame 20 the
    // avatar is put in front of E1M1's own exit switch, and from frame 24 B is
    // held for three frames. B is bit 5 -- the world's USE -- so the press goes
    // through the real tic path; the frames before it are what release the use
    // latch G_PlayerReborn() left set.
    if (showFrame_ == 20u) {
        (void)doomc::frameLevelExitScript(11);
#if defined(PLATFORM_NATIVE)
        // The desktop loop draws several times per 35 Hz tic, so the board's
        // three-frame press can fall entirely between two tics and never reach
        // a ticcmd. Latch the same USE press for the next four world tics, a
        // frame-rate-independent window the world is guaranteed to sample.
        levelTestPressTic_ = ticClock_.tic();
#endif
    }
#if defined(PLATFORM_NATIVE)
    if (showFrame_ >= 24u
        && (levelTestPressTic_ == 0xFFFFFFFFu
            || ticClock_.tic() <= levelTestPressTic_ + 4u)) {
        buttons = static_cast<std::uint8_t>(buttons | doomc::kButtonB);
    }
#else
    if (showFrame_ >= 24u && showFrame_ <= 26u) {
        buttons = static_cast<std::uint8_t>(buttons | doomc::kButtonB);
    }
#endif
#if DOOM_PERSISTENCE
    // The save half of the same proof, on the level the transition reached: write
    // the live world -- slot 3, so the power-cycle record in slot 0 is
    // untouched -- go back to E1M1 with a New Game, then load the record. The load
    // must reopen the level the record names (E1M2), not the level the world is
    // on.
    if (showFrame_ == 60u) {
        (void)gPersistence.saveSlot(3);
    }
    if (showFrame_ == 80u) {
        (void)sceneRenderer_.newGame(2);
    }
    if (showFrame_ == 100u) {
        (void)gPersistence.loadSlot(3);
    }
    if (showFrame_ == 120u) {
        // The victory branch: on E1M8 with the exit requested, the next tic takes
        // the shipped path (the run reloads on its first map, the menu opens).
        (void)doomc::frameLevelVictoryScript();
    }
#endif  // DOOM_PERSISTENCE
#endif  // DOOM_LEVEL_WALK
#endif  // PIXELROOT32_DOOM_SHOW_E1M1 && DOOM_LEVEL_TEST

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The run ended (E1M8's ending has no finale screen). The driver has
    // already put the world back on the level the run started on; this frame asks
    // the menu to open over it, and `runMenu()` reports the open with its own
    // line, so there is no second menu-state printer here.
    {
        const bool victory = doomc::frameLevelTakeVictory();
#if defined(DOOM_MENU)
        if (victory) {
            victoryMenuPending_ = true;
        }
#else
        (void)victory;
#endif
    }
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The world ticks on every frame, before the early return below. That
    // return exists for the shell's repaint and the INPUT line; it must not stop
    // the simulation, or a held button would freeze the game.
#if defined(DOOM_MENU)
    // The menu sees the raw buttons and answers with what the world may
    // see. Open or latched, that is nothing, and runWorldTics() does not tick.
    std::uint8_t menuButtons = buttons;
    if (input.isButtonDown(kStartIndex)) {
        menuButtons = static_cast<std::uint8_t>(menuButtons | M_BTN_START);
    }
    std::uint8_t worldButtons = runMenu(menuButtons);
#if defined(DOOM_HUD)
    // The automap owns Select and, while it is up, the d-pad, A and B. The
    // menu comes first: while it is open it owns the frame's input and the map is
    // only a picture under it (upstream draws the menu on top of the automap).
    worldButtons = runMap(worldButtons, m_menu_active(&menu_) != 0);
#endif
    runWorldTics(worldButtons);
#else
    runWorldTics(buttons);
#endif
#endif

#if defined(DOOM_AUDIO_SCRIPT)
    // The diagnostic build's listen test, after the world has ticked so the
    // level exists and the boot log is already on the wire. It uses its own
    // 700 ms clock, not the frame rate.
    runAudioScript();
#endif

#if defined(DOOM_AUDIO_SOAK)
    // The soak: the gate that paces the four-cue load is the tic, not
    // the frame, so a slow frame can skip a round but never add one.
    runAudioSoak();
#endif

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

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

void DoomScene::runWorldTics(std::uint8_t buttons) {
    if (!sceneRenderer_.isOpen()) {
        return;
    }

    // The clock's tic is the target; worldTic_ is the tic the world has run to.
    // The difference is exactly how many tics this frame owes, and it is the
    // clock's own derivation -- never a per-frame increment -- so the world
    // cannot drift away from 35 Hz the way an accumulated delta would.
    const std::uint32_t tic = ticClock_.tic();
    if (tic == worldTic_) {
        return;
    }
    const std::uint32_t owed = tic - worldTic_;
    worldTic_ = tic;
#if defined(DOOM_MENU)
    // The menu pauses the world: the tic clock keeps running and `worldTic_`
    // follows it, so no backlog is replayed when the menu closes. The skull still
    // animates, in tics.
    if (m_menu_active(&menu_)) {
        m_menu_tick(&menu_, owed);
        return;
    }
#endif

    // Every owed tic runs, in order. A long stall (a serial write, a debugger
    // pause) replays its backlog instead of dropping it: DOOM's own loop runs
    // every available tic, and a dropped tic would be a simulation that silently
    // ran slower than 35 Hz. There is no cap: the burst is bounded by the
    // stall, not by a constant.
    (void)sceneRenderer_.runTics(owed, buttons);
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1

#if defined(DOOM_AUDIO)

void DoomScene::reportAudio() {
    const std::uint32_t now = sampleMicros();
    // One report per second. It is its own constant rather than the
    // frame line's so the two can move independently.
    if (now - audioLastReportUs_ < 1000000u) {
        return;
    }
    audioLastReportUs_ = now;

    const doomc::DoomAudioStats& stats = doomc::doomAudioStats();
    doomc::DoomAudioTelemetry telemetry = {0u, 0u};
    const bool haveTelemetry = doomc::doomAudioReadTelemetry(&telemetry);

    // The line is built by the seam (src/doomc/AudioTelemetry.cpp), outside the
    // engine gate, so env:sound_test can prove the `-` case and the real
    // numbers without an engine, an APU or a board.
    char line[doomc::kAudioReportLineBytes];
    doomc::formatAudioReportLine(line, sizeof line, stats, doomc::doomAudioVoiceCount(),
                                 doomc::doomAudioActiveVoices(), haveTelemetry, telemetry);
    writeConsoleLine(line);

    // The transport beside the counters: S_Start asks for E1M1 at level load,
    // so this line is the board proof that the converted track reached an APU
    // sequencer and stayed there.
    doomc::doomMusicReport("periodic");
}

#if defined(DOOM_AUDIO_SCRIPT)

void DoomScene::runAudioScript() {
    // The twelve ids are sfxenum_t values chosen to cover weapons, doors,
    // monsters and pickups; the comments beside them name each one. NULL is
    // upstream's "no origin": the
    // script is a cue-by-cue listen test, not a positional one.
    static const int kScript[] = {
        sfx_pistol,  /* pistol */
        sfx_shotgn,  /* shotgun */
        sfx_doropn,  /* door open */
        sfx_dorcls,  /* door close */
        sfx_swtchn,  /* switch */
        sfx_stnmov,  /* lift */
        sfx_bgsit1,  /* imp sight */
        sfx_plpain,  /* player pain */
        sfx_itemup,  /* item pickup */
        sfx_telept,  /* teleport */
        sfx_barexp,  /* barrel explosion */
        sfx_pldeth,  /* player death */
    };
    constexpr std::uint32_t kStepCount = sizeof(kScript) / sizeof(kScript[0]);
    constexpr std::uint32_t kStepIntervalUs = 700000u;

    if (audioScriptStep_ >= kStepCount) {
        return;
    }
    const std::uint32_t now = sampleMicros();
    if (now - audioScriptLastUs_ < kStepIntervalUs) {
        return;
    }
    audioScriptLastUs_ = now;
    S_StartSound(nullptr, kScript[audioScriptStep_]);
    ++audioScriptStep_;
}

#endif  // DOOM_AUDIO_SCRIPT

#if defined(DOOM_AUDIO_SOAK)

namespace {

/// The four one-shots the soak holds: the longest cues the bank has (1.69 s
/// barexp, 1.40 s telept, 1.40 s rlaunc, 1.00 s pldeth), one per voice. Two are
/// `Reserved`, so a monster's cue cannot steal them and shrink the test.
constexpr int kSoakCues[] = {sfx_barexp, sfx_telept, sfx_rlaunc, sfx_pldeth};
constexpr int kSoakVoices = static_cast<int>(sizeof kSoakCues / sizeof kSoakCues[0]);

/// Four tics at 35 Hz: each voice restarts every 114 ms while its cue lasts
/// 1.0-1.7 s, so all four keep synthesising all the time. The gate, not the
/// frame rate, is what paces it -- but a round runs at most once per frame, so
/// at the shipped ~15 fps the gate lands on 4-5 tics and the measured cadence is
/// ~30 submits/s rather than the 35 a 35 Hz caller would ask for. The voices are
/// re-armed every ~130 ms either way, so the render cost is the same number.
constexpr std::uint32_t kSoakTicInterval = 4u;

/// One line a minute, for ten minutes.
constexpr std::uint32_t kSoakReportSeconds = 60u;
constexpr std::uint32_t kSoakMinutes = 10u;
constexpr std::uint32_t kSoakReportMicros = kSoakReportSeconds * 1000000u;

/// The reference block: 128 samples at 22050 Hz (5.80 ms), the DMA
/// granularity the APU's own measurements use. The engine's render block here
/// is 512 samples (23.22 ms); the README states both, so the percentage cannot
/// be read as a quarter of the wrong budget without the sentence beside it.
constexpr std::uint32_t kSoakBlockUs = 128u * 1000000u / 22050u;

}  // namespace

void DoomScene::runAudioSoak() {
    const std::uint32_t now = sampleMicros();
    if (!soakStarted_) {
        soakStarted_ = true;
        soakStartUs_ = now;
        soakWindowStartUs_ = now;
        soakLastTic_ = ticClock_.tic();
    }
    ++soakWindowFrames_;

    // The SFX load. Four distinct sector tokens, because `one sound per origin`
    // means one token per voice: the same token re-fired would stop its own cue
    // first and the four voices would not stay busy. A sector token is also
    // never distance-culled (s_sound.c's documented cost), so the cues play at
    // full volume wherever the player stands. A round never fires more often
    // than every four tics; a slow frame can only skip one.
    const std::uint32_t tic = ticClock_.tic();
    if (tic - soakLastTic_ >= kSoakTicInterval) {
        soakLastTic_ = tic;
        for (int index = 0; index < kSoakVoices; ++index) {
            S_StartSoundAtVolume(S_SectorOrigin(static_cast<uint16_t>(index)),
                                 kSoakCues[index], S_MAX_VOLUME);
        }
    }

    // One line per whole minute reached. A stalled frame can reach two minutes
    // at once and print two lines back to back; `t=` is the truth, and the run
    // is not re-timed by it.
    const std::uint32_t elapsed = now - soakStartUs_;
    const std::uint32_t dueMinutes = elapsed / kSoakReportMicros;
    while (soakMinutes_ < kSoakMinutes && soakMinutes_ < dueMinutes) {
        ++soakMinutes_;

        // The window's own frame rate: frames over the minute it took, in
        // tenths, the same unit `SHOW,frame` reports.
        const std::uint32_t windowUs = now - soakWindowStartUs_;
        const std::uint32_t fpsX10 =
            windowUs != 0u
                ? static_cast<std::uint32_t>(
                      static_cast<std::uint64_t>(soakWindowFrames_) * 10000000ull / windowUs)
                : 0u;
        soakWindowFrames_ = 0u;
        soakWindowStartUs_ = now;

        const doomc::DoomAudioStats& stats = doomc::doomAudioStats();
        doomc::DoomAudioTelemetry telemetry = {0u, 0u};
        const bool haveTelemetry = doomc::doomAudioReadTelemetry(&telemetry);
        char line[224];

        if (haveTelemetry) {
            std::snprintf(line, sizeof line,
                          "SHOW,audio,soak,t=%u,plays=%u,steals=%u,refused=%u,"
                          "active=%d,underruns=%u,worst_us=%u,fps_x10=%u\n",
                          static_cast<unsigned>(soakMinutes_ * kSoakReportSeconds),
                          static_cast<unsigned>(stats.plays),
                          static_cast<unsigned>(stats.steals),
                          static_cast<unsigned>(stats.refused),
                          doomc::doomAudioActiveVoices(),
                          static_cast<unsigned>(telemetry.underruns),
                          static_cast<unsigned>(telemetry.worstUs),
                          static_cast<unsigned>(fpsX10));
        } else {
            // No source is a state, not a zero: the soak environment installs
            // one, so this arm exists for a copy of the build that does not.
            std::snprintf(line, sizeof line,
                          "SHOW,audio,soak,t=%u,plays=%u,steals=%u,refused=%u,"
                          "active=%d,underruns=-,worst_us=-,fps_x10=%u\n",
                          static_cast<unsigned>(soakMinutes_ * kSoakReportSeconds),
                          static_cast<unsigned>(stats.plays),
                          static_cast<unsigned>(stats.steals),
                          static_cast<unsigned>(stats.refused),
                          doomc::doomAudioActiveVoices(),
                          static_cast<unsigned>(fpsX10));
        }
        writeConsoleLine(line);

        if (soakMinutes_ < kSoakMinutes) {
            continue;
        }

        // The closing record. `resets=0` is the soak's own restart count: the
        // window runs once and nothing restarts it, while a board reset ends
        // the capture at a boot line instead of incrementing a field here.
        if (haveTelemetry) {
            std::snprintf(
                line, sizeof line,
                "SHOW,audio,soak,end,minutes=%u,underruns=%u,worst_us=%u,plays=%u,"
                "steals=%u,refused=%u,resets=0\n",
                static_cast<unsigned>(kSoakMinutes),
                static_cast<unsigned>(telemetry.underruns),
                static_cast<unsigned>(telemetry.worstUs),
                static_cast<unsigned>(stats.plays),
                static_cast<unsigned>(stats.steals),
                static_cast<unsigned>(stats.refused));
        } else {
            std::snprintf(
                line, sizeof line,
                "SHOW,audio,soak,end,minutes=%u,underruns=-,worst_us=-,plays=%u,"
                "steals=%u,refused=%u,resets=0\n",
                static_cast<unsigned>(kSoakMinutes),
                static_cast<unsigned>(stats.plays),
                static_cast<unsigned>(stats.steals),
                static_cast<unsigned>(stats.refused));
        }
        writeConsoleLine(line);

        // The block's share, as a tenth of a percent: `getWorstRenderUs()` says
        // nothing on its own, and the deadline is what it is measured against.
        if (haveTelemetry) {
            const std::uint32_t percentX10 = telemetry.worstUs * 1000u / kSoakBlockUs;
            std::snprintf(line, sizeof line,
                          "SHOW,audio,soak,budget,block_us=%u,worst_us=%u,pct_x10=%u\n",
                          static_cast<unsigned>(kSoakBlockUs),
                          static_cast<unsigned>(telemetry.worstUs),
                          static_cast<unsigned>(percentX10));
            writeConsoleLine(line);
        }
    }
}

#endif  // DOOM_AUDIO_SOAK

#endif  // DOOM_AUDIO

#if defined(DOOM_MENU)

#if defined(DOOM_AUDIO)
void DoomScene::applyAudioLevels() {
    // One comparison per frame, and only a moved setting costs an engine call.
    // `audioLevelApplied_` holds the level the master gain was set to, not the
    // raw volume, because two different settings can produce the same audible
    // level (volume 0 and mute) and re-submitting that command buys nothing.
    const int level = doomc::audioLevel(menu_.settings.volume, menu_.settings.muted != 0);
    if (level == audioLevelApplied_) {
        return;
    }
    audioLevelApplied_ = level;
    // The engine's master gain is applied to the accumulated mix -- the four
    // SFX voices and the four music tracks together -- which is the one place a
    // single "volume" can scale both without scaling either twice. Naming the
    // engine rather than `MusicPlayer::setMasterVolume()` is deliberate: the
    // player's setter only forwards here (DoomAudio.h's header says the same).
    engine.getAudioEngine().setMasterVolume(doomc::audioMasterVolume(
        menu_.settings.volume, menu_.settings.muted != 0));
}

#endif  // DOOM_AUDIO

void DoomScene::applyMenuGamma() {
    gammaApplied_ = menu_.settings.gamma;
    if (fadeActive_) {
        // A fade owns the tables until it finishes; its next step composes
        // the new gamma. Rebuilding here would flash the frame to full brightness.
        return;
    }
#if defined(DOOM_PALFLASH)
    // The gamma changes under whatever flash is on the screen.
    if (!applyPalette(paletteApplied_, gammaApplied_)) {
        applyPalette(0, gammaApplied_);
        paletteApplied_ = 0;
    }
#else
    palette::applyGamma(sceneRenderer_.playpal(), gammaApplied_);
    engine.getRenderer().getDrawSurface().setPalette8(palette::playpal565Table());
#endif
}

void DoomScene::startFade() {
    fadeActive_ = true;
    fadeStep_ = 0;
    fadeMicros_ = 0;
}

bool DoomScene::applyFade(std::uint16_t scale) {
    const std::uint8_t* playpal = sceneRenderer_.playpal();
    if (playpal == nullptr) {
        return false;
    }
    palette::applyGammaScaled(playpal, gammaApplied_, scale);
    engine.getRenderer().getDrawSurface().setPalette8(palette::playpal565Table());
    return true;
}

void DoomScene::updateFade() {
    if (!sceneRenderer_.isOpen()) {
        return;
    }
    // A level load restarts `leveltime` at 0 -- the next map, and G_DoReborn's
    // reload too -- so a tic count that went backwards is the transition. No new
    // plumbing, and it cannot fire on a pause: a paused world does not run tics.
    if (showPlayer_.valid && showPlayer_.tics < fadeLastTics_) {
#if defined(DOOM_FULL_PROFILE) && defined(PLATFORM_NATIVE)
        // The full profile keeps the old level's finished frame and melts it
        // into the new one. An explicit fade (New Game, LOAD) still owns its
        // transition, and a melt already in flight is not restarted.
        if (!fadeActive_ && !meltActive_) {
            startMelt();
        }
#else
        startFade();
#endif
    }
    if (showPlayer_.valid) {
        fadeLastTics_ = showPlayer_.tics;
    }
    if (!fadeActive_) {
        return;
    }

    const std::uint32_t startUs = sampleMicros();
    if (fadeStep_ == 0u) {
        (void)applyFade(0u);  // the reloaded level's first frame is black
        fadeStep_ = 1u;
    } else if (fadeStep_ < kFadeSteps) {
        ++fadeStep_;
        (void)applyFade(static_cast<std::uint16_t>(256u * fadeStep_ / kFadeSteps));
    } else {
        fadeActive_ = false;
#if defined(DOOM_PALFLASH)
        // Hand the table back to the flash path: force it to rebuild page 0 (or
        // the page the player's counters call for) on the next frame.
        paletteApplied_ = 0xFFu;
#else
        (void)applyFade(palette::kFadeFull);
#endif
    }
    fadeMicros_ += sampleMicros() - startUs;

    if (!fadeActive_) {
        char line[96];
        std::snprintf(line, sizeof line, "SHOW,fade,steps=%u,us=%u,frame=%u\n",
                      static_cast<unsigned>(kFadeSteps), static_cast<unsigned>(fadeMicros_),
                      static_cast<unsigned>(showFrame_));
        writeConsoleLine(line);
    }
}

#if defined(DOOM_FULL_PROFILE) && defined(PLATFORM_NATIVE)
void DoomScene::startMelt() {
    // The plane the last completed draw filled still holds the old level's
    // final frame. Recording the other plane here is what freezes the flip in
    // drawSceneFrame(): every melt frame renders the new level into
    // `meltPlane_` and never advances `indexPlaneCurrent_` past it, so the
    // old plane stays readable until every column reaches the bottom.
    meltActive_ = true;
    meltStep_ = 0;
    meltPlane_ = static_cast<std::uint8_t>(indexPlaneCurrent_ ^ 1u);
}
#endif

#if defined(DOOM_PALFLASH)

bool DoomScene::applyPalette(int page, std::uint8_t gamma) {
    std::uint8_t bytes[palette::kRgb888Bytes];
    const std::uint8_t* source = sceneRenderer_.playpal();
    if (page != 0) {
        if (!doomc::frameLevelPlaypalPage(page, bytes)) {
            return false;
        }
        source = bytes;
    }
    if (source == nullptr) {
        return false;
    }
    palette::applyGamma(source, gamma);
    engine.getRenderer().getDrawSurface().setPalette8(palette::playpal565Table());
    return true;
}

void DoomScene::updatePaletteFlash() {
#if defined(DOOM_PALFLASH_SCRIPT)
    // Diagnostic build only: a fixed script plays damage and pickups into the real
    // player state (P_DamageMobj, P_TouchSpecialThing). Never in a shipped image.
    {
        int step = -1;
        if (showFrame_ == 60u) step = 0;
        if (showFrame_ == 140u) step = 1;
        if (showFrame_ == 220u) step = 2;
        if (showFrame_ == 300u) step = 3;
        if (step >= 0) {
            const bool played = doomc::frameLevelPalflashScript(step);
            char event[96];
            std::snprintf(event, sizeof event, "SHOW,palscript,step=%d,frame=%u,played=%d\n", step,
                          static_cast<unsigned>(showFrame_), played ? 1 : 0);
            writeConsoleLine(event);
        }
    }
#endif
    const int page = doomc::frameLevelPaletteIndex();
    if (page == paletteApplied_) {
        return;
    }
#if defined(DOOM_FRAME_DUMP)
    // Diagnostic: every page change, so a capture says which palette the
    // artifact's frames were drawn under (a flash page or page 0).
    {
        char event[80];
        std::snprintf(event, sizeof event, "SHOW,palette,page=%d,frame=%u\n", page,
                      static_cast<unsigned>(showFrame_));
        writeConsoleLine(event);
    }
#endif
#if defined(DOOM_PALFLASH_SCRIPT)
    const std::uint32_t startUs = sampleMicros();
#endif
    const bool applied = applyPalette(page, gammaApplied_);
    if (applied) {
        paletteApplied_ = static_cast<std::uint8_t>(page);
    }
#if defined(DOOM_PALFLASH_SCRIPT)
    const std::uint32_t costUs = sampleMicros() - startUs;
    char line[128];
    std::snprintf(line, sizeof line,
                  "SHOW,palette,frame=%u,tic=%u,page=%d,gamma=%d,us=%u,applied=%d\n",
                  static_cast<unsigned>(showFrame_), static_cast<unsigned>(showPlayer_.tics), page,
                  static_cast<int>(gammaApplied_), static_cast<unsigned>(costUs), applied ? 1 : 0);
    writeConsoleLine(line);
#endif
}

#endif  // DOOM_PALFLASH

std::uint8_t DoomScene::runMenu(std::uint8_t buttons) {
#if defined(DOOM_MENU_SCRIPT)
    buttons = static_cast<std::uint8_t>(
        buttons | menuScriptButtons(menuScriptFrame_++, menuScriptLoadOnly_));
#endif
    // The run ended on the frame before this one. Opening here -- after the
    // previous active state is sampled and before this frame's -- is what makes
    // the open visible below: the same frame prints `SHOW,menu,state=open` and
    // calls S_PauseSound, exactly as a Start press does.
    const bool wasActive = m_menu_active(&menu_) != 0;
    if (victoryMenuPending_) {
        victoryMenuPending_ = false;
        m_menu_open(&menu_);
    }
    const std::uint8_t worldButtons = m_menu_buttons(&menu_, buttons);
    const bool active = m_menu_active(&menu_) != 0;
    char line[160];

#if DOOM_PERSISTENCE
    // The LOAD/SAVE screens draw labels the menu never reads itself, and the
    // labels are read from the medium on entry, not on every frame: six
    // loadRecord() calls are six NVS reads, and a menu is exactly the safe point
    // the engine's guide allows a write (or a batch of reads) at.
    if (menu_.screen != menuScreenLast_
        && (menu_.screen == M_SCREEN_LOAD || menu_.screen == M_SCREEN_SAVE)) {
        refreshSlotLabels();
    }
    menuScreenLast_ = menu_.screen;

    int slot = -1;
    const int slotAction = m_menu_take_slot_action(&menu_, &slot);
    if (slotAction != M_ACTION_NONE) {
        handleSlotAction(slotAction, slot);
    }
#endif

    if (active != wasActive) {
#if defined(DOOM_AUDIO)
        // Upstream's menu pause: P_Pause calls S_PauseSound, P_Unpause calls
        // S_ResumeSound, so the music holds while the menu is open.
        if (active) {
            S_PauseSound();
        } else {
            S_ResumeSound();
        }
#endif
        // One short line per open/close, never per frame. It carries the world's
        // arena so a New Game that overflowed it (a refused allocation) is visible.
        std::uint32_t inUse = 0u, spare = 0u, refusedAllocs = 0u, mobjs = 0u;
        (void)doomc::frameLevelArenaStats(&inUse, &spare, &refusedAllocs, &mobjs);
        std::snprintf(line, sizeof line,
                      "SHOW,menu,state=%s,frame=%u,arena_used=%u,arena_free=%u,arena_refused=%u,"
                      "mobjs=%u,refused=%u,missing=%s\n",
                      active ? "open" : "closed", static_cast<unsigned>(showFrame_),
                      static_cast<unsigned>(inUse), static_cast<unsigned>(spare),
                      static_cast<unsigned>(refusedAllocs), static_cast<unsigned>(mobjs),
                      static_cast<unsigned>(showRefused_), doomc::frameLevelLastMissing());
        writeConsoleLine(line);
    }

    int skill = 0;
    if (m_menu_take_new_game(&menu_, &skill)) {
        const bool started = sceneRenderer_.newGame(skill);
        // The reload is a stall, not a tic: do not replay the tics that elapsed.
        worldTic_ = ticClock_.tic();
        if (started) {
            // A New Game is a level load with no level change to notice.
            startFade();
        }
        std::snprintf(line, sizeof line, "SHOW,newgame,skill=%d,status=%s,frame=%u\n", skill,
                      started ? "ok" : "fail", static_cast<unsigned>(showFrame_));
        writeConsoleLine(line);
    }

    if (m_menu_take_settings_changed(&menu_)) {
        settingsDirty_ = true;
    }
    if (menu_.settings.gamma != gammaApplied_) {
        // Live: the palette follows the thermometer, so the player sees the gamma
        // while choosing it.
        applyMenuGamma();
    }
#if defined(DOOM_AUDIO)
    // The volume and the mute row follow the same rule as the gamma row --
    // live while they move, from the menu's own per-frame turn rather than a
    // second poll in update(). The method's first statement is the comparison.
    applyAudioLevels();
#endif

    // Written once the player leaves the options screen (or the menu), not on every
    // step of a slider: NVS commits are slow and the flash wears.
    if (settingsDirty_ && (!active || menu_.screen != M_SCREEN_OPTIONS)) {
        const bool saved = doomc::saveSettings(settingsStore(), menu_.settings);
        settingsDirty_ = false;
        std::snprintf(line, sizeof line,
                      "SHOW,menu,saved=%s,skill=%d,volume=%d,gamma=%d,mute=%d\n",
                      saved ? "ok" : "fail", static_cast<int>(menu_.settings.skill),
                      static_cast<int>(menu_.settings.volume),
                      static_cast<int>(menu_.settings.gamma),
                      static_cast<int>(menu_.settings.muted));
        writeConsoleLine(line);
    }
    return worldButtons;
}

#if DOOM_PERSISTENCE
void DoomScene::refreshSlotLabels() {
    // Six buffers on the stack, then the pointer array the menu's signature asks
    // for: the menu copies them at once, so nothing here outlives the call.
    char labels[M_SLOT_COUNT][M_SLOT_LABEL_SIZE];
    const char* pointers[M_SLOT_COUNT];

    gPersistence.readSlotLabels(labels);
    for (int index = 0; index < M_SLOT_COUNT; ++index) {
        pointers[index] = labels[index];
    }
    m_menu_set_slot_labels(&menu_, pointers);
#if defined(DOOM_MENU_SCRIPT)
    // The six rows are pixels the build host cannot read, so the scripted
    // diagnostic prints them as well. The screen is the truth; this line is the
    // same six strings at the same moment, used to report what it showed.
    for (int index = 0; index < M_SLOT_COUNT; ++index) {
        char line[64];
        std::snprintf(line, sizeof line, "SHOW,slot,row=%d,label=%s\n", index, labels[index]);
        writeConsoleLine(line);
    }
#endif
}

void DoomScene::handleSlotAction(int action, int slot) {
    if (action == M_ACTION_SAVE_SLOT) {
        (void)gPersistence.saveSlot(slot);
        // The row the player just wrote has a new clock, so the next open shows
        // it without waiting for a screen change to force a re-read.
        refreshSlotLabels();
        return;
    }
    if (action != M_ACTION_LOAD_SLOT) {
        return;
    }
    if (!gPersistence.loadSlot(slot)) {
        // The failure is already on its own line; the world is either the fresh
        // level the driver leaves (a rejected record) or untouched (an unreadable
        // slot), so the menu's claim "nothing happened" is true.
        return;
    }
    // A load is a level load: the tics that elapsed during the reload are not
    // replayed, and the transition fade runs from black exactly as a New Game's does.
    worldTic_ = ticClock_.tic();
    startFade();
    // Make the very next drawn frame report itself. The power-cycle proof
    // compares the restored x/y/angle/health/tics with the previous boot's; the
    // ordinary one-second cadence could already be a second of world tics past
    // the restore, which would compare different states.
    showReportAfterLoad_ = true;
}
#endif  // DOOM_PERSISTENCE

#endif  // DOOM_MENU

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)

/*
 * THE AUTOMAP'S CONTROLS
 *
 * Select toggles on its rising edge and latches until release -- only the edge is
 * read, so one press is one toggle. While the map is up the d-pad pans, A zooms
 * in, B zooms out, and NOTHING reaches the world's ticcmd: the map's buttons are
 * the map's, so the player stands still behind it. That is the same shape the
 * menu uses (a paused world and no input), and it is what keeps a pan from
 * turning the player and a zoom from firing the pistol.
 *
 * Upstream needs no such rule because its arrow keys are shared: following, they
 * turn the player and the map ignores them; the 'f' key is what hands them to the
 * map. The board has no 'f', so the d-pad is always the map's while the map is
 * up, and the first press is what stops the follow (am_map.c's am_pan()).
 *
 * The menu wins: while it is open, Select is recorded but changes nothing, so a
 * press that belonged to the menu does not toggle the map when the menu closes.
 *
 * THE KEY is Select (GPIO 35), as upstream's TAB: the reference board has a
 * 10 kOhm pull-up on it, so it reads high when released. Measured at 5 kHz over
 * five seconds: 0-25 short low pulses, almost all under 1 ms -- electrical noise
 * on the wire, which the engine's 100 ms debounce absorbs. The arming latch
 * (`mapSelectArmed_`) is what keeps a genuinely stuck-low pin from toggling.
 */
std::uint8_t DoomScene::runMap(std::uint8_t buttons, bool menuOpen) {
    static_assert(AM_PAN_UP == doomc::kButtonUp && AM_PAN_DOWN == doomc::kButtonDown
                      && AM_PAN_LEFT == doomc::kButtonLeft
                      && AM_PAN_RIGHT == doomc::kButtonRight,
                  "the automap's pan bits are the engine's d-pad bits");
    static_assert(AM_ZOOM_IN == doomc::kMapZoomIn && AM_ZOOM_OUT == doomc::kMapZoomOut
                      && AM_ZOOM_RELEASE == doomc::kMapZoomRelease,
                  "the automap's zoom directions are the driver's");

#if defined(DOOM_MAP_SCRIPT)
    // Diagnostic build: the script's own Select bit, so the board's electrical
    // noise on a real pin cannot scramble the scripted states.
    const bool select = (buttons & kMapScriptSelect) != 0u;
#else
    // Select (GPIO 35) has a 10 kOhm pull-up on the reference board and reads
    // high when released (measured at 5 kHz: 0-25 short low pulses per 5 s, almost
    // all under 1 ms -- electrical noise the engine's 100 ms debounce absorbs).
    // It is armed only after being seen released once: that costs nothing on a
    // healthy pin and disables a stuck-low one instead of toggling forever.
    const bool physical = engine.getInputManager().isButtonDown(kSelectIndex);
    if (!physical) {
        mapSelectArmed_ = true;
    }
    const bool select = mapSelectArmed_ && physical;
#endif
    if (select != mapSelectHeld_) {
        mapSelectHeld_ = select;
        if (select && !menuOpen) {
            if (sceneRenderer_.mapActive()) {
                sceneRenderer_.mapStop();
            } else {
                (void)sceneRenderer_.mapStart();
            }
        }
    }

    if (menuOpen || !sceneRenderer_.mapActive()) {
        return buttons;
    }

    const std::uint8_t held = static_cast<std::uint8_t>(buttons & 0x3Fu);
    (void)sceneRenderer_.mapPan(
        static_cast<std::uint8_t>(held & (AM_PAN_UP | AM_PAN_DOWN | AM_PAN_LEFT | AM_PAN_RIGHT)));
    if ((held & doomc::kButtonA) != 0u) {
        (void)sceneRenderer_.mapZoom(AM_ZOOM_IN);
    } else if ((held & doomc::kButtonB) != 0u) {
        (void)sceneRenderer_.mapZoom(AM_ZOOM_OUT);
    } else {
        (void)sceneRenderer_.mapZoom(AM_ZOOM_RELEASE);
    }
    return 0;
}

#endif  // SHOW && DOOM_HUD

void DoomScene::draw(Renderer& renderer) {
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The game frame: the player's viewpoint, presented by the engine with
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

#if defined(PLATFORM_NATIVE)
const std::uint8_t* DoomScene::previousIndexPlane() const {
#if defined(DOOM_FULL_PROFILE)
    // The frame just drawn lives in `indexPlaneCurrent_`; the other plane is
    // the one that still holds the frame before it. Without the gate there is
    // no second plane and no previous frame to hand out.
    return indexPlanes_[static_cast<std::size_t>(indexPlaneCurrent_ ^ 1u)];
#else
    return nullptr;
#endif
}
#endif  // PLATFORM_NATIVE

#if defined(DOOM_SCREENSHOT)

namespace {

/*
 * DOOM_SCREENSHOT (env native_shot_*): one finished frame, written to a file.
 *
 * The frame index and the path are compile-time, which is the whole point: this
 * port runs the headless loop at several hundred frames a second, so a key press
 * or a wall-clock timer cannot name a state, and a capture that could be retimed
 * at run time would only move that problem into a build flag read at run time.
 * `-D DOOM_SCREENSHOT=<frame>` names the frame; the frame's *state* is whatever
 * the frame-indexed script (DOOM_MENU_SCRIPT and `kMenuScript`) has driven by
 * then, so the three captures S7 needs are three frames of one script rather
 * than three timings.
 *
 * The write happens after the RGB565 plane is complete -- the expansion above
 * filled every pixel and the engine's present() has not run yet -- and reads the
 * surface, not the window: it is the same picture with SDL_VIDEODRIVER=dummy and
 * with a visible window, because the window is only the present's output.
 *
 * The file is a binary PPM (P6), three bytes per pixel behind a 15-byte text
 * header. PPM because it needs no encoder and no new dependency here; Pillow
 * reads it directly, which is what turns it into the committed PNG.
 */

/// The frame this build writes, and where.
constexpr std::uint32_t kScreenshotFrame = static_cast<std::uint32_t>(DOOM_SCREENSHOT);

#ifndef DOOM_SCREENSHOT_PATH
#define DOOM_SCREENSHOT_PATH "doom-frame.ppm"
#endif

constexpr const char* kScreenshotPath = DOOM_SCREENSHOT_PATH;

/// One P6 row: 320 pixels, three bytes each. The writer's only scratch.
constexpr std::size_t kScreenshotRowBytes = static_cast<std::size_t>(DoomScene::kLogicalWidth) * 3u;

/**
 * @brief Writes @p pixels (RGB565, first row at the top) as a binary PPM.
 *
 * 5 -> 8 and 6 -> 8 bits are bit replication, not a shift. The engine packs a
 * channel by truncation (`BaseDrawSurface::color565()` keeps the top five and
 * six bits), so replication is what maps 31 -> 255 and 63 -> 255 and keeps white
 * white; a plain shift would leave the brightest colour at 248/252 and dim the
 * whole file by 3 %. The values are the quantised ones the surface holds, never
 * the palette's original 8-bit entries: the file shows the frame the panel was
 * given, which is the point of capturing the finished plane.
 *
 * @return false on a file that cannot be opened, written or closed, or a surface
 *         wider than the logical frame (the buffer this writer sizes a row for).
 */
bool writeScreenshotPpm(const std::uint16_t* pixels, int width, int height, const char* path) {
    if (pixels == nullptr || width <= 0 || height <= 0
        || static_cast<std::size_t>(width) > static_cast<std::size_t>(DoomScene::kLogicalWidth)) {
        return false;
    }
    // The documented path lives under `.pio/screenshots/`, and a capture should
    // not need a mkdir first. `error_code` overloads, never the throwing ones:
    // the firmware's flag list carries -fno-exceptions and this file is compiled
    // with it, so a throw here would end the process instead of reporting.
    std::error_code ignored;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ignored);
    }
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3u;
    bool ok = std::fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    std::uint8_t row[kScreenshotRowBytes];
    for (int y = 0; y < height && ok; ++y) {
        const std::uint16_t* source =
            pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
        for (int x = 0; x < width; ++x) {
            const std::uint16_t value = source[x];
            const unsigned red = (value >> 11) & 0x1Fu;
            const unsigned green = (value >> 5) & 0x3Fu;
            const unsigned blue = value & 0x1Fu;
            const std::size_t at = static_cast<std::size_t>(x) * 3u;
            row[at + 0u] = static_cast<std::uint8_t>((red << 3) | (red >> 2));
            row[at + 1u] = static_cast<std::uint8_t>((green << 2) | (green >> 4));
            row[at + 2u] = static_cast<std::uint8_t>((blue << 3) | (blue >> 2));
        }
        ok = std::fwrite(row, 1u, rowBytes, file) == rowBytes;
    }
    // Closed before the caller reports: the SHOW line that names the file is
    // written after the bytes it describes are on disk, so a reader that waits
    // for that line never sees a partial frame.
    if (std::fclose(file) != 0) {
        ok = false;
    }
    return ok;
}

}  // namespace

#endif  // DOOM_SCREENSHOT

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

        char line[448];
        std::snprintf(line, sizeof line,
                      "SHOW,frame=%u,x=%ld,y=%ld,angle=%lu,viewz=%ld,tics=%u,health=%ld,"
                      "render_us=%u,convert_us=%u,present_us=%u,"
                      "frame_us=%u,fps_x10=%u,update_us=%u,hash_us=%u,other_us=%u,"
                      "refused=%u,stack_free=%u,hash=%s\n",
                      static_cast<unsigned>(showFrame_ - 1u),
                      // DOOM's own units, unconverted: 16.16 map units and a
                      // 32-bit binary angle, the same integers the host's
                      // movement gate prints, so the two logs compare directly.
                      static_cast<long>(showPlayer_.x), static_cast<long>(showPlayer_.y),
                      static_cast<unsigned long>(showPlayer_.angle),
                      static_cast<long>(showPlayer_.viewz),
                      static_cast<unsigned>(showPlayer_.tics),
                      static_cast<long>(showPlayer_.health),
                      static_cast<unsigned>(showRenderMicros_),
                      static_cast<unsigned>(showConvertMicros_),
                      static_cast<unsigned>(presentUs),
                      static_cast<unsigned>(frameUs),
                      static_cast<unsigned>(fpsX10),
                      static_cast<unsigned>(showUpdateMicros_),
                      static_cast<unsigned>(showHashMicros_),
                      static_cast<unsigned>(otherUs), static_cast<unsigned>(showRefused_),
                      static_cast<unsigned>(doomc::frameStackFree()),
                      showHash_);
        writeConsoleLine(line);
    }

#if defined(DOOM_AUDIO)
    // Beside the frame line, with its own one-second clock: printed with the
    // frame cadence it would burst ten identical lines at boot.
    reportAudio();
#endif

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
    // The player state this frame draws, captured before the render: the line is
    // written at the top of the *next* draw, and every field on it describes the
    // frame it reports.
    showPlayer_ = sceneRenderer_.player();
#if DOOM_PERSISTENCE
    // A load asked for its first restored frame to be reported. The line is
    // written at the top of the next draw and describes the state captured just
    // above, so setting the flag here is what puts the restored x/y/angle/
    // health/tics on the wire before the world can tick again.
    if (showReportAfterLoad_) {
        showReportAfterLoad_ = false;
        showPendingReport_ = true;
    }
#endif
#if defined(DOOM_MENU)
    // A level load's fade owns the palette while it runs; asked before the
    // flash so the flash never rebuilds a full-brightness table mid-fade.
    updateFade();
#endif
#if defined(DOOM_PALFLASH)
#if defined(DOOM_MENU)
    if (!fadeActive_) {
        updatePaletteFlash();
    }
#else
    updatePaletteFlash();
#endif
#endif
    bool rendered = false;

    // The refusal diagnostic outlives the two surface branches below. A refused
    // frame is not replaced by the shell, so this record is the only place
    // the cause is named.
    const char* refusedStage = "none";
    int refusedCode = 0;

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
            } else {
                refusedStage = measured.refusedStage;
                refusedCode = measured.refusedCode;
            }
        }
    }
#if defined(PLATFORM_NATIVE)
    // The desktop mirror: SDL2 has no 8bpp surface, so the indices land in a
    // scene-owned plane and are expanded to RGB565 exactly the way the panel's
    // LUT expands them, which keeps the window showing the panel's colours
    // instead of a second, slightly different conversion. Guarded by
    // PLATFORM_NATIVE because on the board the plane would cost DRAM for a
    // branch the TFT driver never takes.
    else if (std::uint16_t* pixels = surface.getPixelBuffer()) {
#if defined(DOOM_FULL_PROFILE)
        // Alternate the twin planes per native frame: flip before the render
        // so this frame draws into the plane the last expansion did not read,
        // leaving the other one holding the previous completed frame for the
        // level transition. A refused frame leaves that completed plane alone.
        //
        // A melt freezes that pair instead: the old level's frame stays in
        // `meltStartPlane_` and every melt frame renders into `meltPlane_`,
        // which is also the composite destination. The offsets are drawn on
        // the first melt frame, after the plane is fixed, so
        // `previousIndexPlane()` names the old level's frame.
        if (meltActive_) {
            indexPlaneCurrent_ = meltPlane_;
            if (meltStep_ == 0u) {
                wipe_StartScreen(&wipe_, previousIndexPlane(), kLogicalWidth, kLogicalHeight);
            }
        } else {
            indexPlaneCurrent_ = static_cast<std::uint8_t>(indexPlaneCurrent_ ^ 1u);
        }
        std::uint8_t* indices = indexPlanes_[indexPlaneCurrent_];
#else
        std::uint8_t* indices = indexPlane_;
#endif
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

#if defined(DOOM_FULL_PROFILE)
                if (meltActive_) {
                    // The plane just rendered is the new level's frame and the
                    // composite's destination; `previousIndexPlane()` is the
                    // old level's last frame, held intact by the frozen flip.
                    // The melt only reads that plane.
                    wipe_EndScreen(&wipe_, indices);
                    const int meltDone = wipe_DoMelt(&wipe_, indices, kMeltTicks);
                    ++meltStep_;

                    char meltLine[64];
                    std::snprintf(meltLine, sizeof meltLine,
                                  "SHOW,melt,frame=%u,step=%u,done=%u\n",
                                  static_cast<unsigned>(showFrame_),
                                  static_cast<unsigned>(meltStep_),
                                  static_cast<unsigned>(meltDone > 0 ? 1u : 0u));
                    writeConsoleLine(meltLine);

                    if (meltDone > 0) {
                        // The final composite is the new frame byte for byte;
                        // hand the pair back to the normal alternating flip.
                        meltActive_ = false;
                        wipe_Shutdown(&wipe_);
                    }
                }
#endif

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
            } else {
                refusedStage = measured.refusedStage;
                refusedCode = measured.refusedCode;
            }
        }
    }
#endif

    if (!rendered) {
        // Two different failures, two different answers.
        //
        //   no session, or a surface the renderer cannot use
        //       The shell is the truth: no partition and no accepted container
        //       mean the port really has no level, and its comment says so.
        //
        //   an open session whose pass refused this viewpoint
        //       The session is fine and the surface still holds the last good
        //       picture, because renderOnce() prefills only after the walk
        //       succeeds. Replacing it with the shell would claim a
        //       fallback that never happened. Count it, name the stage/code pair
        //       once, and leave the picture alone.
        if (!sceneRenderer_.isOpen() || width != kLogicalWidth
            || height != kLogicalHeight) {
            // A session that never opened gets the failure screen, not
            // the shell. The loading screen is the fallback for the case
            // where `init()`'s open never ran at all. The open-but-refused case
            // below is untouched and keeps its last good picture.
            //
            // The boot screen is painted only while no session is open: after
            // `setPalette8()` the same surface byte is a PLAYPAL index, so
            // painting RGB332 bytes then would lie about every colour. A broken
            // surface size on an open session therefore keeps the shell
            // fallback rather than risking that mix.
            std::uint8_t* bootPixels = surface.getSpriteBuffer();
            if (!sceneRenderer_.isOpen() && bootPixels != nullptr) {
                const doomc::BootScreen screen =
                    bootFailed_ ? doomc::bootScreenError(bootFailure_, "E1M1")
                                : doomc::bootScreenLoading("E1M1");
#if !defined(NDEBUG)
                // The gate proves this for 320x200; the assert is the board's own
                // last line of defence against a longer sentence later.
                assert(doomc::bootScreenFits(screen, width, height));
#endif
                doomc::bootScreenPaint(bootPixels, width, height, screen);
            } else {
                paintShellFrame(renderer, buttons_);
            }
        } else {
            ++showRefused_;
            bool seen = false;
            for (int index = 0; index < showRefusalKinds_; ++index) {
                if (showRefusalCode_[index] == refusedCode
                    && std::strcmp(showRefusalStage_[index], refusedStage) == 0) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                char line[96];
                std::snprintf(line, sizeof line,
                              "SHOW,renderfail,stage=%s,code=%d,frame=%u\n",
                              refusedStage, refusedCode,
                              static_cast<unsigned>(showFrame_));
                writeConsoleLine(line);
                if (showRefusalKinds_ < kShowRefusalKinds) {
                    showRefusalStage_[showRefusalKinds_] = refusedStage;
                    showRefusalCode_[showRefusalKinds_] = refusedCode;
                    ++showRefusalKinds_;
                }
            }
        }
        // A line at the top of the next draw would describe a frame that was not
        // drawn, so it is dropped in both cases.
        showPendingReport_ = false;
    }

#if defined(DOOM_SCREENSHOT)
    // The one frame this build was asked for. It sits after the two surface
    // branches and after the refusal handling, so the plane is complete and the
    // scene knows whether the picture is the game's: a frame that fell back to
    // the shell (no session) or was refused exits non-zero and writes nothing,
    // instead of leaving a file that claims to be the game.
    //
    // The build stops here on purpose. Leaving the loop running would make the
    // caller time a kill, which is the clock this flag exists to remove, and a
    // `pio run -e <env> -t exec` would never return. Every SHOW line is already
    // flushed per call (platformPrint), so the log that names the file is
    // complete when the process ends.
    if (showFrame_ == kScreenshotFrame) {
        const std::uint16_t* finished = surface.getPixelBuffer();
        const bool drawn = rendered && finished != nullptr;
        const bool wrote = drawn && writeScreenshotPpm(finished, width, height, kScreenshotPath);

        const char* reason = "none";
        if (!drawn) {
            reason = !sceneRenderer_.isOpen() ? "nosession" : (rendered ? "nosurface" : refusedStage);
        }
#if defined(DOOM_MENU)
        const char* menuState = m_menu_active(&menu_) != 0 ? "open" : "closed";
#else
        const char* menuState = "none";
#endif
        // What a P6 of this size must be: the 15-byte header plus three bytes a
        // pixel, so a truncated file is caught here and not by the converter.
        char header[32];
        const int headerBytes = std::snprintf(header, sizeof header, "P6\n%d %d\n255\n", width, height);
        const int bytes = wrote ? headerBytes + width * height * 3 : 0;

        char line[288];
        std::snprintf(line, sizeof line,
                      "SHOW,screenshot,frame=%u,status=%s,w=%d,h=%d,bytes=%d,header=%d,"
                      "menu=%s,reason=%s,x=%ld,y=%ld,angle=%lu,path=%s\n",
                      static_cast<unsigned>(showFrame_), wrote ? "ok" : "fail",
                      width, height, bytes, headerBytes, menuState, reason,
                      static_cast<long>(showPlayer_.x), static_cast<long>(showPlayer_.y),
                      static_cast<unsigned long>(showPlayer_.angle), kScreenshotPath);
        writeConsoleLine(line);

        // 0 for a written frame, 2 for a frame that was not the game, 3 for a
        // file the filesystem refused. The distinct codes are what lets T5's
        // loop stop on a real failure instead of converting whatever is there.
        std::exit(wrote ? 0 : (drawn ? 3 : 2));
    }
#endif  // DOOM_SCREENSHOT

    showDrawStart_ = drawStart;
    showDrawEnd_ = sampleMicros();

#if defined(DOOM_FRAME_DUMP)
    // Diagnostic: the stack margin once a second, independent of the frame
    // report's own limit. The firing path is the deepest chain the game has, and
    // its peak only shows after minutes of play, long after `SHOW,frame` has
    // stopped printing.
    {
        static std::uint32_t lastStackUs = 0u;
        const std::uint32_t now = sampleMicros();
        if (lastStackUs == 0u || now - lastStackUs >= 1000000u) {
            lastStackUs = now;
            char stackLine[64];
            std::snprintf(stackLine, sizeof stackLine, "SHOW,stack,free=%u,frame=%u\n",
                          static_cast<unsigned>(doomc::frameStackFree()),
                          static_cast<unsigned>(showFrame_));
            writeConsoleLine(stackLine);
        }
    }
#endif

#if defined(DOOM_FRAME_DUMP)
    // Diagnostic (env `esp32dev_ILI9341_show_dump` / `_hud_dump`): hold A and
    // B together and the finished surface goes out over the console as RLE, so
    // the frame that looked wrong on the glass can be rebuilt on the host and
    // diffed against its own render of the same viewpoint.
    //
    // Three frames, about a second of play apart: the room that reported the
    // artifact blinks (sector special 12), so one sample could easily be the
    // phase that renders correctly. The burst costs ~10 s of blocked console per
    // frame, and the scene must not dump while it is already writing.
    {
        static int remaining = 0;
        static std::uint32_t cooldown = 0;
        const std::uint8_t both = static_cast<std::uint8_t>(doomc::kButtonA | doomc::kButtonB);
        bool dump = false;
        if (remaining == 0) {
            if ((buttons_ & both) == both) {
                remaining = 3;
                cooldown = 0;
            }
        } else if (cooldown > 0u) {
            --cooldown;
        } else {
            dump = true;
        }
#if defined(DOOM_FRAME_DUMP_AUTO)
        // Self-test hook: `-D DOOM_FRAME_DUMP_AUTO=<frame>` dumps at that frame
        // without a button, so the protocol can be checked end to end from a
        // capture before anyone is asked to press A and B in a room. Never
        // defined by an environment in this file.
        if (remaining == 0 && showFrame_ == static_cast<std::uint32_t>(DOOM_FRAME_DUMP_AUTO)) {
            remaining = 1;
            cooldown = 0;
        }
#endif
        if (dump) {
            // The per-record answer only: which field or which texture byte
            // differs. That costs ~2 KB instead of the 64,000-byte RLE frame, so
            // a press does not freeze the game for twelve seconds and no long
            // capture window is needed.
            if (remaining == 3) {
                doomc::frameDumpSectors();
                // And the records the frame was drawn from -- every drawseg
                // (scales, masked column block, clip codes), every visplane and
                // the submitted sprite list -- as `VD,...` lines that
                // tools/viewdiag_compare.py diffs against the host's render of
                // the same viewpoint.
                doomc::frameDumpViewDiag();
            }
            --remaining;
            cooldown = 20;  // ~1.3 s at the shipped frame rate
        }
    }
#endif

    ++showFrame_;
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1

}  // namespace doom
