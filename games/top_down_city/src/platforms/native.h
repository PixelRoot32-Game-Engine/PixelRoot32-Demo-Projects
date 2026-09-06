#ifdef PLATFORM_NATIVE

#include <SDL2/SDL.h>

#include <drivers/native/SDL2_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#if PIXELROOT32_ENABLE_AUDIO
#include <drivers/native/SDL2_AudioBackend.h>
#include "audio/AudioDirector.h"
#endif

#include "game/scenes/CityScene.h"
#include "game/scenes/CityWorld.h"
#include "game/scenes/CornerShopScene.h"
#include "game/scenes/PoliceStationScene.h"

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

// 6 buttons: Up, Down, Left, Right, Space (run / contextual), S (fire).
pr32::input::InputConfig inputConfig(SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
                                     SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
                                     SDL_SCANCODE_SPACE, SDL_SCANCODE_S);

#if PIXELROOT32_ENABLE_AUDIO
// 22050 Hz / 1024-sample buffer -- the same pair bomberbot uses, so no new
// MSYS2 flag and no new dependency were needed to add sound here.
pr32::drivers::native::SDL2_AudioBackend audioBackend(22050, 1024);
pr32::audio::AudioConfig audioConfig(&audioBackend, audioBackend.getSampleRate());
pr32::core::Engine engine(config, inputConfig, audioConfig);
#else
pr32::core::Engine engine(config, inputConfig);
#endif

// One scene per space, one run. The scenes are swapped through CityWorld,
// which holds everything a doorway must not reset -- the player, the wanted
// level, the delivery clock, both weapon systems, the day/night cycle and the
// hideout burn. Adding an interior is one more scene object here and one more
// row in the table below, not a change to any of them.
top_down_city::CityScene          cityScene;
top_down_city::PoliceStationScene policeStationScene;
top_down_city::CornerShopScene    cornerShopScene;

// In the order top_down_city::kDoorways names them, which is what the door
// table's `interior` index means. Get this order wrong and both doors still
// work -- they open onto each other's rooms.
//
// BaseCityScene* and not core::Scene*, which is the only guard against that:
// chapter 2 reaches the duty staff from OUTDOORS, so something holds a pointer
// to a room it is not. Typed as a scene it would need a static_cast over a
// build with RTTI off, making a swapped row undefined behaviour rather than a
// wrong room; typed as a room it is a virtual question with a null answer --
// see BaseCityScene::stationStaff.
top_down_city::BaseCityScene* const interiorScenes[] = {
    &policeStationScene,
    &cornerShopScene,
};
static_assert(sizeof(interiorScenes) / sizeof(interiorScenes[0])
                  == top_down_city::kNumDoorways,
              "every doorway needs a room bound behind it");

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    engine.init();
#if PIXELROOT32_ENABLE_AUDIO
    // Between init() and setScene(): setScene() calls CityScene::init(),
    // which is where the ambient track would start once Slice 2 lands.
    // A director bound after that point would miss the very first cue.
    top_down_city::AudioDirector::instance().bind(&engine.getAudioEngine(),
                                                   &engine.getMusicPlayer());
#endif
    // Before the first setScene(), and for the same reason the director is:
    // CityScene::init() asks the world whether this is a first entry, and a
    // world that does not know its scenes yet cannot route the door out of it.
    top_down_city::CityWorld::instance().bind(&engine, &cityScene,
                                              interiorScenes);

    engine.setScene(&cityScene);

    engine.run();

    return 0;
}

#endif // PLATFORM_NATIVE
