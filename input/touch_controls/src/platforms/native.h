#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <core/Engine.h>
#include <drivers/native/SDL2_Drawer.h>
#include <platforms/EngineConfig.h>

#include "TouchControlsScene.h"

namespace pr32 = pixelroot32;

// Engine::init() hands its own TouchEventDispatcher to the SDL2 drawer via
// SDL2_Drawer::setTouchDispatcher(), and processEvents() then turns every mouse
// button and motion event into dispatcher.processTouch(0, pressed, x, y, ticks).
// So on native the mouse IS the finger: it enters the pipeline at the same call
// the XPT2046 adapter enters it on hardware, and no TouchManager is needed here.
//
// That wiring lives behind PIXELROOT32_ENABLE_TOUCH. Build with the flag at 0
// and the drawer falls back to InputManager::processSDLEvent(), whose dispatcher
// nothing in the Engine ever drains -- the scene simply stops being called.
pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::NONE,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

// The demo binds no buttons; touch is the only input. The keyboard mapping is
// kept only because it costs nothing and makes the window behave like the other
// native demos when a key is pressed by accident.
pr32::input::InputConfig inputConfig(
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN);   // 6 buttons: Up, Down, Left, Right, A, B

pr32::core::Engine engine(config, inputConfig);

touch_controls::TouchControlsScene touchControlsScene;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
    engine.setScene(&touchControlsScene);
    engine.run();

    return 0;
}

#endif  // PLATFORM_NATIVE
