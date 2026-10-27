/*
 * Networking Pong - host scene (CYD and native).
 *
 * The scene is the composition root of the host: it brings the link up, starts
 * the session, owns the match adapter and paints the court that adapter feeds.
 */
#include <core/Engine.h>

#include "PongHostScene.h"

#include "NetConfig.h"
#include "net/PongProtocol.h"
#include "presentation/CourtLayout.h"
#include "presentation/CourtRenderer.h"
#include "presentation/PongSfx.h"

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

/// Indices of the four native keys in the InputConfig built by platforms/native.h.
/// The order there and the order here are the same contract: W/S drive P1,
/// UP/DOWN drive P2.
constexpr std::uint8_t kP1UpKey = 0;    ///< SDL_SCANCODE_W.
constexpr std::uint8_t kP1DownKey = 1;  ///< SDL_SCANCODE_S.
constexpr std::uint8_t kP2UpKey = 2;    ///< SDL_SCANCODE_UP.
constexpr std::uint8_t kP2DownKey = 3;  ///< SDL_SCANCODE_DOWN.
#endif

/// Samples the diagnostics panel from the host adapter's public getters.
/// @param host Adapter whose link facts the panel prints.
/// @return Plain-value snapshot; allocates nothing.
presentation::NetStatus makeNetStatus(const PongHost& host) {
    presentation::NetStatus status;
    status.channel = host.channel();
    for (std::uint8_t player = 0; player < PongHost::kPlayerCount; ++player) {
        const Binding& slot = host.binding(player);
        // One token answers "can this paddle be played with right now": bound to
        // the slot, reachable, and speaking a protocol version this host parses.
        status.slotOk[player] = slot.bound && slot.online && slot.versionOk;
    }
    status.lastRttMs = host.lastRttMs();
    status.rxDropped = host.rxDropped();
    status.pendingCount = host.pendingCount();
    return status;
}

}  // namespace

#ifdef PLATFORM_NATIVE
PongHostScene::PongHostScene()
    : loopbackBus(),
      transport(loopbackBus, loopbackAddress(kNodeIdHost)),
      netSession(transport),
      host(netSession, 0),
      p1Transport(loopbackBus, loopbackAddress(kNodeIdP1)),
      p1Session(p1Transport),
      p1Controller(p1Session, 0, 0),
      p2Transport(loopbackBus, loopbackAddress(kNodeIdP2)),
      p2Session(p2Transport),
      p2Controller(p2Session, 1, 0),
      shakeCamera(game::kCourtWidth, game::kCourtHeight) {}
#else
PongHostScene::PongHostScene()
    : transport(),
      netSession(transport),
      host(netSession, 0),
      shakeCamera(game::kCourtWidth, game::kCourtHeight) {}
#endif

void PongHostScene::init() {
    // Clears the entity array and rewinds the scene arena, so a second init()
    // cannot stack state on top of the first.
    Scene::init();

    // A restart must not inherit a burst or a jolt from the previous match.
    effects.reset();

    // The court, the HUD and the overlay all name slots of the generated table,
    // so the palette is bound before the first frame is drawn. The draw entry
    // points re-bind it too, which keeps a scene re-init from falling back to an
    // engine preset.
    presentation::installPalette();

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
        lastPhase = host.rules().phase();
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
    // WaitingForPlayers is the baseline, not an edge: init must not play the
    // countdown cue before there is anyone to serve to.
    lastPhase = host.rules().phase();
    if (start != NetResult::Ok) {
        return;
    }

#ifdef PLATFORM_NATIVE
    // One Engine dispatches one session, so the native build registers the host
    // here and pumps the two simulated controllers itself. Both boards join the
    // host's bus and start after it, exactly as two C3s would after the CYD.
    engine.setNetworkSession(&netSession);
    startController(p1Transport, p1Session, p1Controller, kNodeIdP1, nowMs);
    startController(p2Transport, p2Session, p2Controller, kNodeIdP2, nowMs);
#endif
}

void PongHostScene::update(unsigned long deltaTime) {
    // Entities first, as everywhere else in the repository; this scene has none
    // today, so the call is the base contract rather than work.
    Scene::update(deltaTime);

    const std::uint32_t nowMs = static_cast<std::uint32_t>(millis());

    // The Engine already pumped the session and dispatched its events before
    // this call, so the only clock work left is the fixed-step match. The
    // adapter takes `millis()` rather than the frame delta: a slow frame must
    // not change how far the ball travelled.
    host.update(nowMs);

#ifdef PLATFORM_NATIVE
    // The loopback bus has no clock of its own. Hand it the frame time the host
    // just used, then pump both simulated boards: without an Engine each, they
    // need the same update/drain/firmware sequence the Engine performs for the
    // host. This is why the native window leaves WAITING_FOR_PLAYERS on its own.
    loopbackBus.setNowMs(nowMs);

    const auto& keyboard = engine.getInputManager();
    std::uint8_t p1Buttons = 0;
    if (keyboard.isButtonDown(kP1UpKey)) {
        p1Buttons |= net::kButtonLeft;
    }
    if (keyboard.isButtonDown(kP1DownKey)) {
        p1Buttons |= net::kButtonRight;
    }
    std::uint8_t p2Buttons = 0;
    if (keyboard.isButtonDown(kP2UpKey)) {
        p2Buttons |= net::kButtonLeft;
    }
    if (keyboard.isButtonDown(kP2DownKey)) {
        p2Buttons |= net::kButtonRight;
    }

    pumpController(p1Session, p1Controller, nowMs, p1Buttons);
    pumpController(p2Session, p2Controller, nowMs, p2Buttons);
#endif

    // Effects advance before this frame's cues are triggered: a burst queued
    // below must be drawn at full life instead of being aged by the frame that
    // created it.
    effects.update(nowMs);

    // The host queues contacts in its local effect ring; goals, pauses and the
    // match end are phase edges, handled after the drain. The flags let a cue
    // that ever arrives both ways play once, never twice.
    const game::Rules& rules = host.rules();
    bool goalSeen = false;
    bool matchSeen = false;
    bool pauseSeen = false;
    bool resumeSeen = false;

    game::Event effect;
    while (host.pollEffect(effect)) {
        switch (effect.type) {
            case game::EventType::PaddleHit:
                playPaddleHit(rules, effect.player);
                break;
            case game::EventType::WallBounce:
                playWallBounce(rules);
                break;
            case game::EventType::Goal:
                playGoal(rules, effect.player);
                goalSeen = true;
                break;
            case game::EventType::MatchOver:
                playMatchPoint();
                matchSeen = true;
                break;
            case game::EventType::Paused:
                playSfx(engine, SfxId::Pause);
                pauseSeen = true;
                break;
            case game::EventType::Resumed:
                playSfx(engine, SfxId::Pause);
                resumeSeen = true;
                break;
            case game::EventType::ServeStarted:
                // The countdown blip already announced this serve; a second
                // sound on release would double up on every point.
                break;
        }
    }

    const game::Phase phase = rules.phase();
    if (phase == lastPhase) {
        return;
    }

    // A point is the edge out of Play into Point, or straight into MatchOver
    // when it is the winning point. The rules do not re-centre the ball during
    // this frame, so the scorer is still readable from where it left the court.
    if (lastPhase == game::Phase::Play &&
        (phase == game::Phase::Point || phase == game::Phase::MatchOver) && !goalSeen) {
        playGoal(rules, goalScorer(rules));
    }
    if (phase == game::Phase::MatchOver && !matchSeen) {
        playMatchPoint();
    }
    if (phase == game::Phase::Paused && !pauseSeen) {
        playSfx(engine, SfxId::Pause);
    }
    if (phase == game::Phase::Countdown) {
        // Resume goes Paused -> Countdown, so this one edge plays both cues:
        // the boards just came back, and the serve is about to be released.
        if (lastPhase == game::Phase::Paused && !resumeSeen) {
            playSfx(engine, SfxId::Pause);
        }
        playSfx(engine, SfxId::Countdown);
    }

    lastPhase = phase;
}

void PongHostScene::draw(Renderer& renderer) {
    const game::Rules& rules = host.rules();

    // Court first: it fills the score band as its own strip, so it can be
    // painted before everything else and still never erase a score. This base
    // pass is never shaken, so when the court is drawn again under the offset
    // the pixels the shifted copy vacates are already painted.
    presentation::drawBackground(renderer);

    // One sample per frame: getOffset() draws fresh random numbers on every
    // call, so the start and the end of the pass must reuse this value or the
    // court would tear against its own actors.
    const pixelroot32::math::Vector2 shake = effects.shake();
    const bool shaking = effects.shaking();
    shakeCamera.apply(renderer, shake);

    if (shaking) {
        // The frame and the centre line ride the jolt with the actors; the HUD
        // band is repainted below and never enters this pass.
        presentation::drawBackground(renderer);
    }

    // Entities belong on top of the court, and only the base class draws them.
    Scene::draw(renderer);

    // Actors, then the particles above them, both inside the shaken pass.
    presentation::drawActors(renderer, rules);
    effects.draw(renderer);

    shakeCamera.apply(renderer);
    if (shaking) {
        repaintHudBand(renderer);
    }

    // The HUD and the overlay are the only things allowed to sit over a live
    // court, and neither one moves with the shake.
    presentation::drawHud(renderer, rules, host.level());
    presentation::drawOverlay(renderer, rules, makeNetStatus(host));
}

void PongHostScene::playPaddleHit(const game::Rules& rules, std::uint8_t player) {
    presentation::CourtRect ball = presentation::ballRect(rules);
    if (ball.empty()) {
        // A queued contact should still be inside the play area, but if the
        // rule state has already moved on, land on the paddle the court drew
        // rather than throwing the burst at the origin.
        ball = presentation::paddleRect(rules, player);
    }

    const auto contactY = static_cast<std::int16_t>(ball.y + ball.height / 2);
    const auto ballX = static_cast<std::int16_t>(ball.x + ball.width / 2);
    effects.onPaddleHit(player, contactY, ballX);
    playSfx(engine, SfxId::PaddleHit);
}

void PongHostScene::playWallBounce(const game::Rules& rules) {
    const presentation::CourtRect ball = presentation::ballRect(rules);
    const int x = ball.empty() ? presentation::kCourtCentreX : ball.x + ball.width / 2;
    const int y = ball.empty() ? presentation::kPlayY : ball.y + ball.height / 2;
    effects.onWallBounce(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y));
    playSfx(engine, SfxId::WallBounce);
}

void PongHostScene::playGoal(const game::Rules& rules, std::uint8_t scorer) {
    const presentation::CourtRect play = presentation::playAreaRect();
    const presentation::CourtRect ball = presentation::ballRect(rules);
    const int ballY = ball.empty() ? play.y + play.height / 2 : ball.y + ball.height / 2;

    // Scorer 0 put the ball out on the right edge, scorer 1 on the left.
    const int edgeX =
        (scorer == 0) ? play.x + play.width - 1 : play.x;

    effects.onGoal(scorer, static_cast<std::int16_t>(edgeX),
                   static_cast<std::int16_t>(ballY));
    playSfx(engine, SfxId::Goal);
}

void PongHostScene::playMatchPoint() {
    effects.onMatchPoint();
    playSfx(engine, SfxId::Win);
}

std::uint8_t PongHostScene::goalScorer(const game::Rules& rules) const {
    if (rules.ballX() < 0) {
        return 1;
    }
    if (rules.ballX() > game::kCourtWidth * game::kOne) {
        return 0;
    }
    // Fallback for a caller that asks after the ball was placed again: the
    // score is the only remaining evidence of who was awarded the point.
    return rules.score(1) > rules.score(0) ? 1 : 0;
}

void PongHostScene::repaintHudBand(Renderer& renderer) {
    const presentation::ViewOffset origin =
        presentation::viewOffset(renderer.getLogicalWidth(), renderer.getLogicalHeight());

    // The shifted playfield can reach a few pixels under the band, so the band
    // is restored with the same paper slot the court paints it with. The HUD
    // text is drawn afterwards, by drawHud(), never inside the shaken pass.
    renderer.drawFilledRectangle(origin.x, origin.y, presentation::kDesignWidth,
                                 presentation::kHudHeight, presentation::kPaper);
}

void PongHostScene::onNetworkEvent(const pixelroot32::networking::NetEvent& event) {
    host.onEvent(event, static_cast<std::uint32_t>(millis()));
}

pixelroot32::networking::NetSession* PongHostScene::session() { return &netSession; }

#ifdef PLATFORM_NATIVE
pixelroot32::networking::LoopbackBus& PongHostScene::bus() { return loopbackBus; }

void PongHostScene::startController(
    pixelroot32::networking::LoopbackTransport& controllerTransport,
    pixelroot32::networking::NetSession& controllerSession,
    PongController& controller, std::uint8_t nodeId, std::uint32_t nowMs) {
    NetTransportConfig transportConfig;
    transportConfig.channel = kChannel;
    transportConfig.nodeId = nodeId;
    // Same posture as the C3 build: the station comes up without joining an AP.
    transportConfig.manageWifi = true;
    if (controllerTransport.init(transportConfig) != NetResult::Ok) {
        return;
    }

    NetSessionConfig sessionConfig;
    sessionConfig.channel = kChannel;
    sessionConfig.nodeId = nodeId;
    // 0 asks for a per-boot generation, so a re-init looks like a reboot to the
    // host instead of a duplicate of the previous peer.
    sessionConfig.bootId = 0;
    sessionConfig.discovery = true;
    if (controllerSession.init(sessionConfig) != NetResult::Ok) {
        return;
    }

    // Announce after the session exists; the controller looks the host up in the
    // peer table as well, because discovery may have run before this call.
    controller.start(nowMs);
}

void PongHostScene::pumpController(
    pixelroot32::networking::NetSession& controllerSession,
    PongController& controller, std::uint32_t nowMs, std::uint8_t buttons) {
    controllerSession.update(nowMs);

    // Neither consumer owns the queue: drain it once and hand every event to the
    // firmware, exactly like the Engine does for the host and like the ESP-NOW
    // dice demo does for its simulated second board.
    pixelroot32::networking::NetEvent event;
    while (controllerSession.pollEvent(event)) {
        controller.onEvent(event, nowMs);
    }

    // The firmware's cadence is driven by the same millisecond clock the host
    // used this frame, so the two boards and the tests share one time base.
    controller.update(nowMs, buttons);
}
#endif

}  // namespace netpong
