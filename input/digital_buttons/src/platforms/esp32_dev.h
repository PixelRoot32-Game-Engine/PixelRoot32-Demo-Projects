#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "DigitalButtonsScene.h"

namespace pr32 = pixelroot32;

// Button Mapping (Arduino ESP32)
// Common mapping for a 4-way pad plus A and B.
//
// Wire each button between its GPIO and ground, with nothing else.
// InputManager::init() calls pinMode(pin, INPUT_PULLUP) on every configured
// pin and its update() reads `digitalRead(pin) == LOW`, so the wiring is
// active-low by contract: idle reads HIGH through the internal pull-up, and a
// closed button pulls the line down. A button wired to 3V3 instead reads
// permanently pressed and never produces an edge.
const int BTN_UP = 32;
const int BTN_DOWN = 27;
const int BTN_LEFT = 33;
const int BTN_RIGHT = 14;
const int BTN_A = 13;
const int BTN_B = 12;

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

// 6 buttons, in the order InputConfig assigns indices:
// 0 Up, 1 Down, 2 Left, 3 Right, 4 A, 5 B.
//
// The same struct that carries SDL scancodes on native carries GPIO numbers
// here: `inputPins[]` instead of `buttonNames[]`, selected by PLATFORM_NATIVE
// inside InputConfig itself. Sixteen inputs fit (InputConfig::MAX_INPUT_COUNT,
// matching InputManager::MAX_BUTTONS); this demo, like every other in the
// repository, configures six.
pr32::input::InputConfig inputConfig(BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B);

pr32::core::Engine engine(config, inputConfig);

digital_buttons::DigitalButtonsScene buttonsScene;

void setup() {
    engine.init();
    engine.setScene(&buttonsScene);
}

void loop() {
    engine.run();
}

#endif // PLATFORM_ESP32DEV
