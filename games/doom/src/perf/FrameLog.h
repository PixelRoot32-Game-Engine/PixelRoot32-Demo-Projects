#pragma once

#include <cstdint>

namespace doom {
namespace perf {

#if defined(PIXELROOT32_DOOM_FRAME_LOG)

/**
 * @brief Allocation-free per-frame CSV recorder for the frame-pacing harness.
 *
 * Enabled only when `PIXELROOT32_DOOM_FRAME_LOG` is defined
 * (`env:esp32dev_ILI9341_perf`). With the macro undefined the class below is an
 * empty, inline no-op and the translation unit emits no code at all.
 *
 * The recorder streams one line per completed frame through a caller-supplied
 * sink. It formats into a fixed stack buffer with `snprintf`; there is no
 * allocation, no container, no floating point and no platform header, so the
 * same class is exercised by a host test and used by the panel build.
 *
 * The exact line, including the trailing newline, is:
 *
 *     FRAME,i=%u,tic=%u,render_us=%u,present_us=%u,free_heap=%u\n
 *
 * Fields, and exactly what each one does and does not include:
 *
 *  - `i` -- frame index since the first `beginFrame()` on this instance,
 *    starting at 0. It increments once per emitted line, never per sample.
 *
 *  - `tic` -- the 35 Hz tic the simulation clock reported for this frame's
 *    update. The caller captures it after polling `TicClock` in `update()`.
 *    It is a counter, printed unsigned.
 *
 *  - `render_us` -- `endDraw.micros - beginFrame.micros`: the time the scene
 *    spent in one `update()` plus the `draw()` that followed. Concretely it
 *    includes the base `Scene::update()`, the tic poll, the input sample, and
 *    all of `DoomScene::draw()` -- and, because the engine calls it between
 *    those two hooks, `Renderer::beginFrame()`. It does NOT include:
 *    `present()`, `flushPendingTransfers()`, `drawer->processEvents()`,
 *    `sceneManager.aggregateShouldRedrawFramebuffer()`, the pre-scene part of
 *    `Engine::update()` (`inputManager.update()`), the engine's post-draw
 *    transition/debug work, or the frame-log write itself. A frame that never
 *    reaches `endDraw()` reports 0; its whole period then lands in
 *    `present_us`.
 *
 *  - `present_us` -- the remainder of the frame period:
 *    `(nextFrameStart - thisFrameStart) - render_us`, saturating at 0. It is a
 *    residual, not a measurement of the SPI transfer: it contains everything
 *    in the loop that is not the scene's own work -- `present()` and its DMA
 *    drain, `processEvents()`, the engine's next input update, the redraw
 *    query, and any scheduler time between the two samples. When
 *    `render_us >= period` the subtraction clamps to 0 rather than wrapping.
 *
 *  - `free_heap` -- a caller-supplied heap reading sampled at the end of the
 *    frame's `draw()`. The class never reads a platform API itself; the panel
 *    build passes `ESP.getFreeHeap()`, the desktop mirror passes 0 (no ESP32
 *    heap exists there, so 0 is a sentinel, not a measurement). Printed
 *    unsigned.
 *
 * Emission timing: the line for frame N is written by `endFrame()` at the top
 * of frame N+1's `update()`, using the boundary sample taken immediately
 * before the write. The caller then takes frame N+1's start sample *after* the
 * write. The serial cost of the record is therefore bracketed out of both
 * frame N's period and frame N+1's render interval instead of being charged
 * to either; if the sink blocks, that excluded gap grows and is invisible in
 * the CSV. Because the closing sample comes from the next frame, N frames
 * produce N-1 lines: the final frame of a run has no successor to bound it and
 * is never emitted.
 *
 * Resolution and honesty limits: timestamps are the caller's 32-bit
 * microsecond samples, subtracted with unsigned arithmetic. A difference is
 * correct across the `micros()` wrap (~71.6 minutes) as long as one frame is
 * shorter than 2^32 us, which every frame in this port is. The instrument
 * cannot separate the SPI wire time from other engine overhead inside
 * `present_us`, cannot measure the DMA tail that continues after `present()`
 * returns once the drain flag is absent, and cannot attribute CPU time to
 * individual engine stages. Those are exactly the limits the phase needs to
 * know about, not numbers this recorder can invent.
 */
class FrameLog {
public:
    /// True in the build that compiles the recorder.
    static constexpr bool kEnabled = true;

    /// Receives one complete line, including its trailing newline.
    using Sink = void (*)(const char* line);

    FrameLog() = default;

    explicit FrameLog(Sink sink) : sink_(sink) {}

    /// Replaces the sink; a null sink suppresses output but keeps the record.
    void setSink(Sink sink) { sink_ = sink; }

    /**
     * @brief Opens a frame at `micros` with the tic the clock reported for it.
     *
     * Must be called once per frame, after the previous frame's `endFrame()`.
     * Calling it while a frame is already open abandons that frame without
     * emitting a line.
     */
    void beginFrame(std::uint32_t micros, std::uint32_t tic);

    /**
     * @brief Records the end of the scene's draw work for the open frame.
     *
     * @param micros   Sample taken at the end of `DoomScene::draw()`.
     * @param freeHeap Heap reading for that same instant.
     */
    void endDraw(std::uint32_t micros, std::uint32_t freeHeap);

    /**
     * @brief Closes the open frame at `micros` and emits its CSV line.
     *
     * @param micros Boundary sample taken at the top of the next `update()`,
     *               before the line is written. A call with no open frame does
     *               nothing and emits nothing.
     */
    void endFrame(std::uint32_t micros);

private:
    /// Formats and sinks one line; `periodMicros` is the closed frame's period.
    void emit(std::uint32_t periodMicros);

    Sink sink_ = nullptr;
    std::uint32_t frameStartMicros_ = 0;
    std::uint32_t renderMicros_ = 0;
    std::uint32_t tic_ = 0;
    std::uint32_t freeHeap_ = 0;
    std::uint32_t frameIndex_ = 0;
    bool open_ = false;
};

#else

/**
 * @brief The disabled form: an empty class whose inline methods compile away.
 *
 * The shipped environments never define `PIXELROOT32_DOOM_FRAME_LOG`; they get
 * this definition, with no data members and no out-of-line code. `DoomScene`
 * also guards its member and every call site with the same macro, so a shipped
 * build contains no recorder, no extra `micros()` read and no heap read.
 */
class FrameLog {
public:
    static constexpr bool kEnabled = false;

    using Sink = void (*)(const char* line);

    FrameLog() = default;

    explicit FrameLog(Sink) {}

    void setSink(Sink) {}

    void beginFrame(std::uint32_t, std::uint32_t) {}

    void endDraw(std::uint32_t, std::uint32_t) {}

    void endFrame(std::uint32_t) {}
};

#endif

}  // namespace perf
}  // namespace doom
