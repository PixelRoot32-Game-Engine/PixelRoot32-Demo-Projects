#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <platforms/EngineConfig.h>

#include "ColourScene.h"
#include "TransitionEngine.h"

namespace pr32 = pixelroot32;

// Button Mapping (Arduino ESP32)
// Common mapping for 5-directional pad and A button
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

pr32::input::InputConfig inputConfig(BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B); // 6 buttons: Up, Down, Left, Right, A, B

// A plain Engine plus a wipe-direction setter; see TransitionEngine.h for why.
scene_transitions::TransitionEngine engine(config, inputConfig);

scene_transitions::ColourScene sceneA("SCENE A", pr32::graphics::Color::Navy);
scene_transitions::ColourScene sceneB("SCENE B", pr32::graphics::Color::DarkRed);

void setup() {
    sceneA.setOther(sceneB);
    sceneB.setOther(sceneA);

    engine.init();
    engine.setScene(&sceneA);
}

void loop() {
    engine.run();
}

#endif
