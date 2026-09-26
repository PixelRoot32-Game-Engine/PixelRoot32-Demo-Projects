#pragma once

#include <cstdint>

#include <audio/AudioMusicTypes.h>
#include <audio/AudioTypes.h>

/**
 * @file PoolMusic.h
 * @brief One looping stage track per table, plus win/lose jingles.
 *
 * Original chiptune compositions in the spirit of the NES era, not
 * transcriptions: a square-wave lead (pulse 50%), a triangle bass walking
 * the chord roots, an optional pulse harmony entering from stage 6, and a
 * shared noise groove. Durations are beats (quarter = 1.0); every loop is
 * 8 beats. All data is `static const` (flash, zero RAM) and must stay in
 * scope while playing — the sequencer references it by pointer.
 */

namespace pool {
namespace music {

namespace a = pixelroot32::audio;

constexpr float E = 0.5f;  // eighth
constexpr float Q = 1.0f;  // quarter
constexpr float H = 2.0f;  // half

// --- Shared drum grooves ---------------------------------------------------
// Drum type comes from the preset (KICK/SNARE/HIHAT); the note itself is
// always Rest. A 0.0 duration fires a stacked hit without advancing tempo.

#define POOL_KICK a::makeNote(a::INSTR_KICK, a::Note::Rest, 1, E)
#define POOL_HAT a::makeNote(a::INSTR_HIHAT, a::Note::Rest, 1, E)
#define POOL_SNARE a::makeNote(a::INSTR_SNARE, a::Note::Rest, 1, E)
#define POOL_HAT_STACK a::makeNote(a::INSTR_HIHAT, a::Note::Rest, 1, 0.0f)
#define POOL_KICK_STACK a::makeNote(a::INSTR_KICK, a::Note::Rest, 1, 0.0f)

// Stages 1-5: backbeat stroll.
static const a::MusicNote GROOVE_A_NOTES[] = {
    POOL_KICK, POOL_HAT, POOL_HAT, POOL_SNARE,
    POOL_HAT, POOL_KICK, POOL_HAT, POOL_SNARE,
};

// Stages 6-10: driving, with a stacked hat riding each kick.
static const a::MusicNote GROOVE_B_NOTES[] = {
    POOL_KICK, POOL_HAT_STACK, POOL_HAT, POOL_SNARE, POOL_HAT,
    POOL_KICK, POOL_HAT_STACK, POOL_HAT, POOL_SNARE, POOL_KICK,
};

#undef POOL_KICK
#undef POOL_HAT
#undef POOL_SNARE
#undef POOL_HAT_STACK
#undef POOL_KICK_STACK

static const a::MusicTrack GROOVE_A = {
    GROOVE_A_NOTES, sizeof(GROOVE_A_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::NOISE, 0.0f,
};

static const a::MusicTrack GROOVE_B = {
    GROOVE_B_NOTES, sizeof(GROOVE_B_NOTES) / sizeof(a::MusicNote),
    true, a::WaveType::NOISE, 0.0f,
};

// --- Stage 1: Classic (C F G C, airy 16-beat loop) ---------------------------
// Nods to the original's 9-second ambient stage loop: a slower harmonic
// rhythm (half-note bass), a sparse lead with breathing room, and an
// unhurried 16-beat span instead of the 8-beat bounce of the other stages.

static const a::MusicNote S1_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, Q),
    a::makeRest(Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, H),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, Q),
    a::makeRest(Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, H),
    a::makeRest(H),
};

static const a::MusicNote S1_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, H),
};

// --- Stage 2: Bites (C C F G, jaunty) ---------------------------------------

static const a::MusicNote S2_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
};

static const a::MusicNote S2_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
};

// --- Stage 3: Zigzag (G C D G, wide leaps) ----------------------------------

static const a::MusicNote S3_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Fs, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
};

static const a::MusicNote S3_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
};

// --- Stage 4: Gate (F Bb C F, march) ----------------------------------------

static const a::MusicNote S4_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
};

static const a::MusicNote S4_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
};

// --- Stage 5: Chevron (Dm Gm A Dm, turn to minor) ----------------------------

static const a::MusicNote S5_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Cs, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
};

static const a::MusicNote S5_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
};

// --- Stage 6: Fortress (Dm, harmony enters, driving) -------------------------

static const a::MusicNote S6_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 5, E),
};

static const a::MusicNote S6_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
};

static const a::MusicNote S6_HARMONY[] = {
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::F, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::As, 3, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::C, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::A, 3, H),
};

// --- Stage 7: Donut (Em, half-time, mysterious) ------------------------------

static const a::MusicNote S7_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, H),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, H),
};

static const a::MusicNote S7_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::B, 2, H),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, H),
};

static const a::MusicNote S7_HARMONY[] = {
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::E, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::A, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::B, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::E, 4, H),
};

// --- Stage 8: Twins (Am call and response) -----------------------------------

static const a::MusicNote S8_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
};

static const a::MusicNote S8_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::F, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::A, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, Q),
};

static const a::MusicNote S8_HARMONY[] = {
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::C, 5, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::F, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::G, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::A, 4, H),
};

// --- Stage 9: Octagon (Em, tense) --------------------------------------------

static const a::MusicNote S9_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Fs, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Fs, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Fs, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::B, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, E),
};

static const a::MusicNote S9_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::D, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::E, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::B, 2, Q),
};

static const a::MusicNote S9_HARMONY[] = {
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::E, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::G, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::A, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::B, 4, H),
};

// --- Stage 10: Corridor (Cm, urgent) ------------------------------------------

static const a::MusicNote S10_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Gs, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Gs, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Ds, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::As, 5, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, E),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::Ds, 6, E),
};

static const a::MusicNote S10_BASS[] = {
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::Gs, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::Gs, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::As, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::C, 2, Q),
    a::makeNote(a::INSTR_TRIANGLE_BASS, a::Note::G, 2, Q),
};

static const a::MusicNote S10_HARMONY[] = {
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::Ds, 5, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::Gs, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::As, 4, H),
    a::makeNote(a::INSTR_PULSE_HARMONY, a::Note::C, 5, H),
};

// --- Bass sub-tracks (secondVoice, track slot 1) ------------------------------

#define POOL_BASS_TRACK(name)                                           \
    static const a::MusicTrack name##_BASS_TRACK = {                     \
        name##_BASS, sizeof(name##_BASS) / sizeof(a::MusicNote), true,   \
        a::WaveType::TRIANGLE, 0.5f,                                     \
    }

POOL_BASS_TRACK(S1);
POOL_BASS_TRACK(S2);
POOL_BASS_TRACK(S3);
POOL_BASS_TRACK(S4);
POOL_BASS_TRACK(S5);
POOL_BASS_TRACK(S6);
POOL_BASS_TRACK(S7);
POOL_BASS_TRACK(S8);
POOL_BASS_TRACK(S9);
POOL_BASS_TRACK(S10);

#undef POOL_BASS_TRACK

// --- Harmony sub-tracks (thirdVoice, track slot 2, stages 6-10) ---------------

#define POOL_HARMONY_TRACK(name)                                        \
    static const a::MusicTrack name##_HARMONY_TRACK = {                 \
        name##_HARMONY, sizeof(name##_HARMONY) / sizeof(a::MusicNote),   \
        true, a::WaveType::PULSE, a::INSTR_PULSE_HARMONY.duty,           \
    }

POOL_HARMONY_TRACK(S6);
POOL_HARMONY_TRACK(S7);
POOL_HARMONY_TRACK(S8);
POOL_HARMONY_TRACK(S9);
POOL_HARMONY_TRACK(S10);

#undef POOL_HARMONY_TRACK

// --- Stage tracks (melody on slot 0 + bass + optional harmony + groove) ------

#define POOL_STAGE_TRACK(num, groove, harmony)                          \
    static const a::MusicTrack STAGE##num = {                           \
        S##num##_LEAD, sizeof(S##num##_LEAD) / sizeof(a::MusicNote),     \
        true, a::WaveType::PULSE, a::INSTR_PULSE_LEAD.duty,              \
        &S##num##_BASS_TRACK, harmony, groove,                           \
    }

POOL_STAGE_TRACK(1, &GROOVE_A, nullptr);
POOL_STAGE_TRACK(2, &GROOVE_A, nullptr);
POOL_STAGE_TRACK(3, &GROOVE_A, nullptr);
POOL_STAGE_TRACK(4, &GROOVE_A, nullptr);
POOL_STAGE_TRACK(5, &GROOVE_A, nullptr);
POOL_STAGE_TRACK(6, &GROOVE_B, &S6_HARMONY_TRACK);
POOL_STAGE_TRACK(7, &GROOVE_A, &S7_HARMONY_TRACK);
POOL_STAGE_TRACK(8, &GROOVE_B, &S8_HARMONY_TRACK);
POOL_STAGE_TRACK(9, &GROOVE_B, &S9_HARMONY_TRACK);
POOL_STAGE_TRACK(10, &GROOVE_B, &S10_HARMONY_TRACK);

#undef POOL_STAGE_TRACK

static const a::MusicTrack* const STAGE_TRACKS[10] = {
    &STAGE1, &STAGE2, &STAGE3, &STAGE4, &STAGE5,
    &STAGE6, &STAGE7, &STAGE8, &STAGE9, &STAGE10,
};

// Tempo climb with the difficulty curve: stage 1 is the baseline.
static constexpr float STAGE_TEMPO[10] = {
    1.00f, 1.04f, 1.08f, 1.10f, 1.14f,
    1.18f, 1.16f, 1.22f, 1.28f, 1.34f,
};

[[nodiscard]] inline const a::MusicTrack& trackForStage(uint8_t stage) {
    if (stage < 1) {
        stage = 1;
    } else if (stage > 10) {
        stage = 10;
    }
    return *STAGE_TRACKS[stage - 1];
}

[[nodiscard]] inline float tempoForStage(uint8_t stage) {
    if (stage < 1) {
        stage = 1;
    } else if (stage > 10) {
        stage = 10;
    }
    return STAGE_TEMPO[stage - 1];
}

// --- Win / lose jingles (one-shot, loop = false) ------------------------------

static const a::MusicNote WIN_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 5, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::C, 6, H),
};

static const a::MusicTrack WIN_JINGLE = {
    WIN_LEAD, sizeof(WIN_LEAD) / sizeof(a::MusicNote),
    false, a::WaveType::PULSE, a::INSTR_PULSE_LEAD.duty,
};

static const a::MusicNote LOSE_LEAD[] = {
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::A, 4, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::G, 4, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::F, 4, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::E, 4, Q),
    a::makeNote(a::INSTR_PULSE_LEAD, a::Note::D, 4, H),
};

static const a::MusicTrack LOSE_JINGLE = {
    LOSE_LEAD, sizeof(LOSE_LEAD) / sizeof(a::MusicNote),
    false, a::WaveType::PULSE, a::INSTR_PULSE_LEAD.duty,
};

}  // namespace music
}  // namespace pool
