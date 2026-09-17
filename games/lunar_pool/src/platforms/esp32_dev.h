#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <core/Engine.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <platforms/EngineConfig.h>

#include "LunarPoolScene.h"

namespace pr32 = pixelroot32;

// Button mapping (Arduino ESP32), same 6-button layout as the SDL2 build.
const int BTN_UP = 32;
const int BTN_DOWN = 27;
const int BTN_LEFT = 33;
const int BTN_RIGHT = 14;
const int BTN_A = 13;
const int BTN_B = 12;

pr32::graphics::DisplayConfig config(pr32::graphics::DisplayType::ST7789, DISPLAY_ROTATION,
                                      PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT, LOGICAL_WIDTH,
                                      LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

pr32::input::InputConfig inputConfig(BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A,
                                      BTN_B);  // 6 buttons: Up, Down, Left, Right, A, B

pr32::core::Engine engine(config, inputConfig);

lunar_pool::LunarPoolScene scene;

void setup() {
    engine.init();
    engine.setScene(&scene);
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV
