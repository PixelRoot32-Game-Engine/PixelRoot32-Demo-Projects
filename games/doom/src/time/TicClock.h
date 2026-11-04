#pragma once

#include <cstdint>

namespace doom {
namespace time {

/**
 * @brief DOOM's 35 Hz tic, derived from a monotonic microsecond source.
 *
 * The tic is not counted, it is computed: for an elapsed time `t` the answer is
 * exactly `t * 35 / 1000000`, evaluated from the sample itself. Summing a
 * per-call delta with truncation would lose up to one microsecond every update
 * (and with it a tic every ~28,571 updates); deriving from the base cannot
 * drift, because every call recomputes the same quotient from the same origin.
 *
 * The caller owns the microsecond source and hands the class its raw value.
 * That value is 32-bit on both shipped targets -- `micros()` on the board, the
 * truncated steady clock on the desktop mirror -- and wraps every ~71 minutes,
 * so the clock extends it to a 64-bit count:
 *
 *  - each sample is compared with the previous one as a signed 32-bit delta, so
 *    a counter that wrapped reads as a small step forward, not as a huge one;
 *  - a sample that went backwards (a caller repeating an older reading) is
 *    ignored: the clock holds its high-water mark and never reports a lower
 *    tic. That tolerates reordering instead of rewinding the simulation;
 *  - a genuine gap longer than 2^31 us (~35.8 minutes) is indistinguishable
 *    from a backwards jump in one 32-bit sample and is therefore held, not
 *    extrapolated. No frame in this port runs for half an hour.
 *
 * The same arithmetic yields the fraction of the current tic for free: it is
 * the remainder of that division, in millionths of a tic (0..999999). No
 * floating point appears anywhere in this file, and there is no allocation,
 * no container and no platform header, so a host test can drive it directly.
 *
 * The class holds a single tic counter, so an instance is tied to one monotonic
 * source for its lifetime. `reset()` re-anchors it and is what restarting the
 * scene does; the first sample after it is tic 0.
 */
class TicClock {
public:
    /// Simulated tics per second. DOOM's simulation runs at this rate.
    static constexpr std::uint32_t kTicsPerSecond = 35;

    /// Microseconds in a second: the denominator of the tic derivation.
    static constexpr std::uint32_t kMicrosPerSecond = 1000000;

    /// A clock that has not seen a sample yet; the first `update()` starts it.
    TicClock() = default;

    /// A clock anchored at `micros`, which is tic 0.
    explicit TicClock(std::uint32_t micros) { reset(micros); }

    /**
     * @brief Anchors the clock at `micros` and rewinds it to tic 0.
     *
     * Call this once when the simulation starts, and again only to restart it:
     * the sample becomes the origin, so tics are elapsed time since this call.
     */
    void reset(std::uint32_t micros);

    /**
     * @brief Feeds one monotonic microsecond sample and returns the tic.
     *
     * A sample earlier than the high-water mark (an out-of-order reading, or a
     * source that reset itself) leaves the clock where it is; a sample that
     * moved forward advances it by exactly that many microseconds. The return
     * value never decreases across the life of the instance.
     *
     * The first call on a default-constructed clock anchors it at `micros` and
     * returns 0, so the caller does not need a separate `reset()` to start.
     */
    std::uint32_t update(std::uint32_t micros);

    /// Tic index since `reset()`. Non-decreasing for the life of the instance.
    std::uint32_t tic() const { return tic_; }

    /// Fraction of the current tic, in millionths: 0..999999, exactly.
    std::uint32_t phase() const { return phase_; }

    /// Microseconds since `reset()`, extended past the source's 32-bit range.
    std::uint64_t elapsedMicros() const { return elapsed_; }

private:
    /// Recomputes `tic_` and `phase_` from `elapsed_`. Exact, no accumulation.
    void derive();

    /// Microseconds since the origin, extended to 64 bits. The count itself
    /// outlives the hardware; `derive()` multiplies it by 35, which bounds it at
    /// 2^64 / 35 us, about 16,700 years of uptime.
    std::uint64_t elapsed_ = 0;

    /// Last forward sample, kept at the high-water mark, in source units.
    std::uint32_t lastRaw_ = 0;

    /// Largest tic reached, and its fractional position, in millionths.
    std::uint32_t tic_ = 0;
    std::uint32_t phase_ = 0;

    /// False until the first sample, so `update()` can anchor a fresh clock.
    bool started_ = false;
};

}  // namespace time
}  // namespace doom
