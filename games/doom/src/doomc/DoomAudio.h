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

/// The APU's own numbers, supplied by whoever installs a source (T48).
struct DoomAudioTelemetry {
    std::uint32_t underruns;
    std::uint32_t worstUs;
};

/// Reads the APU telemetry. Returns false when no source is installed.
using DoomAudioTelemetrySource = bool (*)(void* user, DoomAudioTelemetry* out);

/**
 * @brief Installs the engine the seam plays through. Called once by the scene.
 *
 * Clears the voice table and the counters: a second call is a fresh audio
 * session, not a second engine.
 */
void doomAudioInit(pixelroot32::audio::AudioEngine& engine);

/// The counters, in place; valid for the process's life.
const DoomAudioStats& doomAudioStats();

/// The SFX subpool size the seam reduces onto (4, the APU's slots 4..7).
int doomAudioVoiceCount();

/// How many of those voices hold a cue right now.
int doomAudioActiveVoices();

/**
 * @brief Installs the APU telemetry source (T48), or clears it with NULL.
 *
 * Until one is installed `doomAudioReadTelemetry()` answers false and the
 * `SHOW,audio` line prints `-` for `underruns`/`worst_us`, which is this
 * task's honest state: the branch this port pins does not expose
 * `getUnderrunCount()` yet.
 */
void doomAudioSetTelemetrySource(DoomAudioTelemetrySource source, void* user);

/// Reads the installed source's numbers; false when there is none.
bool doomAudioReadTelemetry(DoomAudioTelemetry* out);

}  // namespace doomc
}  // namespace doom
