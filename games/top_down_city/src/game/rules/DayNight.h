#pragma once
#include <cstdint>

/**
 * @brief The city's clock and the light it is seen under.
 *
 * Integer RGB565 arithmetic, engine-free so `pio test -e host_test` covers
 * it; the renderer half -- RAM palette buffers, slot rebinding -- lives in
 * CityDayNight. The whole effect is a palette swap: tile indices, tileset
 * pools and behaviour layers never change, so a 128x128 city at night costs
 * the same 240 colour conversions as one at noon, paid once per step advance
 * and nothing at all on the frames between.
 */
namespace top_down_city::daynight {

/// Steps in a full 24-hour cycle. One step is 30 in-game minutes, and it is
/// also the quantum at which the fifteen palettes are rebuilt.
constexpr std::uint8_t kSteps = 48;

/// Real milliseconds for a whole in-game day. Four minutes: long enough that
/// noon and midnight are separate moods, short enough that someone who
/// launches the demo sees both without waiting.
constexpr std::uint32_t kCycleMs = 240000;

/// Real milliseconds per step. Exact by construction, which is what keeps
/// `stepAt` from drifting.
constexpr std::uint32_t kStepMs = kCycleMs / kSteps;
static_assert(kStepMs * kSteps == kCycleMs,
              "kCycleMs must divide evenly into kSteps");

/// The city opens at 08:00, not at midnight: the demo should look like a city
/// before it looks like a light show.
constexpr std::uint8_t kStartStep = 16;

/// The step interiors are lit at, whatever the sky is doing outside. Noon is
/// on the curve's flat daylight plateau: the tint is the exact identity and
/// the art is shown as authored. The host test pins that; the number is
/// arbitrary anywhere in 15..34.
constexpr std::uint8_t kDaylightStep = 24;

/// A light level, as a per-channel multiplier over the daylight art. 255
/// means "leave this channel alone", so full daylight is the identity
/// transform and the generated palettes stay the single source of truth for
/// what the city looks like.
struct Ambient {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

/// The step a clock reading falls in. Wraps, so the caller may let its clock
/// run past kCycleMs without reducing it first.
std::uint8_t stepAt(std::uint32_t clockMs);

/// In-game wall clock at `step`, for the HUD. Out-of-range steps are reduced
/// rather than rejected: there is no sensible error value for an hour.
std::uint8_t hourOf(std::uint8_t step);
std::uint8_t minuteOf(std::uint8_t step);

/// The light the city is under at `step`, interpolated between the keyframes
/// in the .cpp. Adjacent steps differ by small amounts, so the cycle reads as
/// dusk falling rather than as a switch being thrown.
Ambient ambientAt(std::uint8_t step);

/**
 * @brief One RGB565 palette entry under `light`.
 *
 * Two invariants, both the engine's rules rather than taste. 0x0000 in,
 * 0x0000 out: entry 0 of every palette is the transparent index for 4bpp
 * sprites and tiles, and tinting it would paint over every hole. And never
 * 0x0000 out for a non-zero input: the 8bpp framebuffer treats a colour that
 * packs to zero as "skip" rather than as black, so a dim colour rounded down
 * to zero would punch holes in the city instead of darkening it -- such a
 * colour is floored to 0x0001.
 */
std::uint16_t tint(std::uint16_t color, Ambient light);

}  // namespace top_down_city::daynight
