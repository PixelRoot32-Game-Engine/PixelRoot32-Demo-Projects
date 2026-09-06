#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <core/Engine.h>
#include <drivers/native/SDL2_AudioBackend.h>
#include <drivers/native/SDL2_Drawer.h>
#include <platforms/EngineConfig.h>

#include "ChessScene.h"

namespace pr32 = pixelroot32;

// The SDL2 drawer forwards mouse events into the touch dispatcher, so with
// PIXELROOT32_ENABLE_TOUCH the mouse behaves exactly like the CYD panel and no
// TouchManager is needed here. The keyboard InputConfig is only kept because
// the Engine constructor expects one; the demo binds no buttons.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::NONE,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

pr32::input::InputConfig inputConfig(
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);   // 6 buttons: Up, Down, Left, Right, A, B

pr32::drivers::native::SDL2_AudioBackend audioBackend(22050, 1024);
pr32::audio::AudioConfig audioConfig(&audioBackend, 22050);

pr32::core::Engine engine(config, inputConfig, audioConfig);

chessdemo::ChessScene chessScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&chessScene);
    engine.run();

    return 0;
}

#endif  // PLATFORM_NATIVE
