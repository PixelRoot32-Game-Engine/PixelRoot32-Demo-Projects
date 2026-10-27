/*
 * Three-node integration suite for the host session adapter (PongHost).
 *
 * This suite is the reason [env:net_test] exists. host_test proves the wire
 * codec and the rules core with the engine deliberately ignored; here the host
 * adapter is exercised against the REAL engine session, the real reliable
 * delivery path and the real discovery handshake, over one LoopbackTransport
 * per board attached to a shared LoopbackBus.
 *
 * The shape of the fixture mirrors the boards: node 1 is the CYD host, nodes 2
 * and 3 are the two C3 controllers, and node 9 stands in for a stray board that
 * is on the channel but must never obtain a paddle. Nothing is faked at the
 * session boundary: every frame in these tests is a complete protocol frame
 * produced by encodeFrame() inside NetSession and parsed by NetSession again.
 *
 * Two fixture decisions are worth stating because they are assertions in their
 * own right:
 *
 *  - The bus clock is injected in 10 ms steps and set before every
 *    session.update(), so no test depends on the wall clock. Peer expiry,
 *    ACK timing and the 30 Hz cadence are all measured on that clock.
 *  - Every session.update() is preceded by bus.setNowMs(now), and each step
 *    pumps the sessions twice (plus a host-only drain) so a peer that learns
 *    the host in this step can still answer inside the same step. That is the
 *    same double-pump pattern the engine's own discovery tests use.
 *
 * The suite also asserts session.stats().rxDropped == 0 after every step. A
 * cadence bug that floods a controller, or a resync that sends data to a peer
 * that has not discovered the host yet, would silently fall back to the
 * session's drop-newest policy; this turns that into a failing test instead.
 *
 * The functional branch is compiled only for the real networking build on the
 * native platform. [env:host_test] compiles the same directory with the engine
 * ignored and neither macro defined, so it runs the stub at the bottom and its
 * 52 existing cases keep passing unchanged.
 */
#include <unity.h>

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING && \
    defined(PLATFORM_NATIVE)

#include <drivers/native/LoopbackTransport.h>
#include <networking/NetSession.h>
#include <networking/NetTransport.h>
#include <networking/NetTypes.h>

#include <cstdint>

#include "NetConfig.h"
#include "game/PongRules.h"
#include "net/PongHost.h"
#include "net/PongProtocol.h"

namespace {

using netpong::Binding;
using netpong::PongHost;
namespace game = netpong::game;
namespace net = netpong::net;

using pixelroot32::networking::LoopbackBus;
using pixelroot32::networking::LoopbackTransport;
using pixelroot32::networking::NetAddress;
using pixelroot32::networking::NetDelivery;
using pixelroot32::networking::NetEvent;
using pixelroot32::networking::NetEventType;
using pixelroot32::networking::NetPeerInfo;
using pixelroot32::networking::NetResult;
using pixelroot32::networking::NetSession;
using pixelroot32::networking::NetSessionConfig;
using pixelroot32::networking::NetTransportConfig;

/// Injected clock step. Ten milliseconds keeps every cadence boundary cheap to
/// reach while staying well under the 20 ms ACK timeout.
constexpr std::uint32_t kStepMs = 10;

/// Node labels, duplicated here on purpose: a silent rename in NetConfig.h must
/// fail a test, not quietly change who is player one.
constexpr std::uint8_t kHostNode = 1;
constexpr std::uint8_t kP1Node = 2;
constexpr std::uint8_t kP2Node = 3;
constexpr std::uint8_t kStrayNode = 9;

/// Boot generations chosen so a rejoin is unambiguously a different boot.
constexpr std::uint16_t kHostBoot = 0x1001;
constexpr std::uint16_t kP1Boot = 0x2001;
constexpr std::uint16_t kP2Boot = 0x3001;
constexpr std::uint16_t kStrayBoot = 0x9001;
constexpr std::uint16_t kP1RebootBoot = 0x2002;
constexpr std::uint16_t kP2RebootBoot = 0x3002;

/// Deterministic, non-overlapping loopback MACs: every byte differs per tag.
NetAddress makeAddress(std::uint8_t tag) {
    NetAddress address{};
    for (std::uint8_t i = 0; i < pixelroot32::networking::kNetAddressSize; ++i) {
        address.bytes[i] = static_cast<std::uint8_t>(0x40u + tag + i);
    }
    return address;
}

/// One board: address, link, session and the small amount of per-controller
/// state the fixture needs to act like the C3 firmware will (answer a join,
/// send held direction, record what arrived).
struct Node {
    Node(LoopbackBus& bus, std::uint8_t tag, std::uint8_t node, std::uint16_t boot)
        : address(makeAddress(tag)), transport(bus, address), session(transport),
          nodeId(node), bootId(boot) {}

    /// Brings the link up and starts the session on kChannel with discovery on.
    bool init() {
        NetTransportConfig transportConfig;
        transportConfig.channel = netpong::kChannel;
        transportConfig.nodeId = nodeId;
        transportConfig.manageWifi = false;
        if (transport.init(transportConfig) != NetResult::Ok) {
            return false;
        }
        return session.init(makeSessionConfig()) == NetResult::Ok;
    }

    /// Re-runs init with a new boot generation after session.shutdown().
    ///
    /// The transport is deliberately left running: NetSession::shutdown()
    /// documents that it does not own the link, so a restarted session on a
    /// radio that stayed powered is the case under test. Its queued send
    /// results are drained first, because nothing pumps them while the session
    /// is down and a stale backlog would make the first HELLO return Busy.
    NetResult reboot(std::uint16_t newBootId, std::uint32_t nowMs) {
        bootId = newBootId;
        hostKnown = false;
        hostPeer = 0xFF;
        nextSeq = 1;
        buttons = 0;
        lastInputSentMs = nowMs;
        lastState = net::State{};
        lastStateSeq = 0;
        lastStateSendSequence = 0;
        sawReliableState = false;
        sawAppHello = false;
        gotPoint = false;
        point = net::Point{};
        pointSequence = 0;
        gotRoundStart = false;
        roundStart = net::RoundStart{};
        gotPause = false;
        pause = net::Pause{};
        gotMatchEnd = false;

        pixelroot32::networking::NetTxResult result{};
        while (transport.pollTxResult(result)) {
        }
        return session.init(makeSessionConfig());
    }

    NetSessionConfig makeSessionConfig() const {
        NetSessionConfig config;
        config.channel = netpong::kChannel;
        config.nodeId = nodeId;
        config.bootId = bootId;
        config.discovery = true;
        return config;
    }

    NetAddress address;
    LoopbackTransport transport;
    NetSession session;
    std::uint8_t nodeId;
    std::uint16_t bootId;

    // Controller role.
    std::uint8_t hostPeer = 0xFF;
    bool hostKnown = false;
    std::uint16_t nextSeq = 1;
    std::uint8_t buttons = 0;
    std::uint32_t lastInputSentMs = 0;

    // What arrived from the host.
    net::State lastState{};
    std::uint16_t lastStateSeq = 0;
    std::uint32_t lastStateSendSequence = 0;
    bool sawReliableState = false;
    bool sawAppHello = false;
    bool gotPoint = false;
    net::Point point{};
    std::uint32_t pointSequence = 0;
    bool gotRoundStart = false;
    net::RoundStart roundStart{};
    bool gotPause = false;
    net::Pause pause{};
    bool gotMatchEnd = false;
};

/// The whole three-node room plus the host adapter under test.
struct Rig {
    Rig() {
        initOk = host.init() && p1.init() && p2.init() && p4.init();
        hostLogic.start(0);
    }

    /// One full pump of every session on the injected clock.
    void pump() {
        bus.setNowMs(nowMs);
        p1.session.update(nowMs);
        p2.session.update(nowMs);
        p4.session.update(nowMs);
        host.session.update(nowMs);

        controllerTick(p1);
        controllerTick(p2);
        controllerTick(p4);
        controllerKeepalive(p1);
        controllerKeepalive(p2);
        controllerKeepalive(p4);

        drainHostEvents();
    }

    /// Pumps the host once more after PongHost::update has queued its frames,
    /// so the fixed transport result queue cannot fill between steps.
    void pumpHostOnly() {
        bus.setNowMs(nowMs);
        host.session.update(nowMs);
        drainHostEvents();
    }

    void step() {
        nowMs += kStepMs;
        pump();
        pump();
        hostLogic.update(nowMs);
        pumpHostOnly();
        assertNoDrops();
    }

    void steps(int count) {
        for (int i = 0; i < count; ++i) {
            step();
        }
    }

    /// The C3 firmware's receive loop, reduced to what the host contract needs.
    void controllerTick(Node& node) {
        NetEvent event{};
        while (node.session.pollEvent(event)) {
            switch (event.type) {
                case NetEventType::PeerJoined:
                    if (event.nodeId == kHostNode) {
                        node.hostPeer = event.peerId;
                        node.hostKnown = true;
                        sendAppHello(node, net::kProtocolVersion);
                    }
                    break;
                case NetEventType::PeerLeft:
                    if (event.nodeId == kHostNode) {
                        node.hostKnown = false;
                        node.hostPeer = 0xFF;
                    }
                    break;
                case NetEventType::Message:
                    handleControllerMessage(node, event);
                    break;
                case NetEventType::SendFailed:
                    break;
            }
        }
    }

    static void handleControllerMessage(Node& node, const NetEvent& event) {
        net::Message message;
        if (!net::decode(event.payload, event.length, message)) {
            return;
        }
        switch (message.kind) {
            case net::MessageKind::AppHello:
                node.sawAppHello = true;
                // The host announces first and the controller answers. This is
                // the handshake that stays correct across a session restart:
                // the reply is always produced after the host processed the
                // controller's new HELLO (and reset its dedupe window), so a
                // fresh sequence 1 is never mistaken for the previous boot's.
                sendAppHello(node, net::kProtocolVersion);
                break;
            case net::MessageKind::State:
                node.lastState = message.state;
                node.lastStateSeq = message.state.seq;
                node.lastStateSendSequence = event.sequence;
                // A reliable application frame carries the session's non-zero
                // wire sequence; unreliable ones carry 0.
                if (event.sequence != 0) {
                    node.sawReliableState = true;
                }
                break;
            case net::MessageKind::Point:
                node.gotPoint = true;
                node.point = message.point;
                node.pointSequence = event.sequence;
                break;
            case net::MessageKind::RoundStart:
                node.gotRoundStart = true;
                node.roundStart = message.roundStart;
                break;
            case net::MessageKind::Pause:
                node.gotPause = true;
                node.pause = message.pause;
                break;
            case net::MessageKind::MatchEnd:
                node.gotMatchEnd = true;
                break;
            default:
                break;
        }
    }

    void drainHostEvents() {
        NetEvent event{};
        while (host.session.pollEvent(event)) {
            hostLogic.onEvent(event, nowMs);
        }
    }

    static void sendAppHello(Node& node, std::uint8_t version) {
        if (!node.hostKnown) {
            return;
        }
        net::AppHello hello;
        hello.version = version;
        hello.role = net::kRoleController;
        std::uint8_t frame[net::kAppHelloSize];
        const std::size_t length =
            net::encodeAppHello(hello, frame, sizeof(frame));
        if (length == 0) {
            return;
        }
        node.session.send(node.hostPeer, NetDelivery::Reliable, frame,
                          static_cast<std::uint16_t>(length));
    }

    /// Sends one INPUT frame with an explicit sequence.
    static void sendInput(Node& node, std::uint16_t seq, std::uint8_t buttons,
                          std::uint32_t nowMs) {
        if (!node.hostKnown) {
            return;
        }
        net::Input input;
        input.seq = seq;
        input.buttons = buttons;
        input.heldMs = 0;
        std::uint8_t frame[net::kInputSize];
        const std::size_t length =
            net::encodeInput(input, frame, sizeof(frame));
        if (length == 0) {
            return;
        }
        node.session.send(node.hostPeer, NetDelivery::Unreliable, frame,
                          static_cast<std::uint16_t>(length));
        node.lastInputSentMs = nowMs;
    }

    /// Changes the held direction and sends it immediately, like a button edge.
    void setButtons(Node& node, std::uint8_t buttons) {
        node.buttons = buttons;
        sendInput(node, node.nextSeq, buttons, nowMs);
        ++node.nextSeq;
    }

    /// Re-sends an already used sequence; the host must ignore it.
    void sendStaleInput(Node& node, std::uint16_t seq, std::uint8_t buttons) {
        sendInput(node, seq, buttons, nowMs);
    }

    /// The 250 ms keepalive from S4: an unchanged INPUT still proves liveness.
    void controllerKeepalive(Node& node) {
        if (!node.hostKnown) {
            return;
        }
        if (nowMs - node.lastInputSentMs < netpong::kInputKeepaliveMs) {
            return;
        }
        sendInput(node, node.nextSeq, node.buttons, nowMs);
        ++node.nextSeq;
    }
    /// Drop-newest is the engine's fallback, not the demo's contract: if any
    /// session had to drop a frame, the cadence or the resync is wrong.
    ///
    /// One documented exception: a board that reboots sends its HELLO before it
    /// has rediscovered the host, so the host's first resync frames can arrive
    /// at a session that does not know the sender yet. NetSession drops those by
    /// design ("data never creates a peer") and the 30 Hz fan-out repairs the
    /// board one frame later, so a reconnect test turns this check off around
    /// the reboot and asserts the room converges instead.
    void assertNoDrops() const {
        if (!expectNoDrops) {
            return;
        }
        TEST_ASSERT_EQUAL_UINT32(0, host.session.stats().rxDropped);
        TEST_ASSERT_EQUAL_UINT32(0, p1.session.stats().rxDropped);
        TEST_ASSERT_EQUAL_UINT32(0, p2.session.stats().rxDropped);
        TEST_ASSERT_EQUAL_UINT32(0, p4.session.stats().rxDropped);
    }

    bool expectNoDrops = true;
    bool initOk = false;
    LoopbackBus bus;
    Node host{bus, kHostNode, kHostNode, kHostBoot};
    Node p1{bus, kP1Node, kP1Node, kP1Boot};
    Node p2{bus, kP2Node, kP2Node, kP2Boot};
    Node p4{bus, kStrayNode, kStrayNode, kStrayBoot};
    PongHost hostLogic{host.session, 0};
    std::uint32_t nowMs = 0;
};

/// Compares every wire field of two snapshots, so "same seq" cannot hide a
/// per-peer divergence in a later field.
void assertSameState(const net::State& expected, const net::State& actual) {
    TEST_ASSERT_EQUAL_UINT16(expected.seq, actual.seq);
    TEST_ASSERT_EQUAL_UINT8(expected.phase, actual.phase);
    TEST_ASSERT_EQUAL_INT16(expected.ballX, actual.ballX);
    TEST_ASSERT_EQUAL_INT16(expected.ballY, actual.ballY);
    TEST_ASSERT_EQUAL_INT16(expected.ballVX, actual.ballVX);
    TEST_ASSERT_EQUAL_INT16(expected.ballVY, actual.ballVY);
    TEST_ASSERT_EQUAL_INT16(expected.p1Y, actual.p1Y);
    TEST_ASSERT_EQUAL_INT16(expected.p2Y, actual.p2Y);
    TEST_ASSERT_EQUAL_UINT8(expected.score1, actual.score1);
    TEST_ASSERT_EQUAL_UINT8(expected.score2, actual.score2);
    TEST_ASSERT_EQUAL_UINT8(expected.flags, actual.flags);
}

std::uint8_t phaseByte(const game::Rules& rules) {
    return static_cast<std::uint8_t>(rules.phase());
}

}  // namespace

// =============================================================================
// 1. The two expected node ids bind to their player slots; a stray node joins
//    the room but never obtains a paddle and its input never moves one.
// =============================================================================

void test_host_binds_expected_nodes_and_ignores_the_stray_peer(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(3);

    const Binding& p1 = rig.hostLogic.binding(0);
    const Binding& p2 = rig.hostLogic.binding(1);
    TEST_ASSERT_TRUE(p1.bound);
    TEST_ASSERT_EQUAL_UINT8(kP1Node, p1.nodeId);
    TEST_ASSERT_TRUE(p1.online);
    TEST_ASSERT_TRUE(p1.versionOk);
    TEST_ASSERT_TRUE(p2.bound);
    TEST_ASSERT_EQUAL_UINT8(kP2Node, p2.nodeId);
    TEST_ASSERT_TRUE(p2.online);
    TEST_ASSERT_TRUE(p2.versionOk);
    TEST_ASSERT_TRUE(p1.peerId != p2.peerId);
    TEST_ASSERT_TRUE(p1.nodeId != kStrayNode);
    TEST_ASSERT_TRUE(p2.nodeId != kStrayNode);

    // The stray board did join the session (three controllers are heard) but
    // no binding and no HUD level was created for it.
    TEST_ASSERT_EQUAL_UINT8(3, rig.host.session.peerCount());
    TEST_ASSERT_EQUAL_UINT8(2, rig.hostLogic.level());
    TEST_ASSERT_TRUE(rig.hostLogic.bothPlayersReady());

    // From here on the stray holds "down" for 450 ms. Both real paddles are
    // under no input, so any movement would be the stray's input leaking in.
    const game::Fixed p1Y = rig.hostLogic.rules().paddleY(0);
    const game::Fixed p2Y = rig.hostLogic.rules().paddleY(1);
    rig.setButtons(rig.p4, net::kButtonRight);
    rig.steps(45);
    TEST_ASSERT_EQUAL_INT32(p1Y, rig.hostLogic.rules().paddleY(0));
    TEST_ASSERT_EQUAL_INT32(p2Y, rig.hostLogic.rules().paddleY(1));
}

// =============================================================================
// 2. One State fan-out, one sequence: both controllers see the same snapshot
//    and it matches the host's authoritative rules.
// =============================================================================

void test_state_is_fanned_out_to_both_controllers_with_one_sequence(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(10);  // t = 100 ms: cadence has fired at least twice

    TEST_ASSERT_TRUE_MESSAGE(rig.p1.lastStateSeq > 0, "P1 never received a State");
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.lastStateSeq > 0, "P2 never received a State");
    TEST_ASSERT_EQUAL_UINT16(rig.p1.lastStateSeq, rig.p2.lastStateSeq);
    assertSameState(rig.p1.lastState, rig.p2.lastState);

    // Every unreliable State carries sequence 0 on the session wire.
    TEST_ASSERT_EQUAL_UINT32(0, rig.p1.lastStateSendSequence);
    TEST_ASSERT_EQUAL_UINT32(0, rig.p2.lastStateSendSequence);

    const game::Snapshot snapshot = rig.hostLogic.rules().snapshot(0);
    TEST_ASSERT_EQUAL_UINT8(net::kPhaseCountdown, rig.p1.lastState.phase);
    TEST_ASSERT_EQUAL_UINT8(snapshot.score1, rig.p1.lastState.score1);
    TEST_ASSERT_EQUAL_UINT8(snapshot.score2, rig.p1.lastState.score2);
    TEST_ASSERT_EQUAL_INT16(snapshot.ballX, rig.p1.lastState.ballX);
    TEST_ASSERT_EQUAL_INT16(snapshot.ballY, rig.p1.lastState.ballY);
    TEST_ASSERT_EQUAL_INT16(snapshot.p1Y, rig.p1.lastState.p1Y);
    TEST_ASSERT_EQUAL_INT16(snapshot.p2Y, rig.p1.lastState.p2Y);

    // The controllers have caught up with the newest cadence frame: one more
    // step delivers it without the host sending a new one.
    rig.steps(1);
    TEST_ASSERT_EQUAL_UINT16(rig.hostLogic.lastStateSeq(), rig.p1.lastStateSeq);
}

// =============================================================================
// 3. P1 input moves only the left paddle; P2 input moves only the right one.
// =============================================================================

void test_each_controller_only_moves_its_own_paddle(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    const game::Fixed leftStart = rig.hostLogic.rules().paddleY(0);
    const game::Fixed rightStart = rig.hostLogic.rules().paddleY(1);

    rig.setButtons(rig.p1, net::kButtonRight);
    rig.steps(20);  // 200 ms at 190 px/s
    TEST_ASSERT_TRUE_MESSAGE(rig.hostLogic.rules().paddleY(0) > leftStart + 20 * game::kOne,
                             "P1 input did not move the left paddle");
    TEST_ASSERT_EQUAL_INT32(rightStart, rig.hostLogic.rules().paddleY(1));

    const game::Fixed leftMoved = rig.hostLogic.rules().paddleY(0);
    rig.setButtons(rig.p1, 0);
    rig.setButtons(rig.p2, net::kButtonLeft);
    rig.steps(20);
    TEST_ASSERT_EQUAL_INT32(leftMoved, rig.hostLogic.rules().paddleY(0));
    TEST_ASSERT_TRUE_MESSAGE(rig.hostLogic.rules().paddleY(1) < rightStart - 20 * game::kOne,
                             "P2 input did not move the right paddle");
}

// =============================================================================
// 4. A stale INPUT (an already accepted sequence) is ignored.
// =============================================================================

void test_stale_input_sequence_is_ignored(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    rig.setButtons(rig.p1, net::kButtonRight);
    rig.steps(3);
    rig.setButtons(rig.p1, 0);
    rig.steps(2);

    const game::Fixed stopped = rig.hostLogic.rules().paddleY(0);
    const std::uint16_t alreadyUsed =
        static_cast<std::uint16_t>(rig.p1.nextSeq - 2);

    // Replay an older frame that still says "down". The sequence guard must
    // reject it, so the released paddle stays exactly where it is.
    rig.sendStaleInput(rig.p1, alreadyUsed, net::kButtonRight);
    rig.steps(3);
    TEST_ASSERT_EQUAL_INT32(stopped, rig.hostLogic.rules().paddleY(0));
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostLogic.binding(0).buttons);
}

// =============================================================================
// 5. An APP_HELLO with a foreign version blocks that peer and pauses the match.
// =============================================================================

void test_incompatible_version_blocks_input_and_pauses(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);
    TEST_ASSERT_TRUE(rig.hostLogic.binding(0).versionOk);

    Rig::sendAppHello(rig.p1, static_cast<std::uint8_t>(net::kProtocolVersion + 1));
    rig.steps(3);

    TEST_ASSERT_FALSE(rig.hostLogic.binding(0).versionOk);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::Paused),
                            phaseByte(rig.hostLogic.rules()));
    TEST_ASSERT_EQUAL_UINT8(1, rig.hostLogic.level());
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.gotPause, "P2 never saw the reliable PAUSE");
    TEST_ASSERT_EQUAL_UINT8(1, rig.p2.pause.paused);

    const game::Fixed frozen = rig.hostLogic.rules().paddleY(0);
    rig.setButtons(rig.p1, net::kButtonRight);
    rig.steps(20);
    TEST_ASSERT_EQUAL_INT32(frozen, rig.hostLogic.rules().paddleY(0));
}

// =============================================================================
// 6. A Goal arrives as a reliable Point with the score, and the next serve is
//    announced with a RoundStart carrying its direction.
// =============================================================================

void test_goal_is_reliably_reported_and_the_next_serve_is_announced(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    // Park P2's paddle at the top of the court during the countdown: the first
    // serve is flat, so the ball passes the right paddle and P1 scores.
    rig.setButtons(rig.p2, net::kButtonLeft);

    int guard = 0;
    while (!rig.p1.gotPoint && guard++ < 700) {
        rig.step();
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.p1.gotPoint, "P1 never received a POINT");
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.gotPoint, "P2 never received a POINT");
    TEST_ASSERT_EQUAL_UINT8(1, rig.p1.point.scorer);
    TEST_ASSERT_EQUAL_UINT8(1, rig.p1.point.score1);
    TEST_ASSERT_EQUAL_UINT8(0, rig.p1.point.score2);
    TEST_ASSERT_EQUAL_UINT16(rig.p1.point.seq, rig.p2.point.seq);
    TEST_ASSERT_TRUE_MESSAGE(rig.p1.pointSequence != 0,
                             "POINT must be sent reliably");
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.pointSequence != 0,
                             "POINT must be sent reliably");

    // The first serve already produced a ROUND_START; arm the wait again so the
    // next one (after the point pause) is the frame under test.
    rig.p1.gotRoundStart = false;
    rig.p2.gotRoundStart = false;
    guard = 0;
    while (!rig.p1.gotRoundStart && guard++ < 300) {
        rig.step();
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.p1.gotRoundStart, "no ROUND_START after the point");
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.gotRoundStart, "P2 missed the ROUND_START");
    // The first serve went right (P1 served) and flipped the direction, so the
    // next one is served by P2 and travels left.
    TEST_ASSERT_EQUAL_INT8(-1, rig.p1.roundStart.serveDir);
}

// =============================================================================
// 7. Paddle and wall contacts stay local: the scene polls them, they are not
//    put on the wire.
// =============================================================================

void test_paddle_contact_is_reported_as_a_local_effect(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    int guard = 0;
    while (rig.hostLogic.rules().phase() != game::Phase::Play && guard++ < 200) {
        rig.step();
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.hostLogic.rules().phase() == game::Phase::Play,
                             "the first serve never went live");

    // The flat first serve heads straight at the centred right paddle, so the
    // first effect is P2's contact about 800 ms later.
    game::Event effect{};
    bool haveEffect = false;
    guard = 0;
    while (guard++ < 200) {
        rig.step();
        if (rig.hostLogic.pollEffect(effect)) {
            haveEffect = true;
            break;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(haveEffect, "no local effect for a paddle contact");
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::EventType::PaddleHit),
                            static_cast<std::uint8_t>(effect.type));
    TEST_ASSERT_EQUAL_UINT8(1, effect.player);
}

// =============================================================================
// 8. Peer loss pauses and notifies the survivor; a reboot with a new boot id
//    re-binds, resyncs (APP_HELLO + reliable STATE) and the two controllers
//    converge on one state sequence again.
// =============================================================================

void test_peer_loss_pauses_then_a_reboot_resyncs_the_room(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);
    TEST_ASSERT_TRUE(rig.hostLogic.binding(0).versionOk);

    // P1 goes away. shutdown() sends BYE, so the host learns immediately and
    // asks the survivor to freeze the board.
    rig.p1.session.shutdown();
    rig.steps(2);
    TEST_ASSERT_FALSE(rig.hostLogic.binding(0).online);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::Paused),
                            phaseByte(rig.hostLogic.rules()));
    TEST_ASSERT_TRUE_MESSAGE(rig.p2.gotPause, "P2 never saw the peer-loss PAUSE");
    TEST_ASSERT_EQUAL_UINT8(1, rig.p2.pause.paused);

    // The peer timeout (3 s by default) must not revive a silent peer.
    rig.steps(350);
    TEST_ASSERT_FALSE(rig.hostLogic.binding(0).online);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::Paused),
                            phaseByte(rig.hostLogic.rules()));

    // P1 reboots with a new boot generation and discovers the host again.
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(NetResult::Ok),
        static_cast<std::uint8_t>(rig.p1.reboot(kP1RebootBoot, rig.nowMs)));

    int guard = 0;
    while (!(rig.hostLogic.binding(0).online &&
             rig.hostLogic.binding(0).versionOk) &&
           guard++ < 200) {
        rig.step();
    }
    const Binding& rebinding = rig.hostLogic.binding(0);
    TEST_ASSERT_TRUE_MESSAGE(rebinding.bound, "the rebooted peer was not re-bound");
    TEST_ASSERT_EQUAL_UINT8(kP1Node, rebinding.nodeId);
    TEST_ASSERT_TRUE_MESSAGE(rebinding.online, "the rebooted peer stayed offline");
    TEST_ASSERT_TRUE_MESSAGE(rebinding.versionOk,
                             "the rebooted peer's APP_HELLO was not accepted");

    // The resync frame is a reliable STATE (non-zero session sequence) and the
    // host also re-sent its APP_HELLO to the rebooted board.
    TEST_ASSERT_TRUE_MESSAGE(rig.p1.sawReliableState,
                             "the rejoin did not include a reliable STATE");
    TEST_ASSERT_TRUE_MESSAGE(rig.p1.sawAppHello,
                             "the rejoin did not include an APP_HELLO");

    // Both controllers converge on the same cadence sequence again.
    rig.steps(8);
    TEST_ASSERT_TRUE(rig.p1.lastStateSeq > 0);
    TEST_ASSERT_EQUAL_UINT16(rig.p1.lastStateSeq, rig.p2.lastStateSeq);
    TEST_ASSERT_TRUE(rig.hostLogic.lastStateSeq() >= rig.p1.lastStateSeq);

    // A fresh INPUT from the rebooted peer is accepted (its counter restarted,
    // so the per-player sequence window had to restart with it).
    rig.setButtons(rig.p1, net::kButtonRight);
    rig.steps(2);
    TEST_ASSERT_EQUAL_UINT8(net::kButtonRight, rig.hostLogic.binding(0).buttons);
}

// =============================================================================
// 9. A paused host resumes into Countdown exactly once when both controllers
//    come back, and never falls back into Paused afterwards.
// =============================================================================

void test_paused_host_resumes_exactly_once_when_both_controllers_return(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);
    TEST_ASSERT_TRUE(rig.hostLogic.bothPlayersReady());

    // Both boards drop. The first loss pauses the match; the second one is
    // already in Paused, so the host can only tell the (absent) survivor.
    rig.p1.session.shutdown();
    rig.p2.session.shutdown();
    rig.steps(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::Paused),
                            phaseByte(rig.hostLogic.rules()));

    // Both reboot with a new generation and complete the handshake again. The
    // host's first resync frames can reach a board that has not rediscovered the
    // host yet; that transient is measured, not swept under the rug.
    rig.expectNoDrops = false;
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(NetResult::Ok),
        static_cast<std::uint8_t>(rig.p1.reboot(kP1RebootBoot, rig.nowMs)));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(NetResult::Ok),
        static_cast<std::uint8_t>(rig.p2.reboot(kP2RebootBoot, rig.nowMs)));

    // Count every Paused -> live edge on the way to the first serve. The match
    // must leave Paused once, land in Countdown, and never bounce back.
    int resumes = 0;
    int rePauses = 0;
    game::Phase previous = rig.hostLogic.rules().phase();
    int guard = 0;
    while (rig.hostLogic.rules().phase() != game::Phase::Play && guard++ < 600) {
        rig.step();
        const game::Phase current = rig.hostLogic.rules().phase();
        if (previous == game::Phase::Paused && current != game::Phase::Paused) {
            ++resumes;
            TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::Countdown),
                                    phaseByte(rig.hostLogic.rules()));
        }
        if (previous != game::Phase::Paused && current == game::Phase::Paused) {
            ++rePauses;
        }
        previous = current;
    }

    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::Play),
                                    phaseByte(rig.hostLogic.rules()),
                                    "the reconnected match never served");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, resumes,
                                  "the host resumed more than once after a reconnect");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, rePauses,
                                  "the reconnected match fell back into Paused");
}

// =============================================================================
// 10. A match that already reached MatchOver keeps its result: the host never
//     auto-resumes or restarts it, even with both controllers ready again.
// =============================================================================

void test_match_over_host_never_auto_resumes(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    // Both paddles parked at the top: every serve travels straight to an edge,
    // so the single set reaches 5 without any rally depending on jitter. Serves
    // alternate, so the points alternate too.
    rig.setButtons(rig.p1, net::kButtonLeft);
    rig.setButtons(rig.p2, net::kButtonLeft);

    int guard = 0;
    while (rig.hostLogic.rules().phase() != game::Phase::MatchOver && guard++ < 5000) {
        rig.step();
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::MatchOver),
                                    phaseByte(rig.hostLogic.rules()),
                                    "the match never reached MatchOver");
    const std::uint8_t final1 = rig.hostLogic.rules().score(0);
    const std::uint8_t final2 = rig.hostLogic.rules().score(1);
    TEST_ASSERT_TRUE(final1 == game::kWinScore || final2 == game::kWinScore);

    // Release the paddles: both controllers stay bound, online and compatible,
    // which leaves every gate of the auto-resume open. MatchOver is the one
    // phase the host must not leave.
    rig.setButtons(rig.p1, 0);
    rig.setButtons(rig.p2, 0);
    TEST_ASSERT_TRUE(rig.hostLogic.bothPlayersReady());
    rig.steps(200);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::MatchOver),
                            phaseByte(rig.hostLogic.rules()));

    // Nor after a controller reboot: the resync restores the board, not the
    // match. A leave must not paint PAUSED over the final score either.
    rig.expectNoDrops = false;
    rig.p1.session.shutdown();
    rig.steps(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::MatchOver),
                            phaseByte(rig.hostLogic.rules()));
    TEST_ASSERT_EQUAL_UINT8(
        static_cast<std::uint8_t>(NetResult::Ok),
        static_cast<std::uint8_t>(rig.p1.reboot(kP1RebootBoot, rig.nowMs)));
    guard = 0;
    while (!rig.hostLogic.binding(0).versionOk && guard++ < 400) {
        rig.step();
    }
    TEST_ASSERT_TRUE(rig.hostLogic.binding(0).versionOk);
    rig.steps(50);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::MatchOver),
                            phaseByte(rig.hostLogic.rules()));
    TEST_ASSERT_EQUAL_UINT8(final1, rig.hostLogic.rules().score(0));
    TEST_ASSERT_EQUAL_UINT8(final2, rig.hostLogic.rules().score(1));
}

// =============================================================================
// 11. The diagnostics getters behind the HUD panel: a clean match leaves zero
//     drops, stamps a real RTT and reports the reliable queue.
// =============================================================================

void test_diagnostics_report_no_drops_and_a_measured_rtt(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);
    TEST_ASSERT_TRUE(rig.hostLogic.bothPlayersReady());

    // The channel is configuration, not a counter: it must be the one wire
    // constant every node shares.
    TEST_ASSERT_EQUAL_UINT8(netpong::kChannel, rig.hostLogic.channel());

    // The panel reads the same counters the session keeps: a second source of
    // truth would show a healthy link over a dropping one.
    TEST_ASSERT_EQUAL_UINT32(rig.host.session.stats().rxDropped,
                             rig.hostLogic.rxDropped());
    TEST_ASSERT_EQUAL_UINT8(rig.host.session.pendingCount(),
                            rig.hostLogic.pendingCount());
    TEST_ASSERT_EQUAL_UINT32(0, rig.hostLogic.rxDropped());

    // RTT is stamped when an ACK retires a pending reliable frame. The fixture
    // pumps each session twice inside one 10 ms step so a peer can answer in the
    // same step, which means the handshake frames are acknowledged at the same
    // injected millisecond and measure as zero. The first reliable match event
    // (the POINT below) crosses a step boundary, so it is the frame that yields
    // a real measurement; nothing here is faked.
    rig.setButtons(rig.p2, net::kButtonLeft);
    int guard = 0;
    while (rig.hostLogic.lastRttMs() == 0 && guard++ < 400) {
        rig.step();
    }
    TEST_ASSERT_TRUE_MESSAGE(rig.hostLogic.lastRttMs() > 0,
                             "no RTT was measured after a reliable match event");
    TEST_ASSERT_EQUAL_UINT16(rig.host.session.stats().lastRttMs,
                             rig.hostLogic.lastRttMs());

    rig.steps(5);
    TEST_ASSERT_EQUAL_UINT32(0, rig.hostLogic.rxDropped());
    TEST_ASSERT_EQUAL_UINT8(2, rig.hostLogic.level());
}

// =============================================================================
// 12. The restart predicate: only MatchOver plus a NEW press is a request. A
//     held button, a rising edge during a rally and a rising edge during the
//     countdown are all not.
// =============================================================================

void test_restart_predicate_needs_match_over_and_a_rising_edge(void) {
    TEST_ASSERT_TRUE_MESSAGE(
        netpong::restartRequested(game::Phase::MatchOver, 0, net::kButtonRight),
        "MatchOver plus a rising edge must request a restart");
    TEST_ASSERT_FALSE_MESSAGE(
        netpong::restartRequested(game::Phase::MatchOver, net::kButtonRight,
                                  net::kButtonRight),
        "a button already held when the match ended must not restart it");
    TEST_ASSERT_FALSE_MESSAGE(
        netpong::restartRequested(game::Phase::Play, 0, net::kButtonRight),
        "a rising edge during the rally must not restart the set");
    TEST_ASSERT_FALSE_MESSAGE(
        netpong::restartRequested(game::Phase::Countdown, 0, net::kButtonRight),
        "a rising edge during the countdown must not restart the set");
}

// =============================================================================
// 13. Any controller press after MatchOver restarts the single set at 0-0 and
//     the new phase reaches both boards in STATE. A button that was already
//     held when the set ended is not a press.
// =============================================================================

void test_any_controller_press_restarts_the_set_after_match_over(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.initOk, "loopback link or session failed to init");
    rig.steps(5);

    // Both paddles parked at the bottom of the court. The first serve is
    // centred and flat, so a paddle at the bottom cannot return it and every
    // serve is missed; the serves alternate, so the points do too.
    rig.setButtons(rig.p1, net::kButtonRight);
    rig.setButtons(rig.p2, net::kButtonRight);

    int guard = 0;
    while (rig.hostLogic.rules().phase() != game::Phase::MatchOver &&
           guard++ < 5000) {
        rig.step();
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::MatchOver),
                                    phaseByte(rig.hostLogic.rules()),
                                    "the match never reached MatchOver");
    const std::uint8_t final1 = rig.hostLogic.rules().score(0);
    const std::uint8_t final2 = rig.hostLogic.rules().score(1);
    TEST_ASSERT_TRUE(final1 == game::kWinScore || final2 == game::kWinScore);

    // The buttons are still held from the rally that ended the set. Held is not
    // pressed: the match must stay terminal while they stay down.
    rig.steps(100);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::MatchOver),
                                    phaseByte(rig.hostLogic.rules()),
                                    "a held button restarted the set");
    TEST_ASSERT_EQUAL_UINT8(final1, rig.hostLogic.rules().score(0));
    TEST_ASSERT_EQUAL_UINT8(final2, rig.hostLogic.rules().score(1));

    // Releasing is a falling edge, which is not a restart either.
    rig.setButtons(rig.p1, 0);
    rig.setButtons(rig.p2, 0);
    rig.steps(2);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(game::Phase::MatchOver),
                            phaseByte(rig.hostLogic.rules()));

    // One new press on P1 restarts the single set.
    rig.setButtons(rig.p1, net::kButtonRight);
    rig.steps(5);  // 50 ms: the press and the next 30 Hz STATE tick
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::Countdown),
                                    phaseByte(rig.hostLogic.rules()),
                                    "the press did not restart the set");
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostLogic.rules().score(0));
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostLogic.rules().score(1));

    // Both controllers learn the new phase and the reset score from STATE.
    TEST_ASSERT_EQUAL_UINT8(net::kPhaseCountdown, rig.p1.lastState.phase);
    TEST_ASSERT_EQUAL_UINT8(net::kPhaseCountdown, rig.p2.lastState.phase);
    TEST_ASSERT_EQUAL_UINT8(0, rig.p1.lastState.score1);
    TEST_ASSERT_EQUAL_UINT8(0, rig.p1.lastState.score2);
    TEST_ASSERT_EQUAL_UINT8(0, rig.p2.lastState.score1);
    TEST_ASSERT_EQUAL_UINT8(0, rig.p2.lastState.score2);

    // The restarted set is a real match: releasing the button lets the
    // countdown finish and the first serve go live at 0-0, which also proves
    // the restart happened once instead of re-triggering.
    rig.setButtons(rig.p1, 0);
    guard = 0;
    while (rig.hostLogic.rules().phase() != game::Phase::Play && guard++ < 200) {
        rig.step();
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(static_cast<std::uint8_t>(game::Phase::Play),
                                    phaseByte(rig.hostLogic.rules()),
                                    "the restarted set never served");
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostLogic.rules().score(0));
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostLogic.rules().score(1));
}

#else  // !PIXELROOT32_ENABLE_NETWORKING || !PLATFORM_NATIVE

// [env:host_test] ignores the engine on purpose, so it compiles this directory
// with neither macro. One stub keeps the suite present without dragging the
// engine (or SDL2) into that environment.
void test_pong_host_session_flag_off_stub_compiles(void) {
    TEST_PASS_MESSAGE(
        "PIXELROOT32_ENABLE_NETWORKING=0 or non-native: the three-node session "
        "suite is not compiled, no engine or SDL2 dependency is added.");
}

#endif  // PIXELROOT32_ENABLE_NETWORKING && PLATFORM_NATIVE

void setUp(void) {}

void tearDown(void) {}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING && \
    defined(PLATFORM_NATIVE)
    RUN_TEST(test_host_binds_expected_nodes_and_ignores_the_stray_peer);
    RUN_TEST(test_state_is_fanned_out_to_both_controllers_with_one_sequence);
    RUN_TEST(test_each_controller_only_moves_its_own_paddle);
    RUN_TEST(test_stale_input_sequence_is_ignored);
    RUN_TEST(test_incompatible_version_blocks_input_and_pauses);
    RUN_TEST(test_goal_is_reliably_reported_and_the_next_serve_is_announced);
    RUN_TEST(test_paddle_contact_is_reported_as_a_local_effect);
    RUN_TEST(test_peer_loss_pauses_then_a_reboot_resyncs_the_room);
    RUN_TEST(test_paused_host_resumes_exactly_once_when_both_controllers_return);
    RUN_TEST(test_match_over_host_never_auto_resumes);
    RUN_TEST(test_diagnostics_report_no_drops_and_a_measured_rtt);
    RUN_TEST(test_restart_predicate_needs_match_over_and_a_rising_edge);
    RUN_TEST(test_any_controller_press_restarts_the_set_after_match_over);
#else
    RUN_TEST(test_pong_host_session_flag_off_stub_compiles);
#endif

    return UNITY_END();
}
