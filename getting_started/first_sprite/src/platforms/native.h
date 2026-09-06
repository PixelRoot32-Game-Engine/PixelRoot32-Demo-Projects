/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

// Everything in this file is compiled only for the `native` environment.
// PLATFORM_NATIVE comes from -DPLATFORM_NATIVE in lib/platformio.ini's
// [base_native], and src/main.cpp includes this header only when it is set.
#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "FirstSpriteScene.h"

namespace pr32 = pixelroot32;

// The platform header owns the hardware description. Everything below is a
// file-scope global on purpose: the engine and the scene must outlive every
// frame, and a global costs no allocator.

// DisplayType::NONE means "no physical panel driver" — on the PC the SDL2
// window is the display. PHYSICAL_DISPLAY_* comes from platformio.ini;
// DISPLAY_ROTATION, LOGICAL_*, and the offsets fall back to engine defaults in
// EngineConfig.h, where LOGICAL_* defaults to the physical size.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::NONE,
    DISPLAY_ROTATION,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

// Six buttons in a fixed order: Up, Down, Left, Right, A, B. On native these
// are SDL scancodes; on the ESP32 header the same six slots are GPIO numbers.
// The scene only ever refers to them by index (0..5), so it does not care.
pr32::input::InputConfig inputConfig(
    SDL_SCANCODE_UP,
    SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT,
    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_SPACE,   // A — toggles the walk animation
    SDL_SCANCODE_RETURN   // B — unused by this demo
);

// This is the `engine` that FirstSpriteScene.cpp picks up with
// `extern pixelroot32::core::Engine engine;`.
pr32::core::Engine engine(config, inputConfig);

first_sprite::FirstSpriteScene firstSpriteScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&firstSpriteScene);

    // On native, run() is the whole loop and only returns when the window
    // closes. The ESP32 header calls the same function once per loop() instead.
    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
