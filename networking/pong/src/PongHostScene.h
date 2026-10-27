/*
 * Networking Pong - host scene (CYD and native).
 *
 * The host is the authority: it owns the match, the ball, the paddles and the
 * score, and it is the only node that fans state out. This file is the
 * composition root -- link, session, host adapter -- plus the frame that turns
 * the authoritative match into the arcade court.
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Camera2D.h>
#include <graphics/Renderer.h>
#include <platforms/EngineConfig.h>

#include <cstdint>

// The demo has nothing to show without the module: fail the build instead of
// drawing a static screen with no link behind it.
#if !PIXELROOT32_ENABLE_NETWORKING
#error "networking/pong requires PIXELROOT32_ENABLE_NETWORKING=1"
#endif

#include <networking/NetSession.h>
#include <networking/NetTransport.h>
#include <networking/NetTypes.h>

#ifdef PLATFORM_NATIVE
#include <drivers/native/LoopbackTransport.h>
#else
#include <drivers/esp32/ESP32EspNowTransport.h>
#endif

#include "net/PongController.h"
#include "net/PongHost.h"
#include "presentation/PongEffects.h"

namespace netpong {

#ifdef PLATFORM_NATIVE
/// Simulated link used by the native environment.
using HostTransport = pixelroot32::networking::LoopbackTransport;
#else
/// Radio link used by the CYD.
using HostTransport = pixelroot32::networking::ESP32EspNowTransport;
#endif

/**
 * @class PongHostScene
 * @brief Composition root of the host node: link, session, adapter and court.
 *
 * The scene owns the transport, the session and the host adapter as direct
 * members in that declaration order, so the node reserves its whole footprint at
 * construction and never allocates. The Engine only borrows the session;
 * bringing the link up, starting the session and registering the pointer all
 * happen here.
 *
 * The scene never pumps the session it hands to the Engine: the Engine drains it
 * and dispatches every pending event to @ref onNetworkEvent before @ref update
 * runs, and a second pump would advance the link twice per frame. @ref update
 * therefore only moves the match clock forward.
 *
 * On native the scene also owns the LoopbackBus every simulated node shares and
 * the two controller nodes themselves. Because one Engine pumps exactly one
 * session, @ref update pumps those two after the host step; that is a
 * simulation detail, not an engine limitation (three real boards each run their
 * own Engine). The device builds never instantiate any of it: the guard is
 * `PLATFORM_NATIVE`, so the CYD firmware owns one link, one session and no
 * controller node.
 */
class PongHostScene : public pixelroot32::core::Scene {
public:
    /**
     * @brief Builds the host node.
     *
     * On native the transport is born attached to the scene's own bus; on the
     * CYD the ESP-NOW transport starts idle and claims the radio in @ref init.
     * The adapter is built around the session but sends nothing until
     * @ref init calls `start()`.
     */
    PongHostScene();

    /**
     * @brief Starts the link, the session and the match, then registers the session.
     *
     * Idempotent: re-running it re-applies the same configuration instead of
     * stacking a second link, and it restarts the match from WaitingForPlayers.
     * On native the primary session is registered with the Engine here, because
     * one Engine pumps exactly one session and the native build has three
     * sessions alive at once.
     */
    void init() override;

    /**
     * @brief Advances the match and the 30 Hz fan-out cadence.
     * @param deltaTime Milliseconds since the previous frame.
     *
     * The adapter derives its own fixed-step clock from `millis()`, so the frame
     * delta is unused: a slow frame must not change how far the ball travelled.
     * Entities are updated through the base class as everywhere else in the
     * repository; this scene has none today.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the arcade court.
     * @param renderer Renderer to draw with.
     *
     * Order is the repository's: background, then `Scene::draw()` for entities,
     * then the actors and the particles, then the HUD and the overlay. The score
     * band and the overlay are drawn outside the shaken pass on purpose: a jolt
     * that moved the scores would only make them harder to read.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Forwards one Engine-drained event to the host adapter.
     * @param event Event popped from this node's session.
     *
     * The Engine pumps and drains before @ref update, so joins, leaves and
     * controller payloads land here for the frame that just arrived.
     */
    void onNetworkEvent(const pixelroot32::networking::NetEvent& event) override;

    /**
     * @brief Returns the session the Engine should pump.
     * @return Pointer to this node's session; valid for the scene's lifetime.
     */
    pixelroot32::networking::NetSession* session();

    /// @brief Returns the result of the last start attempt.
    pixelroot32::networking::NetResult startResult() const { return start; }

#ifdef PLATFORM_NATIVE
    /**
     * @brief Returns the bus the simulated nodes share.
     * @return Reference to the scene-owned loopback bus.
     *
     * Exposed so the two simulated controllers can be attached to the same
     * bus without the host scene having to know how they are built.
     */
    pixelroot32::networking::LoopbackBus& bus();
#endif

private:
    /// @brief Pops one paddle contact into a burst and a sound.
    /// @param rules Match the contact happened in.
    /// @param player Paddle that was hit.
    void playPaddleHit(const game::Rules& rules, std::uint8_t player);

    /// @brief Pops one wall contact into a burst and a sound.
    /// @param rules Match the contact happened in.
    void playWallBounce(const game::Rules& rules);

    /// @brief Pops one goal into a burst and a sound.
    /// @param rules Match the goal happened in.
    /// @param scorer Side awarded the point.
    void playGoal(const game::Rules& rules, std::uint8_t scorer);

    /// @brief Fires the heavier feedback that closes a match.
    void playMatchPoint();

    /// @brief Reports which side scored from where the ball left the court.
    /// @param rules Match to read.
    /// @return 0 for P1, 1 for P2.
    ///
    /// `Rules::awardPoint()` does not re-centre the ball, so on the goal frame
    /// the ball is still just outside the edge it crossed. That makes the sign
    /// of `ballX()` the truthful scorer rather than a second score counter.
    [[nodiscard]] std::uint8_t goalScorer(const game::Rules& rules) const;

    /// @brief Repaints the HUD band after a shaken court pass.
    /// @param renderer Renderer to paint.
    ///
    /// The court is drawn a second time under the shake offset so the frame and
    /// the centre line move with the actors. That shifted pass can reach a few
    /// pixels under the score band, so the band is repainted with the court's
    /// own paper slot before the HUD text goes down. The HUD itself is never
    /// drawn inside the shaken pass.
    void repaintHudBand(pixelroot32::graphics::Renderer& renderer);

#ifdef PLATFORM_NATIVE
    /**
     * @brief Brings one simulated controller up on the host's own bus.
     * @param transport Link to initialize; already attached to the bus.
     * @param session Session to start on it.
     * @param controller Firmware to reset and announce.
     * @param nodeId Application label (kNodeIdP1 or kNodeIdP2).
     * @param nowMs Current time, the controller's cadence baseline.
     *
     * Mirrors PongControllerScene::init() without the Scene: the simulator has
     * one window and one Engine, so the two extra boards are plain node objects
     * owned here rather than scenes nobody would ever draw.
     */
    void startController(pixelroot32::networking::LoopbackTransport& transport,
                         pixelroot32::networking::NetSession& session,
                         PongController& controller, std::uint8_t nodeId,
                         std::uint32_t nowMs);

    /**
     * @brief Pumps one simulated controller and feeds it the held direction.
     * @param session Session to pump and drain.
     * @param controller Firmware to advance.
     * @param nowMs Current time.
     * @param buttons Held-direction mask from the keyboard.
     *
     * The Engine pumps exactly one session (the host's), so the two simulated
     * boards are pumped here with the same shape the ESP-NOW dice demo uses:
     * update, drain, then the firmware.
     */
    void pumpController(pixelroot32::networking::NetSession& session,
                        PongController& controller, std::uint32_t nowMs,
                        std::uint8_t buttons);

    /// Bus shared by every simulated node; declared first so it outlives them.
    pixelroot32::networking::LoopbackBus loopbackBus;
#endif
    HostTransport transport;                        ///< Link; never shared.
    pixelroot32::networking::NetSession netSession; ///< Session on that link.
    /// Match authority and wire cadence; born after the session it borrows.
    PongHost host;
#ifdef PLATFORM_NATIVE
    // The two simulated controllers. Each owns its link, session and firmware
    // in that order, exactly like a C3 board; they share the host's bus and
    // never touch the Engine's session slot.
    pixelroot32::networking::LoopbackTransport p1Transport;
    pixelroot32::networking::NetSession p1Session;
    PongController p1Controller;
    pixelroot32::networking::LoopbackTransport p2Transport;
    pixelroot32::networking::NetSession p2Session;
    PongController p2Controller;
#endif
    /// Bursts and shake; owns the only particle emitter in the demo.
    presentation::PongEffects effects;
    /// Phase on the previous frame, for the countdown and goal edges.
    game::Phase lastPhase = game::Phase::WaitingForPlayers;
    /// Parked at the origin; only ever moved by the effect offset.
    pixelroot32::graphics::Camera2D shakeCamera;
    pixelroot32::networking::NetResult start =
        pixelroot32::networking::NetResult::NotInitialized; ///< Last start result.
};

}  // namespace netpong
