#ifdef PLATFORM_ESP32CYD

#include <Arduino.h>

#include <core/Engine.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <input/TouchAdapter.h>
#include <input/TouchManager.h>
#include <input/TouchPoint.h>
#include <platforms/EngineConfig.h>

#include "TouchControlsScene.h"

namespace pr32 = pixelroot32;

// ESP32-2432S028 "Cheap Yellow Display": ILI9341 240x320 with an XPT2046
// resistive panel on its own GPIO bus (see the esp32cyd flags in platformio.ini).

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ILI9341_2,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

// The board has no buttons: everything is touch. A default InputConfig declares
// zero inputs, which is what the Engine constructor needs without claiming
// GPIOs nothing is wired to.
pr32::input::InputConfig inputConfig;

pr32::core::Engine engine(config, inputConfig);

touch_controls::TouchControlsScene touchControlsScene;

// Stage one of the pipeline: TouchManager owns the XPT2046 adapter selected by
// -D TOUCH_DRIVER_XPT2046 and turns raw ADC readings into normalized TouchPoints.
pr32::input::TouchManager touchManager(PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT);

/** Backlight enable pin on the 2432S028. */
constexpr uint8_t kBacklightPin = 21;

static unsigned long touchPreviousFrameMs = 0;

void setup() {
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, HIGH);

    engine.init();
    engine.setScene(&touchControlsScene);

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
    // Stage two. With the manager registered, Engine::update() polls
    // getTouchPoints() every frame, feeds them to its own TouchEventDispatcher
    // and delivers the resulting gestures to the scene. Nothing is injected by
    // hand. Without this flag the accessor and the polling block do not exist,
    // and the panel is read into nothing.
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
