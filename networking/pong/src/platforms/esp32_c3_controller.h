/*
 * Networking Pong - DFRobot Beetle ESP32-C3 controller target.
 *
 * One translation unit serves both controller environments; ESP_NOW_NODE_ID is
 * the entire difference between them.
 */
#ifdef NET_PONG_ROLE_CONTROLLER

#include <Arduino.h>

#include <core/Engine.h>
#include <drivers/esp32/U8G2_Drawer.h>
#include <platforms/EngineConfig.h>

#include <cstdint>

#include "NetConfig.h"
#include "PongControllerScene.h"

namespace pr32 = pixelroot32;

// Both controller environments build this same translation unit. Rejecting a
// bad flag here is cheaper than flashing two boards that both think they are
// Player 1: the host fan-out has one paddle slot per node id, and two nodes
// claiming slot 2 would leave Player 2's paddle permanently still.
#if !defined(ESP_NOW_NODE_ID) || (ESP_NOW_NODE_ID != 2 && ESP_NOW_NODE_ID != 3)
#error "ESP_NOW_NODE_ID must be 2 (P1) or 3 (P2); use env:esp32c3_p1 or env:esp32c3_p2"
#endif

// The preprocessor cannot see the netpong constants, so the literals above are
// re-checked against them here: the wire contract and the guard cannot drift.
static_assert(ESP_NOW_NODE_ID == netpong::kNodeIdP1 || ESP_NOW_NODE_ID == netpong::kNodeIdP2,
              "ESP_NOW_NODE_ID must match kNodeIdP1 or kNodeIdP2");

// Buttons wired to GND, so InputManager's INPUT_PULLUP makes them active-low.
// Index order is the engine's: the controller firmware will read index 0 as
// left and index 1 as right, matching the "GPIO 3 = left, GPIO 1 = right"
// wiring of the brief.
constexpr int kLeftButtonPin = 3;
constexpr int kRightButtonPin = 1;

// I2C wiring for the on-board 0.42" panel. There is no reset line, which 255
// signals to the driver.
constexpr std::uint8_t kOledSda = 5;
constexpr std::uint8_t kOledScl = 6;
constexpr std::uint8_t kOledRst = 255;

// PHYSICAL_* is the SSD1306 controller's framebuffer; the build sets LOGICAL_*
// equal to it (with zero offsets) so the scoreboard owns the whole canvas.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::OLED_SSD1306,
    DISPLAY_ROTATION,
    kOledScl, kOledSda, 255, 255, kOledRst,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

pr32::input::InputConfig inputConfig(kLeftButtonPin, kRightButtonPin);  // 2 buttons: left, right

// No audio backend: PIXELROOT32_ENABLE_AUDIO=0 compiles the whole subsystem out,
// and the two-argument constructor is the one that exists on that budget.
pr32::core::Engine engine(config, inputConfig);

netpong::PongControllerScene pongControllerScene(static_cast<std::uint8_t>(ESP_NOW_NODE_ID));

void setup() {
    engine.init();
    engine.setScene(&pongControllerScene);

    // The Engine borrows the session; it never owns or initializes it. The
    // scene's init() (run by setScene) brought both the link and the session up.
    engine.setNetworkSession(pongControllerScene.session());
}

void loop() {
    engine.run();
}

#endif  // NET_PONG_ROLE_CONTROLLER
