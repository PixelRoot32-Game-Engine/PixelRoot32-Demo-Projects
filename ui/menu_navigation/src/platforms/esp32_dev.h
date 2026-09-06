#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "MenuNavigationScene.h"

namespace pr32 = pixelroot32;

// Button Mapping (Arduino ESP32)
// Common mapping for a 4-way pad plus A and B.
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
// Those indices are what the widgets are constructed with, so this order is
// part of the scene's contract — see kButtonA in MenuNavigationScene.h.
pr32::input::InputConfig inputConfig(BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B);

pr32::core::Engine engine(config, inputConfig);

menu_navigation::MenuNavigationScene menuScene;

void setup() {
    engine.init();
    engine.setScene(&menuScene);
}

void loop() {
    engine.run();
}

#endif // PLATFORM_ESP32DEV
