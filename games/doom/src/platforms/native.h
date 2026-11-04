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

// The engine's `InputConfig` order IS DOOM's button contract: DoomScene samples
// index 0 up ... index 5 B into the world's button byte and paints its slots in
// the same order (see doomc/InputMap.h), then samples two keys by index alone --
// index 6 Start and index 7 Select -- and packs neither into that byte.
//
//   0 up        1 down      2 left      3 right
//   4 A         fire, BT_ATTACK.
//   5 B         use (alone) and the run modifier (with up).
//   6 Start     Escape: the key a DOOM player presses to open the menu. The
//               scene's kStartIndex is 6, so this entry is what makes the menu
//               reachable on the desktop.
//
// Select (index 7, the automap's toggle on the board) stays unbound: the
// keyboard has no second menu key in this layout and `isButtonDown(7)` on an
// index past the configured count returns false, so the automap simply cannot
// be opened from the simulator.
pr32::input::InputConfig inputConfig(
    SDL_SCANCODE_UP,      // 0 up
    SDL_SCANCODE_DOWN,    // 1 down
    SDL_SCANCODE_LEFT,    // 2 left
    SDL_SCANCODE_RIGHT,   // 3 right
    SDL_SCANCODE_SPACE,   // 4 A: fire
    SDL_SCANCODE_RETURN,  // 5 B: use / run
    SDL_SCANCODE_ESCAPE   // 6 Start: the menu
);

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
