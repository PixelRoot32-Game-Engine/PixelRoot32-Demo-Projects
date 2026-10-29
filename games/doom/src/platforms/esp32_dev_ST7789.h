// esp32dev_ST7789 — the primary DOOM target: the square 240x240 ST7789 board
// pong was written for, with a MAX98357A-class I2S DAC on GPIO 26/25/22 and the
// six-button D-pad on GPIO 32/27/33/14/13/12.
//
// The pin map is copied from games/pong/src/platforms/esp32_dev_ST7789.h and
// not shared with it: demos in this repository do not import each other.
#ifndef PLATFORM_ESP32DEV_ST7789
#error "esp32_dev_ST7789.h included without PLATFORM_ESP32DEV_ST7789"
#else

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "DoomScene.h"

namespace pr32 = pixelroot32;

// DOOM's surface is 320x200 and that is a property of the renderer, not of this
// panel (docs/plan-doom-esp32.md 6.1). Failing the build here is cheaper than
// discovering a cropped status bar on hardware.
static_assert(LOGICAL_WIDTH == doom::DoomScene::kLogicalWidth,
              "DOOM renders 320 columns: distscale[], visplane_t and the status bar are sized by SCREENWIDTH");
static_assert(LOGICAL_HEIGHT == doom::DoomScene::kLogicalHeight,
              "DOOM renders 200 rows, status bar included");
static_assert(PHYSICAL_DISPLAY_HEIGHT + 2 * Y_OFF_SET <= 240,
              "the letterboxed window plus its bars does not fit the 240 px tall panel");

// Audio pin configuration (I2S), common mapping for a MAX98357A-class DAC.
const int I2S_BCLK = 26;
const int I2S_LRCK = 25;
const int I2S_DOUT = 22;

// Button mapping (Arduino ESP32), common mapping for a 5-directional pad
// plus one action button.
const int BTN_UP_PIN = 32;
const int BTN_DOWN_PIN = 27;
const int BTN_LEFT_PIN = 33;
const int BTN_RIGHT_PIN = 14;
const int BTN_A_PIN = 13;
const int BTN_B_PIN = 12;

pr32::drivers::esp32::ESP32_I2S_AudioBackend audioBackend(I2S_BCLK, I2S_LRCK, I2S_DOUT, 22050);

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ST7789,
    DISPLAY_ROTATION,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

pr32::input::InputConfig inputConfig(BTN_UP_PIN, BTN_DOWN_PIN, BTN_LEFT_PIN, BTN_RIGHT_PIN, BTN_A_PIN, BTN_B_PIN);

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

doom::DoomScene doomScene;

void setup() {
    engine.init();
    engine.setScene(&doomScene);
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV_ST7789
