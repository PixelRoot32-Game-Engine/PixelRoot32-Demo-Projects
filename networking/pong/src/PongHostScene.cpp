/*
 * Networking Pong - host scene (CYD and native).
 *
 * The scene is the composition root of the host: it brings the link up, starts
 * the session, owns the match adapter and paints the court that adapter feeds.
 */
#include <core/Engine.h>

#include "PongHostScene.h"

#include "NetConfig.h"
#include "presentation/CourtRenderer.h"

// Declared once by the platform header of whichever target is being built; the
// scene only borrows it, exactly as it borrows nothing else from the platform.
extern pixelroot32::core::Engine engine;

namespace netpong {

namespace {

using pixelroot32::graphics::Renderer;
using pixelroot32::networking::NetAddress;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSessionConfig;
using pixelroot32::networking::NetTransportConfig;

#ifdef PLATFORM_NATIVE
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

}  // namespace

#ifdef PLATFORM_NATIVE
PongHostScene::PongHostScene()
    : loopbackBus(),
      transport(loopbackBus, loopbackAddress(kNodeIdHost)),
      netSession(transport),
      host(netSession, 0) {}
#else
PongHostScene::PongHostScene()
    : transport(), netSession(transport), host(netSession, 0) {}
#endif

void PongHostScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();

    // The court, the HUD and the overlay all name palette slots, so the palette
    // is installed once, here, before the first frame is drawn.
    pixelroot32::graphics::setPalette(presentation::kCourtPalette);

    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());

    NetTransportConfig transportConfig;
    transportConfig.channel = kChannel;
    transportConfig.nodeId = kNodeIdHost;
    // The transport brings the station up without joining an access point: an
    // association would move the radio off kChannel and drop the link.
    transportConfig.manageWifi = true;

    NetResult result = transport.init(transportConfig);
    if (result != NetResult::Ok) {
        start = result;
        // Without a link the court still has to be truthful, so the match is
        // reset to WaitingForPlayers and the overlay says so.
        host.start(nowMs);
        return;
    }

    NetSessionConfig sessionConfig;
    sessionConfig.channel = kChannel;
    sessionConfig.nodeId = kNodeIdHost;
    // 0 asks the session for a per-boot generation, which is what makes a
    // rebooted host look new to its controllers instead of like a stale peer.
    sessionConfig.bootId = 0;
    sessionConfig.discovery = true;

    start = netSession.init(sessionConfig);
    // The adapter starts after the session so its cadence baseline comes from a
    // live clock. It sends nothing until a peer joins, so this is safe even when
    // the session failed to start.
    host.start(nowMs);
    if (start != NetResult::Ok) {
        return;
    }

#ifdef PLATFORM_NATIVE
    // One Engine dispatches one session, so the native build registers the host
    // here and pumps the two simulated controllers itself.
    engine.setNetworkSession(&netSession);
#endif
}

void PongHostScene::update(unsigned long deltaTime) {
    // Entities first, as everywhere else in the repository; this scene has none
    // today, so the call is the base contract rather than work.
    Scene::update(deltaTime);

    // The Engine already pumped the session and dispatched its events before
    // this call, so the only clock work left is the fixed-step match. The
    // adapter takes `millis()` rather than the frame delta: a slow frame must
    // not change how far the ball travelled.
    host.update(static_cast<std::uint32_t>(millis()));
}

void PongHostScene::draw(Renderer& renderer) {
    // Court first: it fills the score band as its own strip, so it can be
    // painted before everything else and still never erase a score.
    presentation::drawBackground(renderer);

    // Entities belong on top of the court, and only the base class draws them.
    Scene::draw(renderer);

    // Actors, then the HUD, then the overlay: the last two are the only things
    // allowed to sit over a live court.
    const game::Rules& rules = host.rules();
    presentation::drawActors(renderer, rules);
    presentation::drawHud(renderer, rules, host.level());
    presentation::drawOverlay(renderer, rules);
}

void PongHostScene::onNetworkEvent(const pixelroot32::networking::NetEvent& event) {
    host.onEvent(event, static_cast<std::uint32_t>(millis()));
}

pixelroot32::networking::NetSession* PongHostScene::session() { return &netSession; }

#ifdef PLATFORM_NATIVE
pixelroot32::networking::LoopbackBus& PongHostScene::bus() { return loopbackBus; }
#endif

}  // namespace netpong
