/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <audio/AudioEngine.h>
#include <audio/AudioTypes.h>
#include <audio/SfxBankPlayback.h>
#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

#include "assets/DemoSfxBank.h"

#if !PIXELROOT32_ENABLE_AUDIO
#error "sfx_bank needs -D PIXELROOT32_ENABLE_AUDIO=1 (see lib/platformio.ini)"
#endif

namespace sfx_bank {

/**
 * @class TimedSfxDelayScheduler
 * @brief The piece `playSfxBank` deliberately does not own: a real delay
 *        scheduler, driven by the scene's own `update(deltaTime)`.
 *
 * `playSfxBank` fires an effect's layers immediately and hands every sequence
 * step with `delaySec > 0` to a `SfxDelayScheduler`. The engine ships two
 * implementations and neither is what a game wants:
 * `NullSfxDelayScheduler` silently drops every delayed step, and
 * `ImmediateSfxDelayScheduler` plays them all at t=0, which collapses a
 * three-note fanfare into one chord. Timing belongs to whatever already has a
 * clock — in a game, the scene.
 *
 * This one is a fixed array of pending steps with a live count. It never
 * allocates, never grows, and never touches a container: `schedule()` copies
 * the event into a free slot, `update()` counts every slot down and calls
 * `AudioEngine::playEvent` on the ones that reach zero.
 *
 * When every slot is taken, `schedule()` drops the NEWEST step and counts it.
 * Dropping is a real state, so the scene prints the counter rather than hiding
 * it — the alternative, evicting an already-scheduled step, would silence a
 * sound the player has already started hearing.
 */
class TimedSfxDelayScheduler final : public pixelroot32::audio::SfxDelayScheduler {
public:
    /// Pending steps held at once. Six is deliberately small: two presses of
    /// FANFARE (three steps each) fill it, so the drop counter is reachable.
    static constexpr uint8_t kCapacity = 6;

    /// Longest delay the scheduler will honour. A step further out than this
    /// is data corruption, not a sound effect, so it is clamped rather than
    /// left to sit in a slot forever.
    static constexpr int32_t kMaxDelayMs = 5000;

    /**
     * @brief Points the scheduler at the engine that will play the events.
     * @param engine Audio engine; must outlive the scheduler.
     */
    void bind(pixelroot32::audio::AudioEngine* engine) { engine_ = engine; }

    /**
     * @brief Called by `playSfxBank` for every step with `delaySec > 0`.
     *
     * Steps with `delaySec <= 0` never reach here — the helper plays those
     * itself — so this only ever stores a real countdown.
     */
    void schedule(float delaySec, const pixelroot32::audio::AudioEvent& event) override;

    /**
     * @brief Advances every pending countdown and fires the expired ones.
     * @param deltaTime Frame time in milliseconds, straight from `Scene::update`.
     */
    void update(unsigned long deltaTime);

    /// Cancels everything still pending, without playing it.
    void clear() { count_ = 0; }

    /// Pending steps right now, for the HUD.
    uint8_t pending() const { return count_; }

    /// Steps refused since `init()` because the scheduler was full.
    uint16_t dropped() const { return dropped_; }

private:
    /// One scheduled step. The event is stored BY VALUE: the `SequenceStep`
    /// `playSfxBank` built is a temporary, and its `preset` / `dutySteps` /
    /// `pitchEnvelope` pointers stay valid because they point at the bank's
    /// `inline constexpr` tables, not at that temporary.
    struct PendingStep {
        int32_t remainingMs;
        pixelroot32::audio::AudioEvent event;
    };

    PendingStep slots_[kCapacity];
    uint8_t count_ = 0;
    uint16_t dropped_ = 0;
    pixelroot32::audio::AudioEngine* engine_ = nullptr;
};

/**
 * @class SfxBankScene
 * @brief Walks a `DemoSfxBank` with the D-pad and plays entries through
 *        `pixelroot32::audio::playSfxBank`.
 *
 * The screen always shows the bank as a list, the selected entry's layer and
 * sequence-step counts, and how many delayed steps the scheduler is holding.
 * Everything is drawn with `Renderer` primitives and the built-in 5x7 font;
 * the demo ships no art.
 */
class SfxBankScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Button indices as wired by `InputConfig` in the platform headers.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    // --- Screen layout (128x128, 5x7 font: 6 px advance, 8 px tall) -------
    static constexpr int kListY = 13;
    static constexpr int kRowHeight = 9;
    static constexpr int kNameX = 6;
    static constexpr int kSeparatorY = 86;
    static constexpr int kLayersTextY = 90;
    static constexpr int kPendingTextY = 99;
    static constexpr int kStatusTextY = 108;
    static constexpr int kControlsTextY = 118;
    static constexpr int kTextX = 4;

    void moveSelection(int8_t delta);
    void fireSelected();
    void toggleLoop();
    void stopAllSfxVoices();
    void refreshHud();

    TimedSfxDelayScheduler delays_;

    uint8_t selected_ = 0;

    /// True after B started `SfxId::AlarmLoop`; cleared when B stops it. The
    /// engine cannot be asked, so the scene tracks its own intent — see
    /// `stopAllSfxVoices()` for why the slot itself is unknown.
    bool loopArmed_ = false;

    /// Last values the HUD strings were built from, so `snprintf` runs on the
    /// frames where something changed and not on every frame.
    uint8_t hudPending_ = 0xFF;
    uint16_t hudDropped_ = 0xFFFF;
    uint8_t hudSelected_ = 0xFF;
    bool hudLoopArmed_ = false;

    char layersText_[24] = {0};
    char pendingText_[24] = {0};
    char statusText_[24] = {0};
    char controlsText_[24] = {0};
    pixelroot32::graphics::Color statusColor_ = pixelroot32::graphics::Color::Green;
    pixelroot32::graphics::Color pendingColor_ = pixelroot32::graphics::Color::White;
};

} // namespace sfx_bank
