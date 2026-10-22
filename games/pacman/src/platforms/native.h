#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "PacmanScene.h"

namespace pr32 = pixelroot32;

// The Engine lives in the platform file, never in the scene: the scene reaches
// it through the extern declaration below.
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

// Six buttons in the engine's fixed order: Up, Down, Left, Right, A, B.
// The arrow keys drive the maze; Space pauses and Enter restarts.
pr32::input::InputConfig inputConfig(
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);

pr32::core::Engine engine(config, inputConfig);

pacman::PacmanScene pacmanScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&pacmanScene);
    engine.run();

    return 0;
}

#endif  // PLATFORM_NATIVE
