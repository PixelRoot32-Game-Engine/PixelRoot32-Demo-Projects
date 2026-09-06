/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "DigitalButtonsScene.h"

#include <core/Engine.h>

#include <cstdio>

namespace pr32 = pixelroot32;

// The platform header owns the engine instance and this is how a scene reaches
// it — the established pattern across these demos, not a workaround.
extern pr32::core::Engine engine;

namespace digital_buttons {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// Row labels, in InputConfig index order.
const char* const kButtonLabels[] = {"UP", "DN", "LT", "RT", "A", "B"};

/// Column labels. Two characters each so six rows of four fit across 128 px.
const char* const kVerbLabels[] = {"DN", "PR", "RL", "CK"};

/// One colour per query, so a glance at the matrix says which verb lit up.
constexpr Color kVerbColors[] = {Color::Green, Color::Cyan, Color::Yellow, Color::Magenta};

const char* const kPageNames[] = {"VERBS", "CLICK TRAP", "RATE", "REPEAT", "BUFFER", "COMBO"};

/// Subtracts a frame from a millisecond countdown without underflowing.
inline uint16_t decay(uint16_t remaining, unsigned long dt) {
    if (remaining == 0) return 0;
    if (dt >= remaining) return 0;
    return static_cast<uint16_t>(remaining - dt);
}

} // namespace

DigitalButtonsScene::DigitalButtonsScene()
    : repeat_(kRepeatInitialDelayMs, kRepeatIntervalMs),
      buffer_(kBufferWindowMs),
      combo_(kComboToleranceMs) {}

void DigitalButtonsScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    page_ = PageVerbs;
    elapsedMs_ = 0;
    resetPageState();
}

void DigitalButtonsScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    // isButtonClicked() is const but mutates a mutable flag, so a const
    // reference is enough for all four queries.
    const pr32::input::InputManager& input = engine.getInputManager();

    elapsedMs_ += deltaTime;

    // Sample the current page before navigating. Left and Right are ordinary
    // buttons: on the VERBS page they light their own row on the same frame
    // that flips the page, which is correct and worth seeing.
    switch (page_) {
        case PageVerbs:     updateVerbs(input, deltaTime); break;
        case PageClickTrap: updateClickTrap(input, deltaTime); break;
        case PageRate:      updateRate(input, deltaTime); break;
        case PageRepeat:    updateRepeat(input, deltaTime); break;
        case PageBuffer:    updateBuffer(input, deltaTime); break;
        case PageCombo:     updateCombo(input, deltaTime); break;
        default: break;
    }

    const bool left = input.isButtonPressed(kButtonLeft);
    const bool right = input.isButtonPressed(kButtonRight);

    if (left || right) {
        const uint8_t step = left ? static_cast<uint8_t>(kPageCount - 1) : uint8_t{1};
        page_ = static_cast<uint8_t>((page_ + step) % kPageCount);
        resetPageState();
    }
}

void DigitalButtonsScene::draw(gfx::Renderer& renderer) {
    // Background first, base class second: Scene::draw() paints the scene's
    // entities, so a fill placed after it would overpaint them.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    snprintf(header_, sizeof(header_), "%u/%u %s",
             static_cast<unsigned>(page_ + 1), static_cast<unsigned>(kPageCount), pageName());
    renderer.drawText(header_, kTextX, kHeaderY, Color::White, 1);
    renderer.drawFilledRectangle(0, kSeparatorY, renderer.getLogicalWidth(), 1, Color::Gray);

    switch (page_) {
        case PageVerbs:     drawVerbs(renderer); break;
        case PageClickTrap: drawClickTrap(renderer); break;
        case PageRate:      drawRate(renderer); break;
        case PageRepeat:    drawRepeat(renderer); break;
        case PageBuffer:    drawBuffer(renderer); break;
        case PageCombo:     drawCombo(renderer); break;
        default: break;
    }

    renderer.drawText("LT/RT PAGE", kTextX, kFooterY, Color::Gray, 1);
}

// =============================================================================
// Page 1 — VERBS
// =============================================================================

void DigitalButtonsScene::updateVerbs(const pr32::input::InputManager& input, unsigned long deltaTime) {
    for (uint8_t b = 0; b < kButtonCount; ++b) {
        live_[b][kVerbDown] = input.isButtonDown(b);
        live_[b][kVerbPressed] = input.isButtonPressed(b);
        live_[b][kVerbReleased] = input.isButtonReleased(b);

        // Exactly one call per button per frame. isButtonClicked() arms itself
        // on the press edge and disarms itself on the release that follows, so
        // calling it twice — or skipping a frame — changes its answer. Page 2
        // is that experiment; this page is the well-behaved usage.
        live_[b][kVerbClicked] = input.isButtonClicked(b);

        for (uint8_t v = 0; v < kVerbCount; ++v) {
            latch_[b][v] = live_[b][v] ? kLatchMs : decay(latch_[b][v], deltaTime);
        }
    }
}

void DigitalButtonsScene::drawVerbs(gfx::Renderer& renderer) {
    for (uint8_t v = 0; v < kVerbCount; ++v) {
        const int x = kMatrixColLeft + v * kMatrixColPitch;
        // Two characters at 6 px each, centred over a 20 px cell.
        renderer.drawText(kVerbLabels[v], static_cast<int16_t>(x + 4),
                          kMatrixHeaderY, kVerbColors[v], 1);
    }

    for (uint8_t b = 0; b < kButtonCount; ++b) {
        const int y = kMatrixTop + b * kMatrixRowPitch;

        renderer.drawText(kButtonLabels[b], kMatrixLabelX, static_cast<int16_t>(y + 1), Color::White, 1);

        for (uint8_t v = 0; v < kVerbCount; ++v) {
            const int x = kMatrixColLeft + v * kMatrixColPitch;

            if (live_[b][v]) {
                // Bright fill: the query is true on this very frame.
                renderer.drawFilledRectangle(x, y, kMatrixCellW, kMatrixCellH, kVerbColors[v]);
            } else if (latch_[b][v] > 0) {
                // Dim outline: it was true recently. DOWN stays filled for as
                // long as the button is held; the other three can only ever
                // flash, because stateChanged[] is cleared at the top of
                // InputManager::update() and set again only on a transition.
                renderer.drawRectangle(x, y, kMatrixCellW, kMatrixCellH, Color::Gray);
            }
        }
    }

    renderer.drawText("FILL=NOW DIM=250MS", kTextX, 104, Color::Gray, 1);
}

// =============================================================================
// Page 2 — CLICK TRAP
// =============================================================================

void DigitalButtonsScene::updateClickTrap(const pr32::input::InputManager& input, unsigned long deltaTime) {
    // Two calls, same button, same frame. The signature says const; the body
    // writes to `mutable bool clickFlag[]`. The first call finds the flag
    // armed by the press edge and clears it on the release; the second finds
    // nothing left to consume.
    const bool first = input.isButtonClicked(kButtonA);
    const bool second = input.isButtonClicked(kButtonA);

    if (first) ++firstCallFired_;
    if (second) ++secondCallFired_;

    if (first || second) {
        lastFirstResult_ = first;
        lastSecondResult_ = second;
        trapLatchMs_ = kLatchMs;
    } else {
        trapLatchMs_ = decay(trapLatchMs_, deltaTime);
        if (trapLatchMs_ == 0) {
            lastFirstResult_ = false;
            lastSecondResult_ = false;
        }
    }
}

void DigitalButtonsScene::drawClickTrap(gfx::Renderer& renderer) {
    renderer.drawText("CLICK A. TWO CALLS:", kTextX, 18, Color::White, 1);

    snprintf(line_, sizeof(line_), "1ST %s", lastFirstResult_ ? "TRUE " : "FALSE");
    renderer.drawText(line_, kTextX, 34, lastFirstResult_ ? Color::LightGreen : Color::Gray, 1);
    snprintf(line_, sizeof(line_), "  FIRED %u", static_cast<unsigned>(firstCallFired_));
    renderer.drawText(line_, kTextX, 44, Color::LightGreen, 1);

    snprintf(line_, sizeof(line_), "2ND %s", lastSecondResult_ ? "TRUE " : "FALSE");
    renderer.drawText(line_, kTextX, 60, lastSecondResult_ ? Color::LightGreen : Color::Gray, 1);
    snprintf(line_, sizeof(line_), "  FIRED %u", static_cast<unsigned>(secondCallFired_));
    renderer.drawText(line_, kTextX, 70, Color::Red, 1);

    renderer.drawText("A CONST QUERY THAT", kTextX, 96, Color::Yellow, 1);
    renderer.drawText("CONSUMES ITS FLAG.", kTextX, 106, Color::Yellow, 1);
}

// =============================================================================
// Page 3 — RATE
// =============================================================================

void DigitalButtonsScene::updateRate(const pr32::input::InputManager& input, unsigned long deltaTime) {
    const bool pressed = input.isButtonPressed(kButtonA);
    const bool released = input.isButtonReleased(kButtonA);

    if (pressed || released) {
        meter_.edge(elapsedMs_);
        // Both edges arm the debounce, which is why the ceiling is about ten
        // edges — five full press-release cycles — per second.
        cooldownMs_ = kDebounceMirrorMs;
    } else {
        cooldownMs_ = decay(cooldownMs_, deltaTime);
    }

    liveRate_ = meter_.ratePerSecond(elapsedMs_);
}

void DigitalButtonsScene::drawRate(gfx::Renderer& renderer) {
    renderer.drawText("MASH A", kTextX, 18, Color::White, 1);

    snprintf(line_, sizeof(line_), "NOW  %u EDGE/S", static_cast<unsigned>(liveRate_));
    renderer.drawText(line_, kTextX, 34, Color::Cyan, 1);

    snprintf(line_, sizeof(line_), "PEAK %u EDGE/S", static_cast<unsigned>(meter_.peak()));
    renderer.drawText(line_, kTextX, 46, Color::LightGreen, 1);

    renderer.drawText("DEBOUNCE (MIRROR)", kTextX, 64, Color::Gray, 1);

    // A model of InputManager's private waitTime[], running beside it — not a
    // reading of it. There is no accessor; the constant is a literal in
    // InputManager::update().
    const int barW = 120;
    renderer.drawRectangle(kTextX, 74, barW, 8, Color::Gray);
    const int filled = static_cast<int>((static_cast<long>(cooldownMs_) * (barW - 2)) / kDebounceMirrorMs);
    if (filled > 0) {
        renderer.drawFilledRectangle(kTextX + 1, 75, filled, 6, Color::Orange);
    }

    renderer.drawText("100 MS PER EDGE", kTextX, 96, Color::Yellow, 1);
    renderer.drawText("CAPS NEAR 10 E/S", kTextX, 106, Color::Yellow, 1);
}

// =============================================================================
// Page 4 — REPEAT
// =============================================================================

void DigitalButtonsScene::updateRepeat(const pr32::input::InputManager& input, unsigned long deltaTime) {
    const bool held = input.isButtonDown(kButtonUp);

    // The naive reading of a level query: one step per frame, so the cursor
    // moves at whatever frame rate the target happens to run at.
    if (held) ++rawCount_;

    if (repeat_.tick(held, deltaTime)) ++repeatCount_;
}

void DigitalButtonsScene::drawRepeat(gfx::Renderer& renderer) {
    renderer.drawText("HOLD UP", kTextX, 18, Color::White, 1);

    renderer.drawText("RAW", 8, 36, Color::Red, 1);
    renderer.drawText("REPEAT", 68, 36, Color::LightGreen, 1);

    snprintf(line_, sizeof(line_), "%lu", static_cast<unsigned long>(rawCount_));
    renderer.drawText(line_, 8, 50, Color::Red, 1);

    snprintf(line_, sizeof(line_), "%lu", static_cast<unsigned long>(repeatCount_));
    renderer.drawText(line_, 68, 50, Color::LightGreen, 1);

    renderer.drawText("RAW = ONE PER FRAME", kTextX, 70, Color::Gray, 1);

    renderer.drawText("REPEAT 400MS THEN", kTextX, 96, Color::Yellow, 1);
    renderer.drawText("EVERY 80MS", kTextX, 106, Color::Yellow, 1);
}

// =============================================================================
// Page 5 — BUFFER
// =============================================================================

void DigitalButtonsScene::updateBuffer(const pr32::input::InputManager& input, unsigned long deltaTime) {
    fallMs_ += deltaTime;
    landedThisFrame_ = false;
    if (fallMs_ >= kFallPeriodMs) {
        fallMs_ -= kFallPeriodMs;
        landedThisFrame_ = true;
        ++landings_;
    }

    const bool aPressed = input.isButtonPressed(kButtonA);
    if (aPressed) {
        ++attempts_;
        buffer_.press(elapsedMs_);
    }

    if (landedThisFrame_) {
        // The naive test: the press edge has to happen on the landing frame
        // itself. At 60 fps that is a 16 ms target, and the debounce means the
        // edge may not even be delivered on the frame the button moved.
        if (aPressed) ++naiveHits_;

        // The buffered test: any press inside the last 150 ms still counts.
        if (buffer_.consume(elapsedMs_)) ++bufferedHits_;
    }
}

void DigitalButtonsScene::drawBuffer(gfx::Renderer& renderer) {
    renderer.drawText("PRESS A ON LANDING", kTextX, 16, Color::White, 1);

    const int travel = kFloorY - kBoxSize - kFallTop;
    const int boxY = kFallTop + static_cast<int>((static_cast<unsigned long>(travel) * fallMs_) / kFallPeriodMs);

    renderer.drawFilledRectangle(59, boxY, kBoxSize, kBoxSize, Color::Cyan);
    renderer.drawFilledRectangle(24, kFloorY, 80, 2, Color::Gray);

    snprintf(line_, sizeof(line_), "TRY %u  LAND %u",
             static_cast<unsigned>(attempts_), static_cast<unsigned>(landings_));
    renderer.drawText(line_, kTextX, 76, Color::Gray, 1);

    snprintf(line_, sizeof(line_), "NAIVE    %u", static_cast<unsigned>(naiveHits_));
    renderer.drawText(line_, kTextX, 86, Color::Red, 1);

    snprintf(line_, sizeof(line_), "BUFFERED %u", static_cast<unsigned>(bufferedHits_));
    renderer.drawText(line_, kTextX, 96, Color::LightGreen, 1);

    renderer.drawText("150 MS WINDOW", kTextX, 106, Color::Yellow, 1);
}

// =============================================================================
// Page 6 — COMBO
// =============================================================================

void DigitalButtonsScene::updateCombo(const pr32::input::InputManager& input, unsigned long deltaTime) {
    const bool aEdge = input.isButtonPressed(kButtonA);
    const bool bEdge = input.isButtonPressed(kButtonB);

    // Measured independently of the detector, so the number on screen is the
    // real gap between the two press edges whether or not the combo fired.
    if (aEdge) {
        lastEdgeAMs_ = elapsedMs_;
        haveEdgeA_ = true;
    }
    if (bEdge) {
        lastEdgeBMs_ = elapsedMs_;
        haveEdgeB_ = true;
    }
    if (haveEdgeA_ && haveEdgeB_) {
        const unsigned long delta = (lastEdgeAMs_ > lastEdgeBMs_) ? (lastEdgeAMs_ - lastEdgeBMs_)
                                                                  : (lastEdgeBMs_ - lastEdgeAMs_);
        lastDeltaMs_ = static_cast<uint16_t>(delta > 9999UL ? 9999UL : delta);
        haveDelta_ = true;
        haveEdgeA_ = false;
        haveEdgeB_ = false;
    }

    if (combo_.feed(aEdge, bEdge, deltaTime)) ++comboCount_;
}

void DigitalButtonsScene::drawCombo(gfx::Renderer& renderer) {
    renderer.drawText("PRESS A + B", kTextX, 18, Color::White, 1);

    if (haveDelta_) {
        snprintf(line_, sizeof(line_), "DELTA %u MS", static_cast<unsigned>(lastDeltaMs_));
    } else {
        snprintf(line_, sizeof(line_), "DELTA -- MS");
    }
    renderer.drawText(line_, kTextX, 36, Color::Cyan, 1);

    snprintf(line_, sizeof(line_), "TOL   %u MS", static_cast<unsigned>(kComboToleranceMs));
    renderer.drawText(line_, kTextX, 48, Color::Gray, 1);

    snprintf(line_, sizeof(line_), "COMBOS %u", static_cast<unsigned>(comboCount_));
    renderer.drawText(line_, kTextX, 66, Color::LightGreen, 1);

    renderer.drawText("DEBOUNCE IS PER", kTextX, 96, Color::Yellow, 1);
    renderer.drawText("BUTTON: EDGES SPLIT", kTextX, 106, Color::Yellow, 1);
}

// =============================================================================
// Shared
// =============================================================================

void DigitalButtonsScene::resetPageState() {
    for (uint8_t b = 0; b < kButtonCount; ++b) {
        for (uint8_t v = 0; v < kVerbCount; ++v) {
            live_[b][v] = false;
            latch_[b][v] = 0;
        }
    }

    firstCallFired_ = 0;
    secondCallFired_ = 0;
    lastFirstResult_ = false;
    lastSecondResult_ = false;
    trapLatchMs_ = 0;

    meter_.reset();
    liveRate_ = 0;
    cooldownMs_ = 0;

    repeat_.reset();
    rawCount_ = 0;
    repeatCount_ = 0;

    buffer_.clear();
    fallMs_ = 0;
    attempts_ = 0;
    landings_ = 0;
    naiveHits_ = 0;
    bufferedHits_ = 0;
    landedThisFrame_ = false;

    combo_.reset();
    comboCount_ = 0;
    haveEdgeA_ = false;
    haveEdgeB_ = false;
    lastDeltaMs_ = 0;
    haveDelta_ = false;
}

const char* DigitalButtonsScene::pageName() const {
    return kPageNames[page_ < kPageCount ? page_ : 0];
}

} // namespace digital_buttons
