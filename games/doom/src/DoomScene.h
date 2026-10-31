#pragma once

#include <cstdint>

#include <core/Scene.h>

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
#include "perf/FrameLog.h"
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
#include "doomc/SceneRenderer.h"
#endif

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
 * A fourth, one-shot record comes from the C <-> C++ link probe: `init()` calls
 * `doom_link_probe_run()` from `lib/doom`, a C translation unit that calls the
 * bridge in `src/doomc`, which reaches this port's palette table and the
 * engine's console and returns a value the C side verifies. It prints exactly
 * one `LINK,status=ok,checksum=0x%08X` line (or `status=fail`, with the same
 * fields) through the same console as every other line here, so the boot log
 * states whether the build model the DOOM core needs actually links. Like the
 * other event lines it is compiled out of the frame-log build, whose stream is
 * pure CSV.
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
 * `PIXELROOT32_DOOM_FRAME_LOG` adds the frame-pacing recorder
 * (`perf/FrameLog.h`) and suppresses the shell, tic and input serial lines so
 * the stream is pure `FRAME,...` CSV. It lives in its own build environment
 * (`env:esp32dev_ILI9341_perf`); the member and every call site are compiled
 * only under the flag, so the shipped builds are byte-for-byte behaviourally
 * unchanged and take no extra microsecond samples.
 *
 * `PIXELROOT32_DOOM_WAD_PROBE` adds D2.3's storage probe: `init()` opens the
 * `wad` partition through `doomc/WhdSource.h`, prints one `WAD,status=...` line
 * and one `LUMP,name=...,index=...,size=...` line per resolved lump name, then
 * releases the mapping. It proves the boot path reads the compressed container
 * from flash and resolves real lump names against it. It lives in its own build
 * environment (`env:esp32dev_ILI9341_wad`), which is also the only environment
 * that selects `partitions_wad.csv`; every shipped environment keeps the default
 * table and the original boot log.
 *
 * `PIXELROOT32_DOOM_ZONE_PROBE` adds the zone smoke record: `init()` creates
 * one allocator over a static 64 KB array (never the level working set) and
 * prints one `ZONE,arena=...,overhead=...,largest_free=...,
 * alloc_68168=...` line. It shares `env:esp32dev_ILI9341_wad`, so the same boot
 * log that proves the container also proves that a 64 KB zone accounts for all
 * 65,536 bytes and refuses the container's 68,168 B `HELP1` lump cleanly. The
 * shipped environments define neither flag and carry neither the 64 KB array nor
 * the probe code.
 *
 * `PIXELROOT32_DOOM_FLASH_PROFILE` adds D2.5's flash-read profile: `init()` runs
 * one probe (`doomc/FlashProfile.h`) that decodes E1M1's whole working set out
 * of the mapped container once, replays the identical sequence warm, and prints
 * the two `WADPROF,pass=...` summaries, one line per phase, the warm/cold ratio
 * and the per-frame budget. It lives in its own build environment
 * (`env:esp32dev_ILI9341_flashprof`, same partition table as `_wad`) and costs a
 * 68,168 B static decode buffer, so it is compiled out of every shipped build
 * along with that buffer. Nothing on the frame path changes: the probe runs once,
 * inside `init()`, before the shell draws its first frame.
 *
 * `PIXELROOT32_DOOM_SHOW_E1M1` (D4.0) replaces the shell with the game: `draw()`
 * renders E1M1's spawn viewpoint through `doomc/SceneRenderer.h` into the engine's
 * 8bpp surface as DOOM palette indices, converts that buffer in place to RGB332
 * through the WAD's PLAYPAL table, and lets the engine present it. The level is
 * opened once in `init()` and stays open: the same setup `frameProbeRun()` uses
 * (`doomc/FrameDriver.cpp`), so the board frame and the measurement cannot drift.
 * It prints `SHOW,setup`, `SHOW,mem`, then one bounded `SHOW,frame=...` line: the
 * first ten frames and one per second after, until frame 600. The tic clock, the
 * input line and every event line keep running unchanged. The shipped
 * environments never define it and keep painting the shell.
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

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    /**
     * @brief Renders the game frame, converts it and closes the measurement.
     *
     * This is D4.0's frame: E1M1's spawn viewpoint as DOOM palette indices in
     * the engine's 8bpp buffer, converted in place to RGB332, presented by the
     * engine. It prints one bounded `SHOW,...` line per reported frame and
     * leaves the tic clock, input lines and console behaviour untouched.
     */
    void drawSceneFrame(pixelroot32::graphics::Renderer& renderer);
#endif

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

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    /**
     * @brief The level the game frame renders (D4.0).
     *
     * Declared only under the flag. It holds no buffers: the walk state and the
     * manifest adapter are the ones `src/doomc/FrameDriver.cpp` already owns for
     * the probe, so the game and the measurement share one copy of each.
     */
    doomc::SceneRenderer sceneRenderer_;

    /// Frames rendered since boot, and the ones after which reporting stops.
    /// The scene keeps drawing past the cap; only the serial line ends, so a
    /// capture collected over minutes cannot grow without bound.
    static constexpr std::uint32_t kShowReportFirstFrames = 10u;
    static constexpr std::uint32_t kShowReportFrameLimit = 600u;
    /// One reported line per this many microseconds after the first frames.
    static constexpr std::uint32_t kShowReportIntervalMicros = 1000000u;

    std::uint32_t showFrame_ = 0;

    /// The previous frame's CPU parts and the timestamps that bound it. The line
    /// is written at the top of the *next* draw, after the interval is known.
    std::uint32_t showRenderMicros_ = 0;
    std::uint32_t showConvertMicros_ = 0;
    std::uint32_t showHashMicros_ = 0;
    std::uint32_t showDrawStart_ = 0;
    std::uint32_t showDrawEnd_ = 0;

    /// `sampleMicros()` at the top of the last `update()` and how long the scene
    /// spent in it. The engine's present runs between the end of `draw()` and
    /// that sample, so it is measurable from here without an engine change.
    std::uint32_t showUpdateEntry_ = 0;
    std::uint32_t showUpdateMicros_ = 0;

    /// Whether the previous frame took its index hash, and the hash itself.
    bool showPendingReport_ = false;
    std::uint32_t showLastReportUs_ = 0;
    char showHash_[65] = {0};
#endif

#if defined(PIXELROOT32_DOOM_FRAME_LOG)
    /**
     * @brief Per-frame CSV recorder for `env:esp32dev_ILI9341_perf`.
     *
     * Declared only under the flag: the shipped environments do not carry the
     * member, do not include `perf/FrameLog.h`, and never take the extra
     * end-of-draw microsecond sample.
     */
    perf::FrameLog frameLog_;
#endif
};

}  // namespace doom
