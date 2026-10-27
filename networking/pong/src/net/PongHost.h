/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "NetConfig.h"
#include "game/PongRules.h"

// The host adapter is the only layer of the demo that talks to the engine's
// networking module. PongProtocol and PongRules below it are engine-free, which
// is what lets [env:host_test] keep compiling them with the engine ignored; this
// header must therefore ask for the session types conditionally instead of
// dragging the engine into every test target.
//
// The guard is written as `defined(...) && value` rather than the bare macro so
// a build that never defines PIXELROOT32_ENABLE_NETWORKING (host_test) is not a
// -Wundef warning, only an empty branch.
#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING
#include <networking/NetSession.h>
#include <networking/NetTypes.h>
#endif

/**
 * @file PongHost.h
 * @brief Host-side authority that binds two controllers to the match.
 *
 * The CYD runs the rules core, but the rules core knows nothing about peers,
 * discovery, reliable delivery or the 30 Hz cadence: those are three-node
 * concerns. This adapter is the seam. It turns session events into player input,
 * feeds a fixed 60 Hz clock into `game::Rules`, and fans the resulting snapshot
 * and discrete events back out to the two controllers with the delivery
 * contract each one needs (STATE unreliable, everything discontinuous reliable).
 *
 * The class owns no transport and never pumps the session. The Engine pumps
 * `NetSession` before the scene update on firmware, and the integration test
 * pumps three sessions explicitly on its injected clock; calling update() here
 * would mean two pumps per frame and would make the receive drain
 * non-deterministic.
 */
namespace netpong {

/**
 * @struct Binding
 * @brief One player slot's view of the controller currently attached to it.
 *
 * The host deliberately keeps the session slot (`peerId`) next to the
 * application label (`nodeId`): the slot is how frames are addressed, while the
 * label is how the room decides who is player one. They survive a controller
 * reboot together, because the session keeps a slot for the same MAC and this
 * adapter keeps the player assigned to that slot.
 */
struct Binding {
    std::uint8_t peerId = 0xFF;   ///< NetSession slot, 0xFF while unbound.
    std::uint8_t nodeId = 0;      ///< Application label from the peer's HELLO.
    bool bound = false;           ///< nodeId is one of kNodeIdP1 / kNodeIdP2.
    bool versionOk = false;       ///< Peer sent a compatible AppHello.
    bool online = false;          ///< Peer is currently reachable.
    std::uint32_t lastInputMs = 0;///< Session time of the last accepted INPUT.
    std::uint8_t buttons = 0;     ///< Last accepted held-direction mask.
    /// Held-direction mask carried by the frame before `buttons`.
    ///
    /// The restart gesture is an edge, not a level: the host compares this with
    /// the newest mask so a button that was already down when the match ended
    /// (and is therefore still held) cannot restart the set.
    std::uint8_t previousButtons = 0;
};

/**
 * @brief Reports whether an INPUT is the "play another set" gesture.
 * @param phase Match phase the host is in before this frame is applied.
 * @param previousButtons Held mask from the previous accepted INPUT.
 * @param buttons Held mask from this INPUT.
 * @return true only in MatchOver and only when a bit that was up went down.
 *
 * Pure on purpose: the adapter calls it on the accepted-input path, and the
 * suite pins the four interesting combinations without a session or a clock.
 * A press of either direction on either controller is a request; a held button
 * (no new bit) and any rising edge outside MatchOver are not.
 */
[[nodiscard]] constexpr bool restartRequested(game::Phase phase,
                                             std::uint8_t previousButtons,
                                             std::uint8_t buttons) {
    return phase == game::Phase::MatchOver &&
           (buttons & static_cast<std::uint8_t>(~previousButtons)) != 0;
}

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING

/**
 * @class PongHost
 * @brief Binds two controllers to the rules core and fans state back out.
 *
 * One instance lives for the whole host session. It holds a reference to the
 * session (never a copy: two sessions on one link would fight over the peer
 * table), the rules core by value, and fixed-size tables for everything else.
 * No method allocates, so the adapter is safe to run inside the 60 Hz loop.
 */
class PongHost {
public:
    /// Players the host can bind. Player 0 is the left paddle, 1 the right one.
    static constexpr std::uint8_t kPlayerCount = 2;

    /// Capacity of the local effect ring.
    ///
    /// Paddle and wall contacts are presentation, not authority: the scene
    /// polls them once per frame for particles and sound. A burst larger than
    /// this ring is already stale by the time it is drawn, so the newest
    /// effects are dropped instead of overwriting the beginning of the story.
    static constexpr std::uint8_t kMaxEffects = 8;

    /**
     * @brief Builds the adapter around an already-constructed session.
     * @param session Session the host will send on and be fed events from.
     * @param nowMs Current session time, used as the first cadence baseline.
     *
     * The session reference must outlive this object. Construction only resets
     * local state; it never touches the link, so a caller can build the object
     * before the session is initialized.
     */
    PongHost(pixelroot32::networking::NetSession& session, std::uint32_t nowMs);

    PongHost(const PongHost&) = delete;
    PongHost& operator=(const PongHost&) = delete;
    PongHost(PongHost&&) = delete;
    PongHost& operator=(PongHost&&) = delete;

    /**
     * @brief Returns to a fresh match and clears every binding.
     * @param nowMs Current session time, used as the new cadence baseline.
     *
     * Called once at scene init, before discovery has produced any peer. It
     * sends nothing: there is nobody to send to yet, and the first PeerJoined
     * event is what triggers the APP_HELLO handshake.
     */
    void start(std::uint32_t nowMs);

    /**
     * @brief Advances the match and the wire cadence.
     * @param nowMs Current session time in milliseconds.
     *
     * The rules run on a fixed 60 Hz clock driven by an accumulator, never on
     * the frame clock, so a slow frame cannot change how far the ball travelled.
     * At most kMaxCatchUpSteps steps are replayed per call and the rest of a
     * long stall is dropped: replaying it would make the next frame late too,
     * and the positions are stale by then anyway.
     *
     * A match the watchdog or a lost peer paused is resumed here, as soon as
     * both controllers are bound, compatible and sending again. A match that
     * reached MatchOver is left alone here: only a new controller press (the
     * Input path in onEvent) can restart it.
     */
    void update(std::uint32_t nowMs);

    /**
     * @brief Handles one session event for this host.
     * @param event Event drained from the host's NetSession.
     * @param nowMs Current session time in milliseconds.
     *
     * The caller owns the drain loop (the Engine does it before the scene
     * update). This method reacts to joins, leaves and application payloads; it
     * never calls session.update() itself.
     */
    void onEvent(const pixelroot32::networking::NetEvent& event,
                 std::uint32_t nowMs);

    /**
     * @brief Returns the authoritative rules core.
     * @return Reference to the match simulation.
     *
     * The scene reads positions for drawing and the restart input; the adapter
     * keeps the only write path (input and pause) so the two never disagree.
     */
    [[nodiscard]] game::Rules& rules();

    /**
     * @brief Returns one player's binding.
     * @param player 0 (P1/left) or 1 (P2/right).
     * @return The binding, or a static unbound binding for another index.
     */
    [[nodiscard]] const Binding& binding(std::uint8_t player) const;

    /**
     * @brief Reports whether both controllers are bound, online and compatible.
     * @return true when the match may leave WaitingForPlayers.
     */
    [[nodiscard]] bool bothPlayersReady() const;

    /**
     * @brief Pops one local-only effect for the scene.
     * @param out Destination, written only when an effect is available.
     * @return true when an effect was popped; false when the ring is empty.
     */
    bool pollEffect(game::Event& out);

    /**
     * @brief Returns the sequence carried by the newest STATE frame.
     * @return Last assigned state sequence, 0 before the first send.
     */
    [[nodiscard]] std::uint16_t lastStateSeq() const;

    /**
     * @brief Returns how many controllers the HUD can count as ready.
     * @return 0 while waiting, 1 with one usable controller, 2 when ready.
     */
    [[nodiscard]] std::uint8_t level() const;

    /**
     * @brief Returns the Wi-Fi channel every node of the demo shares.
     * @return The `kChannel` constant the host configures its link with.
     *
     * The diagnostics panel prints it because "same board, different channel"
     * is the failure a player cannot see on either screen.
     */
    [[nodiscard]] std::uint8_t channel() const;

    /**
     * @brief Returns the newest reliable round-trip time.
     * @return Milliseconds of the last ACKed reliable frame, 0 before the first.
     *
     * The host only sends reliable frames on joins and discrete match events, so
     * a value of 0 while both controllers are bound means the handshake has not
     * been acknowledged yet.
     */
    [[nodiscard]] std::uint16_t lastRttMs() const;

    /**
     * @brief Returns how many received frames the session had to drop.
     * @return Monotonic drop counter from the session.
     *
     * The demo's contract is zero drops: the cadence is sized so the receive
     * queue never overflows, and a resync is sent at once instead of waiting for
     * the 30 Hz tick. A non-zero value is a bug, not normal traffic.
     */
    [[nodiscard]] std::uint32_t rxDropped() const;

    /**
     * @brief Returns how many reliable frames are still waiting for an ACK.
     * @return Occupied pending entries in the session.
     *
     * Above zero for a frame or two after a join or a goal; a value that stays
     * at the queue capacity means the link is gone and events are queueing.
     */
    [[nodiscard]] std::uint8_t pendingCount() const;

private:
    /// Session slot value that means "this peer is not a player".
    static constexpr std::uint8_t kUnboundPlayer = 0xFF;

    /// STATE period in microseconds (30 Hz). Integer microseconds keep the
    /// cadence exact enough that a 10 ms test clock still averages 30 Hz.
    static constexpr std::uint32_t kStatePeriodUs = 1000000u / kStateHz;

    /// Frame delta ceiling. A longer stall is already a dropped-frame event;
    /// replaying more than this would only buy a longer catch-up loop.
    static constexpr std::uint32_t kMaxFrameMs = 100;

    /// Silence after which a bound online peer is considered gone. Matches the
    /// controller keepalive contract: 250 ms intervals with four misses.
    static constexpr std::uint32_t kWatchdogMs = 1000;

    /// @brief Maps a peer's application label to a player slot.
    /// @param nodeId Label advertised in HELLO.
    /// @return 0 for P1, 1 for P2, or -1 for a label the host must ignore.
    static int playerForNode(std::uint8_t nodeId);

    /// @brief Handles a PeerJoined event (first join or a rebooted peer).
    void handleJoined(const pixelroot32::networking::NetEvent& event,
                      std::uint32_t nowMs);

    /// @brief Handles a PeerLeft event: freeze the match for the survivor.
    void handleLeft(const pixelroot32::networking::NetEvent& event);

    /// @brief Decodes and routes one application payload from a bound peer.
    void handleMessage(const pixelroot32::networking::NetEvent& event,
                       std::uint32_t nowMs);

    /// @brief Starts the countdown as soon as both controllers are usable.
    void refreshReadyGate();

    /// @brief Pauses the match when a bound peer stopped sending INPUT.
    void maintainWatchdog(std::uint32_t nowMs);

    /// @brief Resumes a paused match once every controller is usable again.
    void resumeWhenReconnected(std::uint32_t nowMs);

    /// @brief Reports whether both players are bound, compatible and sending.
    /// @param nowMs Current session time.
    /// @return true when a resume would be stable instead of being re-paused by
    ///         the watchdog on the next frame.
    [[nodiscard]] bool playersResponsive(std::uint32_t nowMs) const;

    /// @brief Drains the rules event ring and forwards every event.
    ///
    /// Called once per fixed step (and once more after the watchdog), so a
    /// reliable event can never wait for the next frame.
    void drainRuleEvents();

    /// @brief Sends APP_HELLO to one peer, reliably.
    void sendAppHello(std::uint8_t peerId);

    /// @brief Sends the current snapshot to every bound online peer.
    /// @param delivery Delivery contract for this snapshot.
    void broadcastState(pixelroot32::networking::NetDelivery delivery);

    /// @brief Sends one reliable snapshot to a single peer (rejoin resync).
    void sendStateTo(std::uint8_t player,
                     pixelroot32::networking::NetDelivery delivery);

    /// @brief Builds a STATE payload from the rules at @p seq.
    /// @return Wire bytes written, or 0 when the buffer is too small.
    std::size_t buildState(std::uint16_t seq, std::uint8_t* out,
                           std::size_t capacity) const;

    /// @brief Sends one encoded discrete event to every usable peer.
    void sendReliable(const std::uint8_t* frame, std::size_t length);

    /// @brief Encodes and sends POINT for the player who scored.
    void sendPoint(std::uint8_t scorerPlayer);

    /// @brief Encodes and sends ROUND_START for the serving player.
    void sendRoundStart(std::uint8_t serverPlayer);

    /// @brief Encodes and sends PAUSE.
    void sendPause(bool paused);

    /// @brief Encodes and sends MATCH_END.
    void sendMatchEnd(std::uint8_t winnerPlayer);

    /// @brief Appends one local effect, dropping it when the ring is full.
    void pushEffect(const game::Event& effect);

    /// @brief Returns the next state sequence, skipping the wire's 0 sentinel.
    std::uint16_t nextStateSeq();

    /// @brief Returns the next discrete-event sequence.
    std::uint16_t nextEventSeq();

    /// @brief Reports whether a player slot can be addressed right now.
    [[nodiscard]] bool canSend(std::uint8_t player) const;

    pixelroot32::networking::NetSession& session_;  ///< Borrowed link session.
    game::Rules rules_;                             ///< Authoritative match.
    Binding bindings_[kPlayerCount];                ///< One per player slot.
    /// Player each session slot is bound to, kUnboundPlayer when it is not.
    std::uint8_t peerPlayers_[pixelroot32::networking::NetSession::kMaxPeers];
    std::uint16_t lastInputSeq_[kPlayerCount];      ///< Per-player anti-replay.
    std::uint16_t stateSeq_ = 0;                    ///< Last STATE sequence.
    std::uint16_t eventSeq_ = 0;                    ///< Last discrete-event sequence.
    std::uint32_t lastUpdateMs_ = 0;                ///< Previous update() time.
    std::uint32_t accumulatorUs_ = 0;               ///< Fixed-step accumulator.
    std::uint32_t stateElapsedUs_ = 0;              ///< Since the last STATE.
    game::Event effects_[kMaxEffects];              ///< Local-only effect ring.
    std::uint8_t effectHead_ = 0;                   ///< Oldest effect index.
    std::uint8_t effectCount_ = 0;                  ///< Occupied effect slots.
};

#endif  // PIXELROOT32_ENABLE_NETWORKING

}  // namespace netpong
