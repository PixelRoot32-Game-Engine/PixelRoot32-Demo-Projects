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
    /// Drawsegs the walk produced for the spawn viewpoint.
    std::uint16_t drawsegs = 0u;
    /// True when `hash` holds the SHA-256 of the index surface as it was *before*
    /// the scene converted it in place.
    bool hashed = false;
    /// 64 hex characters plus NUL; empty when `hashed` is false.
    char hash[65] = {0};
};

/**
 * @brief The shipped frame's window onto the level (D4.0).
 *
 * This is the module the scene talks to, not the probe's `frameProbeRun()`:
 * `open()` loads E1M1 once and keeps the container mapping, the walk state and
 * the sprite list live across frames; `render()` draws the host harness's frame
 * 00 -- the spawn viewpoint -- into an 8bpp surface as DOOM palette indices.
 *
 * It deliberately owns no buffers of its own. The walk state, the clip pools and
 * the baked manifest adapter are the ones `FrameDriver.cpp` already declares; a
 * second copy would double 71 KB of `.bss` and could let the two frames drift.
 * This class owns the lifecycle (open/close) and the answers the scene needs,
 * and delegates every byte to the session the probe also uses.
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
     * @brief Opens the container, loads E1M1 and builds the PLAYPAL colour tables.
     *
     * Prints one `SHOW,setup,...` line (from the session it delegates to) and one
     * `SHOW,mem,...` line from `frameLevelFormatMemory()`. Idempotent: a second
     * call on an open renderer returns true and does nothing.
     *
     * @return true when the session is open and the palette tables were installed.
     */
    bool open();

    /**
     * @brief Renders the spawn viewpoint into a caller-owned 8bpp surface.
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
     * @return false when the renderer is closed or a pass refused the frame.
     */
    bool render(std::uint8_t* indexPixels, int width, int height, bool wantHash,
                SceneFrameMeasurements* out);

    /// Releases the mapping and the heap blocks the session took. Safe twice.
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
