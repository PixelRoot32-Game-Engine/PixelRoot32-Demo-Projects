#pragma once
#include <cstdint>

#include <graphics/Color.h>

#include "game/rules/DayNight.h"
#include "game/rules/NightLights.h"

namespace top_down_city {

/**
 * @brief The clock, and the fifteen palettes it repaints the city with.
 *
 * The engine-facing half of the cycle; the arithmetic is in DayNight.h, split
 * out because it has invariants worth host-testing and this half does not.
 *
 * The palette setters store a POINTER, not a copy, and the generated palettes
 * live in flash and cannot be written, so a runtime tint needs RAM of its own:
 * eight background and seven sprite palettes, 480 bytes total. That is the
 * entire memory cost of the effect -- tile indices, tileset pools and behaviour
 * layers are untouched, which is why a 128x128 city can change light for the
 * price of a 16x16 one.
 *
 * The city's lights are not a lighting pass either: the generated scene carries
 * a second form of the Items and Details tilesets with the windows and lamp
 * lenses repainted, and `apply()` swaps the pools at the same moment it
 * rebuilds the palettes. The two MUST move together -- the after-dark art
 * paints its lit pixels in an entry that only looks lit because this class
 * stops tinting it, so either half alone is visibly wrong.
 *
 * Sprite slot 0 is not tinted because nothing draws through it. The HUD is
 * drawn with primitives, which read the engine's BACKGROUND palette (see
 * `hud::` in CityConstants.h), so it is not drawn from a palette the cycle can
 * reach at all.
 */
class CityDayNight {
public:
    /// Background slots the generated scene fills, all of them tinted.
    static constexpr std::uint8_t kBackgroundSlots = 8;

    /// Sprite slots 1..7 -- the cars, the four street recolours, the police
    /// uniform and the player's own copy. Slot 0 is the untinted ink, and it
    /// is the only one left out.
    static constexpr std::uint8_t kFirstSpriteSlot  = 1;
    static constexpr std::uint8_t kTintedSpriteSlots = 7;

    CityDayNight();

    /**
     * @brief Adopt whatever is bound to the palette slots as the daylight art,
     *        then apply the opening tint.
     *
     * Must run AFTER `city_scene::init()` and after the sprite slots are
     * filled. Reads the slots back through the engine rather than naming the
     * generated arrays: those are `static const` in a header, so naming them
     * would hand every translation unit its own copy of 96 colours.
     *
     * @warning A second call with our own buffers still bound would adopt an
     *          already-tinted city as daylight; CityWorld calls it once a run.
     */
    void init();

    /// Advance the clock. True when the step changed: the HUD readout -- and,
    /// outside, the light with it -- is now stale and the frame must be
    /// redrawn.
    bool update(unsigned long deltaMs);

    /**
     * Pin the light at full daylight without stopping the clock. For interiors:
     * the cycle is a sky effect, and a police station that goes dark at 22:00
     * with three officers on duty reads as a bug. Suspended, the palettes are
     * built at `daynight::kDaylightStep`, where the tint is the exact identity.
     *
     * The clock keeps running underneath and the repaint is paid once per
     * transition rather than per step: `update()` skips the rebuild while
     * suspended, and clearing this rebuilds once at whatever hour the city has
     * reached.
     */
    void setSuspended(bool suspended);

    bool suspended() const { return suspended_; }

    /// The current tint step. What CityScene compares against the last drawn
    /// frame -- the palettes are not part of the framebuffer, so a step change
    /// is invisible to every other redraw test.
    std::uint8_t step() const { return step_; }

    std::uint8_t hour() const   { return daynight::hourOf(step_); }
    std::uint8_t minute() const { return daynight::minuteOf(step_); }

    /// Are the city's lights on? False indoors, where the light is pinned at
    /// noon and the sky is not visible anyway.
    bool lit() const { return lit_; }

private:
    /// Rebuild all fifteen palettes for the current light and rebind the
    /// slots. The light is `step_` outdoors and kDaylightStep while suspended.
    void apply();

    const std::uint16_t* backgroundSource_[kBackgroundSlots];
    const std::uint16_t* spriteSource_[kTintedSpriteSlots];

    std::uint16_t backgroundTinted_[kBackgroundSlots]
                                   [pixelroot32::graphics::PALETTE_SIZE];
    std::uint16_t spriteTinted_[kTintedSpriteSlots]
                               [pixelroot32::graphics::PALETTE_SIZE];

    /// Real milliseconds since midnight, reduced every cycle so it cannot
    /// overflow however long the demo is left running.
    std::uint32_t clockMs_;
    std::uint8_t  step_;

    /// Indoors. The clock still advances; the palettes stop following it.
    bool          suspended_;

    /// Whether the after-dark tilesets are the ones currently bound. Cached
    /// rather than recomputed because it is also what the HUD and anything
    /// else that wants to know the hour is dark would ask.
    bool          lit_;
};

}  // namespace top_down_city
