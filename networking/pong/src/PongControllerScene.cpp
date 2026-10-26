/*
 * Networking Pong - controller scene scaffold (ESP32-C3, native simulator).
 *
 * Placeholder panel only: it proves the OLED, the I2C wiring and the ESP-NOW
 * session are alive before the real scoreboard exists.
 */
#include "PongControllerScene.h"

#include <cstring>
#include <string_view>

#include "NetConfig.h"

namespace netpong {

namespace {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Renderer;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSessionConfig;
using pixelroot32::networking::NetTransportConfig;

/// Geometry of the 128x64 panel. Every row is stated so the placeholder cannot
/// drift into an overlap that the 1-bit buffer would hide.
constexpr int kHeaderY = 0;
constexpr int kScoreY = 12;
constexpr int kPhaseY = 30;
constexpr int kBarY = 42;
constexpr int kBarHeight = 10;
constexpr int kHintY = 54;
constexpr int kMarginX = 2;

/// The built-in font at size 1 is 6x8 per glyph.
constexpr int kGlyphWidth = 6;
constexpr std::uint8_t kScoreScale = 2;
constexpr std::uint8_t kTextScale = 1;

}  // namespace

#ifdef PLATFORM_NATIVE
PongControllerScene::PongControllerScene(pixelroot32::networking::LoopbackBus& loopbackBus,
                                         const pixelroot32::networking::NetAddress& address,
                                         std::uint8_t nodeId)
    : transport(loopbackBus, address), netSession(transport), nodeIdValue(nodeId) {}
#else
PongControllerScene::PongControllerScene(std::uint8_t nodeId)
    : transport(), netSession(transport), nodeIdValue(nodeId) {}
#endif

void PongControllerScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();

    NetTransportConfig transportConfig;
    transportConfig.channel = kChannel;
    transportConfig.nodeId = nodeIdValue;
    // On device the transport brings the station up without joining an access
    // point, because an association would move the channel and drop the link.
    transportConfig.manageWifi = true;

    NetResult result = transport.init(transportConfig);
    if (result != NetResult::Ok) {
        start = result;
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
    // The Engine pumps this session; pumping it here as well would advance the
    // link twice per frame and halve every timeout in practice.
}

void PongControllerScene::update(unsigned long deltaTime) {
    // Nothing to advance yet: the Engine pumps the session before this call,
    // and the input cadence arrives with the controller firmware.
    (void)deltaTime;
}

void PongControllerScene::draw(Renderer& renderer) {
    const int width = renderer.getLogicalWidth();

    const char* link = (start == NetResult::Ok) ? "LINK" : "NO LINK";
    renderer.drawText(std::string_view(playerLabel()), static_cast<int16_t>(kMarginX),
                      static_cast<int16_t>(kHeaderY), Color::White, kTextScale);

    // Right-align the link word: the panel is narrow enough that a fixed column
    // would either clip "NO LINK" or leave it floating mid-row.
    const int linkX = width - kMarginX - (kGlyphWidth * static_cast<int>(std::strlen(link)));
    renderer.drawText(std::string_view(link), static_cast<int16_t>(linkX),
                      static_cast<int16_t>(kHeaderY), Color::White, kTextScale);

    // Score placeholder. It reads "0 - 0" until the host state arrives, which is
    // also the honest thing to show on a controller that has not joined yet.
    renderer.drawText(std::string_view("0 - 0"), static_cast<int16_t>(kMarginX),
                      static_cast<int16_t>(kScoreY), Color::White, kScoreScale);

    renderer.drawText(std::string_view("WAIT"), static_cast<int16_t>(kMarginX),
                      static_cast<int16_t>(kPhaseY), Color::White, kTextScale);

    // Paddle bar placeholder: the frame is where the bar will slide, the filled
    // block is the centre position it starts from.
    renderer.drawRectangle(kMarginX, kBarY, width - (2 * kMarginX), kBarHeight, Color::White);
    renderer.drawFilledRectangle((width / 2) - 10, kBarY + 2, 20, kBarHeight - 4, Color::White);

    // Wiring hint, so a freshly flashed board says which pin is which before
    // anything has been pressed.
    renderer.drawText(std::string_view("GPIO3<  >GPIO1"), static_cast<int16_t>(kMarginX),
                      static_cast<int16_t>(kHintY), Color::White, kTextScale);
}

pixelroot32::networking::NetSession* PongControllerScene::session() { return &netSession; }

const char* PongControllerScene::playerLabel() const {
    // The label is derived from the node id, not compiled in separately: the
    // env flag and the drawn word can then never disagree.
    return (nodeIdValue == kNodeIdP2) ? "P2" : "P1";
}

}  // namespace netpong
