/*
 * Networking Pong - controller scene scaffold (ESP32-C3, native simulator).
 *
 * A controller owns no game state. It sends the held direction of one paddle
 * and paints the scoreboard it is told about; this file currently holds only
 * the composition root plus a placeholder panel. Input cadence, prediction and
 * the gated redraw land in a later task.
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
 * @brief Composition root of one controller node: link, session and panel.
 *
 * The transport and the session are direct members in that declaration order,
 * so the node reserves its whole footprint at construction and never allocates.
 * The Engine pumps the registered session; this scene never calls update() on
 * it, because a second pump from the scene would advance the link twice per
 * frame.
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
     * @brief Starts the link and the session.
     *
     * Idempotent: re-running it re-applies the same configuration instead of
     * stacking a second link. The Engine pumps the session afterwards; the
     * platform header registers the pointer, because this scene never touches
     * the Engine.
     */
    void init() override;

    /**
     * @brief Advances the controller clock.
     * @param deltaTime Milliseconds since the previous frame; unused until the
     *        input cadence of the next task lands.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the placeholder scoreboard.
     * @param renderer Renderer to draw with; the panel is 1-bit, so every
     *        primitive here is White on an empty buffer.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

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
     * @brief Draws the player label: "P1" or "P2".
     * @return Pointer to a static string, never null.
     */
    const char* playerLabel() const;

    ControllerTransport transport;                  ///< Link; never shared.
    pixelroot32::networking::NetSession netSession; ///< Session on that link.
    std::uint8_t nodeIdValue = 0;                   ///< Application label.
    pixelroot32::networking::NetResult start =
        pixelroot32::networking::NetResult::NotInitialized; ///< Last start result.
};

}  // namespace netpong
