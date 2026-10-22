#pragma once

#include <pixelroot32/apu/AudioTypes.h>

namespace pacman::audio {

/**
 * @file PacmanSounds.h
 * @brief The game's sound effects.
 *
 * The effects are synthesised by the engine's NES-style audio subsystem
 * rather than played from samples: a handful of oscillators is all this
 * game needs, and it keeps the demo free of binary assets.
 *
 * Every event is built with named fields rather than a positional
 * brace-init, because the field order of `AudioEvent` is not the order the
 * names suggest -- and a wrong field order compiles cleanly and sounds
 * wrong.
 *
 * The shapes follow the 1980 Namco cabinet: a two-note "waka" that
 * alternates on every dot, a siren whose pitch belongs to the hunter that
 * is closest to the player, a fast warble while the power effect lasts, a
 * rising chirp for a defeated hunter and a long falling wail for a caught
 * player.
 */

/**
 * @brief The pitch each hunter's siren uses.
 *
 * Every hunter has its own voice, so the siren that follows the player
 * belongs to whichever hunter is on top of him.
 */
inline constexpr float kHunterVoiceHz[4] = {165.0f, 205.0f, 250.0f, 300.0f};

/**
 * @brief The higher of the two "waka" notes.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent wakaHigh() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 520.0f;
    event.duration = 0.055f;
    event.volume = 0.3f;
    event.duty = 0.125f;
    event.sweepEndHz = 760.0f;
    event.sweepDurationSec = 0.055f;
    return event;
}

/**
 * @brief The lower of the two "waka" notes.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent wakaLow() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 380.0f;
    event.duration = 0.055f;
    event.volume = 0.3f;
    event.duty = 0.125f;
    event.sweepEndHz = 560.0f;
    event.sweepDurationSec = 0.055f;
    return event;
}

/**
 * @brief The sound of picking up a power pellet: a warble that climbs.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent powerPelletSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 300.0f;
    event.duration = 0.26f;
    event.volume = 0.4f;
    event.duty = 0.25f;
    event.sweepEndHz = 900.0f;
    event.sweepDurationSec = 0.26f;
    return event;
}

/**
 * @brief The sound of one hunter's siren: a short wail at its own pitch.
 *
 * @param pitch The hunter's pitch, in Hz.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent hunterVoice(float pitch) {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = pitch;
    event.duration = 0.22f;
    event.volume = 0.16f;
    event.duty = 0.5f;
    event.sweepEndHz = pitch * 1.28f;
    event.sweepDurationSec = 0.22f;
    return event;
}

/**
 * @brief The sound the hunters make while the power effect lasts: a fast,
 *        high warble.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent frightenedVoice() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 880.0f;
    event.duration = 0.13f;
    event.volume = 0.18f;
    event.duty = 0.125f;
    event.sweepEndHz = 1350.0f;
    event.sweepDurationSec = 0.13f;
    return event;
}

/**
 * @brief The sound of defeating a hunter: a chirp that climbs away.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent enemyDefeatedSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 220.0f;
    event.duration = 0.45f;
    event.volume = 0.45f;
    event.duty = 0.5f;
    event.sweepEndHz = 1500.0f;
    event.sweepDurationSec = 0.45f;
    return event;
}

/**
 * @brief The sound of losing a life: a long wail that falls away.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent lifeLostSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 700.0f;
    event.duration = 1.2f;
    event.volume = 0.45f;
    event.duty = 0.5f;
    event.sweepEndHz = 80.0f;
    event.sweepDurationSec = 1.2f;
    event.sweepCurve = pixelroot32::audio::SweepCurve::Exponential;
    return event;
}

/**
 * @brief The sound of finishing the level: a rising figure.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent levelCompletedSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 520.0f;
    event.duration = 0.8f;
    event.volume = 0.4f;
    event.duty = 0.5f;
    event.sweepEndHz = 1560.0f;
    event.sweepDurationSec = 0.8f;
    return event;
}

/**
 * @brief The sound of earning an extra life.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent extraLifeSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::TRIANGLE;
    event.frequency = 880.0f;
    event.duration = 0.3f;
    event.volume = 0.35f;
    event.duty = 0.0f;
    event.sweepEndHz = 1760.0f;
    event.sweepDurationSec = 0.3f;
    return event;
}

}  // namespace pacman::audio
