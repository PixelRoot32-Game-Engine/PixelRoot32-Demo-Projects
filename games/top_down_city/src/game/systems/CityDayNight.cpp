#include "game/systems/CityDayNight.h"

#include "generated/tilemaps/city_scene.h"

namespace top_down_city {

namespace gfx = pixelroot32::graphics;

CityDayNight::CityDayNight()
    : backgroundSource_(),
      spriteSource_(),
      backgroundTinted_(),
      spriteTinted_(),
      clockMs_(daynight::kStartStep * daynight::kStepMs),
      step_(daynight::kStartStep),
      suspended_(false),
      lit_(false) {
}

void CityDayNight::init() {
    for (std::uint8_t slot = 0; slot < kBackgroundSlots; ++slot) {
        backgroundSource_[slot] = gfx::getBackgroundPaletteSlot(slot);
    }
    for (std::uint8_t i = 0; i < kTintedSpriteSlots; ++i) {
        spriteSource_[i] =
            gfx::getSpritePaletteSlot(static_cast<std::uint8_t>(kFirstSpriteSlot + i));
    }

    clockMs_   = daynight::kStartStep * daynight::kStepMs;
    step_      = daynight::kStartStep;
    // The scene starts outdoors, and init() may be re-entered from indoors on
    // a scene restart: a suspension left standing would leave the whole city
    // at noon for the rest of the run.
    suspended_ = false;
    apply();
}

bool CityDayNight::update(unsigned long deltaMs) {
    clockMs_ = (clockMs_ + static_cast<std::uint32_t>(deltaMs)) % daynight::kCycleMs;

    const std::uint8_t next = daynight::stepAt(clockMs_);
    if (next == step_) {
        return false;
    }
    step_ = next;
    // Indoors the palettes are not built from the step, so there is nothing to
    // rebuild -- but the step still moved, and the HUD clock is showing it.
    if (!suspended_) {
        apply();
    }
    return true;
}

void CityDayNight::setSuspended(bool suspended) {
    if (suspended == suspended_) {
        return;
    }
    suspended_ = suspended;
    apply();
}

void CityDayNight::apply() {
    // Indoors the light is pinned at noon, so the lights are off there too --
    // not because the police station has none, but because the interior is a
    // scene of its own with its own tilesets and its own slot, and the swap
    // below reaches neither.
    const std::uint8_t at = suspended_ ? daynight::kDaylightStep : step_;
    const daynight::Ambient light = daynight::ambientAt(at);
    lit_ = !suspended_ && nightlights::lightsOn(at);
    city_scene::setNightArt(lit_);

    // 15 palettes x 16 entries = 240 conversions, paid once every kStepMs --
    // five seconds at the shipped cycle length. Doing it per frame would be
    // affordable too, but it would also defeat shouldRedrawFramebuffer(): a
    // palette that is rewritten every frame is a city that repaints every
    // frame.
    for (std::uint8_t slot = 0; slot < kBackgroundSlots; ++slot) {
        const std::uint16_t* source = backgroundSource_[slot];
        if (source == nullptr) {
            continue;
        }
        for (std::uint8_t i = 0; i < gfx::PALETTE_SIZE; ++i) {
            // A light is not dimmed by the night it is lighting. Nothing else
            // in the city may use this entry, so keeping the colour the art
            // gave it lights exactly what the after-dark tileset meant to be
            // lit.
            backgroundTinted_[slot][i] =
                (lit_ && nightlights::isLight(slot, i))
                    ? source[i]
                    : daynight::tint(source[i], light);
        }
        gfx::setBackgroundCustomPaletteSlot(slot, backgroundTinted_[slot]);
    }

    for (std::uint8_t i = 0; i < kTintedSpriteSlots; ++i) {
        const std::uint16_t* source = spriteSource_[i];
        if (source == nullptr) {
            continue;
        }
        for (std::uint8_t entry = 0; entry < gfx::PALETTE_SIZE; ++entry) {
            spriteTinted_[i][entry] = daynight::tint(source[entry], light);
        }
        gfx::setSpriteCustomPaletteSlot(
            static_cast<std::uint8_t>(kFirstSpriteSlot + i), spriteTinted_[i]);
    }
}

}  // namespace top_down_city
