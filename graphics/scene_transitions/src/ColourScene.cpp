/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "ColourScene.h"

#include "TransitionEngine.h"
#include "TransitionSelector.h"

#include <cstdint>

extern scene_transitions::TransitionEngine engine;

namespace scene_transitions {

namespace gfx = pixelroot32::graphics;
using gfx::Color;

namespace {

/// Button indices, in the order InputConfig receives them: Up, Down, Left, Right, A, B.
constexpr std::uint8_t kButtonA = 4;
constexpr std::uint8_t kButtonB = 5;

constexpr int kLabelY = 44;
constexpr int kHudY = 100;
constexpr int kHintY = 104;
constexpr int kSelectionY = 116;

}  // namespace

ColourScene::ColourScene(const char* label, Color background)
    : label_(label), background_(background) {}

void ColourScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    if (input.isButtonPressed(kButtonA)) {
        selector::cycle();
    }

    if (input.isButtonPressed(kButtonB) && other_ != nullptr) {
        selector::swapTo(*other_);
    }
}

void ColourScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints entities.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                 background_);
    Scene::draw(renderer);

    renderer.drawTextCentered(label_, kLabelY, Color::White, 2);

    renderer.drawFilledRectangle(0, kHudY, renderer.getLogicalWidth(),
                                 renderer.getLogicalHeight() - kHudY, Color::Black);
    renderer.drawTextCentered("A NEXT   B SWAP", kHintY, Color::Gray, 1);
    renderer.drawTextCentered(selector::currentName(), kSelectionY, Color::Yellow, 1);
}

}  // namespace scene_transitions
