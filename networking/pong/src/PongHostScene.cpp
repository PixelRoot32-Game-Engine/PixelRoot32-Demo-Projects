/*
 * Networking Pong - host scene scaffold (CYD and native).
 *
 * Placeholder body only: it proves the panel, the rotation and the ESP-NOW link
 * are wired before the court exists.
 */
#include <core/Engine.h>

#include "PongHostScene.h"

#include <cstdio>
#include <string_view>

#include "NetConfig.h"

// Declared once by the platform header of whichever target is being built; the
// scene only borrows it, exactly as it borrows nothing else from the platform.
extern pixelroot32::core::Engine engine;

namespace netpong {

namespace {

using pixelroot32::graphics::Color;
using pixelroot32::graphics::Renderer;
using pixelroot32::networking::NetAddress;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSessionConfig;
using pixelroot32::networking::NetTransportConfig;

#if defined(PLATFORM_NATIVE)
/**
 * @brief Builds a deterministic loopback MAC.
 * @param tag Node tag; every byte differs between tags.
 * @return Address with no byte left at zero.
 *
 * The loopback resolves peers by address, and two attached transports sharing
 * one address would shadow each other, so each simulated node needs its own.
 */
NetAddress loopbackAddress(std::uint8_t tag) {
    NetAddress address{};
    for (std::uint8_t i = 0; i < pixelroot32::networking::kNetAddressSize; ++i) {
        address.bytes[i] = static_cast<std::uint8_t>(0x30u + tag + i);
    }
    return address;
}
#endif

/// Vertical offsets from the canvas centre, in logical pixels. The scene is
/// drawn relative to the centre so the same code composes on the 320x240 CYD
/// canvas and on the identical native window.
constexpr int kTitleOffsetY = -46;
constexpr int kHostOffsetY = -12;
constexpr int kStatusOffsetY = 20;
constexpr int kFpsOffsetY = 34;

constexpr std::uint8_t kTitleScale = 3;
constexpr std::uint8_t kTextScale = 1;

}  // namespace

#if defined(PLATFORM_NATIVE)
PongHostScene::PongHostScene()
    : loopbackBus(),
      transport(loopbackBus, loopbackAddress(kNodeIdHost)),
      netSession(transport) {}
#else
PongHostScene::PongHostScene() : transport(), netSession(transport) {}
#endif

void PongHostScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();

    NetTransportConfig transportConfig;
    transportConfig.channel = kChannel;
    transportConfig.nodeId = kNodeIdHost;
    // The transport brings the station up without joining an access point: an
    // association would move the radio off kChannel and drop the link.
    transportConfig.manageWifi = true;

    NetResult result = transport.init(transportConfig);
    if (result != NetResult::Ok) {
        start = result;
        return;
    }

    NetSessionConfig sessionConfig;
    sessionConfig.channel = kChannel;
    sessionConfig.nodeId = kNodeIdHost;
    // 0 asks the session for a per-boot generation, which is what makes a
    // rebooted host look new to its controllers instead of like a stale peer.
    sessionConfig.bootId = 0;
    sessionConfig.discovery = true;

    result = netSession.init(sessionConfig);
    start = result;
    if (result != NetResult::Ok) {
        return;
    }

#ifdef PLATFORM_NATIVE
    // One Engine dispatches one session, so the native build registers the host
    // here and pumps the two simulated controllers itself.
    engine.setNetworkSession(&netSession);
#endif
}

void PongHostScene::update(unsigned long deltaTime) {
    // Nothing to advance yet: the Engine pumps the session before this call,
    // and the fixed-step rules clock arrives with the rules core.
    (void)deltaTime;
}

void PongHostScene::draw(Renderer& renderer) {
    const int width = renderer.getLogicalWidth();
    const int height = renderer.getLogicalHeight();
    const int centreY = height / 2;

    renderer.drawFilledRectangle(0, 0, width, height, Color::Black);

    renderer.drawTextCentered("NETWORKING PONG", static_cast<int16_t>(centreY + kTitleOffsetY),
                              Color::White, kTitleScale);
    renderer.drawTextCentered("HOST  node 1", static_cast<int16_t>(centreY + kHostOffsetY),
                              Color::Gray, kTextScale);

    char line[kStatusCapacity];
    statusText(line);
    renderer.drawTextCentered(std::string_view(line), static_cast<int16_t>(centreY + kStatusOffsetY),
                              Color::Gray, kTextScale);

    // The frame clock is the only timing the scaffold can report: it shows at a
    // glance whether the panel is being pushed at a sane rate.
    const unsigned long frameMs = engine.getDeltaTime();
    unsigned long fps = (frameMs == 0) ? 0 : (1000ul / frameMs);
    if (fps > 999ul) {
        fps = 999ul;
    }

    char fpsLine[16];
    std::snprintf(fpsLine, sizeof(fpsLine), "%lu fps", fps);
    renderer.drawTextCentered(std::string_view(fpsLine), static_cast<int16_t>(centreY + kFpsOffsetY),
                              Color::Gray, kTextScale);
}

pixelroot32::networking::NetSession* PongHostScene::session() { return &netSession; }

#ifdef PLATFORM_NATIVE
pixelroot32::networking::LoopbackBus& PongHostScene::bus() { return loopbackBus; }
#endif

void PongHostScene::statusText(char* out) const {
    // "2" is the number of controllers the host fans out to; showing it as a
    // fraction makes a half-connected room obvious without opening a monitor.
    std::snprintf(out, kStatusCapacity, "ch %u  peers %u/2  %s",
                  static_cast<unsigned>(kChannel),
                  static_cast<unsigned>(netSession.peerCount()),
                  (start == NetResult::Ok) ? "link" : "no link");
}

}  // namespace netpong
