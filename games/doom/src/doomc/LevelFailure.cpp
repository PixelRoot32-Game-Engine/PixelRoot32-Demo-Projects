#include "doomc/LevelFailure.h"

/*
 * The taxonomy's two tables and the WHD mapping.
 *
 * WHY THE TEXT LIVES HERE AND NOT IN THE SCENE
 *
 * A failure screen is a contract: the title, the sentence and the advice are
 * what a person reads on the panel, and `tools/boot_check.py` proves each one
 * fits and is distinct. Keeping them beside the reason means a new branch adds a
 * row to this table and the gate covers it automatically, instead of the painter
 * growing a second switch that could disagree with the first.
 *
 * The wrap points are in the strings (`\n`), never in the painter: the table
 * decides where a sentence breaks, and the painter only honours the byte. Every
 * rendered line is at most 25 characters, the width scale 2 leaves on the
 * 320-pixel surface, and the gate measures that rather than trusting the comment.
 *
 * UPPERCASE ONLY, on purpose: the embedded font is ASCII 32..95, because the
 * panel's 0.75 downscale keeps capitals legible and the failure it must report
 * includes "the WAD is missing", where no WAD font exists to borrow.
 */

namespace doom {
namespace doomc {

namespace {

/*
 * `doom_whd_source_open()`'s codes, restated as numbers so this file compiles
 * without lib/doom or the flash adapter. FrameDriver.cpp -- the one place that
 * includes both this header and WhdSource.h -- static_asserts each value against
 * the adapter's macro, so a renumbered code is a build failure, not a wrong
 * screen.
 */
enum WhdCode : int {
    kWhdOk = 0,
    kWhdStateNull = 1,
    kWhdAlreadyOpen = 2,
    kWhdPartitionMissing = 3,
    kWhdMmapFailed = 4,
    kWhdContainerInvalid = 5,
    kWhdUnavailable = 6,
};

struct FailureText {
    const char* name;   ///< `SHOW,boot`'s token, lowercase snake_case.
    const char* title;  ///< Scale 3, at most 15 characters.
    const char* body;   ///< Scale 2, at most two lines of 25.
    const char* hint;   ///< Scale 2, at most 25 characters per line.
};

/*
 * One row per enumerator, in declaration order. Titles are distinct, and so are
 * the bodies and hints: two branches that look the same on the panel would make
 * "which one is it?" a serial-port question again.
 */
constexpr FailureText kText[] = {
    // None is never painted; it exists so a builder/test can say "no failure"
    // without a null pointer, and its text is deliberately not the loading
    // screen's.
    {"none", "READY", "NO FAILURE", "STARTING"},
    {"partition_missing", "NO WAD SLOT", "THE WAD PARTITION\nIS MISSING",
     "CHECK THE PARTITION TABLE"},
    {"partition_map_failed", "WAD UNREADABLE", "THE WAD PARTITION\nDID NOT MAP",
     "REFLASH AND RETRY"},
    {"container_invalid", "BAD CONTAINER", "THE WAD IMAGE\nIS INVALID",
     "REFLASH DOOM1.WHD"},
    {"unavailable", "NO FLASH ACCESS", "THIS BUILD CANNOT\nREAD FLASH",
     "USE THE BOARD BUILD"},
    {"baked", "NO BAKE", "BAKED GRAPHICS\nARE MISSING", "BAKE THE GRAPHICS FIRST"},
    {"palette", "NO PALETTE", "PLAYPAL IS MISSING\nOR TOO SHORT",
     "CHECK PLAYPAL IN THE WAD"},
    {"level", "NO LEVEL", "LEVEL MARKER\nNOT FOUND", "CHECK THE WAD CONTENTS"},
    {"map_lump", "MAP LUMP", "A MAP LUMP\nIS MISSING", "REBUILD THE MAP LUMPS"},
    {"colormap", "NO COLORMAP", "COLORMAP IS\nMISSING OR BAD",
     "CHECK COLORMAP IN THE WAD"},
    {"sprites", "NO SPRITES", "SPRITE LIST\nDID NOT FIT", "FREE MEMORY AND RETRY"},
    {"storage", "NO RENDERER", "RENDERER POOLS\nFAILED", "REBOOT THE BOARD"},
    {"world", "NO WORLD", "LEVEL WORLD\nDID NOT FIT", "FREE MEMORY THEN RETRY"},
    {"surface", "NO DISPLAY", "FRAME SURFACE\nIS MISSING", "CHECK THE DISPLAY BUILD"},
};

constexpr int kTextCount = static_cast<int>(sizeof(kText) / sizeof(kText[0]));
static_assert(kTextCount == kLevelFailureReasonCount,
              "every LevelFailureReason needs exactly one text row");

/**
 * @brief The row for @p reason, clamped to `None` for an out-of-range cast.
 *
 * A `static_cast<LevelFailureReason>` of a bad byte is a programming error, but
 * a panel screen is the wrong place to discover it: the clamp makes the failure
 * visible and harmless instead of indexing past the table.
 */
const FailureText& textFor(LevelFailureReason reason) {
    const int index = static_cast<int>(reason);
    if (index < 0 || index >= kTextCount) {
        return kText[0];
    }
    return kText[index];
}

}  // namespace

const char* levelFailureName(LevelFailureReason reason) {
    return textFor(reason).name;
}

LevelFailure levelFailureFromWhd(int whdCode) {
    LevelFailure failure;
    failure.code = whdCode;

    switch (whdCode) {
        case kWhdOk:
            failure.reason = LevelFailureReason::None;
            break;
        case kWhdPartitionMissing:
            failure.reason = LevelFailureReason::PartitionMissing;
            break;
        case kWhdMmapFailed:
            failure.reason = LevelFailureReason::PartitionMapFailed;
            break;
        case kWhdContainerInvalid:
            failure.reason = LevelFailureReason::ContainerInvalid;
            break;
        case kWhdUnavailable:
            failure.reason = LevelFailureReason::Unavailable;
            break;
        default:
            // STATE_NULL, ALREADY_OPEN and the accessor-only codes never come
            // from the open path; if one does, the honest panel answer is "this
            // build cannot read flash", not a fabricated container diagnosis.
            // The serial line keeps the exact code either way.
            failure.reason = LevelFailureReason::Unavailable;
            break;
    }
    return failure;
}

const char* levelFailureTitle(const LevelFailure& failure) {
    return textFor(failure.reason).title;
}

const char* levelFailureBody(const LevelFailure& failure) {
    return textFor(failure.reason).body;
}

const char* levelFailureHint(const LevelFailure& failure) {
    return textFor(failure.reason).hint;
}

}  // namespace doomc
}  // namespace doom
