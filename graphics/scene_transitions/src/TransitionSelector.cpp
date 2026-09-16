/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "TransitionSelector.h"

#include "TransitionEngine.h"

#include <platforms/EngineConfig.h>

#include <cstddef>

extern scene_transitions::TransitionEngine engine;

namespace scene_transitions::selector {

namespace {

using pixelroot32::graphics::TransitionType;
using pixelroot32::graphics::WipeDirection;

/// Length of EACH phase. A swap is Out, then the scene change, then In.
constexpr unsigned long kPhaseMs = 500;

constexpr int kRight = LOGICAL_WIDTH - 1;
constexpr int kBottom = LOGICAL_HEIGHT - 1;

/// -1 is the engine's "use the buffer centre" value for an iris centre.
constexpr int kCentre = -1;

struct Entry {
    const char* name;
    TransitionType type;
    WipeDirection wipe;  ///< Read for DiagonalWipe only.
    int outX, outY;      ///< Iris centre while closing on the old scene.
    int inX, inY;        ///< Iris centre while opening on the new scene.
};

constexpr Entry kCatalogue[] = {
    {"FADE", TransitionType::Fade, WipeDirection::NE_SW, kCentre, kCentre, kCentre, kCentre},
    {"IRIS CENTRE", TransitionType::Iris, WipeDirection::NE_SW, kCentre, kCentre, kCentre, kCentre},
    {"IRIS OUT NW, IN SE", TransitionType::Iris, WipeDirection::NE_SW, 0, 0, kRight, kBottom},
    {"WIPE NW-SE", TransitionType::DiagonalWipe, WipeDirection::NW_SE, kCentre, kCentre, kCentre, kCentre},
    {"WIPE NE-SW", TransitionType::DiagonalWipe, WipeDirection::NE_SW, kCentre, kCentre, kCentre, kCentre},
    {"WIPE SE-NW", TransitionType::DiagonalWipe, WipeDirection::SE_NW, kCentre, kCentre, kCentre, kCentre},
    {"WIPE SW-NE", TransitionType::DiagonalWipe, WipeDirection::SW_NE, kCentre, kCentre, kCentre, kCentre},
};

constexpr std::size_t kCount = sizeof(kCatalogue) / sizeof(kCatalogue[0]);

/// The shared state. Both scenes read it; neither owns it; a swap cannot reset it.
std::size_t selected = 0;

}  // namespace

void cycle() {
    selected = (selected + 1) % kCount;
}

const char* currentName() {
    return kCatalogue[selected].name;
}

void swapTo(pixelroot32::core::Scene& target) {
    const Entry& entry = kCatalogue[selected];

    // Scene::update() does not run while a transition is active, so this cannot
    // retarget a wipe that is already on screen.
    if (entry.type == TransitionType::DiagonalWipe) {
        engine.setWipeDirection(entry.wipe);
    }

    if (entry.outX == kCentre) {
        engine.triggerTransition(&target, entry.type, kPhaseMs);
    } else {
        engine.triggerTransition(&target, entry.type, kPhaseMs,
                                 entry.outX, entry.outY, entry.inX, entry.inY);
    }
}

}  // namespace scene_transitions::selector
