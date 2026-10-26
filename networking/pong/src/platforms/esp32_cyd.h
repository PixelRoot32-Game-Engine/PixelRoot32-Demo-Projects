/*
 * Networking Pong - ESP32-2432S028 "Cheap Yellow Display" host target.
 *
 * Device configuration follows games/chess: esp32dev, ILI9341_2 pin map,
 * backlight on GPIO 21, DAC speaker on GPIO 26 at 22050 Hz. The canvas follows
 * the minesweeper/space_invaders model instead, because Pong wants landscape.
 */
#ifdef PLATFORM_ESP32CYD

#include <Arduino.h>

#include <core/Engine.h>
#include <drivers/esp32/ESP32_DAC_AudioBackend.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <platforms/EngineConfig.h>

#include "PongHostScene.h"

namespace pr32 = pixelroot32;

// PHYSICAL_* describe the panel as mounted (240x320). LOGICAL_* are deliberately
// not set by the build, so they arrive equal to the physical pair and
// DisplayConfig swaps both for the quarter turn; the scene then draws on the
// 320x240 landscape canvas it actually has.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ILI9341_2,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

// The CYD has no buttons wired to this game: both paddles travel over ESP-NOW
// from the C3 controllers. A default InputConfig declares zero inputs, which is
// what the audio-carrying Engine constructor needs without claiming GPIOs.
pr32::input::InputConfig inputConfig;

/** Onboard speaker sits on GPIO 26, which is DAC2. DAC1 (25) is the alternative. */
constexpr int kSpeakerDacPin = 26;

pr32::drivers::esp32::ESP32_DAC_AudioBackend audioBackend(kSpeakerDacPin, 22050);
pr32::audio::AudioConfig audioConfig(&audioBackend, 22050);

pr32::core::Engine engine(config, inputConfig, audioConfig);

netpong::PongHostScene pongHostScene;

/** Backlight enable pin on the 2432S028. */
constexpr uint8_t kBacklightPin = 21;

void setup() {
    // The panel stays dark until the pin is driven, so it has to happen before
    // engine.init() draws anything.
    pinMode(kBacklightPin, OUTPUT);
    digitalWrite(kBacklightPin, HIGH);

    engine.init();
    engine.setScene(&pongHostScene);

    // The Engine borrows the session; it never owns or initializes it. The
    // scene's init() (run by setScene) brought both the link and the session up.
    engine.setNetworkSession(pongHostScene.session());
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32CYD
