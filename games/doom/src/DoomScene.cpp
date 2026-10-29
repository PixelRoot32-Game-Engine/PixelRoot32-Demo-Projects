#include "DoomScene.h"

#include <cstddef>
#include <cstdio>

#include <core/Engine.h>
#include <graphics/Color.h>
#include <graphics/DrawSurface.h>
#include <graphics/Renderer.h>

#if !defined(PLATFORM_NATIVE)
#include <Arduino.h>
#endif

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
 * is file-local there, so the shell keeps its own copy; the static_asserts
 * below pin the arithmetic to the wire colours it must produce, so a change on
 * either side fails this build instead of showing the wrong hue on the panel.
 */
constexpr std::uint8_t packRgb332(std::uint16_t rgb565) {
    return static_cast<std::uint8_t>(((rgb565 & 0xE000u) >> 8) |
                                     ((rgb565 & 0x0700u) >> 6) |
                                     ((rgb565 & 0x0018u) >> 3));
}

static_assert(packRgb332(0xFFFF) == 0xFF, "RGB332: white must survive the pack");
static_assert(packRgb332(0xF800) == 0xE0, "RGB332: red must survive the pack");
static_assert(packRgb332(0x07E0) == 0x1C, "RGB332: green must survive the pack");
static_assert(packRgb332(0x001F) == 0x03, "RGB332: blue must survive the pack");
static_assert(packRgb332(0xFFE0) == 0xFC, "RGB332: yellow must survive the pack");

// --- Serial -----------------------------------------------------------------

/**
 * @brief Write one already-formatted line to the console this build has.
 *
 * The ESP32 build owns the USB/UART through the Arduino core (`Engine::init()`
 * has already called `Serial.begin(115200)` before any scene is entered); the
 * SDL2 build has no `Serial` object at all, so stdout is the only stream both
 * offer. The engine's `logging::log()` is deliberately not used here: it
 * compiles to a no-op unless `PIXELROOT32_DEBUG_MODE` is set, and these two
 * lines are part of the shell contract on every build.
 */
void writeSerialLine(const char* line) {
#if defined(PLATFORM_NATIVE)
    std::fputs(line, stdout);
    std::fflush(stdout);
#else
    Serial.print(line);
#endif
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
 */
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

    // "rgb565" names the palette the drawer presents, not the byte packing: the
    // 8bpp buffer holds RGB332 and the drawer expands it to RGB565 on the wire.
    // A driver with neither buffer has no palette to name.
    const char* palette = bytesPerPixel != 0 ? "rgb565" : "none";

    char line[128];
    std::snprintf(line, sizeof line, "SHELL,surface=%dx%d,stride=%d,bytes=%d,palette=%s\n",
                  width, height, stride, stride * height * bytesPerPixel, palette);
    writeSerialLine(line);
}

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

}  // namespace

void DoomScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first. This scene adds no entities.
    Scene::init();

    // Nothing is held when the shell comes up; the first change edge is what
    // prints the INPUT line.
    buttons_ = 0;

    reportFrameContract(engine.getRenderer());
}

void DoomScene::update(unsigned long deltaTime) {
    // Entities first, as everywhere else in the repository; this scene has
    // none, so the call is the base contract rather than work.
    Scene::update(deltaTime);

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
        return;
    }
    buttons_ = buttons;

    // The bit field is the same number as the index order: bit 0 up ... bit 5
    // B. One line per change, nothing per frame.
    char line[32];
    std::snprintf(line, sizeof line, "INPUT,buttons=0x%02X\n", static_cast<unsigned>(buttons));
    writeSerialLine(line);
}

void DoomScene::draw(Renderer& renderer) {
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
        paintShell(inks, width, height, buttons_,
                   [framebuffer, width](int x, int y, std::uint16_t rgb565) {
                       framebuffer[y * width + x] = packRgb332(rgb565);
                   });
        return;
    }

    // The desktop mirror: SDL2 has no 8bpp surface, but its RGB565 buffer is
    // logical-sized and indexed the same way, so the same pattern (and the same
    // button state) reaches the window.
    if (std::uint16_t* framebuffer = surface.getPixelBuffer()) {
        paintShell(inks, width, height, buttons_,
                   [framebuffer, width](int x, int y, std::uint16_t rgb565) {
                       framebuffer[y * width + x] = rgb565;
                   });
        return;
    }

    // A driver with neither buffer has no frame to write. init() has already
    // reported bytes=0,palette=none, so there is nothing left to say here.
}

}  // namespace doom
