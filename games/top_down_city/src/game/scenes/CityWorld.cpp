#include "game/scenes/CityWorld.h"

#include <core/Engine.h>
#include <graphics/Renderer.h>

#include "generated/tilemaps/city_scene.h"
#include "generated/tilemaps/corner_shop.h"
#include "generated/tilemaps/police_station.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

CityWorld& CityWorld::instance() {
    static CityWorld world;
    return world;
}

CityWorld::CityWorld()
    : player(),
      wanted(wanted::clear()),
      mission(mission::begin(0, 0)),
      contract(contract::clear()),
      purse(economy::clear()),
      missionRng(0xD0DEA71Fu),
      weapons(),
      returnFire(),
      dayNight(),
      hideBurn(hideout::clear()),
      streetX(0),
      streetY(0),
      streetFacing(Facing::Down),
      bustedMs(0),
      pendingRespawn(false),
      zone(scene::kZoneDowntown),
      actionLatched(false),
      fireLatched(false),
      cooling(false),
      engine_(nullptr),
      city_(nullptr),
      interiors_(),
      pending_(nullptr),
      booted_(false) {
    for (std::uint8_t i = 0; i < kNumDoorways; ++i) {
        interiors_[i] = nullptr;
    }
}

void CityWorld::bind(pr32::core::Engine* engine,
                     pr32::core::Scene* city,
                     BaseCityScene* const* interiors) {
    engine_ = engine;
    city_   = city;
    for (std::uint8_t i = 0; i < kNumDoorways; ++i) {
        interiors_[i] = interiors[i];
    }
}

BaseCityScene* CityWorld::interiorScene(std::uint8_t index) const {
    return index < kNumDoorways ? interiors_[index] : nullptr;
}

bool CityWorld::bootOnce() {
    if (booted_) {
        return false;
    }
    booted_ = true;

    // Every exported scene is bound up front rather than on the way through
    // a door: the palette slots are global state and a scene swap is not the
    // place to be re-binding them. The rooms all bind the same slot to the
    // same sixteen colours, so the order among them does not matter.
    scene::init();
    station::init();
    shop_room::init();

    // The sprite palette bank, which the background slots do not touch. Each
    // pedestrian tint takes a slot of its own, so a crowd of four colours
    // costs four palettes rather than four sprite sheets.
    //
    // Nothing is bound to slot 0: the HUD is drawn with primitives, and a
    // primitive reads the engine's BACKGROUND palette. See hud:: in
    // CityConstants.h.
    gfx::initSpritePaletteSlots();
    gfx::setSpriteCustomPaletteSlot(kPlayerPaletteSlot, PLAYER_PALETTE_RGB565);
    gfx::setSpriteCustomPaletteSlot(kVehiclePaletteSlot, VEHICLE_PALETTE_RGB565);
    // kPedestrianPaletteCount, not kPedestrianTintCount: the last one is the
    // police uniform, which the crowd never rolls and the station always uses.
    for (std::uint8_t tint = 0; tint < kPedestrianPaletteCount; ++tint) {
        gfx::setSpriteCustomPaletteSlot(
            static_cast<std::uint8_t>(kPedestrianPaletteSlot + tint),
            PEDESTRIAN_PALETTES[tint]);
    }

    // Last of the palette work, and it has to be: init() adopts whatever is
    // bound right now as the city's daylight and replaces it with a RAM copy
    // it can tint. Anything registered after this line stays at noon.
    dayNight.init();
    return true;
}

bool CityWorld::commit() {
    if (pending_ == nullptr || engine_ == nullptr) {
        return false;
    }
    pr32::core::Scene* next = pending_;
    pending_ = nullptr;
    // setScene, not triggerTransition: the doorway was instant when it was a
    // tilemap swap, and SceneManager does not tick a scene while a transition
    // is in flight.
    engine_->setScene(next);
    return true;
}

}  // namespace top_down_city
