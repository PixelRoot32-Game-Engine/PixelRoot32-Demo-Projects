/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <audio/AudioMusicTypes.h>
#include <audio/AudioTypes.h>

#include <cstddef>

/**
 * @file SequencerTracks.h
 * @brief Two four-track patterns for the music sequencer demo.
 *
 * Shape of a pattern, and the reason this file exists at all:
 *
 *   MusicTrack {
 *       notes, count, loop, channelType, duty,
 *       &secondVoice, &thirdVoice, &percussion
 *   }
 *
 * MusicPlayer::play() walks those three optional pointers in that exact order
 * and hands the sequencer up to MAX_MUSIC_TRACKS (4) parallel note streams:
 * the main track becomes track 0, secondVoice track 1, thirdVoice track 2 and
 * percussion track 3.
 *
 * LIFETIME. Everything here is `static const` / `constexpr` at namespace scope
 * on purpose. MusicPlayer::play() stores a bare `const MusicTrack*` and the
 * audio thread dereferences it, and every note's `preset` pointer, for as long
 * as the track plays. A pattern built on the stack of init() is freed memory
 * by the time the first note sounds.
 *
 * DURATIONS ARE BEATS, NOT SECONDS. ApuCore uses TICKS_PER_BEAT = 4, so a
 * duration of 1.0 is a quarter note, 0.5 an eighth, and anything that is not a
 * multiple of 0.25 gets truncated to a tick boundary. Both patterns below are
 * exactly 8.0 beats long on all four tracks, which is what makes them loop in
 * step with each other. Wall-clock length comes from MusicPlayer::setBPM().
 */
namespace music_sequencer::tracks {

namespace a = pixelroot32::audio;

// --- Instrument aliases, one per voice role -------------------------------
// Each alias is a reference to a `constexpr InstrumentPreset` that lives in the
// engine's own header, so the pointer makeNote() stores has static storage
// duration too.
static constexpr a::InstrumentPreset TRI_LEAD = a::INSTR_TRIANGLE_LEAD;
static constexpr a::InstrumentPreset TRI_PAD = a::INSTR_TRIANGLE_PAD;
static constexpr a::InstrumentPreset TRI_BASS = a::INSTR_TRIANGLE_BASS;
static constexpr a::InstrumentPreset PLS_LEAD = a::INSTR_PULSE_LEAD;
static constexpr a::InstrumentPreset PLS_HARMONY = a::INSTR_PULSE_HARMONY;
static constexpr a::InstrumentPreset PLS_BASS = a::INSTR_PULSE_BASS;
static constexpr a::InstrumentPreset KICK = a::INSTR_KICK;
static constexpr a::InstrumentPreset SNARE = a::INSTR_SNARE;
static constexpr a::InstrumentPreset HIHAT = a::INSTR_HIHAT;

/// Note lengths in beats. Quarter = 1.0, and ApuCore resolves 1/4 of a beat.
static constexpr float HALF = 2.0f;
static constexpr float QUARTER = 1.0f;
static constexpr float EIGHTH = 0.5f;

/// A stacked hit: fires on the current step without advancing the sequencer.
/// Only useful on the percussion track, where two drums share one step.
static constexpr float STACKED = 0.0f;

// =========================================================================
// Pattern 1 — CALM CIRCUIT
// A minor, all four tracks on triangle plus noise drums. Slow, sustained,
// meant to be recognisable at 80 BPM and still musical at 200.
// =========================================================================

/// Track 0 — melodic lead. Rests here are real rests: on a non-noise track a
/// Note::Rest releases the voice instead of firing it.
static const a::MusicNote CALM_LEAD_NOTES[] = {
    a::makeNote(TRI_LEAD, a::Note::A, QUARTER),
    a::makeNote(TRI_LEAD, a::Note::C, QUARTER),
    a::makeNote(TRI_LEAD, a::Note::E, QUARTER + EIGHTH),
    a::makeRest(EIGHTH),
    a::makeNote(TRI_LEAD, a::Note::D, QUARTER),
    a::makeNote(TRI_LEAD, a::Note::C, QUARTER),
    a::makeNote(TRI_LEAD, a::Note::A, QUARTER + EIGHTH),
    a::makeRest(EIGHTH),
};

/// Track 1 — sub-voice. Four sustained pad chord tones under the lead.
static const a::MusicNote CALM_PAD_NOTES[] = {
    a::makeNote(TRI_PAD, a::Note::A, HALF),
    a::makeNote(TRI_PAD, a::Note::F, HALF),
    a::makeNote(TRI_PAD, a::Note::G, HALF),
    a::makeNote(TRI_PAD, a::Note::E, HALF),
};

/// Track 2 — bass. The preset's own defaultOctave is 3, so the three-argument
/// makeNote() overload is enough; no octave override needed.
static const a::MusicNote CALM_BASS_NOTES[] = {
    a::makeNote(TRI_BASS, a::Note::A, QUARTER),
    a::makeNote(TRI_BASS, a::Note::A, QUARTER),
    a::makeNote(TRI_BASS, a::Note::F, QUARTER),
    a::makeNote(TRI_BASS, a::Note::F, QUARTER),
    a::makeNote(TRI_BASS, a::Note::G, QUARTER),
    a::makeNote(TRI_BASS, a::Note::G, QUARTER),
    a::makeNote(TRI_BASS, a::Note::E, QUARTER),
    a::makeNote(TRI_BASS, a::Note::E, QUARTER),
};

/// Track 3 — percussion. A drum hit is Note::Rest plus a noise preset, on a
/// track whose channelType is WaveType::NOISE. Miss either half and every
/// entry below is read as silence.
static const a::MusicNote CALM_DRUM_NOTES[] = {
    a::makeNote(KICK, a::Note::Rest, QUARTER),
    a::makeNote(HIHAT, a::Note::Rest, QUARTER),
    a::makeNote(SNARE, a::Note::Rest, QUARTER),
    a::makeNote(HIHAT, a::Note::Rest, QUARTER),
    a::makeNote(KICK, a::Note::Rest, QUARTER),
    a::makeNote(HIHAT, a::Note::Rest, QUARTER),
    a::makeNote(SNARE, a::Note::Rest, QUARTER),
    a::makeNote(HIHAT, a::Note::Rest, QUARTER),
};

// Sub-tracks carry only the first five fields; their own secondVoice /
// thirdVoice / percussion stay at their nullptr defaults, because the
// sequencer reads sub-track pointers off the main track only.
static const a::MusicTrack CALM_PAD = {
    CALM_PAD_NOTES, sizeof(CALM_PAD_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::TRIANGLE, 0.5f,
};

static const a::MusicTrack CALM_BASS = {
    CALM_BASS_NOTES, sizeof(CALM_BASS_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::TRIANGLE, 0.5f,
};

static const a::MusicTrack CALM_DRUMS = {
    CALM_DRUM_NOTES, sizeof(CALM_DRUM_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::NOISE, 0.0f,
};

/// The whole pattern. Field order is positional, so the three sub-track
/// pointers must appear in this order: second voice, third voice, percussion.
static const a::MusicTrack CALM_CIRCUIT = {
    CALM_LEAD_NOTES,
    sizeof(CALM_LEAD_NOTES) / sizeof(a::MusicNote),
    true,                    // loop
    a::WaveType::TRIANGLE,   // channelType of the main track
    0.5f,                    // duty (ignored unless channelType is PULSE)
    &CALM_PAD,               // secondVoice  -> sequencer track 1
    &CALM_BASS,              // thirdVoice   -> sequencer track 2
    &CALM_DRUMS,             // percussion   -> sequencer track 3
};

// =========================================================================
// Pattern 2 — PULSE RUNNER
// C major, pulse waves, eighth-note motion. Same 8 beats, twice the density.
// =========================================================================

/// Track 0 — melodic lead. PULSE_LEAD's defaultOctave is 4; the four-argument
/// makeNote() overload lifts individual notes to octave 5.
static const a::MusicNote RUN_LEAD_NOTES[] = {
    a::makeNote(PLS_LEAD, a::Note::C, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::E, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::G, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::B, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::C, 5, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::B, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::G, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::E, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::A, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::C, 5, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::E, 5, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::C, 5, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::G, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::E, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::D, EIGHTH),
    a::makeNote(PLS_LEAD, a::Note::C, EIGHTH),
};

/// Track 1 — sub-voice. PULSE_HARMONY defaults to octave 5, which would sit on
/// top of the lead, so every note overrides it down to 4.
static const a::MusicNote RUN_HARMONY_NOTES[] = {
    a::makeNote(PLS_HARMONY, a::Note::E, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::G, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::C, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::G, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::A, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::E, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::G, 4, QUARTER),
    a::makeNote(PLS_HARMONY, a::Note::D, 4, QUARTER),
};

/// Track 2 — bass. Root-and-fifth eighths, PULSE_BASS defaultOctave 2.
static const a::MusicNote RUN_BASS_NOTES[] = {
    a::makeNote(PLS_BASS, a::Note::C, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::C, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::G, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::C, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::A, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::A, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::E, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::A, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::F, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::F, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::C, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::F, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::G, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::G, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::D, EIGHTH),
    a::makeNote(PLS_BASS, a::Note::G, EIGHTH),
};

/// Track 3 — percussion. The first entry has duration STACKED (0.0): it fires
/// on the same step as the hi-hat that follows it and does not advance the
/// sequencer, which is how a kick and a hat share beat one. Sixteen eighths
/// plus that stacked hit still total exactly 8.0 beats.
static const a::MusicNote RUN_DRUM_NOTES[] = {
    a::makeNote(KICK, a::Note::Rest, STACKED),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(SNARE, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(KICK, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(SNARE, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(KICK, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(SNARE, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(KICK, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
    a::makeNote(SNARE, a::Note::Rest, EIGHTH),
    a::makeNote(HIHAT, a::Note::Rest, EIGHTH),
};

static const a::MusicTrack RUN_HARMONY = {
    RUN_HARMONY_NOTES, sizeof(RUN_HARMONY_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::PULSE, 0.25f,
};

static const a::MusicTrack RUN_BASS = {
    RUN_BASS_NOTES, sizeof(RUN_BASS_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::PULSE, 0.25f,
};

static const a::MusicTrack RUN_DRUMS = {
    RUN_DRUM_NOTES, sizeof(RUN_DRUM_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::NOISE, 0.0f,
};

static const a::MusicTrack PULSE_RUNNER = {
    RUN_LEAD_NOTES,
    sizeof(RUN_LEAD_NOTES) / sizeof(a::MusicNote),
    true,                 // loop
    a::WaveType::PULSE,   // channelType of the main track
    0.5f,                 // duty — a square wave for the lead
    &RUN_HARMONY,         // secondVoice  -> sequencer track 1
    &RUN_BASS,            // thirdVoice   -> sequencer track 2
    &RUN_DRUMS,           // percussion   -> sequencer track 3
};

// =========================================================================
// Pattern table
// =========================================================================

/**
 * @struct Pattern
 * @brief One selectable entry in the demo's pattern list.
 *
 * `track` is a pointer into the static tracks above, never a copy: copying a
 * MusicTrack would still alias the same note arrays, but it would also give
 * the audio thread a second object to outlive.
 */
struct Pattern {
    const char* name;                    ///< Shown on screen, 13 chars or fewer.
    const pixelroot32::audio::MusicTrack* track; ///< Static, never null.
    float startBPM;                      ///< Tempo applied when this pattern is selected.
};

/// Names are kept short: the 5x7 font advances 6 px per character, and the
/// pattern line has 128 px of screen to work with.
static const Pattern PATTERNS[] = {
    {"CALM CIRCUIT", &CALM_CIRCUIT, 96.0f},
    {"PULSE RUNNER", &PULSE_RUNNER, 132.0f},
};

static constexpr std::size_t PATTERN_COUNT = sizeof(PATTERNS) / sizeof(Pattern);

} // namespace music_sequencer::tracks
