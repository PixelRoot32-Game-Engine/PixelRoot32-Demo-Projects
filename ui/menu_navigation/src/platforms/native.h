#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "MenuNavigationScene.h"

namespace pr32 = pixelroot32;

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

// 6 buttons, in the order InputConfig assigns indices:
// 0 Up, 1 Down, 2 Left, 3 Right, 4 A (Space), 5 B (Enter).
// Those indices are what the widgets are constructed with, so this order is
// part of the scene's contract — see kButtonA in MenuNavigationScene.h.
pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);

pr32::core::Engine engine(config, inputConfig);

menu_navigation::MenuNavigationScene menuScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&menuScene);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
