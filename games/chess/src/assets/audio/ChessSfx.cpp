/*
 * ChessSfx.cpp - The sound of a chess game, built from AudioEvents.
 *
 * There are no InstrumentPresets here on purpose. A preset is a 17-field
 * aggregate, and AudioEvent::preset sits at field 7 rather than last, so a
 * positional brace-init that looks right silently produces the wrong sound
 * instead of failing to build. Four short effects do not need ADSR shaping that
 * a sweep or a pitch envelope cannot express, so every field is assigned by
 * name and the trap never comes up.
 */
#include "assets/audio/ChessSfx.h"

#if PIXELROOT32_ENABLE_AUDIO

#include <core/Engine.h>

#include <pixelroot32/apu/AudioTypes.h>

namespace pr32 = pixelroot32;

// Scenes reach the engine through the platform-owned instance; see the
// platforms/ headers.
extern pr32::core::Engine engine;

#endif  // PIXELROOT32_ENABLE_AUDIO

namespace chessdemo {

#if PIXELROOT32_ENABLE_AUDIO

namespace {

using pr32::audio::AudioEvent;
using pr32::audio::SfxBreakpoint;
using pr32::audio::SweepCurve;
using pr32::audio::WaveType;

// Breakpoint tables are held by pointer for the life of the voice, so they must
// outlive the call that queues them - hence static constexpr, never locals.

/**
 * Check: D5 stepping up to A5. Pitch envelopes hold between points rather than
 * gliding, so two points read as two notes, which is what makes this an alert
 * and not just another blip.
 */
constexpr SfxBreakpoint kCheckPitch[] = {
    { 0.000f, 587.33f },  // D5
    { 0.075f, 880.00f }   // A5
};

/**
 * Checkmate: a four-step descent through a minor shape. Four is the ceiling -
 * kMaxSfxPitchPoints is 4 - and it is exactly enough to land as "this is over".
 */
constexpr SfxBreakpoint kMatePitch[] = {
    { 0.00f, 523.25f },  // C5
    { 0.13f, 415.30f },  // G#4
    { 0.26f, 349.23f },  // F4
    { 0.39f, 261.63f }   // C4
};

/** A piece set down: one short wooden blip, quiet enough to hear 40 times. */
AudioEvent moveEvent() {
    AudioEvent e{};
    e.type             = WaveType::TRIANGLE;
    e.frequency        = 392.00f;  // G4
    e.duration         = 0.055f;
    e.volume           = 0.30f;
    e.duty             = 0.0f;
    e.sweepEndHz       = 294.00f;  // D4 - the small drop is the "set down"
    e.sweepDurationSec = 0.055f;
    return e;
}

/** Capture, layer 1: the impact. */
AudioEvent captureNoise() {
    AudioEvent e{};
    e.type        = WaveType::NOISE;
    e.frequency   = 1800.0f;  // NOISE frequency is the LFSR clock, not a pitch
    e.duration    = 0.080f;
    e.volume      = 0.45f;
    e.duty        = 0.0f;
    e.noisePeriod = 0;        // 0 = derive the period from the clock
    return e;
}

/** Capture, layer 2: the weight underneath it. */
AudioEvent captureBody() {
    AudioEvent e{};
    e.type             = WaveType::PULSE;
    e.frequency        = 260.0f;
    e.duration         = 0.130f;
    e.volume           = 0.42f;
    e.duty             = 0.5f;
    e.sweepEndHz       = 90.0f;
    e.sweepDurationSec = 0.130f;
    e.sweepCurve       = SweepCurve::Exponential;  // geometric fall reads as a thud
    return e;
}

/** Check: a thin two-note rise that cuts through the move sound under it. */
AudioEvent checkEvent() {
    AudioEvent e{};
    e.type               = WaveType::PULSE;
    e.frequency          = 587.33f;
    e.duration           = 0.170f;
    e.volume             = 0.45f;
    e.duty               = 0.25f;  // narrow duty = reedy, carries over the blip
    e.pitchEnvelope      = kCheckPitch;
    e.pitchEnvelopeCount = 2;
    return e;
}

/** Checkmate, layer 1: the descending figure. */
AudioEvent mateLead() {
    AudioEvent e{};
    e.type               = WaveType::PULSE;
    e.frequency          = 523.25f;
    e.duration           = 0.620f;
    e.volume             = 0.50f;
    e.duty               = 0.5f;
    e.pitchEnvelope      = kMatePitch;
    e.pitchEnvelopeCount = 4;
    return e;
}

/** Checkmate, layer 2: a low pedal so the ending has some floor to it. */
AudioEvent mateBass() {
    AudioEvent e{};
    e.type      = WaveType::TRIANGLE;
    e.frequency = 130.81f;  // C3
    e.duration  = 0.700f;
    e.volume    = 0.35f;
    e.duty      = 0.0f;
    return e;
}

}  // namespace

void playSfx(SfxId id) {
    // Effects share the four-slot SFX subpool (voices 4-7) and can only steal
    // from each other, so a two-layer sound never interrupts music.
    auto& audio = engine.getAudioEngine();

    switch (id) {
        case SfxId::Move:
            audio.playEvent(moveEvent());
            break;

        case SfxId::Capture:
            audio.playEvent(captureNoise());
            audio.playEvent(captureBody());
            break;

        case SfxId::Check:
            audio.playEvent(checkEvent());
            break;

        case SfxId::Checkmate:
            audio.playEvent(mateLead());
            audio.playEvent(mateBass());
            break;

        default:
            break;
    }
}

#else  // PIXELROOT32_ENABLE_AUDIO

void playSfx(SfxId id) {
    // Audio compiled out: call sites stay unguarded and simply make no sound.
    (void)id;
}

#endif  // PIXELROOT32_ENABLE_AUDIO

}  // namespace chessdemo
