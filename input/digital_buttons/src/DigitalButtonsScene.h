/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <input/InputManager.h>

#include <cstdint>

#include "patterns/InputPatterns.h"

namespace digital_buttons {

/**
 * @class DigitalButtonsScene
 * @brief Six pages that make the digital-button half of InputManager visible.
 *
 * `InputManager` answers four questions about a button — is it down, was it
 * pressed this frame, was it released this frame, was it clicked — and this
 * scene shows what each answer actually does over time, then builds the three
 * patterns a game needs on top of them.
 *
 * Page 1 is the matrix: four columns, six rows, filled while a query is true
 * and outlined for a moment afterwards so a one-frame edge is visible to a
 * human. Page 2 calls `isButtonClicked()` twice in one frame and counts both
 * results, which is the demo's sharpest point: that query is `const` in the
 * header and mutates a `mutable` flag in the body. Pages 3 to 6 measure the
 * debounce ceiling and layer `RepeatTimer`, `InputBuffer` and `ComboDetector`
 * over the raw verbs.
 *
 * `update()` and `draw()` allocate nothing. Every counter is an integer
 * member, every string is `snprintf`'d into a fixed member buffer, and the
 * only container is a fixed 6x4 array of latch timers.
 */
class DigitalButtonsScene : public pixelroot32::core::Scene {
public:
    DigitalButtonsScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Button indices as wired by InputConfig in the platform headers:
    /// 0 Up, 1 Down, 2 Left, 3 Right, 4 A, 5 B. The same struct carries GPIO
    /// numbers on ESP32 and SDL scancodes on native; the indices are identical.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    /// Every demo in this repository configures six of InputManager's sixteen.
    static constexpr uint8_t kButtonCount = 6;

    /// The four queries, in the order the matrix draws them.
    static constexpr uint8_t kVerbCount = 4;
    static constexpr uint8_t kVerbDown = 0;
    static constexpr uint8_t kVerbPressed = 1;
    static constexpr uint8_t kVerbReleased = 2;
    static constexpr uint8_t kVerbClicked = 3;

    /// @brief The pages, cycled with Left and Right.
    enum Page : uint8_t {
        PageVerbs = 0,
        PageClickTrap,
        PageRate,
        PageRepeat,
        PageBuffer,
        PageCombo,
        kPageCount
    };

    // --- Tuning -----------------------------------------------------------

    /// How long a one-frame edge stays outlined after it fires. Three of the
    /// four queries are true for exactly one frame, which at 60 fps is 16 ms —
    /// far too short to see. The outline is a display aid and nothing else.
    static constexpr uint16_t kLatchMs = 250;

    /// This demo's own copy of the debounce constant, not a reading of the
    /// engine's. `InputManager::update()` writes `waitTime[i] = 100` into a
    /// private array with no accessor, so the bar on the RATE page is a model
    /// of that timer running alongside it — it will drift if the engine's
    /// constant ever changes.
    static constexpr uint16_t kDebounceMirrorMs = 100;

    static constexpr unsigned long kRepeatInitialDelayMs = 400;
    static constexpr unsigned long kRepeatIntervalMs = 80;
    static constexpr unsigned long kBufferWindowMs = 150;
    static constexpr unsigned long kComboToleranceMs = 100;

    /// One fall of the BUFFER page's square, in milliseconds.
    static constexpr unsigned long kFallPeriodMs = 1000;

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) -----
    static constexpr int kHeaderY = 2;
    static constexpr int kSeparatorY = 11;
    static constexpr int kFooterY = 118;
    static constexpr int kTextX = 4;

    /// VERBS matrix geometry.
    static constexpr int kMatrixLabelX = 2;
    static constexpr int kMatrixHeaderY = 16;
    static constexpr int kMatrixTop = 26;
    static constexpr int kMatrixRowPitch = 13;
    static constexpr int kMatrixColLeft = 20;
    static constexpr int kMatrixColPitch = 25;
    static constexpr int kMatrixCellW = 20;
    static constexpr int kMatrixCellH = 9;

    /// BUFFER page geometry.
    static constexpr int kFallTop = 18;
    static constexpr int kFloorY = 68;
    static constexpr int kBoxSize = 10;

    // --- Per-page update steps -------------------------------------------
    void updateVerbs(const pixelroot32::input::InputManager& input, unsigned long deltaTime);
    void updateClickTrap(const pixelroot32::input::InputManager& input, unsigned long deltaTime);
    void updateRate(const pixelroot32::input::InputManager& input, unsigned long deltaTime);
    void updateRepeat(const pixelroot32::input::InputManager& input, unsigned long deltaTime);
    void updateBuffer(const pixelroot32::input::InputManager& input, unsigned long deltaTime);
    void updateCombo(const pixelroot32::input::InputManager& input, unsigned long deltaTime);

    // --- Per-page draw steps ---------------------------------------------
    void drawVerbs(pixelroot32::graphics::Renderer& renderer);
    void drawClickTrap(pixelroot32::graphics::Renderer& renderer);
    void drawRate(pixelroot32::graphics::Renderer& renderer);
    void drawRepeat(pixelroot32::graphics::Renderer& renderer);
    void drawBuffer(pixelroot32::graphics::Renderer& renderer);
    void drawCombo(pixelroot32::graphics::Renderer& renderer);

    /// Clears the state of whichever page is now showing, so its numbers
    /// always describe the visit you are looking at.
    void resetPageState();

    /// Name of the current page, for the header line.
    const char* pageName() const;

    // --- Shared state -----------------------------------------------------
    uint8_t page_ = PageVerbs;

    /// Scene clock. InputBuffer and EdgeRateMeter both want absolute
    /// milliseconds, and a member accumulator keeps them independent of
    /// whichever millis() the platform provides.
    unsigned long elapsedMs_ = 0;

    // --- VERBS ------------------------------------------------------------
    bool live_[kButtonCount][kVerbCount] = {};      ///< True this frame.
    uint16_t latch_[kButtonCount][kVerbCount] = {}; ///< Milliseconds left on the outline.

    // --- CLICK TRAP -------------------------------------------------------
    uint16_t firstCallFired_ = 0;   ///< Times the first isButtonClicked(A) returned true.
    uint16_t secondCallFired_ = 0;  ///< Times the second one did. Stays at zero.
    bool lastFirstResult_ = false;
    bool lastSecondResult_ = false;
    uint16_t trapLatchMs_ = 0;

    // --- RATE -------------------------------------------------------------
    EdgeRateMeter meter_;
    uint8_t liveRate_ = 0;
    uint16_t cooldownMs_ = 0;  ///< Our mirror of the engine's private waitTime.

    // --- REPEAT -----------------------------------------------------------
    RepeatTimer repeat_;
    uint32_t rawCount_ = 0;     ///< One per frame isButtonDown(UP) is true.
    uint32_t repeatCount_ = 0;  ///< One per RepeatTimer fire.

    // --- BUFFER -----------------------------------------------------------
    InputBuffer buffer_;
    unsigned long fallMs_ = 0;
    uint16_t attempts_ = 0;
    uint16_t landings_ = 0;
    uint16_t naiveHits_ = 0;
    uint16_t bufferedHits_ = 0;
    bool landedThisFrame_ = false;

    // --- COMBO ------------------------------------------------------------
    ComboDetector combo_;
    uint16_t comboCount_ = 0;
    unsigned long lastEdgeAMs_ = 0;
    unsigned long lastEdgeBMs_ = 0;
    bool haveEdgeA_ = false;
    bool haveEdgeB_ = false;
    uint16_t lastDeltaMs_ = 0;
    bool haveDelta_ = false;

    // --- Formatting scratch ----------------------------------------------
    // Two fixed buffers, written with snprintf at draw time. 21 characters
    // fill a 128 px line at 6 px per character, so 24 bytes is one line plus
    // the terminator and a little slack.
    char header_[24] = {0};
    char line_[24] = {0};
};

} // namespace digital_buttons
