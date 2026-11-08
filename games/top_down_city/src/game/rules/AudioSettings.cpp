#include "game/rules/AudioSettings.h"

namespace top_down_city::audio_settings {

namespace {

// Indexed by level. The top entries are today's AudioDirector values.
constexpr float kMasterByLevel[kLevelCount] = {0.45f, 0.65f, 0.85f};
constexpr float kSfxByLevel[kLevelCount] = {0.35f, 0.50f, 0.65f};

constexpr std::uint8_t kTopLevel = kLevelCount - 1;

std::uint8_t clampLevel(std::uint8_t level) {
    return level > kTopLevel ? kTopLevel : level;
}

}  // namespace

Settings defaults() {
    Settings settings;
    settings.audioOn = true;
    settings.level = kTopLevel;
    return settings;
}

float masterGain(const Settings& settings) {
    return settings.audioOn ? kMasterByLevel[clampLevel(settings.level)] : 0.0f;
}

float sfxGain(const Settings& settings) {
    return settings.audioOn ? kSfxByLevel[clampLevel(settings.level)] : 0.0f;
}

Settings cycle(const Settings& settings) {
    Settings next;
    if (!settings.audioOn) {
        next.audioOn = true;
        next.level = 0;
    } else if (settings.level >= kTopLevel) {
        next.audioOn = false;
        next.level = 0;
    } else {
        next.audioOn = true;
        next.level = static_cast<std::uint8_t>(settings.level + 1);
    }
    return next;
}

std::size_t encode(const Settings& settings, std::uint8_t* out, std::size_t capacity) {
    if (out == nullptr || capacity < kPayloadSize) return 0;
    out[0] = settings.audioOn ? 1 : 0;
    out[1] = settings.level;
    return kPayloadSize;
}

bool decode(const std::uint8_t* in, std::size_t size, Settings& settings) {
    if (in == nullptr || size != kPayloadSize) return false;
    if (in[0] > 1) return false;
    if (in[1] > kTopLevel) return false;
    settings.audioOn = in[0] == 1;
    settings.level = in[1];
    return true;
}

}  // namespace top_down_city::audio_settings
