#ifdef PLATFORM_ESP32DEV_ILI9341

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <core/Engine.h>
#include <platforms/EngineConfig.h>

#if PIXELROOT32_ENABLE_AUDIO
#include <drivers/esp32/ESP32_I2S_AudioBackend.h>
#include "audio/AudioDirector.h"
#endif

#include "game/scenes/CityScene.h"
#include "game/scenes/CityWorld.h"
#include "game/scenes/CornerShopScene.h"
#include "game/scenes/PoliceStationScene.h"

namespace pr32 = pixelroot32;

// Button mapping (Arduino ESP32), common mapping for a 4-directional pad plus
// two action buttons. A holds a run and works every door; B fires.
const int BTN_PIN_UP = 32;
const int BTN_PIN_DOWN = 33;
const int BTN_PIN_LEFT = 25;
const int BTN_PIN_RIGHT = 26;
const int BTN_PIN_A = 27;
const int BTN_PIN_B = 13;
const int BTN_PIN_START = 34;
const int BTN_PIN_SELECT = 35;

// ESP32-2432S028 "Cheap Yellow Display": ILI9341 240x320 with an XPT2046
// resistive panel on its own GPIO bus (see the esp32cyd flags in platformio.ini).

pr32::graphics::DisplayConfig config(
    pr32::graphics::DisplayType::ILI9341_2,
    DISPLAY_ROTATION, PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT,
    LOGICAL_WIDTH, LOGICAL_HEIGHT, X_OFF_SET, Y_OFF_SET);

// 6 buttons: Up, Down, Left, Right, A (run / contextual), B (fire).
pr32::input::InputConfig inputConfig(BTN_PIN_UP, BTN_PIN_DOWN, BTN_PIN_LEFT,
                                     BTN_PIN_RIGHT, BTN_PIN_A, BTN_PIN_B, 
                                     BTN_PIN_START, BTN_PIN_SELECT);


#if PIXELROOT32_ENABLE_AUDIO
// I2S output to a MAX98357A-class DAC. GPIO 17 (BCLK), GPIO 21 (LRCK)
// and GPIO 19 (DOUT) are assigned to the I2S audio interface. Verify that
// these pins do not conflict with the TFT/SPI configuration or the six
// physical button pins. GPIO 25 and 26 may serve as internal DAC outputs
// on the original ESP32, but that is irrelevant to this configuration:
// audio is transmitted digitally over I2S, without using
// ESP32_DAC_AudioBackend.
const int I2S_BCLK_PIN = 5;
const int I2S_LRCK_PIN = 21;
const int I2S_DOUT_PIN = 19;

pr32::drivers::esp32::ESP32_I2S_AudioBackend audioBackend(
    I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, 22050);
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

void setup() {
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
}

void loop() {
    engine.run();
}

#endif  // PLATFORM_ESP32DEV_ILI9341
