/*
 * Networking Pong - native SDL2 target.
 *
 * The simulator runs the host session plus the two controller sessions over one
 * LoopbackBus, so the protocol, the cadence and the drawing can be exercised
 * without three boards on the desk. The scene owns the bus and registers the
 * primary session; it pumps the simulated controllers itself, because one
 * Engine dispatches exactly one session.
 */
#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <core/Engine.h>
#include <drivers/native/SDL2_AudioBackend.h>
#include <drivers/native/SDL2_Drawer.h>
#include <platforms/EngineConfig.h>

#include "PongHostScene.h"

namespace pr32 = pixelroot32;

pr32::drivers::native::SDL2_AudioBackend audioBackend(22050, 1024);

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::NONE,
    DISPLAY_ROTATION,
    PHYSICAL_DISPLAY_WIDTH,
    PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH,
    LOGICAL_HEIGHT,
    X_OFF_SET,
    Y_OFF_SET
);

// Four keys drive the two simulated controllers, in the order
// PongHostScene::update() reads them: W/S drive P1 (up/down), UP/DOWN drive P2
// (up/down). The order is the contract between this header and the scene.
pr32::input::InputConfig inputConfig(SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_UP,
                                     SDL_SCANCODE_DOWN);

static_assert(pr32::input::InputConfig::MAX_INPUT_COUNT >= 4,
              "native.h: the two simulated controllers need four keys");

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

netpong::PongHostScene pongHostScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();

    // Scene::init() starts the host transport and session and registers the
    // primary session with the Engine. Controller nodes attach to the same bus.
    engine.setScene(&pongHostScene);

    engine.run();

    return 0;
}

#endif  // PLATFORM_NATIVE
