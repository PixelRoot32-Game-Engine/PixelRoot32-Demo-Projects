/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "SfxBankScene.h"

#include <audio/ApuCore.h>
#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace sfx_bank {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// Upper bound on one integration step. A long stall — a breakpoint, the first
/// frame after init — must not fire the whole queue at once.
constexpr int32_t kMaxStepMs = 100;

/// Highlight bar for the selected row.
constexpr Color kSelectedFill = Color::Blue;
constexpr Color kSelectedText = Color::White;
constexpr Color kNormalText = Color::Gray;

} // namespace

// ---------------------------------------------------------------------------
// TimedSfxDelayScheduler
// ---------------------------------------------------------------------------

void TimedSfxDelayScheduler::schedule(float delaySec, const pr32::audio::AudioEvent& event) {
    if (count_ >= kCapacity) {
        // Full. Drop the newest rather than evicting a step already counting
        // down: the player has begun hearing that effect, and cutting it short
        // would be a worse lie than never starting this one. The scene prints
        // dropped_ so the ceiling is visible instead of silent.
        ++dropped_;
        return;
    }

    int32_t delayMs = static_cast<int32_t>(delaySec * 1000.0f);
    if (delayMs < 1) {
        delayMs = 1;
    } else if (delayMs > kMaxDelayMs) {
        delayMs = kMaxDelayMs;
    }

    slots_[count_].remainingMs = delayMs;
    slots_[count_].event = event;
    ++count_;
}

void TimedSfxDelayScheduler::update(unsigned long deltaTime) {
    if (count_ == 0 || engine_ == nullptr) {
        return;
    }

    int32_t step = static_cast<int32_t>(deltaTime);
    if (step <= 0) {
        return;
    }
    if (step > kMaxStepMs) {
        step = kMaxStepMs;
    }

    // Swap-with-last removal, so a fired slot is reclaimed without shifting the
    // array. The entry swapped in came from a higher index and has not been
    // stepped yet this frame, which is exactly why `i` does not advance here.
    uint8_t i = 0;
    while (i < count_) {
        slots_[i].remainingMs -= step;
        if (slots_[i].remainingMs > 0) {
            ++i;
            continue;
        }

        engine_->playEvent(slots_[i].event);
        --count_;
        slots_[i] = slots_[count_];
    }
}

// ---------------------------------------------------------------------------
// SfxBankScene
// ---------------------------------------------------------------------------

void SfxBankScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    // The scheduler is a plain member of the scene, so its six slots are part
    // of the scene's own sizeof. bind() only records the engine address.
    delays_.clear();
    delays_.bind(&engine.getAudioEngine());

    selected_ = 0;
    loopArmed_ = false;

    // Force the first refreshHud() to rebuild every string.
    hudPending_ = 0xFF;
    hudDropped_ = 0xFFFF;

    std::snprintf(statusText_, sizeof(statusText_), "READY");
    statusColor_ = Color::Green;
    refreshHud();

    pr32::core::logging::log("SfxBankScene: bank ready");
}

void SfxBankScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    // isButtonPressed() is edge triggered, so one press is one action.
    if (input.isButtonPressed(kButtonUp)) {
        moveSelection(-1);
    } else if (input.isButtonPressed(kButtonDown)) {
        moveSelection(1);
    }

    if (input.isButtonPressed(kButtonA)) {
        fireSelected();
    } else if (input.isButtonPressed(kButtonB)) {
        toggleLoop();
    }

    // The scheduler's clock is this scene's clock. Nothing else in the engine
    // knows a sequence step is waiting.
    delays_.update(deltaTime);

    refreshHud();
}

void SfxBankScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, so filling after it would overpaint them. init() and update()
    // are the overrides that call their base first, not draw().
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("SFX BANK", 2, Color::White, 1);

    for (uint8_t i = 0; i < DemoSfxBank::kCount; ++i) {
        const int rowY = kListY + i * kRowHeight;
        const bool isSelected = (i == selected_);

        if (isSelected) {
            renderer.drawFilledRectangle(2, rowY - 1, 124, kRowHeight, kSelectedFill);
        }

        const SfxId id = static_cast<SfxId>(i);
        Color rowColor = isSelected ? kSelectedText : kNormalText;
        if (loopArmed_ && id == SfxId::AlarmLoop) {
            rowColor = Color::Yellow;
        }
        renderer.drawText(DemoSfxBank::name(id), kNameX, rowY, rowColor, 1);
    }

    renderer.drawLine(2, kSeparatorY, 125, kSeparatorY, Color::Gray);

    renderer.drawText(layersText_, kTextX, kLayersTextY, Color::White, 1);
    renderer.drawText(pendingText_, kTextX, kPendingTextY, pendingColor_, 1);
    renderer.drawText(statusText_, kTextX, kStatusTextY, statusColor_, 1);
    renderer.drawText(controlsText_, kTextX, kControlsTextY, Color::Gray, 1);
}

void SfxBankScene::moveSelection(int8_t delta) {
    const int8_t count = static_cast<int8_t>(DemoSfxBank::kCount);
    int8_t next = static_cast<int8_t>(selected_) + delta;
    if (next < 0) {
        next = static_cast<int8_t>(count - 1);
    } else if (next >= count) {
        next = 0;
    }
    selected_ = static_cast<uint8_t>(next);
}

void SfxBankScene::fireSelected() {
    const SfxId id = static_cast<SfxId>(selected_);

    if (DemoSfxBank::isLooping(id)) {
        // A looping entry played with A would hold a voice with no way back:
        // only B tracks the loop and only B can stop it.
        std::snprintf(statusText_, sizeof(statusText_), "USE B FOR LOOP");
        statusColor_ = Color::Orange;
        return;
    }

    const uint16_t droppedBefore = delays_.dropped();

    // The whole point of the demo. playSfxBank plays layerCount(id) events
    // right now through AudioEngine::playEvent, and hands every sequence step
    // with delaySec > 0 to delays_. It owns no clock and no storage.
    pr32::audio::playSfxBank<DemoSfxBank>(engine.getAudioEngine(), id, delays_);

    if (delays_.dropped() != droppedBefore) {
        std::snprintf(statusText_, sizeof(statusText_), "STEPS DROPPED");
        statusColor_ = Color::Red;
    } else {
        std::snprintf(statusText_, sizeof(statusText_), "PLAY %s", DemoSfxBank::name(id));
        statusColor_ = Color::Green;
    }
}

void SfxBankScene::toggleLoop() {
    if (loopArmed_) {
        stopAllSfxVoices();
        delays_.clear();
        loopArmed_ = false;
        std::snprintf(statusText_, sizeof(statusText_), "SFX VOICES STOP");
        statusColor_ = Color::Cyan;
        return;
    }

    const SfxId id = static_cast<SfxId>(selected_);
    if (!DemoSfxBank::isLooping(id)) {
        std::snprintf(statusText_, sizeof(statusText_), "NOT A LOOP");
        statusColor_ = Color::Orange;
        return;
    }

    pr32::audio::playSfxBank<DemoSfxBank>(engine.getAudioEngine(), id, delays_);
    loopArmed_ = true;
    std::snprintf(statusText_, sizeof(statusText_), "LOOP ON");
    statusColor_ = Color::Yellow;
}

void SfxBankScene::stopAllSfxVoices() {
    // AudioEngine::playEvent returns void, so nothing in the public API ever
    // tells the game which voice an effect landed on. STOP_CHANNEL needs a
    // slot index, so the only honest way to stop a loop the game did not place
    // by hand is to stop the whole SFX sub-pool: voices SFX_VOICE_BASE (4)
    // through 7, with 0..3 left alone because those belong to the music
    // sequencer. A game that needs one-loop-at-a-time control has to place its
    // looping voices itself rather than through playSfxBank.
    auto& audio = engine.getAudioEngine();
    for (int slot = pr32::audio::ApuCore::SFX_VOICE_BASE;
         slot < pr32::audio::ApuCore::SFX_VOICE_BASE + pr32::audio::ApuCore::SFX_VOICE_COUNT;
         ++slot) {
        pr32::audio::AudioCommand stop{};
        stop.type = pr32::audio::AudioCommandType::STOP_CHANNEL;
        stop.channelIndex = static_cast<uint8_t>(slot);
        audio.submitCommand(stop);
    }
}

void SfxBankScene::refreshHud() {
    const uint8_t pending = delays_.pending();
    const uint16_t dropped = delays_.dropped();

    // Rebuild only on the frames where something actually moved: snprintf is
    // the most expensive thing in this scene, and it has no business running
    // sixty times a second to produce the same four strings.
    if (pending == hudPending_ && dropped == hudDropped_ && selected_ == hudSelected_ &&
        loopArmed_ == hudLoopArmed_) {
        return;
    }
    hudPending_ = pending;
    hudDropped_ = dropped;
    hudSelected_ = selected_;
    hudLoopArmed_ = loopArmed_;

    const SfxId id = static_cast<SfxId>(selected_);
    std::snprintf(layersText_,
                  sizeof(layersText_),
                  "LAYERS %u  STEPS %u",
                  static_cast<unsigned>(DemoSfxBank::layerCount(id)),
                  static_cast<unsigned>(DemoSfxBank::sequenceStepCount(id)));

    std::snprintf(controlsText_,
                  sizeof(controlsText_),
                  "UP/DN A:PLAY B:%s",
                  loopArmed_ ? "STOP" : "LOOP");

    std::snprintf(pendingText_,
                  sizeof(pendingText_),
                  "PENDING %u/%u DROP %u",
                  static_cast<unsigned>(pending),
                  static_cast<unsigned>(TimedSfxDelayScheduler::kCapacity),
                  static_cast<unsigned>(dropped > 99 ? 99 : dropped));

    if (dropped > 0) {
        pendingColor_ = Color::Red;
    } else if (pending == TimedSfxDelayScheduler::kCapacity) {
        pendingColor_ = Color::Orange;
    } else {
        pendingColor_ = Color::White;
    }
}

} // namespace sfx_bank
