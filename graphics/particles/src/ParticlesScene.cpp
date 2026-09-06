/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "ParticlesScene.h"

#include <core/Engine.h>
#include <core/Log.h>
#include <graphics/particles/ParticlePresets.h>
#include <math/Scalar.h>
#include <math/Vector2.h>

#include <cstdio>
#include <new>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace particles_demo {

namespace gfx = pr32::graphics;
namespace parts = pr32::graphics::particles;

using gfx::Color;
using pr32::math::Scalar;
using pr32::math::Vector2;

namespace {

/// The five presets, in the order ParticlePresets.h declares them. The configs
/// are referenced, not copied: every number the HUD shows is read back through
/// these pointers, so the screen cannot disagree with the header.
constexpr PresetEntry kPresets[] = {
    {"FIRE",      &parts::ParticlePresets::Fire},
    {"EXPLOSION", &parts::ParticlePresets::Explosion},
    {"SPARKS",    &parts::ParticlePresets::Sparks},
    {"SMOKE",     &parts::ParticlePresets::Smoke},
    {"DUST",      &parts::ParticlePresets::Dust}
};
static_assert(sizeof(kPresets) / sizeof(kPresets[0]) == 5,
              "the preset table must list every entry of ParticlePresets.h");

/// Navy rather than black. Explosion and Smoke both end on Color::Black, and a
/// fade that lands on the background colour is a fade you cannot watch finish.
constexpr Color kBackground = Color::Navy;
constexpr Color kHeaderColor = Color::White;
constexpr Color kValueColor = Color::Cyan;
constexpr Color kHintColor = Color::Gray;
constexpr Color kActiveColor = Color::Green;
constexpr Color kMarkerColor = Color::Gray;

/// Formats a Scalar with two fractional digits.
///
/// Scalar is float on the SDL2 build and Fixed16 on a target compiled without
/// an FPU; both convert explicitly to float. This runs only when a preset
/// changes, never inside the frame loop.
void formatScalar(char* out, std::size_t outSize, Scalar value) {
    float v = static_cast<float>(value);
    const bool negative = v < 0.0f;
    if (negative) {
        v = -v;
    }
    const int hundredths = static_cast<int>(v * 100.0f + 0.5f);
    std::snprintf(out, outSize, "%s%d.%02d", negative ? "-" : "", hundredths / 100, hundredths % 100);
}

/// Degrees are whole numbers in every shipped preset, so they print as integers.
int scalarToDegrees(Scalar value) {
    return static_cast<int>(static_cast<float>(value));
}

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

} // namespace

ParticlesScene::~ParticlesScene() {
    // emitterStorage_ is raw bytes: whatever was constructed into it has to be
    // destroyed by hand, exactly once.
    if (emitter_ != nullptr) {
        emitter_->~ParticleEmitter();
        emitter_ = nullptr;
    }
}

const ParticlesScene::ParticleConfig& ParticlesScene::currentConfig() const {
    return *kPresets[presetIndex_].config;
}

void ParticlesScene::init() {
    // Scene::init() runs resetState(), which empties the entity list. Anything
    // this scene wants in that list has to be added again below.
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    presetIndex_ = 0;
    burstCount_ = kDefaultBurst;
    autoEmit_ = false;
    autoEmitTimer_ = 0;
    lastLiveShown_ = -1;
    lastBurstShown_ = -1;

    clearCohorts();

    rebuildEmitter();
    addEntity(emitter_);

    refreshPresetText();
    refreshStatsText();
    refreshModeText();

    pr32::core::logging::log("ParticlesScene: one emitter, five presets");
}

void ParticlesScene::clearCohorts() {
    for (int i = 0; i < kCohortSlots; ++i) {
        cohorts_[i].framesLeft = 0;
        cohorts_[i].count = 0;
    }
}

void ParticlesScene::rebuildEmitter() {
    if (emitter_ != nullptr) {
        emitter_->~ParticleEmitter();
    }

    // Placement new, not operator new: emitterStorage_ is already a member of
    // this scene, so this constructs into memory that exists and allocates
    // nothing. The address never changes, which is why the pointer the base
    // Scene holds in its entity list stays valid across a preset switch.
    emitter_ = new (emitterStorage_) ParticleEmitter(Vector2(kEmitterX, kEmitterY), currentConfig());
}

void ParticlesScene::selectPreset(int index) {
    presetIndex_ = (index + kPresetCount) % kPresetCount;

    // ParticleConfig is a constructor argument the emitter copies into a
    // private member and there is no setter, so a new config means a new
    // emitter. A new emitter starts with an empty pool, which is why switching
    // preset clears whatever the previous one still had in the air.
    rebuildEmitter();

    clearCohorts();
    autoEmitTimer_ = 0;

    refreshPresetText();
    refreshStatsText();
}

void ParticlesScene::changeBurstCount(int delta) {
    burstCount_ = clampInt(burstCount_ + delta, kMinBurst, kMaxBurst);
}

void ParticlesScene::fireBurst(int count) {
    // burst() takes the origin explicitly. The emitter's own Entity position is
    // never consulted for emission, so one emitter can throw particles from
    // anywhere on screen.
    emitter_->burst(Vector2(kEmitterX, kEmitterY), count);
    trackBurst(count);
}

void ParticlesScene::ageCohorts() {
    for (int i = 0; i < kCohortSlots; ++i) {
        if (cohorts_[i].framesLeft > 0) {
            --cohorts_[i].framesLeft;
            if (cohorts_[i].framesLeft == 0) {
                cohorts_[i].count = 0;
            }
        }
    }
}

void ParticlesScene::trackBurst(int count) {
    const uint8_t life = currentConfig().maxLife;
    const uint8_t tracked = static_cast<uint8_t>(clampInt(count, 0, kParticleCeiling));

    int freeSlot = -1;
    int longestSlot = 0;
    for (int i = 0; i < kCohortSlots; ++i) {
        if (cohorts_[i].framesLeft == 0) {
            freeSlot = i;
            break;
        }
        if (cohorts_[i].framesLeft > cohorts_[longestSlot].framesLeft) {
            longestSlot = i;
        }
    }

    if (freeSlot >= 0) {
        cohorts_[freeSlot].framesLeft = life;
        cohorts_[freeSlot].count = tracked;
        return;
    }

    // No free slot: fold the new burst into the cohort that expires last. The
    // particles are then held in the bound for longer than they can actually
    // live, which keeps the figure on screen an over-estimate rather than
    // turning it into an under-estimate.
    Cohort& target = cohorts_[longestSlot];
    target.count = static_cast<uint8_t>(clampInt(target.count + tracked, 0, kParticleCeiling));
    if (life > target.framesLeft) {
        target.framesLeft = life;
    }
}

int ParticlesScene::liveUpperBound() const {
    int total = 0;
    for (int i = 0; i < kCohortSlots; ++i) {
        if (cohorts_[i].framesLeft > 0) {
            total += cohorts_[i].count;
        }
    }
    // The pool cannot hold more than its own array, whatever the bursts asked for.
    return clampInt(total, 0, kParticleCeiling);
}

void ParticlesScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    // isButtonPressed() is edge triggered: one press is one step.
    if (input.isButtonPressed(kButtonLeft)) {
        selectPreset(presetIndex_ - 1);
    } else if (input.isButtonPressed(kButtonRight)) {
        selectPreset(presetIndex_ + 1);
    }

    if (input.isButtonPressed(kButtonUp)) {
        changeBurstCount(kBurstStep);
    } else if (input.isButtonPressed(kButtonDown)) {
        changeBurstCount(-kBurstStep);
    }

    if (input.isButtonPressed(kButtonA)) {
        fireBurst(burstCount_);
    }

    if (input.isButtonPressed(kButtonB)) {
        autoEmit_ = !autoEmit_;
        autoEmitTimer_ = 0;
        refreshModeText();
    }

    // Counted in frames, not milliseconds, on purpose: particle life is a frame
    // count too, so both sides of the effect move on the same clock.
    if (autoEmit_) {
        ++autoEmitTimer_;
        if (autoEmitTimer_ >= kAutoEmitPeriodFrames) {
            autoEmitTimer_ = 0;
            fireBurst(kAutoEmitCount);
        }
    }

    ageCohorts();
    refreshStatsText();
}

void ParticlesScene::refreshPresetText() {
    const ParticleConfig& cfg = currentConfig();

    std::snprintf(headerText_, sizeof(headerText_), "%d/%d %s",
                  presetIndex_ + 1, kPresetCount, kPresets[presetIndex_].name);

    char low[12];
    char high[12];

    formatScalar(low, sizeof(low), cfg.minSpeed);
    formatScalar(high, sizeof(high), cfg.maxSpeed);
    std::snprintf(speedText_, sizeof(speedText_), "SPD %s-%s", low, high);

    formatScalar(low, sizeof(low), cfg.gravity);
    formatScalar(high, sizeof(high), cfg.friction);
    std::snprintf(forceText_, sizeof(forceText_), "GRV %s FRI %s", low, high);

    std::snprintf(lifeText_, sizeof(lifeText_), "LIFE %u-%u FRM",
                  static_cast<unsigned>(cfg.minLife), static_cast<unsigned>(cfg.maxLife));

    std::snprintf(angleText_, sizeof(angleText_), "ANG %d-%d FADE %s",
                  scalarToDegrees(cfg.minAngleDeg), scalarToDegrees(cfg.maxAngleDeg),
                  cfg.fadeColor ? "ON" : "OFF");
}

void ParticlesScene::refreshStatsText() {
    const int live = liveUpperBound();
    if (live == lastLiveShown_ && burstCount_ == lastBurstShown_) {
        return;
    }
    lastLiveShown_ = live;
    lastBurstShown_ = burstCount_;

    // "<=" and not "=": the emitter keeps its Particle array private and offers
    // no live count, so this is the tightest honest figure available from here.
    std::snprintf(statsText_, sizeof(statsText_), "LIVE<=%d/%d N=%d",
                  live, kParticleCeiling, burstCount_);
}

void ParticlesScene::refreshModeText() {
    std::snprintf(modeText_, sizeof(modeText_), "A BURST  B AUTO %s", autoEmit_ ? "ON" : "OFF");
}

void ParticlesScene::drawEmitterMarker(gfx::Renderer& renderer) const {
    renderer.drawLine(kEmitterX - 3, kEmitterY, kEmitterX + 3, kEmitterY, kMarkerColor);
    renderer.drawLine(kEmitterX, kEmitterY - 3, kEmitterX, kEmitterY + 3, kMarkerColor);
}

void ParticlesScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, and the emitter is one of them, so filling after it would erase
    // the particles. init() and update() are the overrides that go base-first.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), kBackground);

    Scene::draw(renderer);

    drawEmitterMarker(renderer);

    renderer.drawTextCentered(headerText_, kHeaderY, kHeaderColor, 1);
    renderer.drawText(statsText_, kTextX, kStatsY, kValueColor, 1);

    renderer.drawText(speedText_, kTextX, kSpeedY, kValueColor, 1);
    renderer.drawText(forceText_, kTextX, kForceY, kValueColor, 1);
    renderer.drawText(lifeText_, kTextX, kLifeY, kValueColor, 1);
    renderer.drawText(angleText_, kTextX, kAngleY, kValueColor, 1);

    renderer.drawText("LR PRESET  UD SIZE", kTextX, kControlsY, kHintColor, 1);
    renderer.drawText(modeText_, kTextX, kModeY, autoEmit_ ? kActiveColor : kHintColor, 1);
}

} // namespace particles_demo
