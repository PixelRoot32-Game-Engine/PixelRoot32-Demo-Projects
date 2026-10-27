/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "presentation/PongSfx.h"

#if PIXELROOT32_ENABLE_AUDIO

#include <core/Engine.h>

#include <pixelroot32/apu/AudioTypes.h>

namespace pr32 = pixelroot32;

#endif  // PIXELROOT32_ENABLE_AUDIO

namespace pong {

#if PIXELROOT32_ENABLE_AUDIO

namespace {

using pr32::audio::AudioEvent;
using pr32::audio::SfxBreakpoint;
using pr32::audio::SweepCurve;
using pr32::audio::WaveType;

/**
 * Win: a three-note major arpeggio C5-E5-G5.
 *
 * A pitch envelope holds between its points instead of gliding, so the three
 * entries read as three separate notes from one voice - the arpeggio fits in a
 * single SFX slot and cannot be cut short by its own second note. Three points
 * is also one under the engine's four-point ceiling, which leaves the table
 * small and the intent obvious.
 */
static constexpr SfxBreakpoint kWinArpeggio[] = {
    { 0.00f, 523.25f },  // C5
    { 0.12f, 659.25f },  // E5
    { 0.24f, 783.99f }   // G5
};

/**
 * Paddle: a short bright pulse at 660 Hz.
 *
 * 40 ms is the shortest cue here: a rally can produce several of these a
 * second, and anything longer would smear them into a drone.
 */
AudioEvent paddleHitEvent() {
    AudioEvent e{};
    e.type      = WaveType::PULSE;
    e.frequency = 660.0f;
    e.duration  = 0.040f;
    e.volume    = 0.35f;
    e.duty      = 0.5f;
    return e;
}

/**
 * Wall: a softer triangle at half the paddle's pitch.
 *
 * The wall is a secondary contact - the ball is still in play - so this sits
 * under the paddle cue instead of competing with it.
 */
AudioEvent wallBounceEvent() {
    AudioEvent e{};
    e.type      = WaveType::TRIANGLE;
    e.frequency = 330.0f;
    e.duration  = 0.030f;
    e.volume    = 0.28f;
    e.duty      = 0.0f;
    return e;
}

/**
 * Goal: a pulse falling 440 Hz -> 110 Hz over 250 ms.
 *
 * Exponential rather than linear because a geometric fall reads as a drop in
 * pitch, while a linear one spends most of its time in the top octave and then
 * arrives late. The point is the one event in a rally that should feel final.
 */
AudioEvent goalEvent() {
    AudioEvent e{};
    e.type             = WaveType::PULSE;
    e.frequency        = 440.0f;
    e.duration         = 0.250f;
    e.volume           = 0.50f;
    e.duty             = 0.5f;
    e.sweepEndHz       = 110.0f;
    e.sweepDurationSec = 0.250f;
    e.sweepCurve       = SweepCurve::Exponential;
    return e;
}

/**
 * Countdown: a thin high blip marking the serve timer.
 *
 * 25% duty keeps it reedy and distinct from the 50% duty of the paddle and the
 * goal, so the player can tell "wait" from "hit" without looking up.
 */
AudioEvent countdownEvent() {
    AudioEvent e{};
    e.type      = WaveType::PULSE;
    e.frequency = 880.0f;
    e.duration  = 0.060f;
    e.volume    = 0.40f;
    e.duty      = 0.25f;
    return e;
}

/**
 * Win: the arpeggio lead.
 *
 * The envelope's three points are the three notes; `frequency` is the first
 * one so a backend that ever ignores the envelope still starts on C5.
 */
AudioEvent winEvent() {
    AudioEvent e{};
    e.type               = WaveType::PULSE;
    e.frequency          = 523.25f;  // C5
    e.duration           = 0.360f;
    e.volume             = 0.50f;
    e.duty               = 0.5f;
    e.pitchEnvelope      = kWinArpeggio;
    e.pitchEnvelopeCount = 3;
    return e;
}

}  // namespace

void playSfx(pr32::core::Engine& engine, SfxId id) {
    auto& audio = engine.getAudioEngine();

    switch (id) {
        case SfxId::PaddleHit:
            audio.playEvent(paddleHitEvent());
            break;

        case SfxId::WallBounce:
            audio.playEvent(wallBounceEvent());
            break;

        case SfxId::Goal:
            audio.playEvent(goalEvent());
            break;

        case SfxId::Countdown:
            audio.playEvent(countdownEvent());
            break;

        case SfxId::Win:
            audio.playEvent(winEvent());
            break;

        default:
            break;
    }
}

#else  // PIXELROOT32_ENABLE_AUDIO

void playSfx(pixelroot32::core::Engine& engine, SfxId id) {
    // Audio compiled out: call sites stay unguarded and simply make no sound.
    (void)engine;
    (void)id;
}

#endif  // PIXELROOT32_ENABLE_AUDIO

}  // namespace pong
