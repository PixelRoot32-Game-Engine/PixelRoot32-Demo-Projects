#pragma once

#include <cstdint>

#include "assets/audio/PoolMusic.h"
#include "assets/audio/PoolSfx.h"

namespace pool {

/**
 * @class PoolAudio
 * @brief Music + SFX director for the Pool demo (bomberbot-style).
 *
 * Owns nothing that ticks the simulation: the scene calls playStageMusic()
 * on stage changes, playSfx() on game events, and update() every tick for
 * sequenced-step delays and per-effect cooldowns. Everything engine-related
 * is fenced by PIXELROOT32_ENABLE_AUDIO, so the scene compiles (silent) with
 * the flag at 0; flip it to 1 in lib/platformio.ini for sound.
 */
class PoolAudio {
public:
    /// @brief Loops the stage track (bass + optional harmony + groove ride along).
    void playStageMusic(uint8_t stage);
    /// @brief One-shot fanfare; the sequencer goes quiet afterwards.
    void playWinJingle();
    /// @brief One-shot descending line; the sequencer goes quiet afterwards.
    void playLoseJingle();
    /// @brief Stops the sequencer (menu silence, shutdown).
    void stopMusic();
    /// @brief Pauses/resumes the sequencer voice without losing its position.
    void setMusicPaused(bool paused);

    /// @brief Mutes stage music and jingles (user pref, survives reset()).
    void setMusicMuted(bool muted);
    /// @brief Mutes all sound effects (user pref, survives reset()).
    void setSfxMuted(bool muted);
    [[nodiscard]] bool isMusicMuted() const { return musicMuted_; }
    [[nodiscard]] bool isSfxMuted() const { return sfxMuted_; }

    /// @brief Fires an effect unless its cooldown is still running.
    void playSfx(PoolSfx id);
    /// @brief Ticks cooldowns and delayed sequence steps. Call every update().
    void update(unsigned long dtMs);
    /// @brief Drops cooldowns and pending steps (new run, stage retry).
    void reset();

private:
    void dispatch(const pixelroot32::audio::AudioEvent& event);
    void enqueueDelayed(float delaySec, const pixelroot32::audio::AudioEvent& event);

    unsigned long cooldownMs_[static_cast<size_t>(PoolSfx::Count)] = {};
    bool musicMuted_ = false;
    bool sfxMuted_ = false;
    uint8_t lastStage_ = 0;

#if PIXELROOT32_ENABLE_AUDIO
    static constexpr uint8_t kMaxPending = 8;

    struct PendingStep {
        unsigned long remainingMs = 0;
        pixelroot32::audio::AudioEvent event{};
        bool active = false;
    };

    float sfxVolume_ = 0.70f;
    PendingStep pending_[kMaxPending] = {};
#endif
};

}  // namespace pool
