#ifdef PLATFORM_ESP32DEV_ILI9341

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "scenes/SpaceInvadersScene.h"

namespace pr32 = pixelroot32;

// ESP32-2432S028 "Cheap Yellow Display": an ILI9341 240x320 panel with an
// XPT2046 resistive touch screen on its own GPIO bus. The panel is mounted in
// portrait and the build rotates it by a quarter turn (DISPLAY_ROTATION=3), so
// the logical framebuffer becomes 320x240 -- the landscape shape the board is
// held in, and the canvas the 224 px playfield is centred on.
//
// The touch panel is left alone: the game needs a left/right pad and a fire
// button, the board already has both as real buttons, and claiming the XPT2046
// bus would add a second input device no scene asks for. PIXELROOT32_ENABLE_TOUCH
// stays at its default of 0 for the same reason.

// I2S output to a MAX98357A-class DAC. GPIO 5 (BCLK), GPIO 21 (LRCK) and
// GPIO 19 (DOUT) carry the audio; none of them collides with the panel's SPI
// bus (MISO 22, MOSI 23, SCLK 14, DC 2, RST 4, CS 15, BL 16) or with the six
// button pins below.
const int I2S_BCLK = 5;
const int I2S_LRCK = 21;
const int I2S_DOUT = 19;

// The six buttons on the board, in the engine's fixed order: Up, Down, Left,
// Right, A (fire), B (unused).
const int BTN_UP_PIN = 32;
const int BTN_DOWN_PIN = 33;
const int BTN_LEFT_PIN = 25;
const int BTN_RIGHT_PIN = 26;
const int BTN_A_PIN = 27;
const int BTN_B_PIN = 13;

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

pr32::input::InputConfig inputConfig(BTN_UP_PIN, BTN_DOWN_PIN, BTN_LEFT_PIN, BTN_RIGHT_PIN, BTN_A_PIN, BTN_B_PIN); // 6 buttons: Up, Down, Left, Right, A (fire), B (unused)

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

spaceinvaders::SpaceInvadersScene spaceInvadersScene;

void setup() {
    engine.init();
    engine.setScene(&spaceInvadersScene);
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV_ILI9341
