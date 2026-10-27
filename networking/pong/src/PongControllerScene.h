/*
 * Networking Pong - controller scene (ESP32-C3, native simulator).
 *
 * A controller owns no game state. It sends the held direction of one paddle
 * and paints the scoreboard it is told about. This scene is the composition
 * root for that role: the link, the session and the controller contract, plus
 * the gated redraw that keeps the I2C panel off the critical path.
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Renderer.h>
#include <platforms/EngineConfig.h>

#include <cstdint>

// The demo has nothing to show without the module: fail the build instead of
// drawing a scoreboard with no link behind it.
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
#include "presentation/ControllerScreen.h"

namespace netpong {

#ifdef PLATFORM_NATIVE
/// Simulated link used by the native environment.
using ControllerTransport = pixelroot32::networking::LoopbackTransport;
#else
/// Radio link used by the two ESP32-C3 boards.
using ControllerTransport = pixelroot32::networking::ESP32EspNowTransport;
#endif

/**
 * @class PongControllerScene
 * @brief Composition root of one controller node: link, session, firmware and panel.
 *
 * The transport, the session and the controller are direct members in that
 * declaration order, so the node reserves its whole footprint at construction
 * and never allocates. The Engine pumps the registered session; this scene never
 * calls update() on it, because a second pump from the scene would advance the
 * link twice per frame.
 *
 * The panel is repainted only when its visible key changes AND at most
 * `kControllerRedrawHz` times a second. A full 128x64 U8G2 frame blocks on I2C
 * for roughly 23 ms, so an unconditional redraw would spend about a quarter of
 * every frame on the wire.
 */
class PongControllerScene : public pixelroot32::core::Scene {
public:
#ifdef PLATFORM_NATIVE
    /**
     * @brief Builds a simulated controller on a shared loopback bus.
     * @param loopbackBus Bus that carries this node's frames; must outlive it.
     * @param address Local address the transport advertises.
     * @param nodeId Application label, kNodeIdP1 or kNodeIdP2.
     */
    PongControllerScene(pixelroot32::networking::LoopbackBus& loopbackBus,
                        const pixelroot32::networking::NetAddress& address,
                        std::uint8_t nodeId);
#else
    /**
     * @brief Builds the controller this build represents.
     * @param nodeId Application label from ESP_NOW_NODE_ID.
     */
    explicit PongControllerScene(std::uint8_t nodeId);
#endif

    /**
     * @brief Starts the link, the session and the controller firmware.
     *
     * Idempotent: re-running it re-applies the same configuration instead of
     * stacking a second link. The Engine pumps the session afterwards; the
     * platform header registers the pointer, because this scene never touches
     * the Engine's session slot.
     */
    void init() override;

    /**
     * @brief Runs one controller frame: prediction and the INPUT cadence.
     * @param deltaTime Milliseconds since the previous frame; unused because
     *        the controller contract takes `millis()` and derives its own delta.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Paints the scoreboard.
     * @param renderer Renderer to draw with; the panel is 1-bit, so every
     *        primitive here is White on an empty buffer.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Skips the frame when the panel would not change.
     * @return true when a visible field changed and the 10 Hz throttle allows a
     *         repaint; false otherwise.
     *
     * A key change inside the throttle window is remembered, not dropped: the
     * next window compares against the last painted key, so the panel catches up
     * as soon as the bus is free instead of waiting for another change.
     */
    bool shouldRedrawFramebuffer() const override;

    /**
     * @brief Forwards one Engine-drained event to the controller.
     * @param event Event popped from this node's session.
     *
     * The Engine pumps and drains before `update()`, so this is where the
     * handshake and the STATE merge happen for the frame that just arrived.
     */
    void onNetworkEvent(
        const pixelroot32::networking::NetEvent& event) override;

    /**
     * @brief Returns the session the Engine should pump.
     * @return Pointer to this node's session; valid for the scene's lifetime.
     */
    pixelroot32::networking::NetSession* session();

    /// @brief Returns the result of the last start attempt.
    pixelroot32::networking::NetResult startResult() const { return start; }

    /// @brief Returns the application label this controller was built with.
    std::uint8_t nodeId() const { return nodeIdValue; }

private:
    /**
     * @brief Maps a node label to the paddle it drives.
     * @return 1 for kNodeIdP2, 0 otherwise.
     *
     * The label is the only source for the player slot: deriving it twice from
     * two different values would let the drawn tag and the predicted paddle
     * disagree.
     */
    std::uint8_t playerIndex() const;

    ControllerTransport transport;                  ///< Link; never shared.
    pixelroot32::networking::NetSession netSession; ///< Session on that link.
    /// Application label first: the controller derives its player slot from it,
    /// and a member initializer may only read fields declared before it.
    std::uint8_t nodeIdValue = 0;
    PongController pongController;                  ///< Firmware of this board.
    pixelroot32::networking::NetResult start =
        pixelroot32::networking::NetResult::NotInitialized; ///< Last start result.

    /// Key of the last frame actually painted. Mutable because
    /// shouldRedrawFramebuffer() is const but has to remember its decision.
    mutable ScreenKey lastPaintedKey{};
    mutable bool hasPaintedKey = false;  ///< false forces the first draw.
    mutable std::uint32_t lastPaintMs = 0;  ///< Throttle baseline.
};

}  // namespace netpong
