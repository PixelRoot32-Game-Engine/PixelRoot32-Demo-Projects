#pragma once

#include <Arduino.h>

#include <cstdint>

#include "time/TicClock.h"

namespace doom {
namespace time {

/**
 * @brief One microsecond sample from the Arduino core, in the clock's own range.
 *
 * `micros()` is `unsigned long` (32-bit on ESP32) and wraps every ~71 minutes.
 * The cast is the truncation the clock expects, so the wrap stays visible to
 * `TicClock` instead of being hidden by a wider type.
 */
inline std::uint32_t esp32Micros() {
    return static_cast<std::uint32_t>(micros());
}

/**
 * @brief The ESP32 adapter for `TicClock`: sample `micros()`, return the tic.
 *
 * Kept as a header so a build that never polls the clock pays nothing for it:
 * no translation unit, no static storage, no constructor to run at boot.
 *
 * `poll()` is the whole contract -- read the source once, feed that one sample
 * to the clock, and hand back the tic it produced. `micros()` returns the same
 * sample, so a caller that logs a tic can log the reading that caused it
 * without taking a second one (which could already belong to the next tic).
 *
 * DoomScene does not use this class yet: the shell owns its `TicClock`
 * directly and takes the sample through `esp32Micros()`, because it also
 * renders through a platform-branched path. The class is the shape the real
 * game loop will use, where there is exactly one clock and one caller.
 */
class Esp32TicSource {
public:
    /// Anchors the clock at the current `micros()` reading; that sample is tic 0.
    void reset() {
        sample_ = esp32Micros();
        clock_.reset(sample_);
        tic_ = 0;
    }

    /// Samples `micros()` once, advances the clock, and returns its tic.
    std::uint32_t poll() {
        sample_ = esp32Micros();
        tic_ = clock_.update(sample_);
        return tic_;
    }

    /// The microsecond reading behind the last `poll()`/`reset()`.
    std::uint32_t micros() const { return sample_; }

    /// The tic the last `poll()` returned.
    std::uint32_t tic() const { return tic_; }

private:
    TicClock clock_;
    std::uint32_t sample_ = 0;
    std::uint32_t tic_ = 0;
};

}  // namespace time
}  // namespace doom
