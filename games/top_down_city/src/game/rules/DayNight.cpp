#include "game/rules/DayNight.h"

namespace top_down_city::daynight {

namespace {

/**
 * @brief The light at a handful of moments; everything between is a lerp.
 *
 * In steps rather than hours because the step is what the rest of the system
 * counts in: a keyframe that does not land on one interpolates to a value no
 * frame ever shows.
 *
 * The shape is deliberately asymmetric -- dawn quick and cool over three
 * steps of thin warm light, dusk drawn out and heavily orange over four --
 * which is the difference a player reads as morning rather than evening. No
 * two consecutive keyframes may differ by more than 40 on a channel across
 * the steps between them: above that the step change is a visible flash
 * rather than a fade, because the panel only repaints when the step advances
 * and there are no in-between frames to soften it.
 * test_the_light_never_jumps holds the line.
 */
struct Keyframe {
    std::uint8_t step;
    Ambient      light;
};

/// Moonlight: a little over a third of the daylight, and blue rather than
/// grey. Red is pulled down hardest because a warm colour left bright at
/// night is what makes a night tint read as "dim day" instead of "night".
constexpr Ambient kNight = { 88, 104, 160 };

/// Dawn: low sun, no heat in it yet.
constexpr Ambient kDawn = { 205, 165, 150 };

/// Full daylight. The identity transform, which is what keeps the generated
/// palettes the single source of truth for the city's colours.
constexpr Ambient kDay = { 255, 255, 255 };

/// Dusk: the one moment the city is warmer than the art it is drawn from is
/// not available here -- a multiplier cannot add light -- so sunset is a red
/// that barely falls while green and blue drop away under it.
constexpr Ambient kDusk = { 245, 150, 105 };

/// Must start at step 0 and end at kSteps: `ambientAt` interpolates between
/// consecutive entries and relies on the last one closing the circle back
/// onto the first, which is how midnight comes out continuous.
constexpr Keyframe kCurve[] = {
    {  0, kNight },   // 00:00
    {  9, kNight },   // 04:30  -- the last of the dark
    { 12, kDawn  },   // 06:00
    { 15, kDay   },   // 07:30  -- fully up
    { 34, kDay   },   // 17:00  -- the last of the light
    { 38, kDusk  },   // 19:00
    { 42, kNight },   // 21:00
    { kSteps, kNight },
};

constexpr std::uint8_t kCurveCount =
    static_cast<std::uint8_t>(sizeof(kCurve) / sizeof(kCurve[0]));

/// Linear between two keyframe channels. `span` is never zero: consecutive
/// keyframe steps are distinct by construction above.
std::uint8_t lerpChannel(std::uint8_t from, std::uint8_t to,
                         std::uint8_t travelled, std::uint8_t span) {
    const int delta = static_cast<int>(to) - static_cast<int>(from);
    return static_cast<std::uint8_t>(
        static_cast<int>(from) + (delta * travelled) / span);
}

/// One 0..max channel under a 0..255 multiplier, rounded rather than
/// truncated. Rounding is what makes a multiplier of 255 the identity:
/// truncation would shave a step off every bright colour and wash the whole
/// city out at noon.
std::uint8_t scaleChannel(unsigned value, std::uint8_t multiplier) {
    return static_cast<std::uint8_t>((value * multiplier + 127u) / 255u);
}

}  // namespace

std::uint8_t stepAt(std::uint32_t clockMs) {
    return static_cast<std::uint8_t>((clockMs / kStepMs) % kSteps);
}

std::uint8_t hourOf(std::uint8_t step) {
    return static_cast<std::uint8_t>((step % kSteps) / 2);
}

std::uint8_t minuteOf(std::uint8_t step) {
    return static_cast<std::uint8_t>((step % kSteps) % 2 == 0 ? 0 : 30);
}

Ambient ambientAt(std::uint8_t step) {
    const std::uint8_t at = static_cast<std::uint8_t>(step % kSteps);

    for (std::uint8_t i = 1; i < kCurveCount; ++i) {
        if (at >= kCurve[i].step) {
            continue;
        }
        const Keyframe& from = kCurve[i - 1];
        const Keyframe& to   = kCurve[i];
        const std::uint8_t span = static_cast<std::uint8_t>(to.step - from.step);
        const std::uint8_t travelled = static_cast<std::uint8_t>(at - from.step);
        return Ambient{
            lerpChannel(from.light.r, to.light.r, travelled, span),
            lerpChannel(from.light.g, to.light.g, travelled, span),
            lerpChannel(from.light.b, to.light.b, travelled, span),
        };
    }
    // Unreachable while the last keyframe sits at kSteps, which the table's
    // own comment requires. Returning the last light is still the right
    // answer if someone edits that table and gets it wrong.
    return kCurve[kCurveCount - 1].light;
}

std::uint16_t tint(std::uint16_t color, Ambient light) {
    if (color == 0x0000) {
        return 0x0000;
    }

    const unsigned r = (color >> 11) & 0x1Fu;
    const unsigned g = (color >> 5) & 0x3Fu;
    const unsigned b = color & 0x1Fu;

    const std::uint16_t out =
        static_cast<std::uint16_t>((scaleChannel(r, light.r) << 11) |
                                   (scaleChannel(g, light.g) << 5) |
                                    scaleChannel(b, light.b));

    // One unit of blue is the darkest thing that still draws -- see `tint`'s
    // second invariant in the header for why zero is not an option.
    return out != 0x0000 ? out : 0x0001;
}

}  // namespace top_down_city::daynight
