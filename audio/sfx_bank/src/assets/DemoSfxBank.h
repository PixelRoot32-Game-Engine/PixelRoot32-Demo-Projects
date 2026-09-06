/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

/**
 * @file DemoSfxBank.h
 * @brief Hand-written SFX bank in the shape `pixelroot32::audio::playSfxBank`
 *        expects.
 *
 * The PixelRoot32 Tool Suite exports a generated header with exactly this
 * static API (see `games/bomberbot/src/assets/audio/SfxBank.h`). This file is
 * the same contract written by hand so the whole of it is readable:
 *
 *   - `static uint8_t     layerCount(SfxId)`
 *   - `static AudioEvent  layerEvent(SfxId, uint8_t)`
 *   - `static uint8_t     sequenceStepCount(SfxId)`
 *   - `static SequenceStep sequenceStep(SfxId, uint8_t)`
 *       where `SequenceStep` has `float delaySec` and `AudioEvent event`
 *
 * `playSfxBank` plays every layer at t=0 through `AudioEngine::playEvent`, and
 * hands every step with `delaySec > 0` to the game's `SfxDelayScheduler`.
 * `name()` and `isLooping()` are extras this demo needs for its HUD; the
 * helper never looks at them.
 *
 * ---------------------------------------------------------------------------
 * `AudioEvent` FIELD ORDER — two traps that compile clean and sound wrong
 * ---------------------------------------------------------------------------
 * The declaration order in `<pixelroot32/apu/AudioTypes.h>` is:
 *
 *   1 type   2 frequency   3 duration   4 volume   5 duty   6 noisePeriod
 *   7 preset 8 sweepEndHz  9 sweepDurationSec  10 loop  11 sweepCurve
 *   12 dutySteps  13 dutyStepCount  14 pitchEnvelope  15 pitchEnvelopeCount
 *
 * Trap 1: it is `frequency, duration, volume` — NOT `frequency, volume,
 * duration`. All three are `float`, so swapping them is not a build error; it
 * is a 0.4-second note at 30% of the volume you asked for.
 *
 * Trap 2: `preset` is field 7, not the last field. Writing
 * `{type, freq, dur, vol, duty, &INSTR_X}` puts the preset pointer where
 * `noisePeriod` (a `uint8_t`) lives, so it either fails on a narrowing
 * conversion or, in the shapes that do compile, silently loses the preset.
 *
 * C++17 has no designated initialisers, so past `duty` this file assigns every
 * field by name through `makeEvent()`. Positional aggregate initialisation is
 * used only for `InstrumentPreset`, whose field order is documented as frozen
 * for exactly that reason.
 */

#include <audio/AudioMusicTypes.h>
#include <audio/AudioTypes.h>

#include <cstdint>

namespace sfx_bank {

/**
 * @brief ADSR/LFO presets referenced by the bank's events.
 *
 * `AudioEvent::preset` is a raw pointer the mixer dereferences on the audio
 * side, so every preset MUST outlive the event. `inline constexpr` at
 * namespace scope gives them static storage duration and one address across
 * the whole program.
 *
 * Positional order (`AudioMusicTypes.h`):
 *   baseVolume, duty, defaultOctave, defaultDuration, noisePeriod,
 *   attackTime, decayTime, sustainLevel, releaseTime
 * Everything after `releaseTime` keeps its default (no LFO, no duty sweep).
 */
namespace presets {

using pixelroot32::audio::InstrumentPreset;

/// Short percussive pulse: near-instant attack, fast decay, low sustain.
inline constexpr InstrumentPreset kPulseBlip{
    0.85f, 0.5f, 4, 0.0f, 0,
    0.001f, 0.040f, 0.15f, 0.040f};

/// Soft body tone used as the low half of layered hits.
inline constexpr InstrumentPreset kTriangleSoft{
    0.75f, 0.5f, 4, 0.0f, 0,
    0.002f, 0.080f, 0.35f, 0.080f};

/// Thin pulse for sweeps: sustains just long enough to hear the glide.
inline constexpr InstrumentPreset kPulseSweep{
    0.80f, 0.25f, 4, 0.0f, 0,
    0.001f, 0.050f, 0.10f, 0.050f};

/// Tight noise crack. `noisePeriod` 40 is a direct LFSR period, not a pitch.
inline constexpr InstrumentPreset kNoiseHit{
    0.70f, 0.5f, 4, 0.0f, 40,
    0.001f, 0.030f, 0.05f, 0.040f};

/// Long noise body with no sustain, so the tail is pure decay.
inline constexpr InstrumentPreset kNoiseBoom{
    0.85f, 0.5f, 4, 0.0f, 20,
    0.001f, 0.120f, 0.00f, 0.100f};

} // namespace presets

/**
 * @brief Breakpoint tables for the two automation-driven effects.
 *
 * `AudioEvent` holds these as pointer + count, so they have the same lifetime
 * rule as the presets above. `timeSec` must be non-decreasing, at most
 * `kMaxSfxDutySteps` (4) duty entries and `kMaxSfxPitchPoints` (4) pitch
 * entries, and a pitch envelope needs at least 2 points before the mixer
 * activates it.
 */
namespace tables {

using pixelroot32::audio::SfxBreakpoint;

/// Rising arpeggio inside a single voice: G4, C5, E5, C6.
inline constexpr SfxBreakpoint kPowerUpPitch[] = {
    {0.00f, 392.0f},
    {0.06f, 523.0f},
    {0.12f, 659.0f},
    {0.20f, 1046.0f}};

/// Duty steps give the looping alarm its timbre change without a second voice.
inline constexpr SfxBreakpoint kAlarmDuty[] = {
    {0.00f, 0.125f},
    {0.15f, 0.250f},
    {0.30f, 0.500f},
    {0.45f, 0.250f}};

} // namespace tables

/**
 * @enum SfxId
 * @brief Game-owned effect ids. `Count` closes the list and is never played.
 */
enum class SfxId : uint8_t {
    MenuBlip,    ///< One layer, no steps. The smallest entry a bank can have.
    CoinPair,    ///< One layer plus one delayed step: the scheduler's hello world.
    LaserSweep,  ///< One layer driven by `sweepEndHz` / `sweepDurationSec`.
    Impact,      ///< Three layers fired together at t=0.
    Explosion,   ///< Noise with a clock sweep, plus two delayed tail steps.
    PowerUp,     ///< One layer arpeggiated by a four-point `pitchEnvelope`.
    AlarmLoop,   ///< `loop = true`. Runs until STOP_CHANNEL or a voice steal.
    Fanfare,     ///< One layer plus three delayed steps: fills the scheduler.
    Count
};

namespace detail {

/**
 * @brief Builds the five positional `AudioEvent` fields and leaves the rest at
 *        their defaults.
 *
 * Every caller below sets fields 6..15 by name on the returned value. That is
 * the whole point: past `duty`, positional initialisation is how the two traps
 * described at the top of this file get in.
 *
 * @param type Wave type. On `NOISE`, @p frequencyHz is the LFSR clock rate and
 *        not a pitch.
 * @param frequencyHz Field 2.
 * @param durationSec Field 3 — seconds, and it comes BEFORE volume.
 * @param volume Field 4 — 0.0 to 1.0.
 * @param duty Field 5 — pulse duty cycle; ignored by the other wave types.
 */
inline pixelroot32::audio::AudioEvent makeEvent(pixelroot32::audio::WaveType type,
                                                float frequencyHz,
                                                float durationSec,
                                                float volume,
                                                float duty) {
    pixelroot32::audio::AudioEvent event{};
    event.type = type;
    event.frequency = frequencyHz;
    event.duration = durationSec;
    event.volume = volume;
    event.duty = duty;
    return event;
}

} // namespace detail

/**
 * @struct DemoSfxBank
 * @brief The bank itself. Every member is static, so nothing is constructed
 *        and nothing is allocated; `playSfxBank<DemoSfxBank>` takes it as a
 *        template parameter, not as an object.
 */
struct DemoSfxBank {
    /// The shape `playSfxBank` requires: a delay and the event to fire after it.
    struct SequenceStep {
        float delaySec;
        pixelroot32::audio::AudioEvent event;
    };

    /// Number of playable ids, for walking the bank on screen.
    static constexpr uint8_t kCount = static_cast<uint8_t>(SfxId::Count);

    /// Longest `sequenceStepCount` in this bank, used to size the HUD.
    static constexpr uint8_t kMaxSequenceSteps = 3;

    /// Display name, at most 11 characters so it fits the 128 px list.
    static const char* name(SfxId id) {
        switch (id) {
            case SfxId::MenuBlip:   return "MENU BLIP";
            case SfxId::CoinPair:   return "COIN PAIR";
            case SfxId::LaserSweep: return "LASER SWEEP";
            case SfxId::Impact:     return "IMPACT";
            case SfxId::Explosion:  return "EXPLOSION";
            case SfxId::PowerUp:    return "POWER UP";
            case SfxId::AlarmLoop:  return "ALARM LOOP";
            case SfxId::Fanfare:    return "FANFARE";
            default:                return "?";
        }
    }

    /// True when the entry holds a voice open until STOP_CHANNEL.
    static bool isLooping(SfxId id) {
        return id == SfxId::AlarmLoop;
    }

    // --- The four functions playSfxBank actually calls --------------------

    static uint8_t layerCount(SfxId id) {
        switch (id) {
            case SfxId::MenuBlip:   return 1;
            case SfxId::CoinPair:   return 1;
            case SfxId::LaserSweep: return 1;
            case SfxId::Impact:     return 3;
            case SfxId::Explosion:  return 1;
            case SfxId::PowerUp:    return 1;
            case SfxId::AlarmLoop:  return 1;
            case SfxId::Fanfare:    return 1;
            default:                return 0;
        }
    }

    static pixelroot32::audio::AudioEvent layerEvent(SfxId id, uint8_t index) {
        using pixelroot32::audio::AudioEvent;
        using pixelroot32::audio::SweepCurve;
        using pixelroot32::audio::WaveType;

        switch (id) {
            case SfxId::MenuBlip: {
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 880.0f, 0.05f, 0.35f, 0.5f);
                event.preset = &presets::kPulseBlip;
                return event;
            }

            case SfxId::CoinPair: {
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 988.0f, 0.06f, 0.35f, 0.5f);
                event.preset = &presets::kPulseBlip;
                return event;
            }

            case SfxId::LaserSweep: {
                // The sweep is active only because BOTH sweepDurationSec and
                // sweepEndHz are > 0. Leave either at 0 and the voice simply
                // holds `frequency` — no warning, no error, no sweep.
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 1200.0f, 0.28f, 0.45f, 0.25f);
                event.preset = &presets::kPulseSweep;
                event.sweepEndHz = 140.0f;
                event.sweepDurationSec = 0.26f;
                event.sweepCurve = SweepCurve::Exponential;
                return event;
            }

            case SfxId::Impact:
                // Three voices at t=0. They are layers, not steps: no delay is
                // involved, so this entry never touches the scheduler — and it
                // takes three of the four SFX slots on its own.
                switch (index) {
                    case 0: {
                        AudioEvent event = detail::makeEvent(WaveType::NOISE, 5200.0f, 0.09f, 0.45f, 0.5f);
                        event.preset = &presets::kNoiseHit;
                        return event;
                    }
                    case 1: {
                        AudioEvent event = detail::makeEvent(WaveType::TRIANGLE, 110.0f, 0.16f, 0.40f, 0.5f);
                        event.preset = &presets::kTriangleSoft;
                        return event;
                    }
                    default: {
                        AudioEvent event = detail::makeEvent(WaveType::PULSE, 220.0f, 0.08f, 0.30f, 0.125f);
                        event.preset = &presets::kPulseBlip;
                        return event;
                    }
                }

            case SfxId::Explosion: {
                // On NOISE, `frequency` and `sweepEndHz` are LFSR clock rates.
                // Sweeping them down is what turns a hiss into a boom.
                AudioEvent event = detail::makeEvent(WaveType::NOISE, 9000.0f, 0.36f, 0.55f, 0.5f);
                event.preset = &presets::kNoiseBoom;
                event.sweepEndHz = 500.0f;
                event.sweepDurationSec = 0.34f;
                event.sweepCurve = SweepCurve::Exponential;
                return event;
            }

            case SfxId::PowerUp: {
                // A pitch envelope with >= 2 points REPLACES the single-segment
                // sweep, so sweepEndHz / sweepDurationSec are deliberately left
                // at 0 here: setting both would be dead data.
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 392.0f, 0.24f, 0.40f, 0.25f);
                event.preset = &presets::kPulseSweep;
                event.pitchEnvelope = tables::kPowerUpPitch;
                event.pitchEnvelopeCount = 4;
                return event;
            }

            case SfxId::AlarmLoop: {
                // duration is meaningless while loop is true: the voice stays
                // enabled until a STOP_CHANNEL command names its slot, or until
                // another effect steals it. The demo's B button sends that stop.
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 440.0f, 0.0f, 0.30f, 0.125f);
                event.preset = &presets::kPulseSweep;
                event.loop = true;
                event.dutySteps = tables::kAlarmDuty;
                event.dutyStepCount = 4;
                return event;
            }

            case SfxId::Fanfare: {
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 523.0f, 0.11f, 0.35f, 0.5f);
                event.preset = &presets::kPulseBlip;
                return event;
            }

            default:
                return detail::makeEvent(WaveType::PULSE, 440.0f, 0.05f, 0.0f, 0.5f);
        }
    }

    static uint8_t sequenceStepCount(SfxId id) {
        switch (id) {
            case SfxId::CoinPair:  return 1;
            case SfxId::Explosion: return 2;
            case SfxId::Fanfare:   return 3;
            default:               return 0;
        }
    }

    static SequenceStep sequenceStep(SfxId id, uint8_t index) {
        using pixelroot32::audio::AudioEvent;
        using pixelroot32::audio::SweepCurve;
        using pixelroot32::audio::WaveType;

        switch (id) {
            case SfxId::CoinPair: {
                AudioEvent event = detail::makeEvent(WaveType::PULSE, 1319.0f, 0.11f, 0.35f, 0.5f);
                event.preset = &presets::kPulseBlip;
                return SequenceStep{0.07f, event};
            }

            case SfxId::Explosion:
                if (index == 0) {
                    AudioEvent event = detail::makeEvent(WaveType::NOISE, 3000.0f, 0.26f, 0.40f, 0.5f);
                    event.preset = &presets::kNoiseBoom;
                    event.sweepEndHz = 300.0f;
                    event.sweepDurationSec = 0.24f;
                    event.sweepCurve = SweepCurve::Exponential;
                    return SequenceStep{0.09f, event};
                } else {
                    AudioEvent event = detail::makeEvent(WaveType::TRIANGLE, 70.0f, 0.34f, 0.35f, 0.5f);
                    event.preset = &presets::kTriangleSoft;
                    return SequenceStep{0.20f, event};
                }

            case SfxId::Fanfare: {
                // C5 - E5 - G5 - C6, one note per step. Three pending entries
                // per press, so two quick presses already fill a six-slot
                // scheduler and the third starts dropping.
                static constexpr float kDelays[3] = {0.12f, 0.24f, 0.36f};
                static constexpr float kNotes[3] = {659.0f, 784.0f, 1046.0f};
                const uint8_t step = (index < 3) ? index : 2;
                AudioEvent event = detail::makeEvent(WaveType::PULSE, kNotes[step], 0.11f, 0.35f, 0.5f);
                event.preset = &presets::kPulseBlip;
                return SequenceStep{kDelays[step], event};
            }

            default:
                return SequenceStep{0.0f, detail::makeEvent(WaveType::PULSE, 440.0f, 0.05f, 0.0f, 0.5f)};
        }
    }
};

} // namespace sfx_bank
