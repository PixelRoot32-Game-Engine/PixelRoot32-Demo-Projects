// PoolAudio.cpp - see PoolAudio.h.
#include "audio/PoolAudio.h"

#if PIXELROOT32_ENABLE_AUDIO
#include <core/Engine.h>

extern pixelroot32::core::Engine engine;

namespace {
namespace a = pixelroot32::audio;

// ESP32 DAC bricks above ~4.5 kHz (bomberbot's measured ceiling); every pool
// event is musical (< 2.4 kHz) but the clamp keeps future edits honest.
constexpr float kDacMaxFrequencyHz = 4500.0f;

void clampEventForDac(a::AudioEvent& event) {
#if defined(PLATFORM_ESP32DEV) || defined(PLATFORM_ESP32S3)
    if (event.frequency > kDacMaxFrequencyHz) {
        event.frequency = kDacMaxFrequencyHz;
    }
    if (event.sweepEndHz > kDacMaxFrequencyHz) {
        event.sweepEndHz = kDacMaxFrequencyHz;
    }
#else
    (void)event;
#endif
}

}  // namespace
#endif

namespace pool {

void PoolAudio::playStageMusic(uint8_t stage) {
#if PIXELROOT32_ENABLE_AUDIO
    lastStage_ = stage;
    if (musicMuted_) {
        return;
    }
    engine.getMusicPlayer().play(music::trackForStage(stage));
    engine.getMusicPlayer().setTempoFactor(music::tempoForStage(stage));
#else
    (void)stage;
#endif
}

void PoolAudio::playWinJingle() {
#if PIXELROOT32_ENABLE_AUDIO
    if (musicMuted_) {
        return;
    }
    engine.getMusicPlayer().play(music::WIN_JINGLE);
    engine.getMusicPlayer().setTempoFactor(1.0f);
#endif
}

void PoolAudio::playLoseJingle() {
#if PIXELROOT32_ENABLE_AUDIO
    if (musicMuted_) {
        return;
    }
    engine.getMusicPlayer().play(music::LOSE_JINGLE);
    engine.getMusicPlayer().setTempoFactor(1.0f);
#endif
}

void PoolAudio::stopMusic() {
#if PIXELROOT32_ENABLE_AUDIO
    lastStage_ = 0;
    engine.getMusicPlayer().stop();
#endif
}

void PoolAudio::setMusicPaused(bool paused) {
#if PIXELROOT32_ENABLE_AUDIO
    if (paused) {
        engine.getMusicPlayer().pause();
    } else {
        engine.getMusicPlayer().resume();
    }
#else
    (void)paused;
#endif
}

void PoolAudio::playSfx(PoolSfx id) {
    if (sfxMuted_) {
        return;
    }
    const size_t idx = static_cast<size_t>(id);
    if (idx >= static_cast<size_t>(PoolSfx::Count)) {
        return;
    }
    if (cooldownMs_[idx] > 0) {
        return;
    }
#if PIXELROOT32_ENABLE_AUDIO
    const uint8_t layers = PoolSfxBank::layerCount(id);
    for (uint8_t i = 0; i < layers; ++i) {
        dispatch(PoolSfxBank::layerEvent(id, i));
    }
    const uint8_t steps = PoolSfxBank::sequenceStepCount(id);
    for (uint8_t i = 0; i < steps; ++i) {
        const PoolSfxBank::SequenceStep step = PoolSfxBank::sequenceStep(id, i);
        enqueueDelayed(step.delaySec, step.event);
    }
#endif
    cooldownMs_[idx] = PoolSfxBank::cooldownMs(id);
}

void PoolAudio::update(unsigned long dtMs) {
    for (size_t i = 0; i < static_cast<size_t>(PoolSfx::Count); ++i) {
        if (cooldownMs_[i] > dtMs) {
            cooldownMs_[i] -= dtMs;
        } else {
            cooldownMs_[i] = 0;
        }
    }
#if PIXELROOT32_ENABLE_AUDIO
    for (uint8_t i = 0; i < kMaxPending; ++i) {
        if (!pending_[i].active) {
            continue;
        }
        if (pending_[i].remainingMs > dtMs) {
            pending_[i].remainingMs -= dtMs;
            continue;
        }
        pending_[i].active = false;
        pending_[i].remainingMs = 0;
        dispatch(pending_[i].event);
    }
#else
    (void)dtMs;
#endif
}

void PoolAudio::setMusicMuted(bool muted) {
    musicMuted_ = muted;
#if PIXELROOT32_ENABLE_AUDIO
    if (muted) {
        engine.getMusicPlayer().stop();
    } else if (lastStage_ >= 1 && lastStage_ <= 10) {
        playStageMusic(lastStage_);
    }
#endif
}

void PoolAudio::setSfxMuted(bool muted) {
    sfxMuted_ = muted;
}

void PoolAudio::reset() {
    for (size_t i = 0; i < static_cast<size_t>(PoolSfx::Count); ++i) {
        cooldownMs_[i] = 0;
    }
#if PIXELROOT32_ENABLE_AUDIO
    for (uint8_t i = 0; i < kMaxPending; ++i) {
        pending_[i].active = false;
        pending_[i].remainingMs = 0;
    }
#endif
}

void PoolAudio::dispatch(const pixelroot32::audio::AudioEvent& event) {
#if PIXELROOT32_ENABLE_AUDIO
    pixelroot32::audio::AudioEvent voiced = event;
    voiced.volume *= sfxVolume_;
    clampEventForDac(voiced);
    engine.getAudioEngine().playEvent(voiced);
#else
    (void)event;
#endif
}

void PoolAudio::enqueueDelayed(float delaySec, const pixelroot32::audio::AudioEvent& event) {
#if PIXELROOT32_ENABLE_AUDIO
    if (delaySec <= 0.0f) {
        dispatch(event);
        return;
    }
    unsigned long delayMs = static_cast<unsigned long>(delaySec * 1000.0f + 0.5f);
    if (delayMs == 0) {
        delayMs = 1;
    }
    for (uint8_t i = 0; i < kMaxPending; ++i) {
        if (!pending_[i].active) {
            pending_[i].active = true;
            pending_[i].remainingMs = delayMs;
            pending_[i].event = event;
            return;
        }
    }
    dispatch(event);  // Queue full — play now rather than drop the step.
#else
    (void)delaySec;
    (void)event;
#endif
}

}  // namespace pool
