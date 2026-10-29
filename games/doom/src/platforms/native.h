// native — the SDL2 simulator. It mirrors the primary board: a 320x200 window,
// 1:1 with DOOM's surface, which is what the rotated ILI9341 panel shows. The
// panel adds two 20 px bars; the simulator does not need to.
#ifndef PLATFORM_NATIVE
#error "native.h included without PLATFORM_NATIVE"
#else

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <drivers/native/SDL2_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "DoomScene.h"

namespace pr32 = pixelroot32;

static_assert(LOGICAL_WIDTH == doom::DoomScene::kLogicalWidth,
              "DOOM renders 320 columns: distscale[], visplane_t and the status bar are sized by SCREENWIDTH");
static_assert(LOGICAL_HEIGHT == doom::DoomScene::kLogicalHeight,
              "DOOM renders 200 rows, status bar included");

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

pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);

pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());

pr32::core::Engine engine(config, inputConfig, audioConfig);

doom::DoomScene doomScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&doomScene);

    engine.run();

    return 0;
}

#endif  // PLATFORM_NATIVE
