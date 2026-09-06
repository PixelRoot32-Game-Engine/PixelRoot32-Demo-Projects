#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <drivers/native/SDL2_AudioBackend.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#include "MusicSequencerScene.h"

namespace pr32 = pixelroot32;

// SDL2 audio backend: 22050 Hz to match AudioConfig below, 1024-sample buffer.
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

pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN); // 6 buttons: Up, Down, Left, Right, Space(A), Enter (B)

pr32::audio::AudioConfig audioConfig(&audioBackend, 22050);

pr32::core::Engine engine(config, inputConfig, audioConfig);

// Declared after `engine`, and in the same translation unit, so the scene is
// constructed second. MusicSequencerScene::init() still defers its MusicPlayer
// to a function-local static rather than relying on that order.
music_sequencer::MusicSequencerScene sequencerScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&sequencerScene);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
