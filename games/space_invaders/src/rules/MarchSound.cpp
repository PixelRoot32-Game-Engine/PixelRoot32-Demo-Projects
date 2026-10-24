#include "rules/MarchSound.h"

#include "rules/RulesConstants.h"

namespace spaceinvaders::rules {

namespace {

/// Aliens a freshly spawned rack holds; reset() opens on this interval.
constexpr uint8_t MarchFullRackAliens =
    static_cast<uint8_t>(RackRows * RackCols);

}  // namespace

void MarchSound::reset() {
    interval = intervalTicks(MarchFullRackAliens);
    countdown = interval;
    nextTone = 0;
    lastTone = 0;
}

bool MarchSound::tick(uint8_t aliveAliens) {
    // A cleared rack has no march: leave the countdown and the rotation where
    // they are, so the beat cannot fire into the gap between two waves.
    if (aliveAliens == 0) {
        return false;
    }

    // A beat always reloads the countdown, so zero is only reachable at the
    // start of a run if a future caller resets differently; reload defensively.
    if (countdown == 0) {
        interval = intervalTicks(aliveAliens);
        countdown = interval;
    }

    --countdown;
    if (countdown != 0) {
        return false;
    }

    // The voice this beat uses is latched before the rotation, so the caller
    // reads the tone that just fired and the next beat takes the next voice.
    lastTone = nextTone;
    nextTone = static_cast<uint8_t>((nextTone + 1u) % MarchToneCount);

    // The live count on the beat tick picks the gap to the next beat. A count
    // that changes mid-gap is therefore heard from the following beat, never as
    // a truncated gap after a kill.
    interval = intervalTicks(aliveAliens);
    countdown = interval;
    return true;
}

int MarchSound::toneIndex() const {
    return lastTone;
}

uint16_t MarchSound::intervalTicks(uint8_t aliveAliens) {
    if (aliveAliens == 0) {
        return 0;
    }

    // The table descends, so the first key at or below the count is the band
    // the rack is in; counts above the first key clamp to the slowest band.
    for (int index = 0; index < MarchTableEntries; ++index) {
        if (aliveAliens >= MarchKeyAliens[index]) {
            return MarchIntervalTicks[index];
        }
    }

    return MarchIntervalTicks[MarchTableEntries - 1];
}

uint16_t MarchSound::lastIntervalTicks() const {
    return interval;
}

}  // namespace spaceinvaders::rules
