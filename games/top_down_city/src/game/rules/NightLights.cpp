#include "game/rules/NightLights.h"

namespace top_down_city::nightlights {

namespace {

/// Rec. 601 luma weights scaled to 256ths: 0.299, 0.587, 0.114. The sum is
/// 256 exactly, which is what keeps a white ambient at 255 rather than 254 --
/// and full daylight reading as full daylight is the one value this has to
/// get right, because it is the value the threshold is measured against.
constexpr unsigned kRedWeight   = 77;
constexpr unsigned kGreenWeight = 150;
constexpr unsigned kBlueWeight  = 29;
static_assert(kRedWeight + kGreenWeight + kBlueWeight == 256,
              "the weights must sum to 256 or white stops being 255");

}  // namespace

std::uint8_t luminance(daynight::Ambient light) {
    const unsigned sum = static_cast<unsigned>(light.r) * kRedWeight
                       + static_cast<unsigned>(light.g) * kGreenWeight
                       + static_cast<unsigned>(light.b) * kBlueWeight;
    return static_cast<std::uint8_t>(sum >> 8);
}

bool lightsOn(std::uint8_t step) {
    return luminance(daynight::ambientAt(step)) < kLightsOnBelow;
}

}  // namespace top_down_city::nightlights
