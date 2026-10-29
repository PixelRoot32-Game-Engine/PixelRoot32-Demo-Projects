// esp32dev_ILI9341 — the primary DOOM target: the ESP32-2432S028 "Cheap Yellow
// Display", an ILI9341 240x320 panel mounted in portrait and rotated a quarter
// turn. DOOM's 320x200 fits natively in the rotated 320x240 frame, so the port
// shows it 1:1 with 20 px bars instead of squashing it.
//
// The build flags state the geometry in the panel's own portrait axes, and
// DisplayConfig swaps them for us: applyRotationNormalization() exchanges
// width/height -- and the offsets -- whenever the rotation is odd. So the flags
// say 200 wide by 320 tall with X_OFF_SET 20, and the drawer receives 320x200
// at offset (0, 20). Read the static_asserts below as the translation table.
//
// The pin map is copied from games/pong/src/platforms/esp32_dev_ILI9341.h,
// which is where it was validated on hardware. The XPT2046 touch bus is left
// unclaimed: the CYD has a touch controller, but no input scheme has been
// decided for the port yet (docs/plan-doom-esp32.md 3.5).
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

// The surface the drawer and the renderer end up with, after the rotation swap.
static constexpr int kSurfaceWidth = doom::DoomScene::kLogicalWidth;   // 320
static constexpr int kSurfaceHeight = doom::DoomScene::kLogicalHeight;  // 200
static constexpr int kPhysicalWidth = 320;
static constexpr int kPhysicalHeight = 200;

static_assert(kSurfaceWidth == 320 && kSurfaceHeight == 200,
              "DOOM renders 320x200; the renderer is column-indexed by SCREENWIDTH");
// Flags are stated pre-swap, so the logical pair arrives transposed.
static_assert(LOGICAL_HEIGHT == kSurfaceWidth && LOGICAL_WIDTH == kSurfaceHeight,
              "LOGICAL_WIDTH/HEIGHT are pre-swap panel axes: 200x320 becomes 320x200");
static_assert(PHYSICAL_DISPLAY_HEIGHT == kPhysicalWidth &&
              PHYSICAL_DISPLAY_WIDTH == kPhysicalHeight,
              "PHYSICAL is pre-swap too: 200x320 becomes the 320x200 window");
static_assert(kPhysicalHeight + 2 * X_OFF_SET == 240,
              "the window plus both bars must fill the rotated panel's 240 px");

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
