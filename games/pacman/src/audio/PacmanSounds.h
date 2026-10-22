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
 */

/**
 * @brief The sound of picking up a dot.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent dotSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 1150.0f;
    event.duration = 0.035f;
    event.volume = 0.22f;
    event.duty = 0.25f;
    return event;
}

/**
 * @brief The sound of picking up a power pellet: a low note that sweeps up.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent powerPelletSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 320.0f;
    event.duration = 0.32f;
    event.volume = 0.4f;
    event.duty = 0.5f;
    event.sweepEndHz = 1200.0f;
    event.sweepDurationSec = 0.32f;
    return event;
}

/**
 * @brief The sound of defeating a hunter: a short rising sweep.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent enemyDefeatedSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 220.0f;
    event.duration = 0.42f;
    event.volume = 0.45f;
    event.duty = 0.5f;
    event.sweepEndHz = 1500.0f;
    event.sweepDurationSec = 0.42f;
    return event;
}

/**
 * @brief The sound of losing a life: a noise burst that falls away.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent lifeLostSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::NOISE;
    event.frequency = 900.0f;
    event.duration = 0.55f;
    event.volume = 0.45f;
    event.duty = 0.0f;
    event.sweepEndHz = 160.0f;
    event.sweepDurationSec = 0.55f;
    return event;
}

/**
 * @brief The sound of finishing the level: a rising arpeggio.
 * @return The event to play.
 */
inline pixelroot32::audio::AudioEvent levelCompletedSound() {
    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = 520.0f;
    event.duration = 0.7f;
    event.volume = 0.4f;
    event.duty = 0.5f;
    event.sweepEndHz = 1560.0f;
    event.sweepDurationSec = 0.7f;
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
    event.duration = 0.25f;
    event.volume = 0.35f;
    event.duty = 0.0f;
    event.sweepEndHz = 1760.0f;
    event.sweepDurationSec = 0.25f;
    return event;
}

}  // namespace pacman::audio
