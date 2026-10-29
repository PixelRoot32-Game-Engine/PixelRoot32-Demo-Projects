#pragma once

#include <cstdint>

#include <core/Scene.h>

#include "time/TicClock.h"

namespace doom {

/**
 * @brief Composition root of the DOOM port: the platform shell DOOM plugs into.
 *
 * Phase 0 built this as a link-and-present test. D1.1 turns it into the shell
 * the 1993 rasteriser will draw into, and owns exactly three things:
 *
 * 1. The framebuffer contract. Both shipped drivers expose the logical surface
 *    as a linear buffer, one entry per pixel, stride = the logical width,
 *    top-left origin:
 *      - ESP32 (`TFT_eSPI_Drawer::getSpriteBuffer()`): one byte per pixel,
 *        RRRGGGBB (RGB332), which the drawer expands to RGB565 as it presents.
 *        This is the buffer DOOM's renderer writes into.
 *      - native (`SDL2_Drawer::getPixelBuffer()`): one `uint16_t` per pixel,
 *        RGB565. It exists so the desktop mirror shows the same frame.
 *    The scene paints the whole surface through those pointers and never
 *    through `Renderer` primitives, because the primitives are the engine's
 *    abstract surface and the rasteriser needs the concrete one.
 *
 * 2. The input map. The engine's `InputConfig` order -- 0 up, 1 down, 2 left,
 *    3 right, 4 A, 5 B -- is also the order of the button slots along the
 *    bottom edge and the bit order of the `INPUT,buttons=0x%02X` serial line.
 *    The three are the same number, so there is no table that can fall out of
 *    step: index 0 is up and bit 0 is up everywhere in this scene.
 *
 * 3. The tic clock. `update()` takes one monotonic microsecond sample per frame
 *    and feeds it to `time::TicClock`, which derives DOOM's 35 Hz tic as
 *    `t * 35 / 1000000` from the elapsed base -- never by accumulating a
 *    per-frame delta, which truncates and drifts. Every tic the clock reaches
 *    prints one `TIC,n=%u,us=%lu` line (n = tic index since `init()`, us = the
 *    32-bit sample that produced it), once per tic and never per frame. That
 *    line is the serial witness for the simulation's clock until the real game
 *    loop exists.
 *
 * Nothing on screen depends on a frame counter or an animation phase. The
 * pattern is a pure function of the surface size, the palette and the pressed
 * set, so a still frame is always the same frame.
 *
 * `PIXELROOT32_DOOM_PALETTE_TEST` replaces that shell with the palette contract
 * screen: a 16x16 grid of all 256 indices in palette order plus a 32-step grey
 * ramp, painted from `palette/DoomPalette.h` through the same 8bpp buffer DOOM
 * will write. It lives in its own build environment
 * (`env:esp32dev_ILI9341_palette`); the shipped environments never define it
 * and render the shell exactly as they always have.
 *
 * The logical surface is a constant here on purpose. DOOM's renderer is
 * column-indexed: `distscale[SCREENWIDTH]`, `visplane_t::top[SCREENWIDTH]`,
 * `floorclip` / `ceilingclip` and the status bar layout all assume 320x200.
 * Rendering into anything narrower is a renderer rewrite plus a HUD relayout,
 * not a build flag, so the panel only ever changes the physical window.
 */
class DoomScene : public pixelroot32::core::Scene {
public:
    /// DOOM's own surface width. See the class comment.
    static constexpr int kLogicalWidth = 320;
    /// DOOM's own surface height, status bar included.
    static constexpr int kLogicalHeight = 200;

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /**
     * @brief Drives the tic clock and reports every tic it reaches.
     *
     * Split out of `update()` so the clock is sampled exactly once per frame,
     * before the input early-return: a quiet frame must still advance the
     * simulation's clock.
     */
    void pollTicClock();

    /**
     * @brief Buttons held at the last `update()`, packed as `INPUT` reports them.
     *
     * Bit 0 up, 1 down, 2 left, 3 right, 4 A, 5 B. Kept from `update()` to
     * `draw()` on purpose: the frame must show the state of the last input
     * sample, not a second reading taken mid-draw that no serial line saw.
     */
    std::uint8_t buttons_ = 0;

    /// Tic 0 is a real tic, so "nothing reported yet" needs a value of its own
    /// as the sentinel; every bit set is outside any reachable tic count.
    static constexpr std::uint32_t kNoTic = 0xFFFFFFFFu;

    /// Derives DOOM's 35 Hz tic from the platform's microsecond source.
    time::TicClock ticClock_;

    /// Last tic written to the serial line; the change filter behind the
    /// once-per-tic (never per-frame) reporting.
    std::uint32_t reportedTic_ = kNoTic;
};

}  // namespace doom
