#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <core/Engine.h>
#include <drivers/native/SDL2_Drawer.h>
#include <platforms/EngineConfig.h>

#include "IsoTilemapExportScene.h"

namespace pr32 = pixelroot32;

// No audio backend: this example draws a tilemap and nothing else, and the
// build turns PIXELROOT32_ENABLE_AUDIO off.
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

pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
                                     SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                                     SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);

pr32::core::Engine engine(config, inputConfig);

iso_tilemap_export::IsoTilemapExportScene exampleScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&exampleScene);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
