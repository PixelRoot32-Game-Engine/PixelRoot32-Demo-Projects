#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <platforms/EngineConfig.h>

#include "ColourScene.h"
#include "TransitionEngine.h"

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

pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN); // 6 buttons: Up, Down, Left, Right, Space(A), Enter (B)

// A plain Engine plus a wipe-direction setter; see TransitionEngine.h for why.
scene_transitions::TransitionEngine engine(config, inputConfig);

scene_transitions::ColourScene sceneA("SCENE A", pr32::graphics::Color::Navy);
scene_transitions::ColourScene sceneB("SCENE B", pr32::graphics::Color::DarkRed);

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    sceneA.setOther(sceneB);
    sceneB.setOther(sceneA);

    engine.init();
    engine.setScene(&sceneA);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
