#pragma once

#include <pixelroot32/apu/AudioMusicTypes.h>

namespace pacman::audio {

/**
 * @file PacmanMusic.h
 * @brief The game's theme.
 *
 * The theme is the jingle the 1980 Namco cabinet plays when a level
 * starts, transcribed from the cabinet recording: four bars in the key of
 * F, with a bass line under it. It plays loud and at full speed while the
 * "ready" beat is on screen, and then drops to a slower, quieter loop for
 * the rest of the level -- the arcade's own trick for getting out of the
 * way of the sound effects.
 *
 * The tables live in flash and must outlive the playback, so they are
 * namespace-scope `static` data rather than anything a caller owns.
 */

namespace detail {

/// The melody. One entry per note; the duration is the sequencer advance
/// in beats, and a quarter note is 1.0.
inline constexpr pixelroot32::audio::MusicNote kThemeMelody[] = {
    // Bar 1 -- the long F that opens the jingle, then the fall to D and C.
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 1.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Cs, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    // Bar 2 -- the rise to A, the chromatic turn and the landing on C.
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::A, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::As, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::B, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    // Bar 3 -- the falling figure and its chromatic turn.
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Ds, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Cs, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    // Bar 4 -- the closing figure.
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 4, 1.0f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 4, 0.5f, 0.40f, &pixelroot32::audio::INSTR_PULSE_LEAD},
};

/// The bass under the melody: one pumping note per beat.
inline constexpr pixelroot32::audio::MusicNote kThemeBass[] = {
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::F, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},

    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},

    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::D, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},

    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::C, 3, 0.5f, 0.36f, &pixelroot32::audio::INSTR_TRIANGLE_BASS},
    pixelroot32::audio::MusicNote{pixelroot32::audio::Note::Rest, 0, 0.5f, 0.0f, nullptr},
};

}  // namespace detail

/**
 * @brief The theme, as the engine's sequencer wants it.
 *
 * The melody is the main voice; the bass runs in parallel under it.
 */
inline const pixelroot32::audio::MusicTrack& themeTrack() {
    static const pixelroot32::audio::MusicTrack kBass = {
        detail::kThemeBass,
        sizeof(detail::kThemeBass) / sizeof(detail::kThemeBass[0]),
        true,
        pixelroot32::audio::WaveType::TRIANGLE,
        0.5f,
        nullptr,
        nullptr,
        nullptr,
    };
    static const pixelroot32::audio::MusicTrack kTrack = {
        detail::kThemeMelody,
        sizeof(detail::kThemeMelody) / sizeof(detail::kThemeMelody[0]),
        true,
        pixelroot32::audio::WaveType::PULSE,
        0.5f,
        &kBass,
        nullptr,
        nullptr,
    };
    return kTrack;
}

/**
 * @brief The tempo the theme plays at, in quarter notes per minute.
 *
 * Four bars of the jingle land on about four seconds at this tempo, which
 * is the length the arcade takes.
 */
inline constexpr float kThemeBpm = 240.0f;

/**
 * @brief The tempo factor the theme drops to once the level is under way.
 *
 * The arcade's quiet background music is the same tune, slower and softer.
 */
inline constexpr float kThemeQuietTempoFactor = 0.72f;

/**
 * @brief The volume the theme drops to once the level is under way.
 */
inline constexpr float kThemeQuietVolume = 0.32f;

/**
 * @brief The volume the title screen's jingle plays at.
 *
 * The arcade's attract-mode tune is loud, but it is music the player is
 * about to start a game over, so it stays a little below the effects.
 */
inline constexpr float kIntroVolume = 0.7f;

}  // namespace pacman::audio
