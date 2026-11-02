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

#if defined(DOOM_MENU)
#include "m_menu.h"
#endif

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
 *      - ESP32 (`TFT_eSPI_Drawer::getSpriteBuffer()`): one byte per pixel. The
 *        shell paints RRRGGGBB (RGB332) colours into it; the game frame writes
 *        DOOM palette indices and installs the PLAYPAL -> RGB565 table with
 *        `DrawSurface::setPalette8()`, so the drawer expands each byte as it
 *        presents. This is the buffer DOOM's renderer writes into.
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
 * renders the player's viewpoint through `doomc/SceneRenderer.h` into the engine's
 * 8bpp surface as DOOM palette indices, and `init()` installs the WAD PLAYPAL as
 * an RGB565 expansion table through `DrawSurface::setPalette8()`, so the engine's
 * present path expands those indices as it transfers. There is no per-frame
 * conversion pass: the panel shows the real palette, and the bytes the frame hash
 * covers are the bytes the panel draws from. The level is opened once in `init()`
 * and stays open: the same setup `frameProbeRun()` uses
 * (`doomc/FrameDriver.cpp`), so the board frame and the measurement cannot drift.
 *
 * D4.2f makes that frame the game's: the same open session owns E1M1's world
 * (`p_level_init()` + `p_spawn_specials()` over an arena the session measures and
 * sizes, printed as two `SHOW,arena,...` lines), `update()` runs one
 * `p_level_tic()` per tic the tic clock reports -- 0, 1 or 2 on a 20 fps panel --
 * and `draw()` renders `playerState.viewz` at `player->x/y/angle` instead of the
 * harness's fixed frame 00. Buttons become a `p_ticcmd_t` through
 * `doomc/InputMap.h`, so `B` uses, `A` is counted for D4.2d and `up` walks. The
 * `SHOW,frame=...` line therefore carries `x`, `y`, `angle`, `viewz`, `tics` and
 * `health` beside the timings it always had, plus `refused=`, the number of
 * frames a pass has refused since boot; and `init()` prints one
 * `SHOW,frame00,...` line: the harness's frame-00 viewpoint rendered through the
 * one view entry point, whose hash must be the host's.
 *
 * D4.2g makes a refused frame visible instead of a lie. A pass that runs out of
 * a walk capacity used to make `drawSceneFrame()` paint the Phase 0 shell, which
 * claims a fallback that never happened. Now the surface keeps the last good
 * picture (`renderOnce()` prefills only after the walk succeeds), the frame is
 * counted, and each distinct stage/code pair prints one
 * `SHOW,renderfail,stage=<stage>,code=<n>,frame=<n>` line. The shell survives
 * only for a session that never opened -- no partition, rejected container --
 * which is what its comment always said it was for.
 *
 * It prints `SHOW,setup`, `SHOW,mem`, the two `SHOW,arena` lines,
 * `SHOW,frame00`, two `SHOW,census` lines and then one bounded `SHOW,frame=...`
 * line: the first ten frames and one per second after, until frame 600. The tic
 * clock, the input line and every event line keep running unchanged. The shipped
 * environments never define it and keep painting the shell.
 *
 * D4.0c asked whether a second 320x200 8-bit frame buffer could overlap the
 * present with the render. The census answers that with the board's own numbers:
 * the heap has one ~110 KB block at boot and a 47 KB largest hole once the sprite
 * exists, so a second 64,000 B buffer does not fit. `SHOW,census,pool` prints the
 * probe's verdict (`twin64=fits|no_fit`) on every boot, so a future board or a
 * changed allocation order can be judged from the log instead of re-derived.
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
     * @brief Renders the game frame, measures it and closes the measurement.
     *
     * This is D4.0's frame: the player's viewpoint as DOOM palette indices in
     * the engine's 8bpp buffer, presented by the engine through the PLAYPAL
     * table installed in `init()`. It prints one bounded `SHOW,...` line per
     * reported frame and leaves the tic clock, input lines and console behaviour
     * untouched.
     */
    void drawSceneFrame(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief Runs the world tics this frame owes the simulation (D4.2f).
     *
     * Split out of `update()` and called on *every* frame, before the
     * button-change early return: a frame that reports no input change still
     * advances the simulation. The count is the tic clock's own delta, so a
     * frame that covers two tics runs two, and the world's rate is DOOM's 35 Hz
     * rather than the frame rate.
     *
     * @param buttons The buttons sampled for this frame.
     */
    void runWorldTics(std::uint8_t buttons);

#if defined(DOOM_HUD)
    /**
     * @brief The automap's turn of one frame (D5.3): Select, then its controls.
     *
     * Select (engine input index 7, GPIO 35) toggles the map on its rising edge:
     * an edge opens it, the next one closes it, and the same press cannot toggle
     * twice because only the edge is read. While the map is up the d-pad pans
     * (which cancels the follow, as am_map.c's comment explains), A zooms in and
     * B zooms out, and **none of those bits reach the world's ticcmd**: the map's
     * buttons are the map's, so `runMap()` answers 0 and the player stands still
     * behind the map, exactly as the menu answers 0 while it is open.
     *
     * A Select press is swallowed while the menu is open (the menu owns the
     * input) but still recorded, so closing the menu does not toggle the map on a
     * press that belonged to the menu.
     *
     * @param buttons  The world's buttons from `runMenu()`.
     * @param menuOpen Whether the menu was open this frame.
     * @return The buttons the world may see: @p buttons, or 0 while the map is up.
     */
    std::uint8_t runMap(std::uint8_t buttons, bool menuOpen);

    /// Select's held state on the previous frame: the map's toggle edge.
    bool mapSelectHeld_ = false;

    /**
     * @brief The shipped Up+Down chord's latch and frame count (D5.3).
     *
     * GPIO 35 has no pull-up on this board and floats, so the shipped build
     * toggles the map on Up+Down held together for `kMapChordFrames` frames: both
     * are real pull-up'd keys, and upstream adds then subtracts them, so the
     * world sees `forwardmove 0`. `mapChordArmed_` is cleared when the chord
     * fires and set again only when a key is released, so one hold is one toggle.
     */
    bool mapChordArmed_ = true;
    std::uint8_t mapChordFrames_ = 0;

    /**
     * @brief Whether Select has been seen released since boot (D5.3).
     *
     * Only `DOOM_MAP_SELECT` builds read the physical pin. GPIO 35 is input-only
     * on this SoC and has no internal pull-up, so `INPUT_PULLUP` does nothing
     * there and a floating pin reads LOW -- "held" -- from the first sample (this
     * board's own log: the map opened at boot with no press, then toggled every
     * ~1.5 s as the pin drifted). A control that opened the map on a floating pin
     * and could never close it is worse than no control, so Select is armed only
     * after it has been read released once: a stuck-low pin is then simply dead,
     * which is what a missing pull-up deserves. The README says so.
     */
    bool mapSelectArmed_ = false;

    /// Frames the scripted map diagnostic has played (DOOM_MAP_SCRIPT only).
    std::uint32_t mapScriptFrame_ = 0;
#endif
#endif

#if defined(DOOM_MENU)
    /**
     * @brief The menu's turn of one frame (D5.2): input, its three outcomes, the save.
     *
     * Feeds the sampled buttons to the menu state machine and returns what the
     * WORLD may see (nothing while the menu is open or latched). Then carries out
     * what the menu asked for: a New Game reload, a gamma change on the palette,
     * and the write of the settings when the player leaves the options screen.
     * The menu being open pauses the 35 Hz world tic, in `runWorldTics()`.
     */
    std::uint8_t runMenu(std::uint8_t buttons);

    /// Rebuilds the PLAYPAL tables at the menu's gamma and hands them to the panel.
    void applyMenuGamma();

    /**
     * @brief Starts the level transition's palette fade (D5.4).
     *
     * Vanilla's `f_wipe.c` melt copies `screens[2]` and `screens[3]`: two more
     * 64,000 B surfaces, which this board does not have (the largest free block
     * after setup is 47,092 B -- PLAN 12.1). The 4 MB profile substitutes this
     * fade: the new frame ramps up from black through the PLAYPAL table the
     * palette path already owns, one table rebuild per step, no buffer.
     */
    void startFade();

    /**
     * @brief One fade step per rendered frame, and the reload that starts one.
     *
     * A level load is noticed without new plumbing: `leveltime` restarts at 0 on
     * every load, `G_DoReborn`'s reload included, so a tic count that goes
     * backwards is a reload. While a fade runs the palette flash is skipped --
     * the fade owns the table -- and the last step re-applies the exact palette
     * the flip-flop path would have built.
     */
    void updateFade();

    /// Builds the panel's tables from the resident PLAYPAL at @p scale/256.
    bool applyFade(std::uint16_t scale);

#if defined(DOOM_PALFLASH)
    /**
     * @brief Builds the panel's tables from PLAYPAL page @p page at @p gamma (D5.3).
     *
     * Page 0 is resident; any other page is decoded from the container for the
     * duration of the call. Hands the (unchanged) table pointer to the panel
     * again. Returns false, leaving the panel as it was, when the page cannot be
     * read.
     */
    bool applyPalette(int page, std::uint8_t gamma);

    /**
     * @brief ST_doPaletteStuff, once per rendered frame: the damage, pickup and
     *        radiation-suit flashes.
     *
     * Asks the world which page the player's counters call for and re-applies the
     * palette only when that page differs from the one on the screen.
     */
    void updatePaletteFlash();
#endif
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

#if defined(DOOM_MENU)
    /// The menu and the settings it edits: skill, volume, gamma (D5.2). ~24 bytes.
    m_menu_t menu_;
    /// A persisted value changed and has not been written yet.
    bool settingsDirty_ = false;
    /// The gamma level the panel's palette was last built for.
    std::uint8_t gammaApplied_ = 0;

    /// Fade steps a level load runs: eight frames at the shipped rate is ~0.5 s.
    static constexpr std::uint8_t kFadeSteps = 8u;
    /// True while a level transition's fade owns the palette (D5.4).
    bool fadeActive_ = false;
    /// Steps already applied: 0 means the black frame was just painted.
    std::uint8_t fadeStep_ = 0;
    /// The previous frame's `leveltime`, for the reload that starts a fade.
    std::uint32_t fadeLastTics_ = 0;
    /// Microseconds the whole fade spent rebuilding tables, reported once.
    std::uint32_t fadeMicros_ = 0;
#if defined(DOOM_PALFLASH)
    /// The PLAYPAL page the panel's palette was last built from (0 = normal).
    std::uint8_t paletteApplied_ = 0;
#endif
    /// Frames the scripted-input diagnostic has played (DOOM_MENU_SCRIPT only).
    std::uint32_t menuScriptFrame_ = 0;
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    /**
     * @brief The level the game frame renders (D4.0) and its world (D4.2f).
     *
     * Declared only under the flag. It holds no buffers: the walk state and the
     * manifest adapter are the ones `src/doomc/FrameDriver.cpp` already owns for
     * the probe, so the game and the measurement share one copy of each.
     */
    doomc::SceneRenderer sceneRenderer_;

    /// The tic the world has been ticked to. The clock's tic is the target and
    /// this is the high-water mark, so `tic - worldTic_` is exactly how many tics
    /// the world still owes this frame -- 0, 1 or (at 20 fps) occasionally 2.
    std::uint32_t worldTic_ = 0;

    /// The player state the *reported* frame drew, captured before that frame's
    /// render: the line is written at the top of the next draw, as every other
    /// field on it is.
    doomc::ScenePlayerState showPlayer_;

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

    /// Frames a pass refused, and the stage/code pairs already reported.
    /// D4.2g: a refused frame keeps the previous picture, so the log is the only
    /// place the refusal is visible -- one `SHOW,renderfail` line per distinct
    /// pair, plus the running count on every periodic `SHOW,frame` line.
    std::uint32_t showRefused_ = 0;
    /// The stage and code of each pair already named on the serial line. The
    /// vocabulary is the driver's five stages, so six slots cannot overflow for a
    /// real build; a pair past the table is printed rather than swallowed.
    static constexpr int kShowRefusalKinds = 6;
    const char* showRefusalStage_[kShowRefusalKinds] = {};
    int showRefusalCode_[kShowRefusalKinds] = {};
    int showRefusalKinds_ = 0;
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
