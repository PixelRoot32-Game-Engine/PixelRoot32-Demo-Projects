#pragma once
#include <cstddef>
#include <cstdint>

#include "game/rules/Contract.h"
#include "game/rules/Economy.h"

/**
 * What the city remembers between runs: the longest delivery streak, which
 * chapters have ever been finished, and the most cash held at once.
 *
 * It is a record book, not a save: a run never restores from it and always
 * starts clean. Everything here only ever goes up -- a bad run cannot take a
 * best away -- which is what `merge` enforces and why its return value is
 * "something improved": the caller shows "NEW BEST ..." and decides whether a
 * save is worth doing from that single bool.
 *
 * Field types follow where the values live today: `bestStreak` is
 * `mission::State::streak`/`best` (uint8_t), `bestCash` is
 * `economy::Purse::cash` (uint16_t).
 *
 * The payload is a fixed little-endian layout owned by the game and versioned
 * by `kSchemaVersion`. It carries no CRC or magic on purpose: the engine's
 * `RecordCodec` envelope wraps it and owns integrity.
 *
 * Engine-free like the rest of `game/rules/`.
 */
namespace top_down_city::records {

/// Bump when the payload layout changes; the engine envelope stores it.
constexpr std::uint16_t kSchemaVersion = 1;

/// [bestStreak u8][chapters u8][bestCash u16 LE]
constexpr std::size_t kPayloadSize = 4;

struct Record {
    std::uint8_t  bestStreak;  ///< longest delivery streak ever
    std::uint8_t  chapters;    ///< bit (1 << Chapter) per chapter completed
    std::uint16_t bestCash;    ///< most cash ever held
};

/// Nothing recorded.
Record clear();

/// Fold `seen` (a finished run or a live moment) into `into`, never lowering
/// any value. @return true when at least one value rose or a chapter was added.
bool merge(Record& into, const Record& seen);

/// Which values of a run would beat the book. Same comparisons as `merge`, but
/// reports each field, so the banner can say WHAT was beaten.
struct Improved {
    bool streak  = false;
    bool cash    = false;
    bool chapter = false;
    bool any() const { return streak || cash || chapter; }
};

/// What `merge(base, seen)` would raise, without changing anything.
/// `improved(b, s).any()` equals the bool `merge` returns.
Improved improved(const Record& base, const Record& seen);

/// Write the payload.
/// @return bytes written (`kPayloadSize`), or 0 when `capacity` is too small
///         (nothing is written).
std::size_t encode(const Record& record, std::uint8_t* out, std::size_t capacity);

/// Read a payload.
/// @return false -- leaving `record` untouched -- on a null input, a size other
///         than `kPayloadSize`, a chapter bit beyond `Chapter::Count`, or cash
///         above `economy::kMaxCash`.
bool decode(const std::uint8_t* in, std::size_t size, Record& record);

}  // namespace top_down_city::records
