#ifdef PLATFORM_ESP32CYD

#include <Arduino.h>

#include <core/Engine.h>
#include <drivers/esp32/ESP32_DAC_AudioBackend.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <input/TouchAdapter.h>
#include <input/TouchEvent.h>
#include <input/TouchManager.h>
#include <input/TouchPoint.h>
#include <platforms/EngineConfig.h>

#include "ChessScene.h"

namespace pr32 = pixelroot32;

// ESP32-2432S028 "Cheap Yellow Display": ILI9341 240x320 with an XPT2046
// resistive panel on its own GPIO bus (see the esp32cyd flags in platformio.ini).

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ILI9341_2,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

// The board has no buttons: everything is touch. A default InputConfig declares
// zero inputs, which is what the audio-carrying Engine constructor needs without
// claiming GPIOs nothing is wired to.
pr32::input::InputConfig inputConfig;

/** Onboard speaker sits on GPIO 26, which is DAC2. DAC1 (25) is the alternative. */
constexpr int kSpeakerDacPin = 26;

pr32::drivers::esp32::ESP32_DAC_AudioBackend audioBackend(kSpeakerDacPin, 22050);
pr32::audio::AudioConfig audioConfig(&audioBackend, 22050);

pr32::core::Engine engine(config, inputConfig, audioConfig);

chessdemo::ChessScene chessScene;

pr32::input::TouchManager touchManager(PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT);

/** Backlight enable pin on the 2432S028. */
constexpr uint8_t kBacklightPin = 21;

static unsigned long touchPreviousFrameMs = 0;

void setup() {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, HIGH);

    engine.init();
    engine.setScene(&chessScene);

    // The calibration preset must be set before init(): the default is 320x240
    // and would mirror every tap on a portrait panel.
    {
        pr32::input::TouchCalibration calibration =
            pr32::input::TouchCalibration::forResolution(PHYSICAL_DISPLAY_WIDTH,
                                                         PHYSICAL_DISPLAY_HEIGHT);
        touchManager.setCalibration(calibration);
    }
    touchManager.init();

#if PIXELROOT32_ENABLE_TOUCH
    // With the manager registered, the Engine polls it and feeds the scene;
    // no manual touch point injection is needed.
    engine.setTouchManager(&touchManager);
#endif
}

void loop() {
    const unsigned long nowMs = millis();
    const unsigned long frameDeltaMs =
        (touchPreviousFrameMs == 0) ? 1u : (nowMs - touchPreviousFrameMs);
    touchPreviousFrameMs = nowMs;

    touchManager.update(frameDeltaMs);
    engine.run();
}

#endif  // PLATFORM_ESP32CYD
