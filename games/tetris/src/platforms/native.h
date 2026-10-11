#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <drivers/native/SDL2_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "ui/TetrisScene.h"

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

// 6 buttons: Up (hard drop), Down (soft drop), Left, Right, A (rotate CW),
// B (rotate CCW). Pause is the A+B chord.
pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_X, SDL_SCANCODE_Z);

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

tetris::TetrisScene tetrisScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&tetrisScene);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
