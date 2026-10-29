#include "time/TicClock.h"

namespace doom {
namespace time {

void TicClock::reset(std::uint32_t micros) {
    elapsed_ = 0;
    lastRaw_ = micros;
    tic_ = 0;
    phase_ = 0;
    started_ = true;
}

std::uint32_t TicClock::update(std::uint32_t micros) {
    if (!started_) {
        reset(micros);
        return tic_;
    }

    // Unsigned subtraction first, then read back as a signed delta: that is the
    // modulo-2^32 step between the samples, and the sign is what separates "the
    // counter wrapped forward" (small positive delta) from "the caller repeated
    // an older reading" (negative delta). Computing an unsigned delta instead
    // would turn one out-of-order sample into a ~71 minute jump.
    const std::int32_t delta = static_cast<std::int32_t>(micros - lastRaw_);

    // A repeated or out-of-order sample is not time passing. Keep the
    // high-water mark unchanged, so the source has to pass it again to move the
    // clock -- and the tic count can never decrease.
    if (delta <= 0) {
        return tic_;
    }

    elapsed_ += static_cast<std::uint64_t>(static_cast<std::uint32_t>(delta));
    lastRaw_ = micros;
    derive();
    return tic_;
}

void TicClock::derive() {
    // Exact by construction: the quotient is recomputed from the whole elapsed
    // interval on every sample, so no truncated remainder is ever carried or
    // dropped. The multiplication cannot overflow a 64-bit count within any
    // realistic uptime (see elapsed_ in the header).
    const std::uint64_t scaled = elapsed_ * kTicsPerSecond;
    tic_ = static_cast<std::uint32_t>(scaled / kMicrosPerSecond);
    phase_ = static_cast<std::uint32_t>(scaled % kMicrosPerSecond);
}

}  // namespace time
}  // namespace doom
