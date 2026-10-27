/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include "NetConfig.h"
#include "game/PongRules.h"
#include "net/PongProtocol.h"

// The controller is the one controller-side layer that talks to the engine's
// networking module. PongProtocol and PongRules below it are engine-free, which
// is what lets [env:host_test] keep compiling them with the engine ignored; this
// header therefore asks for the session types conditionally, exactly like
// PongHost.h does.
//
// The guard is written as `defined(...) && value` rather than the bare macro so
// a build that never defines PIXELROOT32_ENABLE_NETWORKING is not a -Wundef
// warning, only an empty branch.
#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING
#include <networking/NetSession.h>
#include <networking/NetTypes.h>
#endif

/**
 * @file PongController.h
 * @brief Controller-side mirror of one paddle, with prediction and a watchdog.
 *
 * The C3 owns no match state. It sends the held direction of one paddle and it
 * paints the board the host tells it about. This class is the whole
 * controller-side contract behind that sentence, and it deliberately has no
 * Arduino, no Engine and no clock dependency: `millis()` is passed in by the
 * scene, which keeps the class replayable in the host test suite.
 *
 * Three things make a controller usable on a lossy 30 Hz stream:
 *
 *  - a handshake in both directions. The board announces itself on start and on
 *    every PeerJoined, and it answers the host's APP_HELLO too. Without that
 *    reply a board that booted before the host can stay unready forever;
 *  - a held-state INPUT cadence (on change, plus a 250 ms keepalive) instead of
 *    one frame per rendered frame, because the host's watchdog reads silence as
 *    a lost player;
 *  - a local Q8.8 paddle that predicts between STATE frames and then reconciles
 *    with the host instead of hard-snapping on every frame, so the OLED marker
 *    moves smoothly while the authoritative paddle stays the host's.
 *
 * The class never allocates, never uses floating point and never reads a clock.
 */
namespace netpong {

/**
 * @struct ControllerView
 * @brief Everything one controller board needs to send and to draw.
 *
 * The struct is the single seam between the protocol layer and the panel: it is
 * filled by PongController and read by ControllerScreen, so the screen never
 * has to know a STATE frame exists. Positions are whole pixels measured from the
 * court origin; paddles are CENTRE-Y, matching `net::State`.
 */
struct ControllerView {
    std::uint8_t playerIndex = 0;  ///< 0 = P1 (left), 1 = P2 (right).
    std::uint8_t phase = net::kPhaseWaitingForPlayers;  ///< One kPhase* value.
    std::uint8_t score1 = 0;  ///< Points scored by player one.
    std::uint8_t score2 = 0;  ///< Points scored by player two.
    std::int16_t myPaddleY = 0;     ///< Local paddle centre Y, whole pixels.
    std::int16_t otherPaddleY = 0;  ///< Remote paddle centre Y, whole pixels.
    std::int16_t ballX = 0;  ///< Ball centre X, whole pixels.
    std::int16_t ballY = 0;  ///< Ball centre Y, whole pixels.
    bool hostOnline = false;  ///< True until 1500 ms pass without a STATE.
    bool versionOk = false;   ///< The host's APP_HELLO was compatible.
    std::uint16_t stateSeq = 0;    ///< Sequence of the newest accepted STATE.
    std::uint32_t lastStateMs = 0; ///< Arrival time of that STATE, in ms.
};

#if defined(PIXELROOT32_ENABLE_NETWORKING) && PIXELROOT32_ENABLE_NETWORKING

/**
 * @class PongController
 * @brief Sends one held direction and mirrors the authoritative board.
 *
 * One instance lives for the whole controller scene. It borrows the session
 * (never copies: two sessions on one link would fight over the peer table) and
 * keeps every other field inline, so it is safe to run inside the frame loop.
 *
 * The caller owns the drain loop: the Engine pumps the session and dispatches
 * events before the scene update, so `onEvent()` is fed there and `update()` is
 * called once per frame. This class never calls `session.update()` itself.
 */
class PongController {
public:
    /// Silence after which the board stops calling the host online.
    ///
    /// The host sends STATE at 30 Hz; four seconds of missing frames would be
    /// 120 in a row, but 1500 ms already means something is wrong and the
    /// controller should say so instead of painting a frozen score as live.
    static constexpr std::uint32_t kHostTimeoutMs = 1500;

    /**
     * @brief Builds the controller around an already-constructed session.
     * @param session Session this board sends on and is fed events from.
     * @param playerIndex 0 for P1/left, 1 for P2/right.
     * @param nowMs Current time, used as the first cadence baseline.
     *
     * Construction only stores references and resets local state; it never
     * touches the link, so the object may exist before `session.init()`.
     */
    PongController(pixelroot32::networking::NetSession& session,
                   std::uint8_t playerIndex, std::uint32_t nowMs);

    PongController(const PongController&) = delete;
    PongController& operator=(const PongController&) = delete;
    PongController(PongController&&) = delete;
    PongController& operator=(PongController&&) = delete;

    /**
     * @brief Resets the view and announces this board to the host.
     * @param nowMs Current time, used as the new cadence baseline.
     *
     * The scene calls this once from `init()`, after the session started. It
     * also looks for an already-known host in the peer table, so a scene that
     * is created after discovery (the native simulator) still sends its
     * APP_HELLO instead of waiting for a PeerJoined that will never repeat.
     */
    void start(std::uint32_t nowMs);

    /**
     * @brief Advances prediction, the STATE watchdog and the INPUT cadence.
     * @param nowMs Current time in milliseconds, from `millis()`.
     * @param buttons Held-direction mask: `net::kButtonLeft` / `kButtonRight`.
     *
     * Prediction integrates the local paddle at `game::kPaddleSpeed` between
     * STATE frames, clamped to the play area. INPUT is sent when the mask
     * changes and again every `kInputKeepaliveMs`, never once per frame.
     */
    void update(std::uint32_t nowMs, std::uint8_t buttons);

    /**
     * @brief Handles one session event for this board.
     * @param event Event drained from this board's NetSession.
     * @param nowMs Current time, stamped on an accepted STATE.
     *
     * PeerJoined triggers the APP_HELLO send; messages are decoded and routed
     * to the handshake, the STATE merge or the discrete-event view updates.
     */
    void onEvent(const pixelroot32::networking::NetEvent& event,
                 std::uint32_t nowMs);

    /**
     * @brief Returns the current panel/protocol view.
     * @return Reference to the live view; valid for the object's lifetime.
     */
    [[nodiscard]] const ControllerView& view() const;

    /**
     * @brief Returns the sequence carried by the newest INPUT actually sent.
     * @return That sequence, or 0 before the first successful send.
     */
    [[nodiscard]] std::uint16_t lastSentSeq() const;

private:
    /// Longest prediction delta replayed in one call.
    ///
    /// Mirrors the host's frame ceiling: a longer stall is already a dropped
    /// frame, and integrating it would slide the paddle far past the host and
    /// force a snap on the next STATE.
    static constexpr std::uint32_t kMaxPredictionMs = 100;

    /// First INPUT sequence. Zero is the session's "no sequence" sentinel for
    /// unreliable traffic, so the application counter starts at one.
    static constexpr std::uint16_t kFirstInputSeq = 1;

    /// Paddle-centre limits of the play area, in Q8.8.
    static constexpr game::Fixed kPaddleCentreMin =
        (game::kHudHeight + game::kPaddleHeight / 2) * game::kOne;
    static constexpr game::Fixed kPaddleCentreMax =
        (game::kCourtHeight - game::kPaddleHeight / 2) * game::kOne;

    /// Centre both paddles start at, matching `game::Rules::centreActors()`.
    static constexpr game::Fixed kPaddleCentreMid =
        (kPaddleCentreMin + kPaddleCentreMax) / 2;

    /// Distance beyond which the host's paddle replaces the prediction.
    ///
    /// Six pixels is two 60 Hz host steps plus one frame of network jitter. A
    /// smaller threshold would snap on ordinary RTT noise; a larger one would
    /// leave the OLED visibly behind the court.
    static constexpr game::Fixed kCentreSnapThreshold = 6 * game::kOne;

    /// @brief Sends this board's APP_HELLO reliably, when a host is known.
    void sendAppHello();

    /// @brief Sends one INPUT frame and advances the sequence on success.
    /// @param buttons Mask to send.
    /// @param nowMs Current time, used for heldMs and the keepalive baseline.
    void sendInput(std::uint8_t buttons, std::uint32_t nowMs);

    /// @brief Merges one accepted STATE and reconciles the local paddle.
    /// @param state Snapshot that already passed the sequence guard.
    /// @param nowMs Arrival time, copied into the view as the host clock.
    void applyState(const net::State& state, std::uint32_t nowMs);

    /// @brief Routes one decoded application frame from the host.
    void handleMessage(const pixelroot32::networking::NetEvent& event,
                       std::uint32_t nowMs);

    /// @brief Finds the host in the session peer table, if it is there yet.
    void resolveHost();

    /// @brief Clamps the local Q8.8 paddle to the play area.
    void clampLocalPaddle();

    pixelroot32::networking::NetSession& session_;  ///< Borrowed link session.
    std::uint8_t playerIndex_ = 0;   ///< 0 = P1/left, 1 = P2/right.
    ControllerView view_{};          ///< Latest board picture.
    game::Fixed localPaddleY_ = 0;   ///< Predicted paddle centre, Q8.8.
    std::uint8_t hostPeerId_ = 0xFF; ///< Session slot of the host, 0xFF unknown.
    bool hostKnown_ = false;         ///< True once the host slot is known.
    std::uint16_t nextInputSeq_ = kFirstInputSeq;  ///< Next sequence to assign.
    std::uint16_t lastSentSeq_ = 0;  ///< Last sequence that left the board.
    std::uint8_t observedButtons_ = 0;  ///< Mask of the current hold period.
    std::uint8_t sentButtons_ = 0;      ///< Mask of the last sent INPUT.
    std::uint32_t heldSinceMs_ = 0;  ///< When the current mask was first held.
    std::uint32_t lastInputSentMs_ = 0;  ///< Keepalive baseline.
    std::uint32_t lastUpdateMs_ = 0;     ///< Prediction delta baseline.
};

#endif  // PIXELROOT32_ENABLE_NETWORKING

}  // namespace netpong
