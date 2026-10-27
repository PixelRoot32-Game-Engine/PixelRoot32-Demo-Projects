/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "net/PongController.h"

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING

#include <cstddef>

#include "net/PongProtocol.h"

namespace netpong {

namespace {

using pixelroot32::networking::NetDelivery;
using pixelroot32::networking::NetEvent;
using pixelroot32::networking::NetEventType;
using pixelroot32::networking::NetPeerInfo;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSession;

/// Session peer slot that means "the host is not known yet".
constexpr std::uint8_t kUnknownPeer = 0xFF;

/// Ceiling of the wire's heldMs byte. The field is a single byte on purpose:
/// the host only needs to know whether a direction is still held, not an exact
/// duration, and the cap keeps a long hold from wrapping to zero.
constexpr std::uint32_t kHeldMsMax = 255;

}  // namespace

PongController::PongController(NetSession& session, std::uint8_t playerIndex,
                               std::uint32_t nowMs)
    : session_(session), playerIndex_(playerIndex) {
    // Construction must not touch the link: the scene builds this object before
    // the transport exists, and start() is the explicit "announce yourself".
    view_.playerIndex = playerIndex_;
    localPaddleY_ = kPaddleCentreMid;
    heldSinceMs_ = nowMs;
    lastInputSentMs_ = nowMs;
    lastUpdateMs_ = nowMs;
}

void PongController::start(std::uint32_t nowMs) {
    // A fresh view must look like the court the rules core parks at boot, not
    // like four zeros: a paddle marker at the top edge would read as a real
    // position before the first STATE arrives.
    view_ = ControllerView{};
    view_.playerIndex = playerIndex_;
    view_.phase = net::kPhaseWaitingForPlayers;
    view_.myPaddleY = static_cast<std::int16_t>(kPaddleCentreMid >> game::kFixedShift);
    view_.otherPaddleY = view_.myPaddleY;
    view_.ballX = static_cast<std::int16_t>(game::kBallCenterX >> game::kFixedShift);
    view_.ballY = static_cast<std::int16_t>(game::kBallCenterY >> game::kFixedShift);

    localPaddleY_ = kPaddleCentreMid;
    hostPeerId_ = kUnknownPeer;
    hostKnown_ = false;
    nextInputSeq_ = kFirstInputSeq;
    lastSentSeq_ = 0;
    observedButtons_ = 0;
    sentButtons_ = 0;
    heldSinceMs_ = nowMs;
    lastInputSentMs_ = nowMs;
    lastUpdateMs_ = nowMs;

    // The native simulator creates its scene after discovery has already run,
    // and the firmware can be re-initialized on a link that stayed up. Looking
    // the host up in the peer table covers both without waiting for a
    // PeerJoined that will never happen again.
    resolveHost();
    sendAppHello();
}

void PongController::update(std::uint32_t nowMs, std::uint8_t buttons) {
    std::uint32_t deltaMs = nowMs - lastUpdateMs_;
    lastUpdateMs_ = nowMs;
    if (deltaMs > kMaxPredictionMs) {
        deltaMs = kMaxPredictionMs;
    }

    // Prediction mirrors `game::Rules::updatePaddles()`: both buttons (or
    // neither) is ambiguous and must not move the paddle, and the sign matches
    // the wire's "left is towards the top of the court" contract.
    const bool up = (buttons & net::kButtonLeft) != 0;
    const bool down = (buttons & net::kButtonRight) != 0;
    if (up != down) {
        // Rounding, not truncation: 190 Q8.8 px/s over exactly 100 ms must be
        // 19 whole pixels, which is the value the panel test pins.
        const game::Fixed travel =
            (game::kPaddleSpeed * static_cast<game::Fixed>(deltaMs) + 500) / 1000;
        localPaddleY_ += up ? -travel : travel;
    }
    clampLocalPaddle();
    view_.myPaddleY =
        static_cast<std::int16_t>(localPaddleY_ >> game::kFixedShift);

    // Unsigned subtraction keeps the watchdog correct across the millis() wrap.
    if (view_.hostOnline &&
        static_cast<std::uint32_t>(nowMs - view_.lastStateMs) >=
            kHostTimeoutMs) {
        view_.hostOnline = false;
    }

    if (buttons != observedButtons_) {
        // A new hold period starts now; heldMs is measured from this edge.
        observedButtons_ = buttons;
        heldSinceMs_ = nowMs;
    }

    if (!hostKnown_) {
        // Nothing to address yet. The edge is kept in observedButtons_, so the
        // first update after the host joins still sends the current mask.
        return;
    }

    const bool changed = buttons != sentButtons_;
    const bool keepalive =
        static_cast<std::uint32_t>(nowMs - lastInputSentMs_) >=
        kInputKeepaliveMs;
    if (changed || keepalive) {
        sendInput(buttons, nowMs);
    }
}

void PongController::onEvent(const NetEvent& event, std::uint32_t nowMs) {
    switch (event.type) {
        case NetEventType::PeerJoined:
            if (event.nodeId != kNodeIdHost) {
                break;
            }
            hostPeerId_ = event.peerId;
            hostKnown_ = true;
            // A joining host has not exchanged versions with this boot yet,
            // even if the previous one did. The reply below is what closes it.
            view_.versionOk = false;
            sendAppHello();
            break;
        case NetEventType::PeerLeft:
            if (!hostKnown_ || event.peerId != hostPeerId_) {
                break;
            }
            hostKnown_ = false;
            hostPeerId_ = kUnknownPeer;
            view_.hostOnline = false;
            break;
        case NetEventType::Message:
            handleMessage(event, nowMs);
            break;
        case NetEventType::SendFailed:
            // A reliable APP_HELLO that exhausted its retries is superseded by
            // the next PeerJoined; the board must not wedge on it.
            break;
    }
}

const ControllerView& PongController::view() const { return view_; }

std::uint16_t PongController::lastSentSeq() const { return lastSentSeq_; }

void PongController::sendAppHello() {
    if (!hostKnown_) {
        return;
    }
    net::AppHello hello;
    hello.version = net::kProtocolVersion;
    hello.role = net::kRoleController;

    std::uint8_t frame[net::kAppHelloSize];
    const std::size_t length = net::encodeAppHello(hello, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    // Reliable: the host refuses INPUT and state until it has parsed this
    // frame, so losing it would leave the board visibly dead until the next
    // PeerJoined. APP_HELLO is a 3-byte, rare frame, never per-rally traffic.
    session_.send(hostPeerId_, NetDelivery::Reliable, frame,
                  static_cast<std::uint16_t>(length));
}

void PongController::sendInput(std::uint8_t buttons, std::uint32_t nowMs) {
    std::uint32_t held = nowMs - heldSinceMs_;
    if (held > kHeldMsMax) {
        held = kHeldMsMax;
    }

    net::Input input;
    input.seq = nextInputSeq_;
    input.buttons = buttons;
    input.heldMs = static_cast<std::uint8_t>(held);

    std::uint8_t frame[net::kInputSize];
    const std::size_t length = net::encodeInput(input, frame, sizeof(frame));
    if (length == 0) {
        return;
    }
    const NetResult result = session_.send(
        hostPeerId_, NetDelivery::Unreliable, frame,
        static_cast<std::uint16_t>(length));
    if (result != NetResult::Ok) {
        // A busy link is not a state change: retrying the same edge next frame
        // keeps both the sequence and the held-state contract intact.
        return;
    }

    lastSentSeq_ = input.seq;
    // Unreliable INPUT carries no session sequence, so any value the host can
    // order is valid; wrapping 0xFFFF -> 0x0000 is a newer frame under RFC 1982
    // and the host's `isNewerSequence()` accepts it.
    ++nextInputSeq_;
    sentButtons_ = buttons;
    lastInputSentMs_ = nowMs;
}

void PongController::handleMessage(const NetEvent& event, std::uint32_t nowMs) {
    // The session reports the sender's label, not just the slot: a stray board
    // that somehow reached this slot must not be able to paint the panel.
    if (event.nodeId != kNodeIdHost) {
        return;
    }

    net::Message message;
    if (!net::decode(event.payload, event.length, message)) {
        return;
    }

    switch (message.kind) {
        case net::MessageKind::AppHello:
            view_.versionOk = net::isCompatibleVersion(message.appHello.version);
            // Always answer, even when the versions differ: the host needs this
            // board's version to decide whether to keep it, and a rebooted host
            // waits for exactly this reply before it marks the slot ready.
            sendAppHello();
            break;
        case net::MessageKind::State:
            // Unreliable STATE can overtake an older one. Applying it would move
            // the paddle and the ball backwards in time, so the guard drops it.
            if (!net::isNewerSequence(message.state.seq, view_.stateSeq)) {
                break;
            }
            applyState(message.state, nowMs);
            break;
        case net::MessageKind::Point:
            // Scores travel with the point, so a lost STATE cannot leave the
            // panel one rally behind.
            view_.score1 = message.point.score1;
            view_.score2 = message.point.score2;
            view_.phase = net::kPhasePoint;
            break;
        case net::MessageKind::RoundStart:
            // ROUND_START is emitted when the serve goes live, which is the
            // moment the countdown ends; it is the phase a lost STATE would
            // otherwise leave stuck on the board.
            view_.phase = net::kPhasePlay;
            break;
        case net::MessageKind::Pause:
            // The rules resume through a fresh countdown, so a resume message
            // maps to Countdown rather than straight back to Play.
            view_.phase = message.pause.paused ? net::kPhasePaused
                                               : net::kPhaseCountdown;
            break;
        case net::MessageKind::MatchEnd:
            view_.score1 = message.matchEnd.score1;
            view_.score2 = message.matchEnd.score2;
            view_.phase = net::kPhaseMatchOver;
            break;
        default:
            // Controllers never receive INPUT, and nothing else is theirs.
            break;
    }
}

void PongController::applyState(const net::State& state, std::uint32_t nowMs) {
    view_.stateSeq = state.seq;
    view_.phase = state.phase;
    view_.score1 = state.score1;
    view_.score2 = state.score2;
    view_.ballX = state.ballX;
    view_.ballY = state.ballY;

    const std::int16_t mine = (playerIndex_ == 0) ? state.p1Y : state.p2Y;
    view_.otherPaddleY = (playerIndex_ == 0) ? state.p2Y : state.p1Y;

    // Reconcile the prediction with the authority. A large disagreement is a
    // real authority change (a goal reset, a paused court) and snaps; a small
    // one is RTT noise, and keeping 70 % of the local estimate hides it without
    // ever fighting the host for more than a few frames.
    const game::Fixed hostCentre =
        static_cast<game::Fixed>(mine) << game::kFixedShift;
    const game::Fixed diff = hostCentre - localPaddleY_;
    const game::Fixed magnitude = (diff < 0) ? -diff : diff;
    if (magnitude > kCentreSnapThreshold) {
        localPaddleY_ = hostCentre;
    } else {
        localPaddleY_ += diff * 3 / 10;
    }
    clampLocalPaddle();
    view_.myPaddleY =
        static_cast<std::int16_t>(localPaddleY_ >> game::kFixedShift);

    // The first accepted STATE is what makes the panel say LINK: the host is
    // provably reachable at that point, whatever the handshake said.
    view_.hostOnline = true;
    view_.lastStateMs = nowMs;
}

void PongController::resolveHost() {
    for (std::uint8_t slot = 0; slot < NetSession::kMaxPeers; ++slot) {
        NetPeerInfo info{};
        if (session_.peerInfo(slot, info) && info.online &&
            info.nodeId == kNodeIdHost) {
            hostPeerId_ = slot;
            hostKnown_ = true;
            return;
        }
    }
}

void PongController::clampLocalPaddle() {
    if (localPaddleY_ < kPaddleCentreMin) {
        localPaddleY_ = kPaddleCentreMin;
    } else if (localPaddleY_ > kPaddleCentreMax) {
        localPaddleY_ = kPaddleCentreMax;
    }
}

}  // namespace netpong

#endif  // PIXELROOT32_ENABLE_NETWORKING
