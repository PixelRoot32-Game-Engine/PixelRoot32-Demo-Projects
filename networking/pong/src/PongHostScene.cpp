/*
 * Networking Pong - host scene (CYD and native).
 *
 * The scene is the composition root of the host: it brings the link up, starts
 * the session, owns the match adapter and paints the court that adapter feeds.
 */
#include <core/Engine.h>

#include "PongHostScene.h"

#include "NetConfig.h"
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
#endif

}  // namespace

#ifdef PLATFORM_NATIVE
PongHostScene::PongHostScene()
    : loopbackBus(),
      transport(loopbackBus, loopbackAddress(kNodeIdHost)),
      netSession(transport),
      host(netSession, 0),
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
    // here and pumps the two simulated controllers itself.
    engine.setNetworkSession(&netSession);
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
    presentation::drawOverlay(renderer, rules);
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
#endif

}  // namespace netpong
