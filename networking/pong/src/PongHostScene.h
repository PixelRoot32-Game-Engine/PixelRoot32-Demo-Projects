/*
 * Networking Pong - host scene scaffold (CYD and native).
 *
 * The host is the authority: it owns the match, the ball, the paddles and the
 * score, and it is the only node that fans state out. This file currently holds
 * only the composition root plus a placeholder frame; the court, HUD and phase
 * machine land in a later task.
 */
#pragma once

#include <core/Scene.h>
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
 * @brief Composition root of the host node: link, session and the drawn match.
 *
 * The scene owns the transport and the session as direct members in that
 * declaration order, so the node reserves its whole footprint at construction
 * and never allocates. The Engine only borrows the session; bringing the link
 * up, starting the session and registering the pointer all happen here.
 *
 * On native the scene also owns the LoopbackBus every simulated node shares, so
 * the host is the single place the bus lifetime is decided.
 */
class PongHostScene : public pixelroot32::core::Scene {
public:
    /**
     * @brief Builds the host node.
     *
     * On native the transport is born attached to the scene's own bus; on the
     * CYD the ESP-NOW transport starts idle and claims the radio in @ref init.
     */
    PongHostScene();

    /**
     * @brief Starts the link and the session, then registers the session.
     *
     * Idempotent: re-running it re-applies the same configuration instead of
     * stacking a second link. On native the primary session is registered with
     * the Engine here, because one Engine pumps exactly one session and the
     * native build has three sessions alive at once.
     */
    void init() override;

    /**
     * @brief Advances the host clock.
     * @param deltaTime Milliseconds since the previous frame; unused until the
     *        fixed-step rules clock of the next task lands.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the placeholder frame: black court, title and link status.
     * @param renderer Renderer to draw with.
     *
     * The title and the diagnostics are the two things worth seeing before the
     * court exists: one proves the panel and the rotation are right, the other
     * proves the three boards found each other.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

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
    /**
     * @brief Builds the one-line network status drawn under the title.
     * @param out Destination buffer, at least @ref kStatusCapacity bytes.
     */
    void statusText(char* out) const;

    /// @brief Capacity of the status line, terminator included.
    static constexpr int kStatusCapacity = 48;

#ifdef PLATFORM_NATIVE
    /// Bus shared by every simulated node; declared first so it outlives them.
    pixelroot32::networking::LoopbackBus loopbackBus;
#endif
    HostTransport transport;                        ///< Link; never shared.
    pixelroot32::networking::NetSession netSession; ///< Session on that link.
    pixelroot32::networking::NetResult start =
        pixelroot32::networking::NetResult::NotInitialized; ///< Last start result.
};

}  // namespace netpong
