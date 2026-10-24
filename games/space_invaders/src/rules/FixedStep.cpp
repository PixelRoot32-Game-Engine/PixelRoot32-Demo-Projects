#include "rules/FixedStep.h"

namespace spaceinvaders::rules {

int FixedStep::advance(uint32_t deltaMillis) {
    // Convert once, in integers: 32-bit milliseconds times 1000 overflows for
    // deltas above ~70 minutes, so the accumulator is 64-bit.
    accumulatorMicros += static_cast<uint64_t>(deltaMillis) * 1000u;

    uint64_t ticks = accumulatorMicros / TickMicros;
    accumulatorMicros %= TickMicros;

    // Drop the surplus rather than queueing it: the next frame starts from the
    // sub-tick remainder only, so a stall can never replay a burst of ticks.
    if (ticks > static_cast<uint64_t>(MaxTicksPerFrame)) {
        ticks = static_cast<uint64_t>(MaxTicksPerFrame);
    }

    return static_cast<int>(ticks);
}

void FixedStep::reset() {
    accumulatorMicros = 0;
}

uint32_t FixedStep::remainderMicros() const {
    return static_cast<uint32_t>(accumulatorMicros);
}

}  // namespace spaceinvaders::rules
