/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Engine.h>
#include <graphics/TransitionEffect.h>

namespace scene_transitions {

/**
 * @class TransitionEngine
 * @brief The stock Engine plus one setter: the DiagonalWipe direction.
 *
 * This subclass is a workaround, not a pattern to copy. Engine 1.9.0 has no
 * public way to choose a wipe direction: triggerTransition() takes no
 * WipeDirection, and the TransitionEffect that SceneManager drives is
 * Engine::transitionEffect_, a protected member. Deriving from Engine is the
 * only way to reach it without patching the engine.
 *
 * Setting the direction before triggerTransition() holds for the whole swap.
 * SceneManager calls TransitionEffect::init() twice — once for the Out phase,
 * once for the In phase after the scene swap — and init() resets the timer and
 * the iris centres but not the wipe direction.
 *
 * If a later engine release accepts a WipeDirection directly, this class is no
 * longer needed and the platform headers can instantiate Engine again.
 */
class TransitionEngine : public pixelroot32::core::Engine {
public:
    using Engine::Engine;

    void setWipeDirection(pixelroot32::graphics::WipeDirection direction) {
        transitionEffect_.setWipeDirection(direction);
    }
};

}  // namespace scene_transitions
