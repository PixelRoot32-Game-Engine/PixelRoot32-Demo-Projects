#pragma once

#include <cstdint>

namespace doom {
namespace doomc {

/**
 * @brief Every way a level session can fail to open, as one vocabulary (D7.2).
 *
 * `openLevelSession()` already classifies each failure in the line it prints.
 * This enum is the same classification for the panel and for a test: the serial
 * line stays the precise record, and the screen gets one machine-readable reason
 * instead of a second, hand-written string per branch.
 *
 * The order is declaration order, and `kLevelFailureReasons` below lists every
 * value so the gate and the host tests iterate the taxonomy rather than a copy
 * of it.
 */
enum class LevelFailureReason : std::uint8_t {
    None,
    PartitionMissing,
    PartitionMapFailed,
    ContainerInvalid,
    Unavailable,
    Baked,
    Palette,
    Level,
    MapLump,
    Colormap,
    Sprites,
    Storage,
    World,
    Surface,
};

/**
 * @brief One classified failure: the reason, the stage's own code and a detail.
 *
 * `code` is the numeric code of the stage that failed (a `doom_whd_source` code,
 * a `BakedError`, a `doom_map_error_t`, a count that exceeded a capacity), or 0
 * where the stage has no code. `detail` names the thing a person needs to find
 * the branch: a map marker, a map lump, a baked error name or a world stage. It
 * is empty when the reason already says everything.
 *
 * Kept as a value with a fixed-size detail so recording one costs no allocation
 * and the object can live in `.bss` next to the driver that produces it.
 */
struct LevelFailure {
    LevelFailureReason reason = LevelFailureReason::None;
    int code = 0;
    char detail[20] = {};
};

/// Every reason in declaration order, `None` included: what the gate renders
/// and what the taxonomy tests walk. The array is compile-time data, so a test
/// that forgets a new enumerator stops compiling only if this list is updated
/// with it -- which is why the two are adjacent.
constexpr LevelFailureReason kLevelFailureReasons[] = {
    LevelFailureReason::None,
    LevelFailureReason::PartitionMissing,
    LevelFailureReason::PartitionMapFailed,
    LevelFailureReason::ContainerInvalid,
    LevelFailureReason::Unavailable,
    LevelFailureReason::Baked,
    LevelFailureReason::Palette,
    LevelFailureReason::Level,
    LevelFailureReason::MapLump,
    LevelFailureReason::Colormap,
    LevelFailureReason::Sprites,
    LevelFailureReason::Storage,
    LevelFailureReason::World,
    LevelFailureReason::Surface,
};

/// Number of entries in `kLevelFailureReasons`, without a sentinel value that
/// the enum itself would have to reserve.
constexpr int kLevelFailureReasonCount =
    static_cast<int>(sizeof(kLevelFailureReasons) / sizeof(kLevelFailureReasons[0]));

/**
 * @brief The taxonomy's serial name, e.g. "container_invalid".
 *
 * The same lowercase snake_case the setup lines use (`reason=open`,
 * `reason=baked`), so a `SHOW,boot` line and the existing failure line share a
 * vocabulary. Never NULL and never empty.
 *
 * @param reason The reason to name.
 * @return A static string; an out-of-range value answers "unknown".
 */
const char* levelFailureName(LevelFailureReason reason);

/**
 * @brief Classifies `doom_whd_source_open()`'s return code.
 *
 * The adapter's open path uses codes 0, 3, 4, 5 and 6; the accessor codes are
 * mapped to `Unavailable` because no open-path branch produces them, but every
 * code still reaches a reason instead of silently becoming `None`. A
 * `static_assert` block in `FrameDriver.cpp`, the one translation unit that sees
 * both vocabularies, pins these numeric values to the adapter's macros.
 *
 * @param whdCode The code `doom_whd_source_open()` returned.
 * @return `None` for 0; otherwise the reason with @p whdCode in `code`.
 */
LevelFailure levelFailureFromWhd(int whdCode);

/**
 * @brief The failure screen's title, in UPPERCASE, distinct per reason.
 * @param failure The classified failure.
 * @return A static string; at most 15 characters so it fits at scale 3.
 */
const char* levelFailureTitle(const LevelFailure& failure);

/**
 * @brief The failure screen's body: one sentence, at most two rendered lines.
 *
 * A `\n` in the returned string is the table's own wrap point; it is never
 * computed by the painter. Each rendered line is at most 25 characters, the
 * width the body's scale leaves on a 320-pixel surface.
 *
 * @param failure The classified failure.
 * @return A static string, possibly containing one `\n`.
 */
const char* levelFailureBody(const LevelFailure& failure);

/**
 * @brief What the person holding the board can do, at most two rendered lines.
 * @param failure The classified failure.
 * @return A static string; at most 25 characters per rendered line.
 */
const char* levelFailureHint(const LevelFailure& failure);

}  // namespace doomc
}  // namespace doom
