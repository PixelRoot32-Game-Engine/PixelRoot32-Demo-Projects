/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

namespace scene_transitions {

/**
 * @class ColourScene
 * @brief A solid colour, a name, and a HUD. Nothing else.
 *
 * Both scenes in the demo are instances of this class. It knows the other scene
 * so it can ask for a swap, and it knows nothing about transitions: there is no
 * TransitionType, no effect and no timer in here. The post-effect is applied by
 * the engine to the finished frame, on top of whatever this class drew.
 */
class ColourScene : public pixelroot32::core::Scene {
public:
    ColourScene(const char* label, pixelroot32::graphics::Color background);

    /// The scene button B swaps to. Wired once by the platform header.
    void setOther(pixelroot32::core::Scene& other) { other_ = &other; }

    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    const char* label_;
    pixelroot32::graphics::Color background_;
    pixelroot32::core::Scene* other_ = nullptr;
};

}  // namespace scene_transitions
