#pragma once

#include <platforms/EngineConfig.h>

#if PIXELROOT32_ENABLE_AUDIO

#include <cstddef>

#include <pixelroot32/apu/AudioMusicTypes.h>

#include "game/rules/AudioCues.h"   // RadioTrackId

/**
 * @brief What the city sounds like from inside a car -- four original
 *        stations, chosen by the car's paint, and the siren that can ride
 *        any of them.
 *
 * Music is bound to the CAR: there is none on foot unless wanted (the siren
 * alone) and none for a chase. See `AudioCues.h`'s `musicPlanFor` for the
 * whole model.
 *
 * All data is original, `constexpr` (ADR-9) and typed in by hand -- no
 * generator, no asset file (ADR-4), and not one note transcribed from any
 * existing game. The loop-length discipline follows GTA Advance's own
 * 0:51-2:07 band, which is the only thing taken from that reference.
 *
 * ## Loop length, at the shared tempo
 *
 * `MusicNote::duration` is in BEATS. All four stations and the siren share
 * one BPM (ADR-11, `kRadioBpm`, submitted once in `AudioDirector::bind`)
 * because the siren's wail rate is a function of whichever track it rides,
 * and it must sound the same in every car.
 *
 * | Station | Name | Main voice | Beats | Seconds |
 * |---|---|---|---|---|
 * | One   | Nightcab     | `INSTR_TRIANGLE_LEAD` | 160 | 80.0 s |
 * | Two   | Precinct FM  | `INSTR_PULSE_LEAD`    | 128 | 64.0 s |
 * | Three | Harbour Dub  | `INSTR_PULSE_HARMONY` | 144 | 72.0 s |
 * | Four  | Deadline     | `INSTR_TRIANGLE_PAD`  | 192 | 96.0 s |
 *
 * ## Voices
 *
 * ADR-8 caps a station at three of the four music voices: `secondVoice` is
 * the siren's reserved slot on every station, always, and the compose step
 * never reads it -- so it stays an explicit `nullptr` in every initializer
 * below rather than a field a station's own data could quietly fill.
 *
 * Each sub-voice's note array sums to a cycle length that does NOT evenly
 * divide its station's main-voice beat count, so the two drift in and out of
 * phase across a loop instead of falling into lock-step -- free variation
 * from a handful of extra notes. Station Four keeps its percussion slot empty
 * on purpose: two voices are enough for a drone, and it is the proof ADR-8's
 * cap has slack.
 *
 * Existing shipped presets are reused rather than hand-composed: a 17-field
 * `InstrumentPreset` literal is the single easiest way to get silently wrong
 * audio in this codebase.
 *
 * ## Why each station is recognisable in two seconds
 *
 * `INSTR_TRIANGLE_LEAD`'s soft, edge-free timbre; `INSTR_PULSE_LEAD`'s
 * eighth-note-dense riffing; `INSTR_PULSE_HARMONY`'s sparse offbeat stabs
 * inside long rests (identified by what is MISSING -- the downbeat); and
 * `INSTR_TRIANGLE_PAD`'s slow held drone are four different rhythmic
 * densities, not the same idea in four keys.
 */
namespace top_down_city::city_radio {

using pixelroot32::audio::InstrumentPreset;
using pixelroot32::audio::MusicNote;
using pixelroot32::audio::MusicTrack;
using pixelroot32::audio::Note;
using pixelroot32::audio::WaveType;

/// Shared by the siren and all four stations -- see ADR-11 above: a
/// per-station tempo would make the siren alternate at a different rate in
/// every car, and it is the one sound that must be recognised instantly.
/// Submitted once via `MusicPlayer::setBPM` from `AudioDirector::bind()`.
/// Adding a station with a different intended tempo means reconsidering the
/// siren's cadence, not just this file.
constexpr float kRadioBpm = 120.0f;

/// How loud a station plays against everything else in the mix. There is no
/// music bus to turn down -- see `AudioDirector::bind` for why the master is
/// not one. What music owns, per note, is `MusicNote::volume`, which the
/// sequencer copies into the voice unchanged, so the radio's own level is one
/// factor applied in the one helper every station note is built by, and
/// lowering it moves the radio and NOTHING else. A station is also a lead plus
/// a bass plus percussion, three voices at once against a gunshot's two.
constexpr float kRadioMix = 0.5f;

/// The siren is not radio. It is the one music-subpool voice that has to cut
/// THROUGH whatever station is playing -- and on foot it plays alone, with
/// no station under it -- so it is built at the preset's own full level.
constexpr float kSirenMix = 1.0f;

/**
 * @brief Builds a `MusicNote` at an explicit octave, `constexpr`.
 *
 * The engine's own `pixelroot32::audio::makeNote` is `inline`, not `constexpr`
 * (ADR-9) -- calling it here would dynamically initialize every note array
 * below into RAM instead of `.rodata`. Same field order and defaulting, so
 * call sites read identically; only the linkage and the `mix` factor differ.
 *
 * @param mix Scales the preset's `baseVolume`. Defaults to `kRadioMix`, so
 *        every station note is mixed by omission; the siren passes
 *        `kSirenMix` explicitly, which is the whole of the exception.
 */
constexpr MusicNote note(const InstrumentPreset& preset, Note n,
                         std::uint8_t octave, float beats,
                         float mix = kRadioMix) {
    return MusicNote{n, octave, beats, preset.baseVolume * mix, &preset};
}

/// `constexpr` counterpart to the engine's (also non-`constexpr`)
/// `makeRest` -- same reasoning as `note()` above.
constexpr MusicNote rest(float beats) {
    return MusicNote{Note::Rest, 0, beats, 0.0f, nullptr};
}

// ---------------------------------------------------------------------
// The siren. A voice of whichever station is playing, or the whole of it
// alone on foot. ADR-1: `AudioEngine::playEvent` returns `void`, so a looping
// SFX event is a voice nobody could name and therefore nobody could stop --
// up here it lives in the music subpool, which SFX voice-stealing cannot
// reach. Composed onto a station's reserved `secondVoice` slot at runtime,
// never a station's OWN (ADR-8). One beat per tone at kRadioBpm is ~1 Hz
// alternation, in every car, by construction of ADR-11.
// ---------------------------------------------------------------------

constexpr InstrumentPreset SIREN_INSTRUMENT =
    pixelroot32::audio::INSTR_PULSE_HARMONY;

constexpr MusicNote SIREN_NOTES[] = {
    note(SIREN_INSTRUMENT, Note::A, 5, 1.0f, kSirenMix),
    note(SIREN_INSTRUMENT, Note::E, 5, 1.0f, kSirenMix),
};

constexpr MusicTrack SIREN_TRACK = {
    SIREN_NOTES, sizeof(SIREN_NOTES) / sizeof(MusicNote),
    true, WaveType::PULSE, SIREN_INSTRUMENT.duty};

// ---------------------------------------------------------------------
// Station One: Nightcab. Calm and spacious -- all triangle, so it is the
// SOFT one, recognisable within two notes by the absence of any pulse edge.
// C-minor-pentatonic (C, Eb, F, G, Bb), four 40-beat phrases (A/A'/B/
// turnaround), 160 beats total (80.0 s at kRadioBpm).
// ---------------------------------------------------------------------

constexpr InstrumentPreset NIGHTCAB_INSTRUMENT =
    pixelroot32::audio::INSTR_TRIANGLE_LEAD;

constexpr MusicNote NIGHTCAB_NOTES[] = {
    // -- A (40 beats) --
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 3.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::F, 4, 2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::G, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::As, 3, 4.0f),
    rest(4.0f),
    // -- A' (40 beats): same shape, not the same notes or rests --
    note(NIGHTCAB_INSTRUMENT, Note::G, 3, 4.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::As, 3, 3.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 2.0f),
    rest(4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::F, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::G, 4, 4.0f),
    rest(2.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 4.0f),
    rest(2.0f),
    // -- B (40 beats): higher register, contrasting --
    note(NIGHTCAB_INSTRUMENT, Note::C, 5, 3.0f),
    rest(4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::As, 4, 3.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::G, 4, 4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::F, 4, 2.0f),
    rest(4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 4.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::G, 3, 3.0f),
    // -- turnaround (40 beats): back toward the top of the loop --
    note(NIGHTCAB_INSTRUMENT, Note::F, 4, 4.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::Ds, 4, 3.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    rest(4.0f),
    note(NIGHTCAB_INSTRUMENT, Note::As, 3, 3.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::G, 3, 4.0f),
    rest(3.0f),
    note(NIGHTCAB_INSTRUMENT, Note::C, 4, 4.0f),
    rest(2.0f),
};

// Third voice: a walking bass under the lead, 18 beats, which does not evenly
// divide the main voice's 160 -- see the file header on drift.
constexpr InstrumentPreset NIGHTCAB_BASS_INSTRUMENT =
    pixelroot32::audio::INSTR_TRIANGLE_BASS;

constexpr MusicNote NIGHTCAB_BASS_NOTES[] = {
    note(NIGHTCAB_BASS_INSTRUMENT, Note::C, 3, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::As, 2, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::F, 2, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::G, 2, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::Ds, 3, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::C, 3, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::F, 2, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::G, 2, 2.0f),
    note(NIGHTCAB_BASS_INSTRUMENT, Note::As, 2, 2.0f),
};

constexpr MusicTrack NIGHTCAB_BASS_TRACK = {
    NIGHTCAB_BASS_NOTES, sizeof(NIGHTCAB_BASS_NOTES) / sizeof(MusicNote),
    true, WaveType::TRIANGLE, NIGHTCAB_BASS_INSTRUMENT.duty};

// Percussion: hi-hat on the offbeats only, 6 beats -- does not evenly divide
// 160 either, so the brushed pulse never lines up with the same lyric twice.
constexpr MusicNote NIGHTCAB_HIHAT_NOTES[] = {
    rest(1.0f),
    note(pixelroot32::audio::INSTR_HIHAT, Note::Rest, 0, 1.0f),
    rest(1.0f),
    note(pixelroot32::audio::INSTR_HIHAT, Note::Rest, 0, 1.0f),
    rest(1.0f),
    note(pixelroot32::audio::INSTR_HIHAT, Note::Rest, 0, 1.0f),
};

constexpr MusicTrack NIGHTCAB_HIHAT_TRACK = {
    NIGHTCAB_HIHAT_NOTES, sizeof(NIGHTCAB_HIHAT_NOTES) / sizeof(MusicNote),
    true, WaveType::NOISE, 0.0f};

// secondVoice stays nullptr on every station in this file -- ADR-8, see the
// file header.
constexpr MusicTrack NIGHTCAB_TRACK = {
    NIGHTCAB_NOTES, sizeof(NIGHTCAB_NOTES) / sizeof(MusicNote),
    true, WaveType::TRIANGLE, NIGHTCAB_INSTRUMENT.duty,
    nullptr, &NIGHTCAB_BASS_TRACK, &NIGHTCAB_HIHAT_TRACK};

// ---------------------------------------------------------------------
// Station Two: Precinct FM. Busy and driving -- the only station with real
// rhythmic density, so a "fast feel" at the same shared 120 BPM is bought
// with note density rather than tempo. A minor, four 32-beat blocks
// (verse/chorus-varied/bridge/turnaround), 128 beats total (64.0 s).
// ---------------------------------------------------------------------

constexpr InstrumentPreset PRECINCT_INSTRUMENT =
    pixelroot32::audio::INSTR_PULSE_LEAD;

constexpr MusicNote PRECINCT_NOTES[] = {
    // -- verse (32 beats) --
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    rest(1.0f),
    note(PRECINCT_INSTRUMENT, Note::G, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    rest(2.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 2.0f),
    // -- chorus, varied a step up (32 beats) --
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::F, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    rest(1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::F, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    rest(2.0f),
    note(PRECINCT_INSTRUMENT, Note::F, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 5, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 2.0f),
    // -- bridge, dropped an octave, sparser (32 beats) --
    note(PRECINCT_INSTRUMENT, Note::A, 3, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 3, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 4, 1.0f),
    rest(2.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 3, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 3, 2.0f),
    rest(2.0f),
    note(PRECINCT_INSTRUMENT, Note::G, 3, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 3, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 3, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 4, 2.0f),
    rest(2.0f),
    // -- turnaround, back to the top (32 beats) --
    note(PRECINCT_INSTRUMENT, Note::D, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    rest(1.0f),
    note(PRECINCT_INSTRUMENT, Note::G, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 5, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::B, 4, 0.5f),
    note(PRECINCT_INSTRUMENT, Note::A, 4, 1.0f),
    rest(1.0f),
    note(PRECINCT_INSTRUMENT, Note::G, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::F, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 4, 1.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 4, 1.0f),
    rest(2.0f),
    note(PRECINCT_INSTRUMENT, Note::C, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::D, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::E, 4, 2.0f),
    note(PRECINCT_INSTRUMENT, Note::A, 3, 4.0f),
    rest(5.0f),
};

// Third voice: root eighths under the riff, 12 beats -- does not evenly
// divide the main voice's 128.
constexpr InstrumentPreset PRECINCT_BASS_INSTRUMENT =
    pixelroot32::audio::INSTR_PULSE_BASS;

constexpr MusicNote PRECINCT_BASS_NOTES[] = {
    note(PRECINCT_BASS_INSTRUMENT, Note::A, 2, 2.0f),
    note(PRECINCT_BASS_INSTRUMENT, Note::C, 3, 2.0f),
    note(PRECINCT_BASS_INSTRUMENT, Note::B, 2, 2.0f),
    note(PRECINCT_BASS_INSTRUMENT, Note::D, 3, 2.0f),
    note(PRECINCT_BASS_INSTRUMENT, Note::A, 2, 2.0f),
    note(PRECINCT_BASS_INSTRUMENT, Note::E, 3, 2.0f),
};

constexpr MusicTrack PRECINCT_BASS_TRACK = {
    PRECINCT_BASS_NOTES, sizeof(PRECINCT_BASS_NOTES) / sizeof(MusicNote),
    true, WaveType::PULSE, PRECINCT_BASS_INSTRUMENT.duty};

// Percussion: a kick/snare backbeat, 5 beats -- does not evenly divide 128.
// The trailing snare's 0.0 duration is a stacked hit (gotcha: "0.0 fires
// without advancing tempo"), landing on the same step as the kick before it
// rather than a sixth beat of its own -- the one place this file needs two
// drums on one step.
constexpr MusicNote PRECINCT_DRUMS_NOTES[] = {
    note(pixelroot32::audio::INSTR_KICK, Note::Rest, 0, 1.0f),
    note(pixelroot32::audio::INSTR_SNARE, Note::Rest, 0, 1.0f),
    note(pixelroot32::audio::INSTR_KICK, Note::Rest, 0, 1.0f),
    note(pixelroot32::audio::INSTR_SNARE, Note::Rest, 0, 1.0f),
    note(pixelroot32::audio::INSTR_KICK, Note::Rest, 0, 1.0f),
    note(pixelroot32::audio::INSTR_SNARE, Note::Rest, 0, 0.0f),
};

constexpr MusicTrack PRECINCT_DRUMS_TRACK = {
    PRECINCT_DRUMS_NOTES, sizeof(PRECINCT_DRUMS_NOTES) / sizeof(MusicNote),
    true, WaveType::NOISE, 0.0f};

constexpr MusicTrack PRECINCT_TRACK = {
    PRECINCT_NOTES, sizeof(PRECINCT_NOTES) / sizeof(MusicNote),
    true, WaveType::PULSE, PRECINCT_INSTRUMENT.duty,
    nullptr, &PRECINCT_BASS_TRACK, &PRECINCT_DRUMS_TRACK};

// ---------------------------------------------------------------------
// Station Three: Harbour Dub. Full of holes -- identified by what is
// MISSING, the downbeat: sparse offbeat stabs inside long rests. Four
// 36-beat blocks, 144 beats total (72.0 s).
// ---------------------------------------------------------------------

constexpr InstrumentPreset HARBOUR_INSTRUMENT =
    pixelroot32::audio::INSTR_PULSE_HARMONY;

constexpr MusicNote HARBOUR_NOTES[] = {
    // -- dub 1 (36 beats) --
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 4, 2.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::C, 5, 2.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::G, 4, 1.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 4, 1.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::E, 4, 2.0f),
    rest(8.0f),
    // -- dub 2, stabs shifted (36 beats) --
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::C, 5, 2.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::G, 4, 2.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 4, 1.0f),
    note(HARBOUR_INSTRUMENT, Note::E, 4, 1.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::D, 4, 2.0f),
    rest(8.0f),
    // -- dub 3, lower register (36 beats) --
    rest(8.0f),
    note(HARBOUR_INSTRUMENT, Note::G, 3, 2.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 3, 1.0f),
    note(HARBOUR_INSTRUMENT, Note::C, 4, 1.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::E, 4, 2.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::D, 4, 2.0f),
    rest(6.0f),
    // -- turnaround (36 beats) --
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 4, 2.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::G, 4, 1.0f),
    note(HARBOUR_INSTRUMENT, Note::E, 4, 1.0f),
    rest(4.0f),
    note(HARBOUR_INSTRUMENT, Note::C, 4, 2.0f),
    rest(6.0f),
    note(HARBOUR_INSTRUMENT, Note::A, 3, 2.0f),
    rest(6.0f),
};

// Third voice: a heavy dub line, one register below the main voice, 14
// beats -- does not evenly divide the main voice's 144.
constexpr InstrumentPreset HARBOUR_BASS_INSTRUMENT =
    pixelroot32::audio::INSTR_TRIANGLE_BASS;

constexpr MusicNote HARBOUR_BASS_NOTES[] = {
    note(HARBOUR_BASS_INSTRUMENT, Note::G, 2, 4.0f),
    rest(2.0f),
    note(HARBOUR_BASS_INSTRUMENT, Note::Ds, 2, 3.0f),
    rest(3.0f),
    note(HARBOUR_BASS_INSTRUMENT, Note::C, 2, 2.0f),
};

constexpr MusicTrack HARBOUR_BASS_TRACK = {
    HARBOUR_BASS_NOTES, sizeof(HARBOUR_BASS_NOTES) / sizeof(MusicNote),
    true, WaveType::TRIANGLE, HARBOUR_BASS_INSTRUMENT.duty};

// Percussion: a snare alone, on 3 -- one hit in a 5-beat cycle, which does not
// evenly divide 144 either, so it never lands on the same lyric twice.
constexpr MusicNote HARBOUR_SNARE_NOTES[] = {
    rest(2.0f),
    note(pixelroot32::audio::INSTR_SNARE, Note::Rest, 0, 1.0f),
    rest(2.0f),
};

constexpr MusicTrack HARBOUR_SNARE_TRACK = {
    HARBOUR_SNARE_NOTES, sizeof(HARBOUR_SNARE_NOTES) / sizeof(MusicNote),
    true, WaveType::NOISE, 0.0f};

constexpr MusicTrack HARBOUR_TRACK = {
    HARBOUR_NOTES, sizeof(HARBOUR_NOTES) / sizeof(MusicNote),
    true, WaveType::PULSE, HARBOUR_INSTRUMENT.duty,
    nullptr, &HARBOUR_BASS_TRACK, &HARBOUR_SNARE_TRACK};

// ---------------------------------------------------------------------
// Station Four: Deadline. A drone, almost ambient -- held tones of 4-8
// beats each, the station nobody expects to find in a car. Four 48-beat
// blocks, 192 beats total (96.0 s).
// ---------------------------------------------------------------------

constexpr InstrumentPreset DEADLINE_INSTRUMENT =
    pixelroot32::audio::INSTR_TRIANGLE_PAD;

constexpr MusicNote DEADLINE_NOTES[] = {
    // -- drone 1 (48 beats) --
    note(DEADLINE_INSTRUMENT, Note::C, 4, 8.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::Ds, 4, 6.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::G, 3, 8.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::As, 3, 8.0f),
    rest(4.0f),
    // -- drone 2, varied (48 beats) --
    note(DEADLINE_INSTRUMENT, Note::F, 4, 8.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::Ds, 4, 6.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::C, 4, 8.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::G, 3, 8.0f),
    rest(4.0f),
    // -- drone 3, varied (48 beats) --
    note(DEADLINE_INSTRUMENT, Note::Ds, 4, 8.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::D, 4, 6.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::C, 4, 8.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::As, 3, 8.0f),
    rest(2.0f),
    // -- turnaround (48 beats) --
    note(DEADLINE_INSTRUMENT, Note::C, 4, 8.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::G, 3, 8.0f),
    rest(4.0f),
    note(DEADLINE_INSTRUMENT, Note::As, 3, 6.0f),
    rest(6.0f),
    note(DEADLINE_INSTRUMENT, Note::C, 4, 8.0f),
    rest(2.0f),
};

// Third voice: a slower counter-drone under the pad, 20 beats -- does not
// evenly divide the main voice's 192.
constexpr InstrumentPreset DEADLINE_DRONE_INSTRUMENT =
    pixelroot32::audio::INSTR_PULSE_PAD;

constexpr MusicNote DEADLINE_DRONE_NOTES[] = {
    note(DEADLINE_DRONE_INSTRUMENT, Note::E, 3, 10.0f),
    rest(2.0f),
    note(DEADLINE_DRONE_INSTRUMENT, Note::C, 3, 8.0f),
};

constexpr MusicTrack DEADLINE_DRONE_TRACK = {
    DEADLINE_DRONE_NOTES, sizeof(DEADLINE_DRONE_NOTES) / sizeof(MusicNote),
    true, WaveType::PULSE, DEADLINE_DRONE_INSTRUMENT.duty};

// Deliberately two voices only (no percussion) -- proof that ADR-8's
// three-voice cap has slack, not every station needs to spend it.
constexpr MusicTrack DEADLINE_TRACK = {
    DEADLINE_NOTES, sizeof(DEADLINE_NOTES) / sizeof(MusicNote),
    true, WaveType::TRIANGLE, DEADLINE_INSTRUMENT.duty,
    nullptr, &DEADLINE_DRONE_TRACK, nullptr};

/**
 * @brief The station track for a plan's `RadioTrackId`.
 * @param id Never `RadioTrackId::None` in practice -- the compose step in
 *        `AudioDirector::setMusicPlan` only calls this when the plan names a
 *        real station. Falls back to `NIGHTCAB_TRACK` rather than undefined
 *        behaviour if it ever is, matching the clamp-rather-than-assert
 *        convention of `AudioCues.cpp`'s `cooldownMsFor` and `Wanted.cpp`'s
 *        `penalty`.
 */
inline const MusicTrack& stationTrack(audio_cues::RadioTrackId id) {
    switch (id) {
        case audio_cues::RadioTrackId::One:   return NIGHTCAB_TRACK;
        case audio_cues::RadioTrackId::Two:   return PRECINCT_TRACK;
        case audio_cues::RadioTrackId::Three: return HARBOUR_TRACK;
        case audio_cues::RadioTrackId::Four:  return DEADLINE_TRACK;
        case audio_cues::RadioTrackId::None:
        case audio_cues::RadioTrackId::Count:
        default:
            return NIGHTCAB_TRACK;
    }
}

}  // namespace top_down_city::city_radio

#endif  // PIXELROOT32_ENABLE_AUDIO
