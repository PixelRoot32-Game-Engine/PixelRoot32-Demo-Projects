#pragma once

#include <cstddef>
#include <cstdint>

#include "doomc/FrameDriver.h"

namespace doom {
namespace doomc {

/**
 * @brief What the scene needs to know about one rendered frame (D4.0).
 *
 * The renderer's own timings come straight from the driver's per-frame record,
 * so the numbers the game prints and the numbers the probe prints are produced
 * by the same `esp_timer_get_time()` samples around the same four passes.
 */
struct SceneFrameMeasurements {
    /// The whole four-pass sequence: surface prefill, walk, walls, planes and
    /// masked. Same meaning as `FrameProfileSample::frameMicros`.
    std::uint32_t renderMicros = 0u;
    /// SHA-256 of the index surface (`hashSurface()`), or 0 when not requested.
    std::uint32_t hashMicros = 0u;
    /// Drawsegs the walk produced for the viewpoint.
    std::uint16_t drawsegs = 0u;
    /// True when `hash` holds the SHA-256 of the index surface as it was *before*
    /// the scene converted it in place.
    bool hashed = false;
    /// 64 hex characters plus NUL; empty when `hashed` is false.
    char hash[65] = {0};
    /// The pass that refused this frame, or "none" when it rendered. The names
    /// are the frame driver's own stages: "walk", "walls", "planes", "sprites",
    /// "masked", plus "player" for a world with no avatar. D4.2g: a refused frame
    /// leaves the previous picture on the panel, and this is what the one
    /// diagnostic line names.
    const char* refusedStage = "none";
    /// That pass's error code, in that pass's own vocabulary; 0 when it rendered
    /// and -1 when the refusing stage has no code (the sprite-list build).
    int refusedCode = 0;
};

/**
 * @brief The console player, as the scene's serial line reports it (D4.2f).
 *
 * The fixed-point numbers are DOOM's own: 16.16 map units and a 32-bit binary
 * angle. They are printed raw, not converted, so a board line can be compared
 * with `tools/movement_check.py --player`'s `TIC` lines without a second unit
 * convention. `tics` is the level's own `leveltime` -- the simulation's clock,
 * not the frame counter and not the scene's tic index.
 */
struct ScenePlayerState {
    bool valid = false;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t viewz = 0;
    std::uint32_t angle = 0;
    std::int32_t health = 0;
    std::uint32_t tics = 0;
};

/**
 * @brief The shipped frame's window onto the level (D4.0 / D4.2f).
 *
 * This is the module the scene talks to, not the probe's `frameProbeRun()`:
 * `open()` loads E1M1 once and keeps the container mapping, the walk state, the
 * sprite list and the level's world live across frames; `runTics()` advances the
 * world at DOOM's 35 Hz; `render()` draws the player's viewpoint into an 8bpp
 * surface as DOOM palette indices.
 *
 * It deliberately owns no buffers of its own. The walk state, the clip pools and
 * the baked manifest adapter are the ones `FrameDriver.cpp` already declares; a
 * second copy would double 71 KB of `.bss` and could let the two frames drift.
 * This class owns the lifecycle (open/close), the tic hand-off and the answers
 * the scene needs, and delegates every byte to the session the probe also uses.
 *
 * The class compiles in every environment, but `open()` succeeds only where
 * `PIXELROOT32_DOOM_SHOW_E1M1` is defined and the build has the `wad` partition:
 * the level is read from mapped flash, which is why the `_show` environment is an
 * ESP32 one. Everywhere else `open()` returns false and the scene keeps painting
 * its shell, so a missing flag is a visible fallback instead of a link error.
 */
class SceneRenderer {
public:
    /**
     * @brief Opens the container, loads E1M1, builds the PLAYPAL tables and
     *        loads the level's world.
     *
     * Prints one `SHOW,setup,...` line (from the session it delegates to), one
     * `SHOW,mem,...` line from `frameLevelFormatMemory()` and the two
     * `SHOW,arena,...` lines the world's measurement prints. Idempotent: a
     * second call on an open renderer returns true and does nothing.
     *
     * @return true when the session and the world are open and the palette
     *         tables were installed.
     */
    bool open();

    /**
     * @brief Runs @p tics world tics with the buttons held this frame.
     *
     * The scene decides how many tics elapsed from its tic clock, so simulation
     * time is DOOM's 35 Hz whatever the frame rate is. A frame that covers two
     * tics passes 2 and the world runs both, in order; a frame that covers none
     * passes 0 and nothing runs. Edge-triggered actions (the use latch) are
     * P_PlayerThink's, not this call's.
     *
     * @param tics    Tics elapsed since the previous call.
     * @param buttons The engine's six bits, packed as `INPUT,buttons` prints them.
     * @return false when the world is closed or a tic refused to run.
     */
    bool runTics(std::uint32_t tics, std::uint8_t buttons);

    /**
     * @brief Renders the player's viewpoint into a caller-owned 8bpp surface.
     *
     * The surface receives DOOM palette indices, not colours: on the panel the
     * engine's installed PLAYPAL table expands them during the present, and on
     * the desktop the scene expands them itself because that surface has no
     * 8-bit buffer. The hash, when requested, is taken here -- before any caller
     * expansion -- so it can be compared with the host's `framehash`.
     *
     * @param indexPixels Surface bytes, at least width * height.
     * @param width       Stride in bytes; must be >= R_SCREEN_WIDTH.
     * @param height      Rows; must be >= R_VIEW_HEIGHT.
     * @param wantHash    Take the SHA-256 of the index surface.
     * @param out         Receives the measurements; may be NULL.
     * @return false when the renderer is closed, the world has no player, or a
     *         pass refused the frame.
     */
    bool render(std::uint8_t* indexPixels, int width, int height, bool wantHash,
                SceneFrameMeasurements* out);

    /**
     * @brief The console player's state, for the scene's serial line.
     *
     * @return A snapshot; `valid` is false when the renderer or the world is
     *         closed.
     */
    ScenePlayerState player() const;

    /**
     * @brief Renders the host harness's frame 00 through the view entry point.
     *
     * The byte-identity proof D4.2f owes: `frameLevelRenderView()` is the only
     * render path now, and rendering the harness's own viewpoint through it must
     * reproduce the host's frame-00 hash character for character. Prints one
     * `SHOW,frame00,...` line with the hash and the viewpoint it used, so the
     * check is in the boot log and not only in a report.
     *
     * @param indexPixels Surface bytes, at least 320x200 (it takes the game's
     *                    surface, and the next frame overwrites it).
     * @param width       Stride in bytes.
     * @param height      Rows.
     * @return true when the frame was rendered and hashed.
     */
    bool verifySpawnFrame(std::uint8_t* indexPixels, int width, int height);

    /// Releases the world, the mapping and the heap blocks the session took.
    /// Safe twice.
    void close();

    bool isOpen() const { return open_; }

    /// The level marker the session opened ("E1M1"), empty before `open()`.
    const char* level() const { return marker_; }

private:
    bool open_ = false;
    char marker_[5] = {0, 0, 0, 0, 0};
};

}  // namespace doomc
}  // namespace doom
