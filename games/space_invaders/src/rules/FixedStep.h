#pragma once

#include <cstdint>

namespace spaceinvaders::rules {

/**
 * @class FixedStep
 * @brief Fixed 60 Hz rules clock with a bounded catch-up (S15).
 *
 * The scene used to scale its timers by whatever delta the frame loop
 * delivered, so the game speed followed the render rate. Every rule now runs
 * on this clock instead: the caller hands it the frame delta and it reports
 * how many fixed ticks to run, always the same tick length, so simulated time
 * alone decides the game state.
 *
 * The catch-up is bounded on purpose. A stall (a slow frame, a breakpoint, a
 * stalled display) would otherwise queue dozens of ticks and either freeze the
 * frame or teleport the rack across the field; instead the surplus ticks are
 * dropped and only the sub-tick remainder survives.
 */
class FixedStep {
public:
    /// One tick is 16.667 ms: the 60 Hz period, in microseconds.
    static constexpr uint32_t TickMicros = 16667;
    /// Upper bound on ticks per frame; anything beyond is dropped.
    static constexpr int MaxTicksPerFrame = 5;
    /// Millisecond value handed to the legacy per-frame logic for one tick.
    static constexpr uint32_t TickMs = 16;

    /**
     * @brief Adds one frame delta and reports how many ticks to run.
     *
     * The delta is converted to microseconds and accumulated; the whole
     * ticks it completes are returned, capped at MaxTicksPerFrame, and the
     * remainder is kept for the next call. Base timing is not converted with
     * floats because the rules layer stays integer and deterministic.
     *
     * @param deltaMillis Frame delta since the previous call, in milliseconds.
     * @return Ticks to run this frame, in the range 0..MaxTicksPerFrame.
     */
    int advance(uint32_t deltaMillis);

    /**
     * @brief Clears the accumulator so a new run starts on a clean clock.
     */
    void reset();

    /**
     * @brief Returns the accumulated sub-tick time left for the next call.
     * @return Remainder in microseconds, always below TickMicros after advance.
     */
    uint32_t remainderMicros() const;

private:
    uint64_t accumulatorMicros = 0;  ///< Unconsumed time, always below one tick.
};

}  // namespace spaceinvaders::rules
