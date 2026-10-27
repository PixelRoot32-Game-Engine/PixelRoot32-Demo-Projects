/*
 * Controller firmware suite for PongController.
 *
 * The controller is the mirror image of the host adapter: it owns no match
 * state, sends one held-direction mask and paints the board it is told about.
 * This suite pins the four contracts that make that mirror safe over a lossy
 * link, all of them over the REAL engine session and one LoopbackBus:
 *
 *  - the handshake works in both directions, so a board that boots after the
 *    host (or a host that boots after the board) still ends up compatible;
 *  - INPUT leaves on a change and again on the 250 ms keepalive, never once
 *    per frame, because the host's watchdog reads silence as a lost peer;
 *  - a STATE older than the last accepted one is dropped, including across the
 *    16-bit sequence wrap;
 *  - the local paddle predicts at the rules speed between STATE frames, snaps
 *    when the host disagrees by more than six pixels and interpolates when it
 *    agrees more closely, so the OLED marker never lags a whole frame behind.
 *
 * Deliberately, no test fakes the session: every frame in this file is a
 * complete protocol frame encoded by NetSession on one board and decoded by
 * NetSession on the other. The injected 10 ms bus clock keeps the cadence,
 * the ACK timing and the 1500 ms host watchdog deterministic.
 *
 * [env:host_test] compiles the same directory with the engine ignored and
 * PIXELROOT32_ENABLE_NETWORKING undefined, so it runs the stub at the bottom.
 */
#include <unity.h>

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING && \
    defined(PLATFORM_NATIVE)

#include <drivers/native/LoopbackTransport.h>
#include <networking/NetSession.h>
#include <networking/NetTransport.h>
#include <networking/NetTypes.h>

#include <cstddef>
#include <cstdint>

#include "NetConfig.h"
#include "game/PongRules.h"
#include "net/PongController.h"
#include "net/PongProtocol.h"

namespace {

using netpong::ControllerView;
using netpong::PongController;

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

/// Node labels duplicated on purpose: a silent rename in NetConfig.h must fail
/// a test instead of quietly changing who is player one.
constexpr std::uint8_t kHostNode = 1;
constexpr std::uint8_t kP1Node = 2;

/// Boot generations chosen so the fixture is reproducible and the two boards
/// can never be mistaken for one another.
constexpr std::uint16_t kHostBoot = 0x1001;
constexpr std::uint16_t kP1Boot = 0x2001;

/// Every byte differs per tag, so two loopback transports never share a MAC.
NetAddress makeAddress(std::uint8_t tag) {
    NetAddress address{};
    for (std::uint8_t i = 0; i < pixelroot32::networking::kNetAddressSize; ++i) {
        address.bytes[i] = static_cast<std::uint8_t>(0x50u + tag + i);
    }
    return address;
}

/// One board: address, link and session, started exactly like the firmware
/// starts them (transport first, then the session on the shared channel).
struct Node {
    Node(LoopbackBus& bus, std::uint8_t tag, std::uint8_t node, std::uint16_t boot)
        : address(makeAddress(tag)),
          transport(bus, address),
          session(transport),
          nodeId(node),
          bootId(boot) {}

    bool init() {
        NetTransportConfig transportConfig;
        transportConfig.channel = netpong::kChannel;
        transportConfig.nodeId = nodeId;
        transportConfig.manageWifi = false;
        if (transport.init(transportConfig) != NetResult::Ok) {
            return false;
        }

        NetSessionConfig sessionConfig;
        sessionConfig.channel = netpong::kChannel;
        sessionConfig.nodeId = nodeId;
        sessionConfig.bootId = bootId;
        sessionConfig.discovery = true;
        return session.init(sessionConfig) == NetResult::Ok;
    }

    NetAddress address;
    LoopbackTransport transport;
    NetSession session;
    std::uint8_t nodeId;
    std::uint16_t bootId;
};

/// What the fake host received from the controller, decoded from its own
/// session events. The host never runs PongHost in this suite: the controller
/// contract only cares that the frames left the board.
struct HostObserver {
    std::uint8_t controllerPeer = 0xFF;  ///< Slot the controller joined on.
    bool controllerKnown = false;        ///< True after its PeerJoined.
    int appHelloCount = 0;               ///< APP_HELLO frames received.
    net::AppHello lastHello{};           ///< Payload of the latest APP_HELLO.
    int inputCount = 0;                  ///< INPUT frames received.
    net::Input lastInput{};              ///< Payload of the latest INPUT.

    void reset() {
        appHelloCount = 0;
        lastHello = net::AppHello{};
        inputCount = 0;
        lastInput = net::Input{};
    }
};

/// The two-board room plus the controller under test.
struct Rig {
    Rig()
        : host(bus, kHostNode, kHostNode, kHostBoot),
          controllerNode(bus, kP1Node, kP1Node, kP1Boot),
          pong(controllerNode.session, 0, 0) {
        ok = host.init() && controllerNode.init();
    }

    /// One pump of both sessions on the injected clock. The two-session order
    /// (host first) means a frame the controller queues in the same step is
    /// still drained by the host inside that step, which is what the engine
    /// does on a real board.
    void pumpOnce() {
        bus.setNowMs(nowMs);
        host.session.update(nowMs);
        controllerNode.session.update(nowMs);
        drainController();
        drainHost();
    }

    /// Two pumps per step: discovery and reliable ACKs both need the peer to
    /// answer inside the same injected millisecond.
    void flush() {
        pumpOnce();
        pumpOnce();
    }

    /// Starts the controller at the current injected time and settles the wire.
    void startController() {
        pong.start(nowMs);
        flush();
        assertNoDrops();
    }

    /// Advances the injected clock, runs one controller frame and settles.
    void advance(std::uint32_t ms, std::uint8_t buttons) {
        nowMs += ms;
        pong.update(nowMs, buttons);
        flush();
        assertNoDrops();
    }

    /// One ordinary 10 ms controller frame.
    void stepController(std::uint8_t buttons) { advance(kStepMs, buttons); }

    /// Sends one application frame from the fake host and settles.
    void sendFromHost(const net::Message& message, NetDelivery delivery) {
        TEST_ASSERT_TRUE_MESSAGE(hostObserver.controllerKnown,
                                 "the controller never joined the fake host");
        std::uint8_t frame[net::kStateSize];
        const std::size_t length =
            net::encode(message, frame, sizeof(frame));
        TEST_ASSERT_NOT_EQUAL(0u, length);
        const NetResult result = host.session.send(
            hostObserver.controllerPeer, delivery, frame,
            static_cast<std::uint16_t>(length));
        TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(NetResult::Ok),
                                static_cast<std::uint8_t>(result));
        flush();
        assertNoDrops();
    }

    /// Sends the host's side of the handshake.
    void sendHostAppHello(std::uint8_t version) {
        net::Message message;
        message.kind = net::MessageKind::AppHello;
        message.appHello.version = version;
        message.appHello.role = net::kRoleHost;
        sendFromHost(message, NetDelivery::Reliable);
    }

    /// Sends one STATE frame with the board fields the tests care about.
    void sendState(std::uint16_t seq, std::uint8_t phase, std::uint8_t score1,
                   std::uint8_t score2, std::int16_t p1Y, std::int16_t p2Y,
                   std::int16_t ballX, std::int16_t ballY) {
        net::Message message;
        message.kind = net::MessageKind::State;
        message.state.seq = seq;
        message.state.phase = phase;
        message.state.score1 = score1;
        message.state.score2 = score2;
        message.state.p1Y = p1Y;
        message.state.p2Y = p2Y;
        message.state.ballX = ballX;
        message.state.ballY = ballY;
        sendFromHost(message, NetDelivery::Unreliable);
    }

    void drainController() {
        NetEvent event{};
        while (controllerNode.session.pollEvent(event)) {
            pong.onEvent(event, nowMs);
        }
    }

    void drainHost() {
        NetEvent event{};
        while (host.session.pollEvent(event)) {
            switch (event.type) {
                case NetEventType::PeerJoined:
                    if (event.nodeId == kP1Node) {
                        hostObserver.controllerPeer = event.peerId;
                        hostObserver.controllerKnown = true;
                    }
                    break;
                case NetEventType::Message: {
                    if (event.nodeId != kP1Node) {
                        break;
                    }
                    net::Message message;
                    if (!net::decode(event.payload, event.length, message)) {
                        break;
                    }
                    if (message.kind == net::MessageKind::AppHello) {
                        ++hostObserver.appHelloCount;
                        hostObserver.lastHello = message.appHello;
                    } else if (message.kind == net::MessageKind::Input) {
                        ++hostObserver.inputCount;
                        hostObserver.lastInput = message.input;
                    }
                    break;
                }
                case NetEventType::PeerLeft:
                case NetEventType::SendFailed:
                    break;
            }
        }
    }

    /// Drop-newest is the engine's fallback, not this demo's contract: a
    /// cadence bug would surface here rather than as a silent gap.
    void assertNoDrops() const {
        TEST_ASSERT_EQUAL_UINT32(0, host.session.stats().rxDropped);
        TEST_ASSERT_EQUAL_UINT32(0, controllerNode.session.stats().rxDropped);
    }

    bool ok = false;
    LoopbackBus bus;
    Node host;
    Node controllerNode;
    PongController pong;
    HostObserver hostObserver;
    std::uint32_t nowMs = 0;
};

/// Lets the fixture settle discovery without yet starting the controller.
const std::uint8_t kNoButtons = 0;

}  // namespace

// =============================================================================
// 1. The controller announces itself on start and answers a host-initiated
//    APP_HELLO, which is the reply a rebooted board is missing today.
// =============================================================================

void test_controller_sends_app_hello_on_start_and_replies_to_the_host(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    TEST_ASSERT_TRUE(rig.hostObserver.controllerKnown);

    // Start is the scene's init path: it must announce this board again even
    // though discovery already happened before the scene existed.
    rig.hostObserver.reset();
    rig.startController();
    TEST_ASSERT_EQUAL_INT(1, rig.hostObserver.appHelloCount);
    TEST_ASSERT_EQUAL_UINT8(net::kProtocolVersion,
                            rig.hostObserver.lastHello.version);
    TEST_ASSERT_EQUAL_UINT8(net::kRoleController,
                            rig.hostObserver.lastHello.role);
    TEST_ASSERT_FALSE(rig.pong.view().versionOk);

    // The host may also initiate the handshake (its own PeerJoined path). The
    // controller must answer it instead of waiting for the 1 s host HELLO.
    rig.hostObserver.reset();
    rig.sendHostAppHello(net::kProtocolVersion);
    TEST_ASSERT_EQUAL_INT(1, rig.hostObserver.appHelloCount);
    TEST_ASSERT_EQUAL_UINT8(net::kRoleController,
                            rig.hostObserver.lastHello.role);
    TEST_ASSERT_TRUE_MESSAGE(rig.pong.view().versionOk,
                             "the host APP_HELLO did not set versionOk");
}

// =============================================================================
// 2. INPUT leaves on a change and on the 250 ms keepalive, with a monotonic
//    sequence, and never once per controller frame.
// =============================================================================

void test_input_is_sent_on_change_and_on_the_keepalive_only(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    rig.startController();
    rig.hostObserver.reset();

    // Two unchanged frames: nothing to report yet.
    rig.stepController(kNoButtons);
    rig.stepController(kNoButtons);
    TEST_ASSERT_EQUAL_INT(0, rig.hostObserver.inputCount);

    rig.stepController(net::kButtonRight);
    TEST_ASSERT_EQUAL_INT(1, rig.hostObserver.inputCount);
    TEST_ASSERT_EQUAL_UINT16(1, rig.hostObserver.lastInput.seq);
    TEST_ASSERT_EQUAL_UINT8(net::kButtonRight, rig.hostObserver.lastInput.buttons);
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostObserver.lastInput.heldMs);

    // 240 ms of an unchanged mask: the controller must stay quiet.
    for (int i = 0; i < 24; ++i) {
        rig.stepController(net::kButtonRight);
    }
    TEST_ASSERT_EQUAL_INT(1, rig.hostObserver.inputCount);

    // The 250 ms keepalive proves the paddle is still held.
    rig.stepController(net::kButtonRight);
    TEST_ASSERT_EQUAL_INT(2, rig.hostObserver.inputCount);
    TEST_ASSERT_EQUAL_UINT16(2, rig.hostObserver.lastInput.seq);
    TEST_ASSERT_EQUAL_UINT8(net::kButtonRight, rig.hostObserver.lastInput.buttons);
    TEST_ASSERT_EQUAL_UINT8(250, rig.hostObserver.lastInput.heldMs);

    // Releasing the button is a change too, and it carries the new hold time.
    rig.stepController(kNoButtons);
    TEST_ASSERT_EQUAL_INT(3, rig.hostObserver.inputCount);
    TEST_ASSERT_EQUAL_UINT8(0, rig.hostObserver.lastInput.buttons);
    TEST_ASSERT_EQUAL_UINT16(3, rig.hostObserver.lastInput.seq);
    TEST_ASSERT_EQUAL_UINT16(3, rig.pong.lastSentSeq());
}

// =============================================================================
// 3. A stale STATE is dropped; a newer one is accepted; the wrap past 0xFFFF
//    is accepted rather than frozen.
// =============================================================================

void test_state_sequence_is_guarded_and_wraps(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    rig.startController();

    rig.sendState(10, net::kPhasePlay, 1, 0, 100, 140, 50, 60);
    TEST_ASSERT_EQUAL_UINT16(10, rig.pong.view().stateSeq);
    TEST_ASSERT_EQUAL_UINT8(net::kPhasePlay, rig.pong.view().phase);
    TEST_ASSERT_EQUAL_UINT8(1, rig.pong.view().score1);
    TEST_ASSERT_EQUAL_INT16(100, rig.pong.view().myPaddleY);
    TEST_ASSERT_EQUAL_INT16(140, rig.pong.view().otherPaddleY);
    TEST_ASSERT_EQUAL_INT16(50, rig.pong.view().ballX);
    TEST_ASSERT_EQUAL_INT16(60, rig.pong.view().ballY);

    // An overtaken frame must not move the paddle back in time.
    rig.sendState(9, net::kPhasePoint, 5, 5, 200, 200, 300, 40);
    TEST_ASSERT_EQUAL_UINT16(10, rig.pong.view().stateSeq);
    TEST_ASSERT_EQUAL_UINT8(net::kPhasePlay, rig.pong.view().phase);
    TEST_ASSERT_EQUAL_UINT8(1, rig.pong.view().score1);
    TEST_ASSERT_EQUAL_INT16(100, rig.pong.view().myPaddleY);

    // Walk the sequence window up to the wrap. Only 0x7FFF ahead of the
    // reference is "newer", so 0xFFFF is reached in legal steps rather than by
    // jumping half the space at once.
    rig.sendState(0x7FFE, net::kPhasePlay, 2, 1, 110, 130, 160, 100);
    TEST_ASSERT_EQUAL_UINT16(0x7FFE, rig.pong.view().stateSeq);
    rig.sendState(0xFF00, net::kPhasePlay, 2, 1, 110, 130, 160, 100);
    TEST_ASSERT_EQUAL_UINT16(0xFF00, rig.pong.view().stateSeq);

    rig.sendState(0xFFFF, net::kPhasePlay, 2, 1, 110, 130, 160, 100);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, rig.pong.view().stateSeq);
    TEST_ASSERT_EQUAL_UINT8(2, rig.pong.view().score1);

    // One short of the newest frame is still stale.
    rig.sendState(0xFFFE, net::kPhasePoint, 5, 5, 200, 200, 300, 40);
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, rig.pong.view().stateSeq);

    // The wrap itself: 0x0000 is one frame newer than 0xFFFF, not older.
    rig.sendState(0x0000, net::kPhasePlay, 3, 1, 112, 130, 161, 101);
    TEST_ASSERT_EQUAL_UINT16(0x0000, rig.pong.view().stateSeq);
    TEST_ASSERT_EQUAL_UINT8(3, rig.pong.view().score1);
}

// =============================================================================
// 4. Prediction follows the rules speed, clamps at both ends, snaps when the
//    host is more than six pixels away and interpolates when it is closer.
// =============================================================================

void test_prediction_follows_the_rules_and_reconciles_with_the_host(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    rig.startController();

    // The rules park the paddle at the centre of the play area: 132 px.
    TEST_ASSERT_EQUAL_INT16(132, rig.pong.view().myPaddleY);

    // 100 ms at 190 px/s is 19 whole pixels; the Q8.8 accumulator keeps that
    // exact, which is the point of predicting in fixed point at all.
    rig.advance(100, net::kButtonRight);
    TEST_ASSERT_EQUAL_INT16(151, rig.pong.view().myPaddleY);

    // Keep pushing down until the play area stops the paddle.
    rig.advance(100, net::kButtonRight);
    rig.advance(100, net::kButtonRight);
    rig.advance(100, net::kButtonRight);
    rig.advance(100, net::kButtonRight);
    TEST_ASSERT_EQUAL_INT16(218, rig.pong.view().myPaddleY);
    rig.advance(100, net::kButtonRight);
    TEST_ASSERT_EQUAL_INT16(218, rig.pong.view().myPaddleY);

    // And all the way back up to the HUD edge.
    for (int i = 0; i < 12; ++i) {
        rig.advance(100, net::kButtonLeft);
    }
    TEST_ASSERT_EQUAL_INT16(46, rig.pong.view().myPaddleY);
    rig.advance(100, net::kButtonLeft);
    TEST_ASSERT_EQUAL_INT16(46, rig.pong.view().myPaddleY);

    // A host that disagrees by more than six pixels wins outright.
    rig.sendState(1, net::kPhasePlay, 0, 0, 60, 132, 160, 132);
    TEST_ASSERT_EQUAL_INT16(60, rig.pong.view().myPaddleY);

    // Within six pixels the local estimate keeps 70 % of its lead, so the
    // marker slides towards the host instead of jumping.
    rig.sendState(2, net::kPhasePlay, 0, 0, 64, 132, 160, 132);
    TEST_ASSERT_EQUAL_INT16(61, rig.pong.view().myPaddleY);

    // A full snap still wins after an interpolated step.
    rig.sendState(3, net::kPhasePlay, 0, 0, 80, 132, 160, 132);
    TEST_ASSERT_EQUAL_INT16(80, rig.pong.view().myPaddleY);
}

// =============================================================================
// 5. POINT, PAUSE, ROUND_START and MATCH_END carry the phase and the score on
//    their own, so one lost STATE does not leave a stale board.
// =============================================================================

void test_discrete_events_update_the_view_without_a_state(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    rig.startController();

    net::Message point;
    point.kind = net::MessageKind::Point;
    point.point.seq = 1;
    point.point.scorer = 1;
    point.point.score1 = 1;
    point.point.score2 = 0;
    rig.sendFromHost(point, NetDelivery::Reliable);
    TEST_ASSERT_EQUAL_UINT8(net::kPhasePoint, rig.pong.view().phase);
    TEST_ASSERT_EQUAL_UINT8(1, rig.pong.view().score1);
    TEST_ASSERT_EQUAL_UINT8(0, rig.pong.view().score2);
    TEST_ASSERT_EQUAL_UINT16(0, rig.pong.view().stateSeq);

    net::Message pause;
    pause.kind = net::MessageKind::Pause;
    pause.pause.paused = 1;
    rig.sendFromHost(pause, NetDelivery::Reliable);
    TEST_ASSERT_EQUAL_UINT8(net::kPhasePaused, rig.pong.view().phase);
    TEST_ASSERT_EQUAL_UINT8(1, rig.pong.view().score1);

    net::Message round;
    round.kind = net::MessageKind::RoundStart;
    round.roundStart.seq = 2;
    round.roundStart.serveDir = 1;
    rig.sendFromHost(round, NetDelivery::Reliable);
    TEST_ASSERT_EQUAL_UINT8(net::kPhasePlay, rig.pong.view().phase);

    net::Message end;
    end.kind = net::MessageKind::MatchEnd;
    end.matchEnd.winner = 2;
    end.matchEnd.score1 = 4;
    end.matchEnd.score2 = 5;
    rig.sendFromHost(end, NetDelivery::Reliable);
    TEST_ASSERT_EQUAL_UINT8(net::kPhaseMatchOver, rig.pong.view().phase);
    TEST_ASSERT_EQUAL_UINT8(4, rig.pong.view().score1);
    TEST_ASSERT_EQUAL_UINT8(5, rig.pong.view().score2);
}

// =============================================================================
// 6. hostOnline is true from the first accepted STATE and false 1500 ms after
//    the last one, with the host clock kept for the status line.
// =============================================================================

void test_host_online_watchdog_expires_after_1500_ms(void) {
    Rig rig;
    TEST_ASSERT_TRUE_MESSAGE(rig.ok, "loopback link or session failed to init");
    rig.flush();
    rig.startController();
    TEST_ASSERT_FALSE(rig.pong.view().hostOnline);

    rig.advance(100, kNoButtons);
    rig.sendState(1, net::kPhasePlay, 0, 0, 132, 132, 160, 132);
    TEST_ASSERT_TRUE(rig.pong.view().hostOnline);
    TEST_ASSERT_EQUAL_UINT32(100, rig.pong.view().lastStateMs);

    // 1490 ms of silence: the board is still considered reachable.
    for (int i = 0; i < 149; ++i) {
        rig.stepController(kNoButtons);
    }
    TEST_ASSERT_TRUE(rig.pong.view().hostOnline);

    // Exactly 1500 ms after the last STATE: the host is gone.
    rig.stepController(kNoButtons);
    TEST_ASSERT_FALSE(rig.pong.view().hostOnline);
    TEST_ASSERT_EQUAL_UINT32(100, rig.pong.view().lastStateMs);
}

#else  // !PIXELROOT32_ENABLE_NETWORKING || !PLATFORM_NATIVE

// [env:host_test] deliberately ignores the engine, so it compiles this
// directory with neither macro. One stub keeps the suite present there without
// dragging NetSession, SDL2 or the loopback transport into that environment.
void test_pong_controller_flag_off_stub_compiles(void) {
    TEST_PASS_MESSAGE(
        "PIXELROOT32_ENABLE_NETWORKING=0 or non-native: the controller session "
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
    RUN_TEST(test_controller_sends_app_hello_on_start_and_replies_to_the_host);
    RUN_TEST(test_input_is_sent_on_change_and_on_the_keepalive_only);
    RUN_TEST(test_state_sequence_is_guarded_and_wraps);
    RUN_TEST(test_prediction_follows_the_rules_and_reconciles_with_the_host);
    RUN_TEST(test_discrete_events_update_the_view_without_a_state);
    RUN_TEST(test_host_online_watchdog_expires_after_1500_ms);
#else
    RUN_TEST(test_pong_controller_flag_off_stub_compiles);
#endif

    return UNITY_END();
}
