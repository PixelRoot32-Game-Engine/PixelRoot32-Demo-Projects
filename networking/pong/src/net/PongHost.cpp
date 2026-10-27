/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "net/PongHost.h"

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING

#include <cstddef>

#include "net/PongProtocol.h"

namespace netpong {

namespace {

namespace net = netpong::net;
using pixelroot32::networking::NetDelivery;
using pixelroot32::networking::NetEvent;
using pixelroot32::networking::NetEventType;
using pixelroot32::networking::NetPeerInfo;
using pixelroot32::networking::NetSession;

/// Session peer slots, mirrored so the mapping table below cannot silently
/// disagree with the session when the configured peer count changes.
constexpr std::uint8_t kMaxPeerSlots = NetSession::kMaxPeers;

}  // namespace

PongHost::PongHost(NetSession& session, std::uint32_t nowMs)
    : session_(session) {
    start(nowMs);
}

void PongHost::start(std::uint32_t nowMs) {
    rules_.resetMatch();
    for (std::uint8_t i = 0; i < kPlayerCount; ++i) {
        bindings_[i] = Binding{};
        lastInputSeq_[i] = 0;
    }
    for (std::uint8_t i = 0; i < kMaxPeerSlots; ++i) {
        peerPlayers_[i] = kUnboundPlayer;
    }
    stateSeq_ = 0;
    eventSeq_ = 0;
    // The cadence baselines start at the caller's clock, so the first update
    // measures a real delta instead of everything since boot.
    lastUpdateMs_ = nowMs;
    accumulatorUs_ = 0;
    stateElapsedUs_ = 0;
    effectHead_ = 0;
    effectCount_ = 0;
}

void PongHost::update(std::uint32_t nowMs) {
    std::uint32_t deltaMs = static_cast<std::uint32_t>(nowMs - lastUpdateMs_);
    lastUpdateMs_ = nowMs;
    if (deltaMs > kMaxFrameMs) {
        deltaMs = kMaxFrameMs;
    }

    // Fixed-step simulation. The accumulator is microsecond-based because the
    // rules contract is 16667 us, which a millisecond clock cannot express.
    accumulatorUs_ += deltaMs * 1000u;
    std::uint8_t steps = 0;
    while (accumulatorUs_ >= game::kStepDtUs && steps < kMaxCatchUpSteps) {
        accumulatorUs_ -= game::kStepDtUs;
        rules_.step(game::kStepDtUs);
        ++steps;
        // Draining after each step keeps a burst (serve + wall + goal in one
        // catch-up) in wire order instead of collapsing it at the end.
        drainRuleEvents();
    }
    if (accumulatorUs_ >= game::kStepDtUs) {
        // Saturated. The backlog is dropped, not replayed: after three steps
        // the ball is stale, and replaying the rest would stall the next frame.
        accumulatorUs_ = 0;
    }

    refreshReadyGate();
    maintainWatchdog(nowMs);
    // A controller that came back must un-pause the match on its own; the
    // players are watching a frozen court and have no button to press for it.
    // Resuming before the drain below also puts the Resumed event on the wire
    // in this same frame.
    resumeWhenReconnected(nowMs);
    // The watchdog can pause the match, and that Paused event must reach the
    // controllers in this same frame, not in the next one.
    drainRuleEvents();

    // STATE cadence is independent of the rules clock: 30 Hz per controller is
    // the airtime budget, and sending per frame would only add traffic.
    stateElapsedUs_ += deltaMs * 1000u;
    if (stateElapsedUs_ >= kStatePeriodUs) {
        // Keep the phase but never burst: a long frame yields one STATE, not a
        // train of catch-up snapshots.
        stateElapsedUs_ %= kStatePeriodUs;
        broadcastState(NetDelivery::Unreliable);
    }
}

void PongHost::onEvent(const NetEvent& event, std::uint32_t nowMs) {
    switch (event.type) {
        case NetEventType::PeerJoined:
            handleJoined(event, nowMs);
            break;
        case NetEventType::PeerLeft:
            handleLeft(event);
            break;
        case NetEventType::Message:
            handleMessage(event, nowMs);
            break;
        case NetEventType::SendFailed:
            // A reliable event that both ACKs and three retries missed is
            // already represented by the next STATE; the match must not be
            // frozen because a presentation frame never landed.
            break;
    }

    // A join may have completed the pair; starting the countdown here means the
    // first frame after the handshake already shows it.
    refreshReadyGate();

    // A PeerLeft or an incompatible APP_HELLO pauses the match inside the
    // switch above. Draining here forwards that Paused event in the same event
    // call instead of waiting for the next update, so the survivor freezes the
    // board on the frame it learns about the loss.
    drainRuleEvents();
}

game::Rules& PongHost::rules() { return rules_; }

const Binding& PongHost::binding(std::uint8_t player) const {
    static const Binding kUnbound{};
    if (player >= kPlayerCount) {
        return kUnbound;
    }
    return bindings_[player];
}

bool PongHost::bothPlayersReady() const {
    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        const Binding& candidate = bindings_[player];
        if (!candidate.bound || !candidate.online || !candidate.versionOk) {
            return false;
        }
    }
    return true;
}

std::uint8_t PongHost::channel() const { return kChannel; }

std::uint16_t PongHost::lastRttMs() const { return session_.stats().lastRttMs; }

std::uint32_t PongHost::rxDropped() const { return session_.stats().rxDropped; }

std::uint8_t PongHost::pendingCount() const { return session_.pendingCount(); }

bool PongHost::pollEffect(game::Event& out) {
    if (effectCount_ == 0) {
        return false;
    }
    out = effects_[effectHead_];
    effectHead_ = static_cast<std::uint8_t>((effectHead_ + 1) % kMaxEffects);
    --effectCount_;
    return true;
}

std::uint16_t PongHost::lastStateSeq() const { return stateSeq_; }

std::uint8_t PongHost::level() const {
    std::uint8_t ready = 0;
    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        const Binding& candidate = bindings_[player];
        if (candidate.bound && candidate.online && candidate.versionOk) {
            ++ready;
        }
    }
    return ready;
}

int PongHost::playerForNode(std::uint8_t nodeId) {
    if (nodeId == kNodeIdP1) {
        return 0;
    }
    if (nodeId == kNodeIdP2) {
        return 1;
    }
    // A board flashed with any other label (a stray node on the channel) is
    // not a controller for this match. It is recorded as unbound, never bound
    // to a player "just in case".
    return -1;
}

void PongHost::handleJoined(const NetEvent& event, std::uint32_t nowMs) {
    if (event.peerId >= kMaxPeerSlots) {
        return;
    }
    NetPeerInfo info{};
    if (!session_.peerInfo(event.peerId, info)) {
        return;
    }

    const int player = playerForNode(info.nodeId);
    if (player < 0) {
        peerPlayers_[event.peerId] = kUnboundPlayer;
        return;
    }
    const std::uint8_t index = static_cast<std::uint8_t>(player);
    Binding& candidate = bindings_[index];

    // Keep the first peer that claimed a player. A second board advertising
    // the same node id (a duplicate flash, a ghost after a bad reboot) must not
    // steal the paddle mid-match; it stays unbound and its INPUT is ignored.
    if (candidate.bound && candidate.peerId != event.peerId) {
        peerPlayers_[event.peerId] = kUnboundPlayer;
        return;
    }

    peerPlayers_[event.peerId] = index;
    candidate.peerId = event.peerId;
    candidate.nodeId = info.nodeId;
    candidate.bound = true;
    // A PeerJoined means the session already has the peer online, and a boot
    // with a new generation is a fresh handshake even for the same slot.
    candidate.online = true;
    candidate.versionOk = false;
    candidate.lastInputMs = nowMs;
    candidate.buttons = 0;
    candidate.previousButtons = 0;
    // The controller's sequence counter restarts on reboot, so the per-player
    // anti-replay window has to restart with it. Otherwise the first frames of
    // the new boot would look older than the last frame of the old one.
    lastInputSeq_[index] = 0;
    rules_.setPlayerInput(index, 0);

    // Handshake first, then a reliable snapshot: the rebooted controller may
    // not have painted a single frame yet, and waiting for the 30 Hz tick
    // would leave it showing a stale board for a third of a second.
    sendAppHello(event.peerId);
    sendStateTo(index, NetDelivery::Reliable);
}

void PongHost::handleLeft(const NetEvent& event) {
    if (event.peerId >= kMaxPeerSlots) {
        return;
    }
    const std::uint8_t player = peerPlayers_[event.peerId];
    if (player >= kPlayerCount) {
        return;
    }
    Binding& candidate = bindings_[player];
    if (!candidate.bound || candidate.peerId != event.peerId) {
        return;
    }

    candidate.online = false;
    candidate.buttons = 0;
    candidate.previousButtons = 0;
    rules_.setPlayerInput(player, 0);

    // pause() queues the Paused event only on a real transition. If the match
    // was already paused (both controllers gone) there is no event to forward,
    // so the survivor gets the PAUSE directly instead of silently.
    const bool alreadyPaused = rules_.paused();
    rules_.pause();
    if (alreadyPaused) {
        sendPause(true);
    }
    // Outside Countdown/Play/Point there is nothing to freeze (a controller
    // leaving in WaitingForPlayers or after MatchOver must not paint "PAUSED").
}

void PongHost::handleMessage(const NetEvent& event, std::uint32_t nowMs) {
    if (event.peerId >= kMaxPeerSlots) {
        return;
    }
    const std::uint8_t player = peerPlayers_[event.peerId];
    if (player >= kPlayerCount) {
        return;
    }
    Binding& candidate = bindings_[player];
    if (!candidate.bound || candidate.peerId != event.peerId ||
        !candidate.online) {
        return;
    }
    // The slot may have been re-labelled since the binding was made; the label
    // is the authority for who this peer is, not the slot index.
    if (event.nodeId != candidate.nodeId) {
        return;
    }

    net::Message message;
    if (!net::decode(event.payload, event.length, message)) {
        return;
    }

    switch (message.kind) {
        case net::MessageKind::AppHello: {
            candidate.versionOk = net::isCompatibleVersion(message.appHello.version);
            if (!candidate.versionOk) {
                // Refuse the half-flashed board instead of parsing its frames
                // with this build's field widths. The match freezes so neither
                // player is scored against while the boards disagree.
                rules_.pause();
            }
            break;
        }
        case net::MessageKind::Input:
            if (!candidate.versionOk) {
                break;
            }
            if (!net::isNewerSequence(message.input.seq,
                                      lastInputSeq_[player])) {
                // A reordered or duplicated frame. Applying it would move the
                // paddle backwards in time; the held-state model makes the
                // drop harmless.
                break;
            }
            lastInputSeq_[player] = message.input.seq;
            // The restart gesture is an edge, so the mask from the previous
            // accepted frame is kept before this one replaces it. A button that
            // was already down when the set ended is still down here, produces
            // no edge, and therefore cannot replay the match by itself.
            candidate.previousButtons = candidate.buttons;
            candidate.buttons = message.input.buttons;
            candidate.lastInputMs = nowMs;
            rules_.setPlayerInput(player, candidate.buttons);

            // S9: any controller button restarts after MatchOver. rules()
            // .restart() is a no-op outside MatchOver and only the first frame
            // of a press satisfies the predicate, so this cannot fire twice for
            // one hold. No reliable frame is sent: the phase rides in the next
            // 30 Hz STATE, which the controllers already parse.
            if (restartRequested(rules_.phase(), candidate.previousButtons,
                                 candidate.buttons)) {
                rules_.restart();
            }
            break;
        default:
            // Controllers only send APP_HELLO and INPUT. Anything else from a
            // controller is a version skew symptom and is ignored.
            break;
    }
}

void PongHost::refreshReadyGate() {
    rules_.setPlayersReady(bothPlayersReady());
}

void PongHost::maintainWatchdog(std::uint32_t nowMs) {
    const game::Phase phase = rules_.phase();
    if (phase != game::Phase::Countdown && phase != game::Phase::Play &&
        phase != game::Phase::Point) {
        return;
    }

    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        const Binding& candidate = bindings_[player];
        if (!candidate.bound || !candidate.online) {
            continue;
        }
        // Unsigned subtraction is wrap-safe: the injectable clock is allowed
        // to roll over during a long match.
        if (static_cast<std::uint32_t>(nowMs - candidate.lastInputMs) >=
            kWatchdogMs) {
            // The peer still answers HELLO but stopped sending INPUT: freeze
            // the rally rather than award a point to the board that is still
            // moving its paddle. pause() queues the Paused event that becomes
            // the reliable PAUSE below.
            rules_.pause();
            return;
        }
    }
}

void PongHost::resumeWhenReconnected(std::uint32_t nowMs) {
    if (rules_.phase() != game::Phase::Paused) {
        return;
    }
    if (!bothPlayersReady() || !playersResponsive(nowMs)) {
        return;
    }
    // resume() only acts on Paused and goes straight to Countdown, so this
    // fires exactly once per pause: the next update sees Countdown, not Paused.
    rules_.resume();
}

bool PongHost::playersResponsive(std::uint32_t nowMs) const {
    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        const Binding& candidate = bindings_[player];
        if (!candidate.bound || !candidate.online || !candidate.versionOk) {
            return false;
        }
        // The watchdog reads silence as a lost player. An INPUT that is older
        // than its window would pause the match again on the very next frame,
        // turning the reconnect into a Paused/Countdown loop instead of a match.
        if (static_cast<std::uint32_t>(nowMs - candidate.lastInputMs) >=
            kWatchdogMs) {
            return false;
        }
    }
    return true;
}

void PongHost::drainRuleEvents() {
    game::Event event;
    while (rules_.pollEvent(event)) {
        switch (event.type) {
            case game::EventType::Goal:
                sendPoint(event.player);
                break;
            case game::EventType::ServeStarted:
                sendRoundStart(event.player);
                break;
            case game::EventType::MatchOver:
                sendMatchEnd(event.player);
                break;
            case game::EventType::Paused:
                sendPause(true);
                break;
            case game::EventType::Resumed:
                sendPause(false);
                break;
            case game::EventType::PaddleHit:
            case game::EventType::WallBounce:
                // Presentation-only: the scene polls this ring, nothing goes
                // on the wire for a contact the controllers cannot see.
                pushEffect(event);
                break;
        }
    }
}

void PongHost::sendAppHello(std::uint8_t peerId) {
    net::AppHello hello;
    hello.version = net::kProtocolVersion;
    hello.role = net::kRoleHost;

    std::uint8_t frame[net::kAppHelloSize];
    const std::size_t length = net::encodeAppHello(hello, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    // Reliable: a controller that cannot parse STATE frames must never see one
    // without first having been told which version this host speaks.
    session_.send(peerId, NetDelivery::Reliable, frame,
                  static_cast<std::uint16_t>(length));
}

std::size_t PongHost::buildState(std::uint16_t seq, std::uint8_t* out,
                                 std::size_t capacity) const {
    const game::Snapshot snapshot = rules_.snapshot(seq);

    net::State state;
    state.seq = snapshot.seq;
    state.phase = snapshot.phase;
    state.ballX = snapshot.ballX;
    state.ballY = snapshot.ballY;
    state.ballVX = snapshot.ballVX;
    state.ballVY = snapshot.ballVY;
    state.p1Y = snapshot.p1Y;
    state.p2Y = snapshot.p2Y;
    state.score1 = snapshot.score1;
    state.score2 = snapshot.score2;
    state.flags = snapshot.flags;

    return net::encodeState(state, out, capacity);
}

void PongHost::broadcastState(NetDelivery delivery) {
    const std::uint16_t seq = nextStateSeq();
    std::uint8_t frame[net::kStateSize];
    const std::size_t length = buildState(seq, frame, sizeof(frame));
    if (length == 0) {
        return;
    }

    // NetSession is unicast-only, so the fan-out is an explicit per-peer loop.
    // One sequence for both controllers: the room must be able to compare the
    // frame it received with the one the other controller received.
    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        if (!canSend(player)) {
            continue;
        }
        session_.send(bindings_[player].peerId, delivery, frame,
                      static_cast<std::uint16_t>(length));
    }
}

void PongHost::sendStateTo(std::uint8_t player, NetDelivery delivery) {
    if (player >= kPlayerCount || !canSend(player)) {
        return;
    }
    const std::uint16_t seq = nextStateSeq();
    std::uint8_t frame[net::kStateSize];
    const std::size_t length = buildState(seq, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    session_.send(bindings_[player].peerId, delivery, frame,
                  static_cast<std::uint16_t>(length));
}

void PongHost::sendReliable(const std::uint8_t* frame, std::size_t length) {
    if (frame == nullptr || length == 0) {
        return;
    }
    for (std::uint8_t player = 0; player < kPlayerCount; ++player) {
        if (!canSend(player)) {
            continue;
        }
        session_.send(bindings_[player].peerId, NetDelivery::Reliable, frame,
                      static_cast<std::uint16_t>(length));
    }
}

void PongHost::sendPoint(std::uint8_t scorerPlayer) {
    net::Point point;
    point.seq = nextEventSeq();
    point.scorer = static_cast<std::uint8_t>((scorerPlayer < kPlayerCount ? scorerPlayer : 0) + 1);
    point.score1 = rules_.score(0);
    point.score2 = rules_.score(1);

    std::uint8_t frame[net::kPointSize];
    const std::size_t length = net::encodePoint(point, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    sendReliable(frame, length);
}

void PongHost::sendRoundStart(std::uint8_t serverPlayer) {
    net::RoundStart round;
    round.seq = nextEventSeq();
    // +1 serves right (from P1), -1 serves left (from P2). The controllers only
    // use it for the OLED label, but a wrong sign there tells a player the
    // wrong board is about to serve.
    round.serveDir = (serverPlayer == 0) ? 1 : -1;

    std::uint8_t frame[net::kRoundStartSize];
    const std::size_t length =
        net::encodeRoundStart(round, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    sendReliable(frame, length);
}

void PongHost::sendPause(bool paused) {
    net::Pause message;
    message.paused = paused ? 1u : 0u;

    std::uint8_t frame[net::kPauseSize];
    const std::size_t length = net::encodePause(message, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    sendReliable(frame, length);
}

void PongHost::sendMatchEnd(std::uint8_t winnerPlayer) {
    net::MatchEnd message;
    message.winner = static_cast<std::uint8_t>(
        (winnerPlayer < kPlayerCount ? winnerPlayer : 0) + 1);
    message.score1 = rules_.score(0);
    message.score2 = rules_.score(1);

    std::uint8_t frame[net::kMatchEndSize];
    const std::size_t length =
        net::encodeMatchEnd(message, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    sendReliable(frame, length);
}

void PongHost::pushEffect(const game::Event& effect) {
    if (effectCount_ >= kMaxEffects) {
        // Drop newest, matching the rules ring: during a burst the contact that
        // started the rally is more useful than its tail.
        return;
    }
    effects_[(effectHead_ + effectCount_) % kMaxEffects] = effect;
    ++effectCount_;
}

std::uint16_t PongHost::nextStateSeq() {
    ++stateSeq_;
    if (stateSeq_ == 0) {
        // 0 is the wire's "no ACK / no sequence" sentinel for unreliable data,
        // so the application counter skips it exactly like the session does.
        ++stateSeq_;
    }
    return stateSeq_;
}

std::uint16_t PongHost::nextEventSeq() {
    ++eventSeq_;
    if (eventSeq_ == 0) {
        ++eventSeq_;
    }
    return eventSeq_;
}

bool PongHost::canSend(std::uint8_t player) const {
    if (player >= kPlayerCount) {
        return false;
    }
    const Binding& candidate = bindings_[player];
    return candidate.bound && candidate.online;
}

}  // namespace netpong

#endif  // PIXELROOT32_ENABLE_NETWORKING
