#pragma once

/*
 * The cue -> APU event translation, in one header, so the firmware and the
 * host gate build the *same* event from the *same* code.
 *
 * WHY THIS FILE EXISTS
 *
 * T46 gates the bank by rendering each cue through the real ApuCore and
 * comparing the result with the WAD's original sample. A harness that rebuilt
 * the event its own way would prove only that the harness agrees with itself:
 * the property under test is "the cue the firmware plays", so the translation
 * has to be shared. `DoomAudio.cpp` includes this header instead of carrying
 * its own `makeEvent()`; `tools/sfx_render.cpp` includes the same header.
 *
 * WHAT CROSSES
 *
 * `SfxCue` is engine-free and APU-free on purpose (SfxBank.h). This header is
 * the one place in `src/doomc` that knows both: it maps the cue's wave to
 * `WaveType`, applies the runtime volume and pitch to the cue's numbers, and
 * copies the breakpoint tables into a caller-owned `SfxVoiceScratch` so the
 * event's pointers outlive the call without allocating.
 *
 * WHY THE PRESET
 *
 * `AudioEvent` takes ADSR through `InstrumentPreset` and nowhere else. The
 * bank's 109 presets are generated from the same table as the cues (SfxBank.cpp)
 * and looked up by the cue's identity; this header never owns one.
 *
 * FIELD ORDER IS THE HAZARD
 *
 * `AudioEvent` has 15 fields and `preset` sits at position 7, after
 * `noisePeriod`. A positional initialiser past `duty` silently shifts every
 * field after it (pixelroot32-audio skill, gotcha 13), and `frequency,
 * duration, volume` is a different order than `frequency, volume, duration`
 * (gotcha 14). Hence named assignment below, never a brace list.
 */

#include <cstdint>

#include "doomc/SfxBank.h"

#include <pixelroot32/apu/AudioMusicTypes.h>
#include <pixelroot32/apu/AudioTypes.h>

namespace doom {
namespace doomc {

/// Per-voice storage for the two breakpoint tables an event may point at.
/// One per logical SFX voice, owned by DoomAudio's voice table and by the
/// host harness; the APU keeps only a pointer, so this must outlive the call
/// that plays the event.
struct SfxVoiceScratch {
    pixelroot32::audio::SfxBreakpoint pitch[kSfxMaxBreakpoints];
    std::uint8_t pitchCount;
    pixelroot32::audio::SfxBreakpoint duty[kSfxMaxBreakpoints];
    std::uint8_t dutyCount;
};

/**
 * @brief The preset for a cue, or NULL when @p cue is not from the bank.
 *
 * Declared here (not in SfxBank.h) because the preset type lives in the APU;
 * defined in SfxBank.cpp next to the table it indexes.
 */
const pixelroot32::audio::InstrumentPreset* sfxBankPreset(const SfxCue* cue);

/**
 * @brief Builds the APU event for one cue with an explicit preset.
 *
 * The public entry point looks the preset up from the cue; the host gate's
 * `--perturb`/`--stub` paths need to perturb a copy of the cue while keeping
 * (or replacing) the preset, which is impossible through pointer identity
 * alone. The firmware never calls this overload.
 */
inline pixelroot32::audio::AudioEvent sfxCueEventWithPreset(
    const SfxCue& cue, const pixelroot32::audio::InstrumentPreset* preset,
    int volume, int pitch, SfxVoiceScratch& scratch) {
    using pixelroot32::audio::AudioEvent;
    using pixelroot32::audio::SfxBreakpoint;
    using pixelroot32::audio::SweepCurve;
    using pixelroot32::audio::WaveType;

    auto wave = WaveType::PULSE;
    switch (cue.wave) {
        case SfxWave::Pulse:
            wave = WaveType::PULSE;
            break;
        case SfxWave::Triangle:
            wave = WaveType::TRIANGLE;
            break;
        case SfxWave::Noise:
            wave = WaveType::NOISE;
            break;
        case SfxWave::Sine:
            wave = WaveType::SINE;
            break;
        case SfxWave::Saw:
            wave = WaveType::SAW;
            break;
    }

    // DOOM's pitch is a multiplier in 1/128ths. Clamp it so a frequency can
    // never reach 0 (the APU derives a period from it) and so a pathological
    // caller cannot ask for 100x.
    float pitchScale = static_cast<float>(pitch) / 128.0f;
    if (pitchScale < 0.0625f) {
        pitchScale = 0.0625f;
    } else if (pitchScale > 4.0f) {
        pitchScale = 4.0f;
    }

    AudioEvent event{};
    event.type = wave;
    event.frequency = cue.frequencyHz * pitchScale;
    if (event.frequency < 20.0f) {
        event.frequency = 20.0f;
    }
    event.duration = cue.durationSec;
    event.volume = cue.volume * (static_cast<float>(volume) / 127.0f);
    if (event.volume < 0.0f) {
        event.volume = 0.0f;
    } else if (event.volume > 1.0f) {
        event.volume = 1.0f;
    }
    event.duty = cue.duty;
    event.noisePeriod = cue.noisePeriod;
    event.preset = preset;
    event.loop = false;
    event.sweepCurve = SweepCurve::Linear;

    // A pitch table supersedes the single-segment sweep (the APU's own rule),
    // and its values are the cue's at the requested pitch.
    scratch.pitchCount = 0u;
    if (cue.pitchCount >= 2u) {
        const std::uint8_t count =
            cue.pitchCount > kSfxMaxBreakpoints
                ? static_cast<std::uint8_t>(kSfxMaxBreakpoints)
                : cue.pitchCount;
        for (std::uint8_t index = 0; index < count; ++index) {
            scratch.pitch[index].timeSec = cue.pitch[index].timeSec;
            scratch.pitch[index].value = cue.pitch[index].value * pitchScale;
        }
        scratch.pitchCount = count;
        event.pitchEnvelope = scratch.pitch;
        event.pitchEnvelopeCount = count;
        event.sweepEndHz = 0.0f;
        event.sweepDurationSec = 0.0f;
    } else if (cue.sweepEndHz > 0.0f) {
        event.sweepEndHz = cue.sweepEndHz * pitchScale;
        event.sweepDurationSec = cue.durationSec;
        event.sweepCurve = SweepCurve::Exponential;
    }

    scratch.dutyCount = 0u;
    if (cue.dutyCount > 0u) {
        const std::uint8_t count =
            cue.dutyCount > kSfxMaxBreakpoints
                ? static_cast<std::uint8_t>(kSfxMaxBreakpoints)
                : cue.dutyCount;
        for (std::uint8_t index = 0; index < count; ++index) {
            scratch.duty[index].timeSec = cue.dutyPoints[index].timeSec;
            scratch.duty[index].value = cue.dutyPoints[index].value;
        }
        scratch.dutyCount = count;
        event.dutySteps = scratch.duty;
        event.dutyStepCount = count;
    }

    return event;
}

/**
 * @brief Builds the APU event for one bank cue at one volume/pitch.
 *
 * @param volume DOOM's 0..127 volume, scaled into the cue's own volume.
 * @param pitch DOOM's 0..255 pitch (128 = the cue as authored).
 * @param scratch Breakpoint storage the event points into; must outlive it.
 */
inline pixelroot32::audio::AudioEvent sfxCueEvent(const SfxCue& cue,
                                                  int volume, int pitch,
                                                  SfxVoiceScratch& scratch) {
    return sfxCueEventWithPreset(cue, sfxBankPreset(&cue), volume, pitch, scratch);
}

}  // namespace doomc
}  // namespace doom
