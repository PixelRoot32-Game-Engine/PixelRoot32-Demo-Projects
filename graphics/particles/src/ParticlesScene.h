/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#if !PIXELROOT32_ENABLE_PARTICLES
#error "particles needs -D PIXELROOT32_ENABLE_PARTICLES=1 (see lib/platformio.ini)"
#endif

#include <graphics/particles/ParticleConfig.h>
#include <graphics/particles/ParticleEmitter.h>

#include <cstddef>
#include <cstdint>

namespace particles_demo {

/**
 * @struct PresetEntry
 * @brief One row of the preset carousel: a display name and the engine config.
 *
 * The config is held by pointer straight into ParticlePresets.h, never copied
 * with the numbers retyped. Everything the HUD prints is read back off this
 * pointer at runtime, so the figures on screen cannot drift away from the
 * header they came from.
 */
struct PresetEntry {
    const char* name;                                                   ///< Shown in the header line.
    const pixelroot32::graphics::particles::ParticleConfig* config;     ///< Points into ParticlePresets.h.
};

/**
 * @class ParticlesScene
 * @brief Walks the engine's five particle presets, one at a time.
 *
 * A single ParticleEmitter sits at the middle of the screen. Left/Right swap
 * which ParticlePresets entry it was built from, A fires one burst, B toggles
 * a small burst every few frames, and Up/Down resize the burst against the
 * emitter's own ceiling. The four config lines under the emitter are formatted
 * from the live ParticleConfig, so the effect and its parameters are on screen
 * together.
 *
 * ParticleConfig is a constructor argument the emitter copies into a private
 * member: there is no setConfig(). Switching preset therefore destroys the
 * emitter and constructs a new one *in place*, inside emitterStorage_ — a
 * member buffer, so no heap is touched and the entity pointer already held by
 * the base Scene stays valid.
 */
class ParticlesScene : public pixelroot32::core::Scene {
public:
    ~ParticlesScene() override;

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    using ParticleEmitter = pixelroot32::graphics::particles::ParticleEmitter;
    using ParticleConfig = pixelroot32::graphics::particles::ParticleConfig;

    /// Fire, Explosion, Sparks, Smoke, Dust — the whole of ParticlePresets.h.
    static constexpr int kPresetCount = 5;

    /// The emitter's own ceiling, straight from the macro in ParticleEmitter.h.
    /// It is a #define, not a constant in ParticleConfig.h, and redefining it
    /// here would silently disagree with the array the engine compiled.
    static constexpr int kParticleCeiling = MAX_PARTICLES_PER_EMITTER;

    /// Where burst() is told to spawn from. burst() takes the position as an
    /// argument; the emitter's own Entity position is not used for emission.
    static constexpr int kEmitterX = 64;
    static constexpr int kEmitterY = 48;

    static constexpr int kMinBurst = 1;
    static constexpr int kMaxBurst = kParticleCeiling;
    static constexpr int kDefaultBurst = 16;
    static constexpr int kBurstStep = 1;

    /// Continuous mode: a small burst on this cadence, so the pool refills as
    /// fast as it drains and the effect reads as a stream rather than a puff.
    static constexpr int kAutoEmitPeriodFrames = 4;
    static constexpr int kAutoEmitCount = 4;

    /// Button indices, in the order InputConfig takes them in the platform
    /// headers: Up, Down, Left, Right, A, B.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) -----
    static constexpr int kTextX = 4;
    static constexpr int kHeaderY = 1;
    static constexpr int kStatsY = 10;
    static constexpr int kSpeedY = 82;
    static constexpr int kForceY = 90;
    static constexpr int kLifeY = 98;
    static constexpr int kAngleY = 106;
    static constexpr int kControlsY = 114;
    static constexpr int kModeY = 121;

    /**
     * @struct Cohort
     * @brief One burst, tracked only so the HUD can bound the live count.
     *
     * ParticleEmitter keeps its Particle array private and exposes no count,
     * so the exact number alive is not observable from here. A cohort records
     * how many particles a burst asked for and the longest they could live
     * (config.maxLife, in frames); the HUD sums the cohorts still inside that
     * window. That is an upper bound and is labelled as one on screen: real
     * particles also die early, either by drawing a life shorter than maxLife
     * or by leaving the screen, and burst() spawns fewer than asked when the
     * pool is already full.
     */
    struct Cohort {
        uint8_t framesLeft; ///< Frames until every particle of this burst must be gone.
        uint8_t count;      ///< Particles the burst asked for.
    };

    /// Enough cohorts that a long-lived preset under continuous emission is
    /// still tracked burst-by-burst. When they run out, the newest count is
    /// merged into the longest-lived cohort, which keeps the figure an upper
    /// bound rather than losing particles from it.
    static constexpr int kCohortSlots = 24;

    const ParticleConfig& currentConfig() const;

    void rebuildEmitter();
    void selectPreset(int index);
    void changeBurstCount(int delta);
    void fireBurst(int count);

    void clearCohorts();
    void ageCohorts();
    void trackBurst(int count);
    int liveUpperBound() const;

    void refreshPresetText();
    void refreshStatsText();
    void refreshModeText();

    void drawEmitterMarker(pixelroot32::graphics::Renderer& renderer) const;

    /// Storage for the one emitter. A member, so the pool's ~2 KB is part of
    /// this scene's sizeof and switching preset allocates nothing.
    alignas(ParticleEmitter) unsigned char emitterStorage_[sizeof(ParticleEmitter)];

    /// Points into emitterStorage_ once init() has constructed it; null before.
    ParticleEmitter* emitter_ = nullptr;

    int presetIndex_ = 0;
    int burstCount_ = kDefaultBurst;
    bool autoEmit_ = false;
    int autoEmitTimer_ = 0;

    Cohort cohorts_[kCohortSlots] = {};

    /// Last values the stats line was built from, so snprintf runs on change
    /// rather than on every frame.
    int lastLiveShown_ = -1;
    int lastBurstShown_ = -1;

    char headerText_[24] = {0};
    char statsText_[24] = {0};
    char speedText_[24] = {0};
    char forceText_[24] = {0};
    char lifeText_[24] = {0};
    char angleText_[24] = {0};
    char modeText_[24] = {0};
};

} // namespace particles_demo
