#pragma once

/*
 * The C++ half of the sound seam: the seven I_* calls lib/doom's s_sound.c
 * makes, translated into the APU's one-shot event.
 *
 * WHY THIS HEADER IS ENGINE-FREE
 *
 * `AudioEngine` is forward-declared and only a reference to it crosses this
 * header, so the host tests, `env:link_test` and any build without
 * `DOOM_AUDIO` compile this file without the engine's include path. The APU
 * headers appear in DoomAudio.cpp alone.
 *
 * THE TWO HALVES
 *
 * `DOOM_AUDIO` is on in the shipped HUD environment and its children. Without
 * it the seam functions are defined as silent stubs, so lib/doom links in
 * every environment (the `_show`, `_frame` and `_wad` families, `native`) and
 * a build that never wanted sound carries no engine call, no event and no
 * voice table -- only an empty function apiece, which --gc-sections removes
 * from an image that never calls the sound layer.
 */

#include <cstdint>

namespace pixelroot32 {
namespace audio {
class AudioEngine;
class MusicPlayer;
}  // namespace audio
}  // namespace pixelroot32

namespace doom {
namespace doomc {

/// The counters the `SHOW,audio` line prints.
struct DoomAudioStats {
    /// Cues that reached an APU voice.
    std::uint32_t plays;
    /// Cues that took a voice from a sounding, non-Reserved cue.
    std::uint32_t steals;
    /// Cues held because every voice was Reserved (or no logical slot was
    /// free, or no engine was installed).
    std::uint32_t refused;
    /// Calls whose id has no bank cue: id 0 and ids past NUMSFX-1.
    std::uint32_t unknown;
    /// The highest absolute APU slot used so far (4..7), or -1 before the
    /// first play.
    std::int32_t highestVoice;
};

/// The APU's own numbers, supplied by whoever installs a source.
struct DoomAudioTelemetry {
    std::uint32_t underruns;
    std::uint32_t worstUs;
};

/// Reads the APU telemetry. Returns false when no source is installed.
using DoomAudioTelemetrySource = bool (*)(void* user, DoomAudioTelemetry* out);

/**
 * @brief Installs the engine the seam plays through. Called once by the scene.
 *
 * `engine` owns the SFX voices; `music` is the engine's `MusicPlayer`, which
 * owns the four sequencer tracks. Both are references to the engine the scene
 * already constructs, so no lifetime is introduced here.
 *
 * Clears the voice table and the counters: a second call is a fresh audio
 * session, not a second engine.
 */
void doomAudioInit(pixelroot32::audio::AudioEngine& engine,
                   pixelroot32::audio::MusicPlayer& music);

/// The counters, in place; valid for the process's life.
const DoomAudioStats& doomAudioStats();

/// The SFX subpool size the seam reduces onto (4, the APU's slots 4..7).
int doomAudioVoiceCount();

/// How many of those voices hold a cue right now.
int doomAudioActiveVoices();

/**
 * @brief Installs the APU telemetry source, or clears it with NULL.
 *
 * The board installs one in `src/platforms/esp32_dev_ILI9341.h` after
 * `engine.init()`, reading `getUnderrunCount()`/`getWorstRenderUs()` from the
 * I2S backend. Until a source is installed (or when it answers false)
 * `doomAudioReadTelemetry()` answers false and the `SHOW,audio` line prints `-`
 * for `underruns`/`worst_us` -- the native SDL2 backend exposes no counters, so
 * the simulator keeps its dashes honestly.
 *
 * The store is defined in `src/doomc/AudioTelemetry.cpp`, outside the
 * `DOOM_AUDIO` gate: a build with no sound still owns a real (empty) one.
 */
void doomAudioSetTelemetrySource(DoomAudioTelemetrySource source, void* user);

/// Reads the installed source's numbers; false when there is none.
bool doomAudioReadTelemetry(DoomAudioTelemetry* out);

/**
 * @brief Writes one `SHOW,music` line: the bank's current track, its note
 *        counts and beats, `isPlaying`, and the APU telemetry.
 *
 * `event` names the caller ("init", "start", "periodic") so a boot log can
 * tell the three apart. Under `!DOOM_AUDIO` it writes nothing: there is no
 * transport and no line to print.
 */
void doomMusicReport(const char* event);

}  // namespace doomc
}  // namespace doom
