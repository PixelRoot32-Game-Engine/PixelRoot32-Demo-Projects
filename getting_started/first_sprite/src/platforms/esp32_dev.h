/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

// Compiled only for the `esp32dev` environment (-D PLATFORM_ESP32DEV in
// platformio.ini). src/main.cpp falls through to this header whenever
// PLATFORM_NATIVE is not defined.
#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "FirstSpriteScene.h"

namespace pr32 = pixelroot32;

// Button wiring: six GPIOs for a D-pad plus A and B. Change these to match your
// board — nothing else in the demo knows about pins.
const int BTN_UP    = 32;
const int BTN_DOWN  = 27;
const int BTN_LEFT  = 33;
const int BTN_RIGHT = 14;
const int BTN_A     = 13;
const int BTN_B     = 12;

// Same DisplayConfig as the native header, with a real panel driver instead of
// NONE. The SPI pins themselves are TFT_eSPI defines in platformio.ini.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ST7735,
    DISPLAY_ROTATION,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

// The six slots are in the same order as on native: Up, Down, Left, Right,
// A, B. That is what lets FirstSpriteScene address them by index alone.
pr32::input::InputConfig inputConfig(
    BTN_UP,
    BTN_DOWN,
    BTN_LEFT,
    BTN_RIGHT,
    BTN_A,   // toggles the walk animation
    BTN_B    // unused by this demo
);

// This is the `engine` that FirstSpriteScene.cpp picks up with
// `extern pixelroot32::core::Engine engine;`.
pr32::core::Engine engine(config, inputConfig);

first_sprite::FirstSpriteScene firstSpriteScene;

void setup() {
    engine.init();
    engine.setScene(&firstSpriteScene);
}

void loop() {
    // One call per Arduino loop: engine.run() advances and draws a single
    // frame here, unlike the native build where it owns the loop.
    engine.run();
}

#endif // PLATFORM_ESP32DEV
