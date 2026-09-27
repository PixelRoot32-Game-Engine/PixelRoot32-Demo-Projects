#ifdef PLATFORM_ESP32DEV

#include <Arduino.h>
#include <core/Engine.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include <platforms/EngineConfig.h>

#include "PoolMenuScene.h"
#include "PoolScene.h"

namespace pr32 = pixelroot32;

// Audio out (I2S), common mapping for a MAX98357A-class DAC. Clear of the
// ST7789 SPI pins (23/18/2/4) and the 6 button GPIOs.
const int I2S_BCLK = 26;
const int I2S_LRCK = 25;
const int I2S_DOUT = 22;

pr32::drivers::esp32::ESP32_I2S_AudioBackend audioBackend(I2S_BCLK, I2S_LRCK, I2S_DOUT, 22050);

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

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

pool::PoolScene scene;
pool::PoolMenuScene menuScene;

void setup() {
    engine.init();
    pool::PoolMenuScene::setNextScene(&scene);
    engine.setScene(&menuScene);
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV
