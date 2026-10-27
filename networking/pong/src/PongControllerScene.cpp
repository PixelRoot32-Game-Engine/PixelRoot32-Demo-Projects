/*
 * Networking Pong - controller scene (ESP32-C3, native simulator).
 *
 * The scene is the composition root of one controller: it brings the link up,
 * starts the session, owns the controller firmware and decides when the OLED
 * is worth a full I2C frame.
 */
#include <core/Engine.h>

#include "PongControllerScene.h"

#include "NetConfig.h"

// Declared once by the platform header of whichever target is being built; the
// scene only borrows it, exactly as it borrows nothing else from the platform.
extern pixelroot32::core::Engine engine;

namespace netpong {

namespace {

using pixelroot32::graphics::Renderer;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSessionConfig;
using pixelroot32::networking::NetTransportConfig;

/// Mutable state of both buttons, indexed the way the platform header declares
/// them: 0 is GPIO 3 (left/towards the top), 1 is GPIO 1 (right/towards the
/// bottom). InputManager applies INPUT_PULLUP, so a pressed button reads down.
constexpr std::uint8_t kLeftButton = 0;
constexpr std::uint8_t kRightButton = 1;

/// Panel repaint period. S10 caps the OLED at 10 Hz because a 128x64 frame over
/// I2C blocks for about 23 ms; asking for more would starve the link pump.
constexpr std::uint32_t kRedrawIntervalMs = 1000 / kControllerRedrawHz;

static_assert(kControllerRedrawHz > 0, "the redraw period needs a real rate");
static_assert(kRedrawIntervalMs > 0, "the redraw period must be non-zero");

}  // namespace

#ifdef PLATFORM_NATIVE
PongControllerScene::PongControllerScene(
    pixelroot32::networking::LoopbackBus& loopbackBus,
    const pixelroot32::networking::NetAddress& address, std::uint8_t nodeId)
    : transport(loopbackBus, address),
      netSession(transport),
      nodeIdValue(nodeId),
      pongController(netSession, playerIndex(), 0) {}
#else
PongControllerScene::PongControllerScene(std::uint8_t nodeId)
    : transport(),
      netSession(transport),
      nodeIdValue(nodeId),
      pongController(netSession, playerIndex(), 0) {}
#endif

void PongControllerScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();
    // A fresh init must repaint even when every visible field still reads its
    // initial value.
    hasPaintedKey = false;

    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());

    NetTransportConfig transportConfig;
    transportConfig.channel = kChannel;
    transportConfig.nodeId = nodeIdValue;
    // On device the transport brings the station up without joining an access
    // point, because an association would move the channel and drop the link.
    transportConfig.manageWifi = true;

    NetResult result = transport.init(transportConfig);
    if (result != NetResult::Ok) {
        start = result;
        // Even without a link the panel must show the parked court instead of
        // four zeros, so the player sees "NO LINK" over a truthful board.
        pongController.start(nowMs);
        return;
    }

    NetSessionConfig sessionConfig;
    sessionConfig.channel = kChannel;
    sessionConfig.nodeId = nodeIdValue;
    // 0 asks the session for a per-boot generation, which is what makes a
    // power-cycled controller look new to the host instead of like a duplicate.
    sessionConfig.bootId = 0;
    sessionConfig.discovery = true;

    start = netSession.init(sessionConfig);
    // The controller announces itself here, after the session started. On the
    // native simulator discovery may already have happened before the scene
    // exists; start() looks the host up in the peer table so the handshake is
    // not waiting for a join again.
    pongController.start(nowMs);
    if (start != NetResult::Ok) {
        return;
    }
    // The Engine pumps this session; pumping it here as well would advance the
    // link twice per frame and halve every timeout in practice.
}

void PongControllerScene::update(unsigned long deltaTime) {
    (void)deltaTime;

    // The controller contract takes an explicit clock, so the two boards and
    // the test suite share one cadence implementation. millis() is the only
    // clock available on both the C3 and the native mock.
    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());

    const auto& input = engine.getInputManager();
    std::uint8_t buttons = 0;
    if (input.isButtonDown(kLeftButton)) {
        buttons |= net::kButtonLeft;
    }
    if (input.isButtonDown(kRightButton)) {
        buttons |= net::kButtonRight;
    }

    pongController.update(nowMs, buttons);
}

void PongControllerScene::draw(Renderer& renderer) {
    drawControllerScreen(renderer, pongController.view(), playerIndex());
}

bool PongControllerScene::shouldRedrawFramebuffer() const {
    const ScreenKey current = makeKey(pongController.view());
    if (hasPaintedKey && current == lastPaintedKey) {
        return false;
    }

    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());
    if (hasPaintedKey &&
        static_cast<std::uint32_t>(nowMs - lastPaintMs) <
            kRedrawIntervalMs) {
        // Remember nothing: the next call compares against the last PAINTED
        // key, so a change made during the window is caught the moment the
        // window opens rather than being swallowed by a no-draw frame.
        return false;
    }

    lastPaintedKey = current;
    hasPaintedKey = true;
    lastPaintMs = nowMs;
    return true;
}

void PongControllerScene::onNetworkEvent(
    const pixelroot32::networking::NetEvent& event) {
    pongController.onEvent(event, static_cast<std::uint32_t>(millis()));
}

pixelroot32::networking::NetSession* PongControllerScene::session() {
    return &netSession;
}

std::uint8_t PongControllerScene::playerIndex() const {
    // Derived from the label, never compiled in separately: the env flag, the
    // outgoing INPUT and the drawn tag can then never disagree.
    return (nodeIdValue == kNodeIdP2) ? 1 : 0;
}

}  // namespace netpong
