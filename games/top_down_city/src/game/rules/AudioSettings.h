#pragma once
#include <cstddef>
#include <cstdint>

/**
 * The player's audio preference: sound on/off plus one shared volume level.
 *
 * One level drives both gains `AudioDirector` uses today:
 *  - `masterGain` is the engine master volume (`engine_->setMasterVolume`,
 *    0.85 today); it scales music and effects alike.
 *  - `sfxGain` is the extra multiplier on every cue's own volume
 *    (`AudioDirector::sfxVolume_`, 0.65 today); it keeps effects under the music.
 * Stepping both together keeps the mix balance. The top level is the default
 * and maps to exactly today's 0.85 / 0.65, so a first boot sounds as now.
 * Audio off maps both gains to 0 and remembers nothing else.
 *
 * `cycle` is the single Start-button action. Order (documented, deterministic):
 *   off -> level 0 (low) -> level 1 -> level 2 (high, default) -> off -> ...
 * From the default that means the first press mutes, the next brings the
 * lowest level back, and so on.
 *
 * The payload is a fixed layout owned by the game and versioned by
 * `kSchemaVersion`. It carries no CRC or magic on purpose: the engine's
 * `RecordCodec` envelope wraps it and owns integrity.
 *
 * Engine-free like the rest of `game/rules/`.
 */
namespace top_down_city::audio_settings {

/// Bump when the payload layout changes; the engine envelope stores it.
constexpr std::uint16_t kSchemaVersion = 1;

/// [audioOn u8 (0/1)][level u8]
constexpr std::size_t kPayloadSize = 2;

/// Number of volume levels; valid `level` values are 0 .. kLevelCount - 1.
constexpr std::uint8_t kLevelCount = 3;

struct Settings {
    bool         audioOn;  ///< false mutes everything
    std::uint8_t level;    ///< shared volume level, 0 (low) .. kLevelCount - 1
};

/// Audio on at the top level: today's mix.
Settings defaults();

/// Engine master volume for these settings (0.85f at defaults, 0 when off).
float masterGain(const Settings& settings);

/// SFX multiplier for these settings (0.65f at defaults, 0 when off).
float sfxGain(const Settings& settings);

/// Advance one Start press along the documented order. Pure.
Settings cycle(const Settings& settings);

/// Room for the longest label ("VOLUME 3/3") plus the terminator, with slack.
constexpr std::size_t kLabelCapacity = 12;

/// The text the player sees after a Start press: "AUDIO OFF" when off, else
/// "VOLUME n/N" with n = level + 1 and N = `kLevelCount`.
/// @return characters written (terminator excluded), or 0 -- writing nothing --
///         when `out` is null or `capacity` cannot hold the text and its NUL.
std::size_t label(const Settings& settings, char* out, std::size_t capacity);

/// Write the payload.
/// @return bytes written (`kPayloadSize`), or 0 when `out` is null or
///         `capacity` is too small (nothing is written).
std::size_t encode(const Settings& settings, std::uint8_t* out, std::size_t capacity);

/// Read a payload.
/// @return false -- leaving `settings` untouched -- on a null input, a size
///         other than `kPayloadSize`, an audioOn byte other than 0/1, or a
///         level beyond `kLevelCount - 1`.
bool decode(const std::uint8_t* in, std::size_t size, Settings& settings);

}  // namespace top_down_city::audio_settings
