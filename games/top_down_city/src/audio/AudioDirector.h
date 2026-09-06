#pragma once
#include <cstddef>
#include <cstdint>

#include "game/rules/AudioCues.h"

#if PIXELROOT32_ENABLE_AUDIO
#include <audio/AudioEngine.h>
#include <audio/MusicPlayer.h>
#include <pixelroot32/apu/AudioMusicTypes.h>
#endif

/**
 * @brief The one file in this demo that knows what an AudioEngine is.
 *
 * `game/rules/AudioCues.h` owns every table and decides every transition;
 * this class owns the two engine handles and turns those decisions into calls
 * the engine understands. A scene learns neither type -- it only ever calls
 * `AudioDirector::instance()`.
 */
namespace top_down_city {

class AudioDirector {
public:
    /// One instance for the whole demo: two call sites holding two cooldown
    /// clocks for the same cue is the failure this prevents.
    static AudioDirector& instance();

#if PIXELROOT32_ENABLE_AUDIO
    /**
     * @brief Wire the two engine handles this director will ever call. Both
     *        pointers are non-owning.
     *
     * Also sets the master volume and the shared radio tempo
     * (`city_radio::kRadioBpm`, ADR-11) once: both cheap here, both wrong if
     * repeated per track.
     */
    void bind(pixelroot32::audio::AudioEngine* engine,
              pixelroot32::audio::MusicPlayer* music);
#endif

    /**
     * @brief Play a one-shot cue, if it is not still cooling down.
     *
     * A no-op when disabled, when the director was never bound (a flag-off or
     * host build), or when `audio_cues::cooldownMsFor` has not run out.
     */
    void playCue(audio_cues::Cue cue);

    /**
     * @brief Pitched by the resulting star count, once per real level-up.
     * @param stars The wanted-star count AFTER the crime was reported.
     *
     * Escalation has to be audible on the way up: a player who never hears
     * level 3 arrive learns nothing from it.
     */
    void playWantedUp(std::uint8_t stars);

    /**
     * @brief The one entry point for a footfall.
     * @param running Which gait's stride and sample to use.
     *
     * Call only on a logic step where the sprite actually MOVED -- never on a
     * step where the player held a direction into a wall. Ages the stride clock
     * and decides whether a footfall is due (`audio_cues::footstepDue`); when
     * it is, still subject to the ADR-14 admission gate and to
     * `Cue::Footstep`'s own cooldown row.
     */
    void playFootstep(bool running);

    /**
     * @brief The one entry point for the whole music model.
     * @param plan What the player should be hearing, from
     *        `audio_cues::musicPlanFor`.
     *
     * A no-op unless `audio_cues::planChanged` says the plan differs from
     * last frame's: a settled plan re-issuing play() would restart the
     * station from note 0 every frame. ADR-8: the composed track always
     * substitutes `secondVoice` with the siren or nullptr, never reading the
     * station's own. ADR-3: must be called once per frame, before the
     * scene's early returns.
     */
    void setMusicPlan(audio_cues::MusicPlan plan);

    /**
     * @brief A traffic car crossing into earshot.
     * @param high Which of the two pass variants -- picked by the passing
     *        car's colour parity (ADR-15 17.2), a scene-side decision the
     *        cooldown row and the admission gate know nothing about.
     *
     * A second entry point for one `Cue`, not a second cue, sharing
     * `Cue::TrafficPass`'s cooldown row.
     */
    void playTrafficPass(bool high);

    /// One fixed logic step: ages every cue's cooldown toward zero.
    void update(unsigned long dtMs);

    /// Silences the transport unconditionally, for a scene reset. The plan
    /// self-heals on the next frame regardless, so this is belt-and-braces.
    void stopMusic();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    /// Multiplies every cue's `AudioEvent::volume` before it reaches the
    /// engine. Does not touch the engine's own master volume.
    void setSfxVolume(float volume);

    /// Every cooldown back to zero, so a cue cooling down before a respawn is
    /// not still silenced after one.
    void resetCooldowns();

    [[nodiscard]] std::uint16_t cooldownRemainingMs(
        audio_cues::Cue cue) const;

private:
    AudioDirector() = default;

    bool enabled_ = true;
    /// {None, false} == silence, which is also the correct default: a first
    /// frame that computes silence does nothing, because planChanged against
    /// this value is false.
    audio_cues::MusicPlan plan_{};
    std::uint16_t cooldownMs_[
        static_cast<std::size_t>(audio_cues::Cue::Count)] = {};

    /// Logic steps of movement since the last footfall -- see playFootstep.
    std::uint8_t footstepClock_ = 0;
    /// ADR-14: the shared voice-admission gate every cue submission consults
    /// before playing and charges after. Closes the ambient window uniformly
    /// for every Essential cue, and caps the whole Ambient tier at one live
    /// voice via its own busy hold. Aged in update().
    audio_cues::SfxBudget budget_{};

#if PIXELROOT32_ENABLE_AUDIO
    pixelroot32::audio::AudioEngine* engine_ = nullptr;
    pixelroot32::audio::MusicPlayer* music_ = nullptr;
    float sfxVolume_ = 0.65f;

    /// ADR-1: MusicPlayer::play() stores a live pointer the sequencer
    /// dereferences per tick, on the audio thread. Overwriting a single
    /// composed track in place would be a data race on notes/count, so
    /// setMusicPlan always writes the slot that is NOT live, plays it, then
    /// flips composedSlot_. 64 bytes, no allocation.
    pixelroot32::audio::MusicTrack composed_[2]{};
    std::uint8_t composedSlot_ = 0;
#endif
};

}  // namespace top_down_city
