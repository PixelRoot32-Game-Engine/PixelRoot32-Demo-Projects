/*
 * D2.5's instrument: see FlashProfile.h for the question it answers, what one
 * read is, the timing primitive and the exact line formats.
 *
 * The file has two halves and no third one.
 *
 * The first half -- accumulators, throughput, ratio, frame budget and the three
 * formatters -- is plain integer C++ with <cstdio> and nothing else. It is
 * compiled for every target, because it is the half a host test can pin: the
 * exact line a parser reads, the exclusive millisecond buckets, the bounded
 * record set and the zero-read case. `-ffunction-sections` plus `--gc-sections`
 * removes it from a build that never calls it, so a shipped image pays nothing
 * for the arithmetic existing.
 *
 * The second half is the board probe and only exists when
 * `PIXELROOT32_DOOM_FLASH_PROFILE` is defined (`env:esp32dev_ILI9341_flashprof`).
 * It is the only code in this project that maps flash and takes a timestamp, and
 * it is deliberately a separate file from WhdSource: WhdSource is the device
 * adapter a production loader will use, and a measurement harness must not
 * grow into it.
 *
 * WHAT THE PROBE DID NOT GUESS
 *
 * E1M1's working set is built from the container itself, not from a table in
 * this file. The map lumps come from their names; SIDEDEFS and SECTORS are
 * parsed while they are still the buffer's contents, which yields the texture
 * names and flat names the level actually references; TEXTURE1 is then resolved
 * through r_data (`r_texture_lookup`) for each of those names, and PNAMES turns
 * the patch indices those definitions carry into patch lump names. Every
 * referenced lump is deduplicated by index, so a patch used by six textures is
 * read once per pass. On the shipped container that is 101 lumps and 490,976
 * bytes for E1M1: 11 map entries, TEXTURE1 and PNAMES, 66 patch lumps and 22
 * flats, with a largest lump of 28,080 B.
 *
 * WHAT THE BUFFER COSTS
 *
 * The decode buffer is sized for the largest lump the container holds (68,168 B,
 * HELP1), not for the largest lump E1M1 needs, so a different level cannot
 * silently be refused. It is 68,168 B of static .bss and the probe's first line
 * prints it as `buf=68168`. It is static rather than local because the task
 * stack is a fraction of that; the probe's own stack use is the ~200-byte line
 * buffer and a handful of integers.
 */
#include "doomc/FlashProfile.h"

#include <cstdio>

namespace doom {
namespace doomc {

namespace {

/// One millisecond and four, as microseconds: the two bucket edges.
constexpr std::uint32_t kOneMilliMicros = 1000u;
constexpr std::uint32_t kFourMilliMicros = 4000u;

/// A second and a mebibyte, for throughput in tenths of MiB/s.
constexpr std::uint64_t kMicrosPerSecond = 1000000u;
constexpr std::uint64_t kBytesPerMebibyte = 1048576u;

}  // namespace

void flashProfileReset(FlashProfileStats* stats) {
    if (stats == nullptr) {
        return;
    }
    *stats = FlashProfileStats{};
}

void flashProfileReset(FlashProfileAccumulator* accumulator) {
    if (accumulator == nullptr) {
        return;
    }
    *accumulator = FlashProfileAccumulator{};
}

std::uint32_t flashProfileBin(std::uint32_t micros) {
    // Every closed bucket is (previous edge, edge]; the last bucket is open and
    // its edge value is never compared, which is why the loop stops before it.
    for (std::uint32_t index = 0u; index + 1u < static_cast<std::uint32_t>(kFlashProfileHistogramBins);
         ++index) {
        if (micros <= kFlashProfileBinUpperMicros[index]) {
            return index;
        }
    }
    return static_cast<std::uint32_t>(kFlashProfileHistogramBins - 1);
}

void flashProfileRecord(FlashProfileStats* stats, std::uint32_t bytes, std::uint32_t micros) {
    if (stats == nullptr) {
        return;
    }

    // The first read defines both extremes, so a pass whose reads are all
    // identical still reports that value as min and as max.
    if (stats->reads == 0u || micros < stats->minMicros) {
        stats->minMicros = micros;
    }
    if (stats->reads == 0u || micros > stats->maxMicros) {
        stats->maxMicros = micros;
    }

    ++stats->reads;
    stats->totalMicros += micros;
    stats->totalBytes += bytes;

    // Strictly above the edge: a read of exactly 1 ms is not "over 1 ms".
    if (micros > kOneMilliMicros) {
        ++stats->over1ms;
    }
    if (micros > kFourMilliMicros) {
        ++stats->over4ms;
    }

    ++stats->histogram[flashProfileBin(micros)];
}

void flashProfileRecord(FlashProfileAccumulator* accumulator, std::int32_t lump,
                        std::uint32_t bytes, std::uint32_t micros) {
    if (accumulator == nullptr) {
        return;
    }

    flashProfileRecord(&accumulator->stats, bytes, micros);

    if (accumulator->recorded < static_cast<std::uint32_t>(kFlashProfileRecordCapacity)) {
        accumulator->records[accumulator->recorded].lump = lump;
        accumulator->records[accumulator->recorded].bytes = bytes;
        accumulator->records[accumulator->recorded].micros = micros;
        ++accumulator->recorded;
        return;
    }

    // The detail is bounded; the summary is not. recorded + dropped is always
    // stats.reads, so a reader can tell the two apart without trusting either.
    ++accumulator->dropped;
}

std::uint32_t flashProfileMiBTenths(std::uint64_t bytes, std::uint64_t micros) {
    if (micros == 0u) {
        // Nothing was timed: claim no rate rather than an infinite one. This is
        // the zero-read pass and every sub-microsecond read that rounded to 0.
        return 0u;
    }
    // bytes * 10 * 1e6 fits in 64 bits for any byte count a container can hold.
    const std::uint64_t scaled = bytes * 10u * kMicrosPerSecond;
    return static_cast<std::uint32_t>(scaled / (micros * kBytesPerMebibyte));
}

std::uint32_t flashProfileRatioPermille(std::uint64_t part, std::uint64_t whole) {
    if (whole == 0u) {
        return 0u;
    }
    return static_cast<std::uint32_t>((part * 1000u) / whole);
}

std::uint32_t flashProfileFramePercentTenths(std::uint64_t micros, std::uint32_t frameMicros) {
    if (frameMicros == 0u) {
        return 0u;
    }
    return static_cast<std::uint32_t>((micros * 1000u) / static_cast<std::uint64_t>(frameMicros));
}

int flashProfileSlowestRecordIndex(const FlashProfileAccumulator& accumulator) {
    int slowest = -1;
    for (std::uint32_t index = 0u; index < accumulator.recorded; ++index) {
        if (slowest < 0 || accumulator.records[index].micros > accumulator.records[slowest].micros) {
            slowest = static_cast<int>(index);
        }
    }
    return slowest;
}

std::size_t flashProfileFormatPass(const FlashProfileAccumulator& accumulator, const char* passLabel,
                                   const char* phaseLabel, char* out, std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const FlashProfileStats& stats = accumulator.stats;
    const std::uint32_t tenths = flashProfileMiBTenths(stats.totalBytes, stats.totalMicros);
    const int slowest = flashProfileSlowestRecordIndex(accumulator);
    const int slowestLump = slowest >= 0 ? static_cast<int>(accumulator.records[slowest].lump) : -1;
    const unsigned long slowestMicros =
        slowest >= 0 ? static_cast<unsigned long>(accumulator.records[slowest].micros) : 0ul;

    const int written = std::snprintf(
        out, capacity,
        "WADPROF,pass=%s,phase=%s,total_us=%llu,lumps=%u,bytes=%llu,mbps=%u.%u,min_us=%u,"
        "max_us=%u,gt1ms=%u,gt4ms=%u,slowest_lump=%d,slowest_us=%lu,records=%u,dropped=%u,"
        "failed=%u\n",
        passLabel != nullptr ? passLabel : "?", phaseLabel != nullptr ? phaseLabel : "?",
        static_cast<unsigned long long>(stats.totalMicros), static_cast<unsigned>(stats.reads),
        static_cast<unsigned long long>(stats.totalBytes), static_cast<unsigned>(tenths / 10u),
        static_cast<unsigned>(tenths % 10u), static_cast<unsigned>(stats.minMicros),
        static_cast<unsigned>(stats.maxMicros), static_cast<unsigned>(stats.over1ms),
        static_cast<unsigned>(stats.over4ms), slowestLump, slowestMicros,
        static_cast<unsigned>(accumulator.recorded), static_cast<unsigned>(accumulator.dropped),
        static_cast<unsigned>(stats.failed));

    // snprintf reports the length it would have written; anything at or past the
    // capacity was truncated, and a truncated line is not written at all.
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

std::size_t flashProfileFormatRatio(std::uint64_t coldMicros, std::uint64_t warmMicros, char* out,
                                    std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const int written = std::snprintf(out, capacity, "WADPROF,ratio,cold_us=%llu,warm_us=%llu,"
                                                      "warm_permille=%u\n",
                                      static_cast<unsigned long long>(coldMicros),
                                      static_cast<unsigned long long>(warmMicros),
                                      static_cast<unsigned>(
                                          flashProfileRatioPermille(warmMicros, coldMicros)));
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

std::size_t flashProfileFormatBudget(std::uint64_t coldMicros, std::uint32_t frameMicros, char* out,
                                     std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const int written = std::snprintf(out, capacity,
                                      "WADPROF,budget,frame_us=%u,cold_us=%llu,pct_x10=%u\n",
                                      static_cast<unsigned>(frameMicros),
                                      static_cast<unsigned long long>(coldMicros),
                                      static_cast<unsigned>(
                                          flashProfileFramePercentTenths(coldMicros, frameMicros)));
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

}  // namespace doomc
}  // namespace doom

// --- The board probe (D2.5) --------------------------------------------------

#if defined(PIXELROOT32_DOOM_FLASH_PROFILE) && (defined(ESP32) || defined(ESP_PLATFORM))

#include <cstring>
#include <esp_timer.h>

#include "doomc/DoomConsole.h"
#include "doomc/WhdSource.h"
#include "r_data.h"

namespace doom {
namespace doomc {

namespace {

/**
 * @brief The decode buffer: 68,168 B of static .bss, the container's largest lump.
 *
 * Sized for the whole container (HELP1) and not for E1M1's largest lump
 * (28,080 B) on purpose: a level that references a bigger patch must produce a
 * real read, not a BUFFER_TOO_SMALL line that hides the measurement. The probe's
 * first line prints this number as `buf=` so the log states its own RAM cost.
 */
constexpr std::size_t kLumpBufferBytes = 68168u;
alignas(4) std::uint8_t gLumpBuffer[kLumpBufferBytes];

/// The four phases a working set is read in, in plan order.
enum Phase : std::uint8_t {
    kPhaseMap = 0,          ///< E1M1 and its ten data lumps.
    kPhaseTextureDirectory, ///< TEXTURE1 and PNAMES.
    kPhasePatches,          ///< One lump per patch the level's textures name.
    kPhaseFlats,            ///< One lump per flat the level's sectors name.
    kPhaseCount
};

const char* const kPhaseNames[kPhaseCount] = {"map", "texdir", "patches", "flats"};

/// One planned read: a lump index and the phase it belongs to.
struct PlanEntry {
    std::int32_t lump;
    std::uint8_t phase;
};

/**
 * @brief The fixed working-set plan, built once by the cold pass and replayed.
 *
 * 160 entries hold E1M1's 101 with room for a level that references more; an
 * overflow is reported (`overflow=` bit 0) rather than silently truncating.
 */
constexpr int kPlanCapacity = 160;
PlanEntry gPlan[kPlanCapacity];
int gPlanCount = 0;
bool gPlanOverflow = false;

/// E1M1's map lumps, in WAD order. E1M1 itself is the zero-byte marker; the ten
/// after it are the level's data.
const char* const kMapLumpNames[] = {
    "E1M1",  "THINGS",   "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS",  "REJECT",   "BLOCKMAP",
};
constexpr int kMapLumpCount = static_cast<int>(sizeof kMapLumpNames / sizeof kMapLumpNames[0]);

/// Distinct 8-byte lump names collected from the level, NUL terminated.
constexpr int kNameCapacity = 48;
constexpr int kNameBytes = 9;

char gTextureNames[kNameCapacity][kNameBytes];
int gTextureCount = 0;
bool gTextureOverflow = false;

char gFlatNames[kNameCapacity][kNameBytes];
int gFlatCount = 0;
bool gFlatOverflow = false;

/// PNAMES indices, in texture order, as the definitions carry them.
constexpr int kPatchRefCapacity = 512;
std::uint16_t gPatchRefs[kPatchRefCapacity];
int gPatchRefCount = 0;
bool gPatchRefOverflow = false;

/// DOOM's own record layouts for the two map lumps this probe parses. Every
/// integer is little-endian and read byte by byte; nothing here casts a lump to
/// a struct.
constexpr std::size_t kSidedefBytes = 30;          // toptexture at 4, then two more
constexpr std::size_t kSidedefTextureOffset = 4;
constexpr std::size_t kSidedefTextureStride = 8;
constexpr std::size_t kSectorBytes = 26;           // floorpic at 4, ceilingpic at 12
constexpr std::size_t kSectorFlatOffset = 4;
constexpr std::size_t kSectorFlatStride = 8;

/// PNAMES: int32 count, then count * char[8].
constexpr std::size_t kPnamesHeaderBytes = 4;
constexpr std::size_t kPnamesNameStride = 8;

/// A patch record's PNAMES index: originx (2) + originy (2), then the index.
constexpr std::size_t kTexturePatchIndexOffset = 4;

/// A 35 Hz frame at 1e6 / 35 = 28,571.4 us, truncated to the integer the budget
/// line states. The renderer is held to this number in Phase 3.
constexpr std::uint32_t kFrameMicros = 28570u;

/// Per-phase accumulators (each carries its own record set, so a phase line's
/// `slowest_*` and `records=` mean what they say) and one per pass.
FlashProfileAccumulator gPhase[kPhaseCount];
FlashProfileAccumulator gCold;
FlashProfileAccumulator gWarm;

/// Discovery outcomes, reported as one `workingset` line before the cold pass.
int gMissingMapLumps = 0;
int gMissingPatches = 0;
int gMissingFlats = 0;
int gTextureErrors = 0;
int gPatchNameErrors = 0;
int gInvalidGraphics = 0;
int gMissingTextureDirectory = 0;

std::uint16_t readLe16(const std::uint8_t* at) {
    return static_cast<std::uint16_t>(at[0] | static_cast<std::uint16_t>(at[1] << 8));
}

std::uint32_t readLe32(const std::uint8_t* at) {
    return static_cast<std::uint32_t>(at[0])
           | (static_cast<std::uint32_t>(at[1]) << 8)
           | (static_cast<std::uint32_t>(at[2]) << 16)
           | (static_cast<std::uint32_t>(at[3]) << 24);
}

/// A 8-byte name field is a real lump name unless it is empty or DOOM's "-".
bool nameIsReal(const std::uint8_t* field) {
    if (field[0] == 0u) {
        return false;
    }
    return !(field[0] == static_cast<std::uint8_t>('-') && field[1] == 0u);
}

/// Copies an 8-byte field into a 9-byte NUL-terminated buffer.
void copyLumpName(const std::uint8_t* field, char* out) {
    int index = 0;
    for (; index < 8; ++index) {
        if (field[index] == 0u) {
            break;
        }
        out[index] = static_cast<char>(field[index]);
    }
    out[index] = '\0';
}

/// Appends a name to a fixed list if it is real and not already there.
void appendName(char (*list)[kNameBytes], int& count, bool& overflow, const std::uint8_t* field) {
    if (nameIsReal(field) == false) {
        return;
    }

    char name[kNameBytes];
    copyLumpName(field, name);
    for (int index = 0; index < count; ++index) {
        if (std::strcmp(list[index], name) == 0) {
            return;
        }
    }

    if (count >= kNameCapacity) {
        overflow = true;
        return;
    }
    std::strcpy(list[count], name);
    ++count;
}

/// Appends one lump to the plan, deduplicated by index so one patch is read once.
void appendPlan(std::int32_t lump, std::uint8_t phase) {
    if (lump < 0) {
        return;
    }
    for (int index = 0; index < gPlanCount; ++index) {
        if (gPlan[index].lump == lump) {
            return;
        }
    }
    if (gPlanCount >= kPlanCapacity) {
        gPlanOverflow = true;
        return;
    }
    gPlan[gPlanCount].lump = lump;
    gPlan[gPlanCount].phase = phase;
    ++gPlanCount;
}

std::int64_t nowMicros() {
    // The ESP-IDF microsecond counter: integer, 64-bit, monotonic for the life of
    // a boot. A difference of two samples is never negative and never wraps,
    // which is why the probe takes the difference in this width and only then
    // narrows to the 32 bits a record stores.
    return esp_timer_get_time();
}

/// The result of one timed read: the adapter's code and the decoded size.
struct ReadOutcome {
    int code;
    std::uint32_t bytes;
};

/**
 * @brief One measured read: the only place a timestamp is taken.
 *
 * The two samples bracket `doom_whd_source_read_lump()` and nothing else, so the
 * number is decode-into-the-buffer and never name resolution or parsing. The
 * read is folded into both the phase's accumulator and the pass's, so the phase
 * lines and the summary are derived from the same observations.
 *
 * A refused read (an index that vanished, a payload that did not decode) is
 * still recorded: it cost the time it cost, it contributed no bytes, and
 * `failed` counts it.
 */
ReadOutcome readOne(const whd_state_t* state, const PlanEntry& entry, FlashProfileAccumulator* pass) {
    std::uint32_t requiredSize = 0u;
    const std::int64_t start = nowMicros();
    const int code = doom_whd_source_read_lump(state, entry.lump, gLumpBuffer, sizeof gLumpBuffer,
                                               &requiredSize);
    const std::int64_t elapsed = nowMicros() - start;
    const std::uint32_t micros = elapsed > 0 ? static_cast<std::uint32_t>(elapsed) : 0u;
    const std::uint32_t bytes = code == DOOM_WHD_SOURCE_OK ? requiredSize : 0u;

    flashProfileRecord(&gPhase[entry.phase], entry.lump, bytes, micros);
    flashProfileRecord(pass, entry.lump, bytes, micros);

    if (code != DOOM_WHD_SOURCE_OK) {
        ++gPhase[entry.phase].stats.failed;
        ++pass->stats.failed;
    }

    const ReadOutcome outcome = {code, bytes};
    return outcome;
}

/// Textures name three sidedef slots; every real name is kept once.
void collectSidedefTextures(std::uint32_t bytes) {
    const std::size_t count = static_cast<std::size_t>(bytes) / kSidedefBytes;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t* record = gLumpBuffer + index * kSidedefBytes;
        for (std::size_t slot = 0; slot < 3; ++slot) {
            appendName(gTextureNames, gTextureCount, gTextureOverflow,
                       record + kSidedefTextureOffset + slot * kSidedefTextureStride);
        }
    }
}

/// Sectors name a floor and a ceiling flat; every real name is kept once.
void collectSectorFlats(std::uint32_t bytes) {
    const std::size_t count = static_cast<std::size_t>(bytes) / kSectorBytes;
    for (std::size_t index = 0; index < count; ++index) {
        const std::uint8_t* record = gLumpBuffer + index * kSectorBytes;
        for (std::size_t slot = 0; slot < 2; ++slot) {
            appendName(gFlatNames, gFlatCount, gFlatOverflow,
                       record + kSectorFlatOffset + slot * kSectorFlatStride);
        }
    }
}

/**
 * @brief Resolves each collected texture name through r_data, keeping its patches.
 *
 * `gLumpBuffer` holds TEXTURE1 at this point. `r_texture_lookup()` validates the
 * directory and the matched definition -- including that its patch list fits the
 * lump -- which is what makes walking that list below safe. Only the PNAMES
 * indices are kept: the names live in PNAMES, which has not been read yet.
 */
void resolveTextures(std::uint32_t texture1Size) {
    for (int index = 0; index < gTextureCount; ++index) {
        r_texture_info_t info;
        const r_error_t error =
            r_texture_lookup(gLumpBuffer, texture1Size, nullptr, 0u, gTextureNames[index], &info);
        if (error != R_ERR_OK) {
            ++gTextureErrors;
            continue;
        }

        const std::size_t patchBase =
            info.offset + static_cast<std::size_t>(R_TEXTURE_HEADER_SIZE);
        for (std::int32_t patch = 0; patch < info.patchCount; ++patch) {
            const std::size_t at = patchBase + static_cast<std::size_t>(patch) * R_TEXTURE_PATCH_SIZE
                                   + kTexturePatchIndexOffset;
            if (gPatchRefCount >= kPatchRefCapacity) {
                gPatchRefOverflow = true;
                break;
            }
            gPatchRefs[gPatchRefCount] = readLe16(gLumpBuffer + at);
            ++gPatchRefCount;
        }
    }
}

/**
 * @brief Turns the kept PNAMES indices into patch lump indices in the plan.
 *
 * `gLumpBuffer` holds PNAMES at this point. The table is checked once before it
 * is indexed: a count that does not fit the lump is a refusal, not a wild read.
 */
void resolvePatchLumps(const whd_state_t* state, std::uint32_t pnamesSize) {
    if (pnamesSize < kPnamesHeaderBytes) {
        ++gPatchNameErrors;
        return;
    }

    const std::uint32_t count = readLe32(gLumpBuffer);
    const std::uint64_t needed =
        static_cast<std::uint64_t>(kPnamesHeaderBytes)
        + static_cast<std::uint64_t>(count) * static_cast<std::uint64_t>(kPnamesNameStride);
    if (needed > static_cast<std::uint64_t>(pnamesSize)) {
        ++gPatchNameErrors;
        return;
    }

    for (int index = 0; index < gPatchRefCount; ++index) {
        const std::uint16_t ref = gPatchRefs[index];
        if (ref >= count) {
            ++gPatchNameErrors;
            continue;
        }

        char name[kNameBytes];
        copyLumpName(gLumpBuffer + kPnamesHeaderBytes
                         + static_cast<std::size_t>(ref) * kPnamesNameStride,
                     name);
        const int lump = doom_whd_source_lump_index(state, name);
        if (lump < 0) {
            ++gMissingPatches;
            continue;
        }
        appendPlan(lump, kPhasePatches);
    }
}

/// Proves a patch or a flat really decodes as one, through r_data, after the
/// timed read. This is the probe saying "the bytes were usable", not only "the
/// bytes came back".
bool validateGraphic(std::uint8_t phase, std::uint32_t bytes) {
    if (phase == kPhasePatches) {
        r_patch_info_t info;
        return r_patch_validate(gLumpBuffer, bytes, &info) == R_ERR_OK;
    }
    if (phase == kPhaseFlats) {
        return r_flat_validate(gLumpBuffer, bytes) == R_ERR_OK;
    }
    return true;
}

void resetDiscovery() {
    gPlanCount = 0;
    gPlanOverflow = false;
    gTextureCount = 0;
    gTextureOverflow = false;
    gFlatCount = 0;
    gFlatOverflow = false;
    gPatchRefCount = 0;
    gPatchRefOverflow = false;
    gMissingMapLumps = 0;
    gMissingPatches = 0;
    gMissingFlats = 0;
    gTextureErrors = 0;
    gPatchNameErrors = 0;
    gInvalidGraphics = 0;
    gMissingTextureDirectory = 0;
}

void resetPhaseAccumulators() {
    flashProfileReset(&gCold);
    flashProfileReset(&gWarm);
    for (int phase = 0; phase < kPhaseCount; ++phase) {
        flashProfileReset(&gPhase[phase]);
    }
}

/**
 * @brief The measured cold pass: discover the working set and read it once.
 *
 * Discovery is interleaved with measurement on purpose. The texture and flat
 * names come from SIDEDEFS and SECTORS, which the pass reads anyway; resolving
 * TEXTURE1 and PNAMES before reading the graphics means the plan is complete
 * before the patch and flat reads start, so no lump is read twice and nothing is
 * pre-warmed by a discovery step of its own.
 */
void runColdPass(const whd_state_t* state) {
    // 1. The map. The marker E1M1 is a real zero-byte read and is measured as one.
    for (int index = 0; index < kMapLumpCount; ++index) {
        const char* const name = kMapLumpNames[index];
        const int lump = doom_whd_source_lump_index(state, name);
        if (lump < 0) {
            ++gMissingMapLumps;
            continue;
        }

        const PlanEntry entry = {lump, static_cast<std::uint8_t>(kPhaseMap)};
        appendPlan(entry.lump, entry.phase);
        const ReadOutcome outcome = readOne(state, entry, &gCold);
        if (outcome.code != DOOM_WHD_SOURCE_OK) {
            continue;
        }

        // The buffer still holds this lump, so the names cost no extra read.
        if (std::strcmp(name, "SIDEDEFS") == 0) {
            collectSidedefTextures(outcome.bytes);
        } else if (std::strcmp(name, "SECTORS") == 0) {
            collectSectorFlats(outcome.bytes);
        }
    }

    // 2. TEXTURE1, resolved while its definitions are the buffer's contents.
    const int texture1 = doom_whd_source_lump_index(state, "TEXTURE1");
    if (texture1 >= 0) {
        const PlanEntry entry = {texture1, static_cast<std::uint8_t>(kPhaseTextureDirectory)};
        appendPlan(entry.lump, entry.phase);
        const ReadOutcome outcome = readOne(state, entry, &gCold);
        if (outcome.code == DOOM_WHD_SOURCE_OK) {
            resolveTextures(outcome.bytes);
        }
    } else {
        ++gMissingTextureDirectory;
    }

    // 3. PNAMES, resolved while it is the buffer's contents.
    const int pnames = doom_whd_source_lump_index(state, "PNAMES");
    if (pnames >= 0) {
        const PlanEntry entry = {pnames, static_cast<std::uint8_t>(kPhaseTextureDirectory)};
        appendPlan(entry.lump, entry.phase);
        const ReadOutcome outcome = readOne(state, entry, &gCold);
        if (outcome.code == DOOM_WHD_SOURCE_OK) {
            resolvePatchLumps(state, outcome.bytes);
        }
    } else {
        ++gMissingTextureDirectory;
    }

    // 4. Flats. The plan is patches then flats, so the graphics loop below reads
    //    them in that order and every phase line covers a contiguous run.
    for (int index = 0; index < gFlatCount; ++index) {
        const int lump = doom_whd_source_lump_index(state, gFlatNames[index]);
        if (lump < 0) {
            ++gMissingFlats;
            continue;
        }
        appendPlan(lump, static_cast<std::uint8_t>(kPhaseFlats));
    }

    // 5. Every planned lump that is not map or texture directory: the pixels.
    //    Patches and flats were appended in that order, so this run reads them
    //    in plan order and each phase line covers a contiguous run of the plan.
    for (int index = 0; index < gPlanCount; ++index) {
        const std::uint8_t phase = gPlan[index].phase;
        if (phase != kPhasePatches && phase != kPhaseFlats) {
            continue;
        }
        const ReadOutcome outcome = readOne(state, gPlan[index], &gCold);
        if (outcome.code == DOOM_WHD_SOURCE_OK && validateGraphic(phase, outcome.bytes) == false) {
            ++gInvalidGraphics;
        }
    }
}

/// The warm control: the identical sequence, in plan order, into the same buffer.
void runWarmPass(const whd_state_t* state) {
    for (int index = 0; index < gPlanCount; ++index) {
        (void)readOne(state, gPlan[index], &gWarm);
    }
}

int countPhase(std::uint8_t phase) {
    int count = 0;
    for (int index = 0; index < gPlanCount; ++index) {
        if (gPlan[index].phase == phase) {
            ++count;
        }
    }
    return count;
}

/// Emits the pass summary and then one line per phase, through the gate.
void reportPass(const char* passLabel, const FlashProfileAccumulator& pass) {
    FlashProfileReport<kFlashProfileEnabled>::pass(pass, passLabel, "all", &writeLine);
    for (int phase = 0; phase < kPhaseCount; ++phase) {
        FlashProfileReport<kFlashProfileEnabled>::pass(gPhase[phase], passLabel, kPhaseNames[phase],
                                                       &writeLine);
    }
}

/// One line stating what the plan contains and what discovery could not resolve.
void reportWorkingset() {
    unsigned overflow = 0u;
    if (gPlanOverflow) {
        overflow |= 1u;
    }
    if (gTextureOverflow) {
        overflow |= 2u;
    }
    if (gFlatOverflow) {
        overflow |= 4u;
    }
    if (gPatchRefOverflow) {
        overflow |= 8u;
    }

    char line[kFlashProfileLineCapacity];
    std::snprintf(line, sizeof line,
                  "WADPROF,workingset,lumps=%d,map=%d,texdir=%d,patches=%d,flats=%d,textures=%d,"
                  "patch_refs=%d,overflow=%u,missing_map=%d,missing_texdir=%d,missing_patches=%d,"
                  "missing_flats=%d,texture_errors=%d,patch_name_errors=%d,invalid=%d\n",
                  gPlanCount, countPhase(static_cast<std::uint8_t>(kPhaseMap)),
                  countPhase(static_cast<std::uint8_t>(kPhaseTextureDirectory)),
                  countPhase(static_cast<std::uint8_t>(kPhasePatches)),
                  countPhase(static_cast<std::uint8_t>(kPhaseFlats)), gTextureCount, gPatchRefCount,
                  overflow, gMissingMapLumps, gMissingTextureDirectory, gMissingPatches,
                  gMissingFlats, gTextureErrors, gPatchNameErrors, gInvalidGraphics);
    writeLine(line);
}

}  // namespace

void flashProfileRunProbe() {
    whd_state_t state;
    const int openCode = doom_whd_source_open(&state);
    char line[kFlashProfileLineCapacity];

    if (openCode != DOOM_WHD_SOURCE_OK) {
        std::snprintf(line, sizeof line, "WADPROF,probe=open,status=fail,code=%d\n", openCode);
        writeLine(line);
        return;
    }

    // `buf=` is this probe's static RAM cost, stated in its own log line.
    std::snprintf(line, sizeof line,
                  "WADPROF,probe=open,status=ok,lumps=%u,container=%lu,mapped=%u,buf=%u,"
                  "records=%d,plan=%d,frame_us=%u\n",
                  static_cast<unsigned>(doom_whd_source_lump_count(&state)),
                  static_cast<unsigned long>(state.size),
                  static_cast<unsigned>(doom_whd_source_mapped_size()),
                  static_cast<unsigned>(kLumpBufferBytes),
                  static_cast<int>(kFlashProfileRecordCapacity), static_cast<int>(kPlanCapacity),
                  static_cast<unsigned>(kFrameMicros));
    writeLine(line);

    resetPhaseAccumulators();
    resetDiscovery();

    runColdPass(&state);
    reportPass("cold", gCold);
    reportWorkingset();

    // The phase accumulators carry the last pass's readings, so the cold lines
    // have been written by now and the warm pass can overwrite them.
    for (int phase = 0; phase < kPhaseCount; ++phase) {
        flashProfileReset(&gPhase[phase]);
    }
    runWarmPass(&state);
    reportPass("warm", gWarm);

    const std::uint64_t coldMicros = gCold.stats.totalMicros;
    const std::uint64_t warmMicros = gWarm.stats.totalMicros;

    if (flashProfileFormatRatio(coldMicros, warmMicros, line, sizeof line) > 0u) {
        writeLine(line);
    }
    if (flashProfileFormatBudget(coldMicros, kFrameMicros, line, sizeof line) > 0u) {
        writeLine(line);
    }

    doom_whd_source_close();
}

}  // namespace doomc
}  // namespace doom

#elif defined(PIXELROOT32_DOOM_FLASH_PROFILE)

// Host build with the flag set: there is no partition to map and no
// `esp_timer.h`, so the probe has nothing to measure. The accumulator and the
// formatters above are what a host test drives.
namespace doom {
namespace doomc {

void flashProfileRunProbe() {}

}  // namespace doomc
}  // namespace doom

#endif
