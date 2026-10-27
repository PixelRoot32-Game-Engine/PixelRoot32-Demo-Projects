#ifdef PLATFORM_ESP32DEV_ILI9341

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "PongScene.h"

namespace pr32 = pixelroot32;

// ESP32-2432S028 "Cheap Yellow Display": an ILI9341 240x320 panel with an
// XPT2046 resistive touch screen on its own GPIO bus. The panel is mounted in
// portrait, so the build rotates it by a quarter turn and the logical
// framebuffer becomes 320x240 -- which is the shape the court wants anyway.
//
// The touch panel is left alone: the game is played with the six buttons, and
// the XPT2046 bus is not claimed.

// I2S output to a MAX98357A-class DAC. GPIO 5 (BCLK), GPIO 21 (LRCK) and
// GPIO 19 (DOUT) carry the audio; none of them collides with the panel's SPI
// bus or with the six button pins below.
const int I2S_BCLK = 5;
const int I2S_LRCK = 21;
const int I2S_DOUT = 19;

// The six buttons on the board, in the engine's fixed order: Up, Down, Left,
// Right, A, B.
const int BTN_UP = 32;
const int BTN_DOWN = 33;
const int BTN_LEFT = 25;
const int BTN_RIGHT = 26;
const int BTN_A = 27;
const int BTN_B = 13;

pr32::drivers::esp32::ESP32_I2S_AudioBackend audioBackend(I2S_BCLK, I2S_LRCK, I2S_DOUT, 22050);

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ILI9341_2,
    DISPLAY_ROTATION,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

pr32::input::InputConfig inputConfig(BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B);

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

pong::PongScene pongScene;

void setup() {
    engine.init();
    engine.setScene(&pongScene);
}

void loop() {
    engine.run();
}

#endif // PLATFORM_ESP32DEV_ILI9341
