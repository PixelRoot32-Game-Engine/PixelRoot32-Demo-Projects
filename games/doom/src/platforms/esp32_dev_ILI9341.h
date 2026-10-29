// esp32dev_ILI9341 — the secondary, fidelity target: the ESP32-2432S028 "Cheap
// Yellow Display", an ILI9341 240x320 panel mounted in portrait and rotated a
// quarter turn. 320x200 fits natively, so this build shows DOOM's surface 1:1
// with 20 px bars instead of squashing it 0.75x.
//
// The pin map is copied from games/pong/src/platforms/esp32_dev_ILI9341.h; the
// XPT2046 touch bus is left unclaimed, as it is there.
#ifndef PLATFORM_ESP32DEV_ILI9341
#error "esp32_dev_ILI9341.h included without PLATFORM_ESP32DEV_ILI9341"
#else

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "DoomScene.h"

namespace pr32 = pixelroot32;

static_assert(LOGICAL_WIDTH == doom::DoomScene::kLogicalWidth,
              "DOOM renders 320 columns: distscale[], visplane_t and the status bar are sized by SCREENWIDTH");
static_assert(LOGICAL_HEIGHT == doom::DoomScene::kLogicalHeight,
              "DOOM renders 200 rows, status bar included");
static_assert(PHYSICAL_DISPLAY_HEIGHT + 2 * Y_OFF_SET <= 240,
              "the letterboxed window plus its bars does not fit the rotated 240 px tall panel");

// I2S output to a MAX98357A-class DAC: GPIO 5 (BCLK), 21 (LRCK), 19 (DOUT).
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

doom::DoomScene doomScene;

void setup() {
    engine.init();
    engine.setScene(&doomScene);
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV_ILI9341
