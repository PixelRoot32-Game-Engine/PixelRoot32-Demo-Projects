/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>

/**
 * @namespace scene_transitions::selector
 * @brief The transition catalogue and the current selection.
 *
 * The selection lives here, at file scope, rather than in either scene. A swap
 * runs init() on the scene it lands on, so anything a scene stored about "which
 * transition comes next" would be reset on every swap. Neither scene includes
 * TransitionEffect.h: they ask for a swap, and this file decides how it looks.
 */
namespace scene_transitions::selector {

/// Advances to the next catalogue entry, wrapping at the end.
void cycle();

/// @return The HUD name of the current entry, e.g. "WIPE NW-SE".
const char* currentName();

/// Swaps to @p target with the current entry. Ignored while a swap is running.
void swapTo(pixelroot32::core::Scene& target);

}  // namespace scene_transitions::selector
