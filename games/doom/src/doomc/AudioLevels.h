#pragma once

/*
 * The menu's volume and mute, as the numbers the audio engine takes (T49).
 *
 * WHY THIS IS A SEPARATE, ENGINE-FREE HEADER
 *
 * The mapping is a rule, not a call: `m_menu.h` says the slider is 0..15 and
 * DOOM's mixer says a level is 0..127, and the two scales meet exactly once in
 * this project. Keeping the conversion here means both host suites that own a
 * half of it can prove it -- `menu_test` owns the settings and their stored
 * form, and the same suite can state what 0, the maximum and mute become --
 * without an engine, an APU or a board. `DoomScene` is then the only caller,
 * and it does nothing but hand the result to the engine.
 *
 * WHY THE LEVEL IS DOOM'S 0..127 AND NOT A FLOAT
 *
 * Upstream's `S_SetSfxVolume`/`S_SetMusicVolume` take 0..127, and the port's own
 * `S_MAX_VOLUME` is that number. Stating the mapping in that domain keeps the
 * menu comparable with `s_sound.c`'s own arithmetic (the link clamp, the map-8
 * ladder) and makes "S_MAX_VOLUME at M_VOLUME_MAX" a property a test can read.
 * `audioMasterVolume()` is the same level as the ratio the engine's
 * `setMasterVolume()` takes, so the two forms cannot drift.
 *
 * MUTE IS A GATE, NOT A VOLUME
 *
 * `m_settings_t::muted` is separate from `volume` on purpose: mute answers 0
 * while the slider still holds the player's position, so unmuting restores the
 * level that was there. Folding mute into `volume = 0` would lose it.
 *
 * WHAT THE OWNER DOES WITH IT
 *
 * `DoomScene::applyAudioLevels()` calls `AudioEngine::setMasterVolume()` with
 * `audioMasterVolume()`. That is the engine's one gain and it is applied to the
 * accumulated mix, i.e. to the SFX voices and the music tracks together, which
 * is why the level is applied there exactly once. `MusicPlayer::setMasterVolume()`
 * is the same call (it forwards to the engine), so naming the engine is naming
 * the one that actually scales the mix.
 *
 * DOOM's own `snd_SfxVolume` stays at `S_MAX_VOLUME`: `S_StartSoundAtVolume()`
 * already multiplies every event by `volume / 127`, so passing the menu level
 * there as well would attenuate SFX by the square of the setting (at 1/15 that
 * is -43 dB) while the music only saw one of the two. One knob, one gain stage.
 */

#include <cstdint>

#include "m_menu.h"

namespace doom {
namespace doomc {

/// Upstream's and this port's full-scale level (`S_MAX_VOLUME` in s_sound.h).
constexpr int kAudioLevelMax = 127;

/**
 * @brief The play level for @p volume and @p muted, on DOOM's 0..127 scale.
 *
 * @param volume  The menu's 0..M_VOLUME_MAX; out-of-range is clamped rather
 *                than wrapped, so a hand-built setting is still a legal level.
 * @param muted   true silences everything and returns 0.
 * @return 0 when muted or at volume 0, @ref kAudioLevelMax at M_VOLUME_MAX.
 *
 * The division is the menu's own resolution: 0..15 onto 0..127 so that the
 * slider's ends land on the mixer's ends (15 * 127 / 15 == 127) and the middle
 * is proportional (8 -> 67). It is integer arithmetic because the whole port is
 * integer on the frame path.
 */
constexpr int audioLevel(std::uint8_t volume, bool muted) {
    if (muted) {
        return 0;
    }
    if (volume > M_VOLUME_MAX) {
        volume = M_VOLUME_MAX;
    }
    return static_cast<int>(volume) * kAudioLevelMax / M_VOLUME_MAX;
}

/**
 * @brief The same level as the 0.0..1.0 ratio `AudioEngine::setMasterVolume()`
 *        takes.
 * @param volume The menu's 0..M_VOLUME_MAX.
 * @param muted  true gives 0.0: silence, for both SFX and music.
 * @return @p volume's level divided by @ref kAudioLevelMax.
 */
constexpr float audioMasterVolume(std::uint8_t volume, bool muted) {
    return static_cast<float>(audioLevel(volume, muted))
           / static_cast<float>(kAudioLevelMax);
}

}  // namespace doomc
}  // namespace doom
