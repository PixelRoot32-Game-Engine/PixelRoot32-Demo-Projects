#pragma once
#include <cstdint>

#include "game/rules/DayNight.h"

/**
 * @brief When the city's lights are on, and which colours are lights.
 *
 * Separate from DayNight.h on purpose: that one answers "what light is the
 * city seen under", a curve; this answers "what in the city IS a light", a
 * table. They fail differently and are tested apart.
 *
 * Nothing here is a lighting model. On this renderer a lit window is a second
 * TILESET: the generator emits an after-dark form of the Items and Details
 * pools with the panes and lamp lenses repainted, and `city_scene::setNightArt`
 * swaps the pools with one pointer each. This file supplies the two facts art
 * needs from the clock -- `lightsOn`, so something knows when to swap, and
 * `isLight`, so the tint leaves those entries alone, a lit window that
 * darkens with the street being a blue one rather than a lit one. All of it
 * costs 2,560 bytes of flash and nothing else: no RAM, no per-frame work, no
 * second index array, because the after-dark tileset is the same length and
 * order as the daylight one.
 */
namespace top_down_city::nightlights {

/**
 * @brief Perceived brightness of an ambient light, 0..255 (Rec. 601 weights,
 *        in integers).
 *
 * It exists so "the lights come on when it gets dark" is DERIVED from the
 * tint curve: a second hand-made table of on/off steps would be correct the
 * day it was written and silently wrong the first time the curve was retuned,
 * and lamps lit at noon is not an error anything reports.
 */
std::uint8_t luminance(daynight::Ambient light);

/// Below this luminance the street lighting is on. 200 of 255, which on the
/// shipped curve lights the city at 18:30 and puts it out at 06:30 -- both
/// partway down a ramp rather than on the flat plateau, where the swap would
/// read as a glitch instead of as evening. It cannot be raised past full
/// daylight without lighting the city at noon, and the host suite pins both
/// ends of the schedule rather than the number.
constexpr std::uint8_t kLightsOnBelow = 200;

/// Whether the city's lights are on at `step`. Wrapped steps are reduced.
bool lightsOn(std::uint8_t step);

/**
 * @brief The palette entries that are lights rather than surfaces.
 *
 * One per background slot that has anything to light, and each is an entry
 * the DAYLIGHT art never draws -- the property that makes this safe, because
 * leaving an entry untinted leaves it untinted everywhere and an entry that
 * was also a roof or a badge would take those with it. The generator refuses
 * to emit a scene whose daylight tileset touches one of these, and
 * `city_scene::LIT_ENTRY_SLOT`/`LIT_ENTRY_INDEX` carry the art's own copy for
 * CityConstants.h to assert against. The interior is exempt: the cycle is a
 * sky effect, and CityDayNight pins the light at noon indoors.
 */
constexpr std::uint8_t kLightCount = 4;

/// Background palette slots that own a light entry, ascending.
constexpr std::uint8_t kLightSlot[kLightCount] = { 3, 5, 6, 7 };

/// The entry within each of those slots. FURNITURE's is the street lamp's
/// lens and its pool of light; the three building slots' are lit windows.
constexpr std::uint8_t kLightEntry[kLightCount] = { 11, 13, 12, 11 };

/// Is entry `entry` of background palette slot `slot` a light? A light keeps
/// its authored colour while the lights are on instead of being multiplied
/// down with the rest of the city. Everything else is false, including entry
/// 0 -- the transparency sentinel.
constexpr bool isLight(std::uint8_t slot, std::uint8_t entry) {
    if (entry == 0) {
        return false;
    }
    for (std::uint8_t i = 0; i < kLightCount; ++i) {
        if (kLightSlot[i] == slot && kLightEntry[i] == entry) {
            return true;
        }
    }
    return false;
}

}  // namespace top_down_city::nightlights
