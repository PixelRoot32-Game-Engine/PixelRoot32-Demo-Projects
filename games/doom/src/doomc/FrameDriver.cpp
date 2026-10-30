/*
 * D3.7's frame driver: see FrameDriver.h for the four phases, the two map paths,
 * the 20 views, the hash protocol and the exact line formats.
 *
 * The file has two halves and no third one, the same shape FlashProfile.cpp uses
 * for the same reason.
 *
 * The first half -- the accumulator, the two ratio helpers, the gate and the
 * three formatters -- is plain integer C++ with <cstdio> and nothing else. It is
 * compiled for every target because it is the half a host test can pin: the
 * exact line a parser reads, the means and extremes, the zero-sample case and
 * the gate boundary at exactly 20,000 us. `-ffunction-sections` plus
 * `--gc-sections` removes it from a build that never calls it, so a shipped image
 * pays nothing for the arithmetic existing.
 *
 * The second half is the board driver and only exists when
 * `PIXELROOT32_DOOM_FRAME_PROBE` is defined (`env:esp32dev_ILI9341_frame` and
 * `env:esp32dev_ILI9341_frameram`). It is the only code in this project that
 * renders a whole frame on the board, and it is deliberately the *naive* driver:
 * no cache, no reuse, no residency policy beyond "the current call". Improving it
 * is D3.5's job, and D3.5 measures against the numbers this file prints.
 *
 * WHAT THE NAIVE POLICY COSTS IN BYTES, AND WHERE THOSE BYTES COME FROM NOW
 *
 * Every buffer below is static .bss, because the task stack is 8 KB and the
 * largest E1M1 patch alone is 35,080 B. The total is printed by the probe's own
 * `FRAMEPROF,mem` line from `sizeof`-derived constants, so the RAM cost is a
 * reported number and not an estimate:
 *
 *   walk state     33,184 B   drawsegs 2,080, visplanes 14,608, clip posts 256,
 *                             floorclip/ceilingclip 1,280, clip blocks 9,600,
 *                             opening blocks 640, vis sprites 4,400, covered 320
 *   wall arena     36,864 B   two composed textures: 32,768 + 4,096, the
 *                             measured peak pair sum over the 20 views
 *   scratch        35,080 B   one patch, the largest patch an E1M1 texture
 *                             names (SKY1's, which the plane pass now composes);
 *                             also the flat slot and the sprite patch slot
 *   level              0 B    in the flash build: the map's 46,508 B and the
 *                             directories' 20,742 B are pointers into the
 *                             mapping. 47,888 B in the RAM build.
 *
 * THE TWO MAP PATHS
 *
 * `PIXELROOT32_DOOM_MAP_IN_RAM=1` decodes the map lumps into RAM, which is what
 * D3.6 did and what the A/B needs as its control; without it the map's lumps are
 * raw container entries and `doom_whd_source_lump_view()` hands out the mapped
 * bytes with no copy at all. The three graphics directories stay raw in both
 * builds on purpose: they are read-only tables, not the map, and holding them
 * constant isolates the one variable the A/B is about.
 *
 * THE SHARED SCRATCH, AND WHY IT IS SAFE HERE
 *
 * Three decoders produce small buffers that do not overlap in time: a patch
 * inside a texture compose, a flat, and a sprite patch. They share the 35,080 B
 * scratch with a single owner tag, so the resolver can never hand back a pointer
 * to a buffer some other decoder has since overwritten: a hit requires the owner
 * *and* the name to match, and every decode sets the owner. The one sequence that
 * could alias a live buffer -- a texture compose while a sprite patch is live --
 * is impossible in this frame order unless a masked mid texture is drawn during a
 * sprite's draw, which is exactly the case the probe counts in `hazards=` on the
 * summary line. A flat in the scratch when the sky is composed is not that case:
 * r_plane_draw() resolves every flat again immediately before it reads it, which
 * is why the counter watches the sprite patch only.
 */
#include "doomc/FrameDriver.h"

#include <cstdio>

namespace doom {
namespace doomc {

namespace {

/// The two verdict words, so the formatter and the test cannot disagree.
const char* const kGatePass = "pass";
const char* const kGateFail = "fail";
const char* const kGateNone = "none";

}  // namespace

void frameProfileReset(FrameProfileStats* stats) {
    if (stats == nullptr) {
        return;
    }
    *stats = FrameProfileStats{};
}

void frameProfileRecord(FrameProfileStats* stats, const FrameProfileSample& sample) {
    if (stats == nullptr) {
        return;
    }

    // The first sample defines both extremes, so a run whose frames are all
    // identical still reports that value as min and as max.
    if (stats->samples == 0u || sample.frameMicros < stats->frameMin) {
        stats->frameMin = sample.frameMicros;
    }
    if (stats->samples == 0u || sample.frameMicros > stats->frameMax) {
        stats->frameMax = sample.frameMicros;
    }

    ++stats->samples;
    stats->frameSum += sample.frameMicros;
    stats->walkSum += sample.walkMicros;
    stats->wallsSum += sample.wallsMicros;
    stats->planesSum += sample.planesMicros;
    stats->maskedSum += sample.maskedMicros;
    stats->residencySum += sample.residencyMicros;
    stats->bytesSum += sample.bytesDecoded;
}

std::uint32_t frameProfileMean(std::uint64_t sum, std::uint32_t samples) {
    if (samples == 0u) {
        // The mean of nothing is not a number this port prints.
        return 0u;
    }
    return static_cast<std::uint32_t>(sum / static_cast<std::uint64_t>(samples));
}

std::uint32_t frameProfilePermille(std::uint64_t part, std::uint64_t whole) {
    if (whole == 0u) {
        return 0u;
    }
    return static_cast<std::uint32_t>((part * 1000u) / whole);
}

bool frameProfileGatePass(std::uint32_t meanMicros) {
    // Inclusive on purpose: the gate is a budget, not a threshold to beat by one.
    return meanMicros <= kFrameGateMicros;
}

std::size_t frameProfileFormatFrame(const FrameProfileSample& sample, const char* hash, char* out,
                                    std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,frame=%02u,run=%u,walk_us=%u,walls_us=%u,planes_us=%u,masked_us=%u,"
        "residency_us=%u,frame_us=%u,drawsegs=%u,planes_drawn=%u,planes_alloc=%u,"
        "sprites_submitted=%u,sprites_drawn=%u,tex_res=%u,tex_dec=%u,flat_res=%u,flat_dec=%u,"
        "sprite_res=%u,sprite_dec=%u,bytes_decoded=%u,ovf=%u,hash=%s\n",
        static_cast<unsigned>(sample.frame), static_cast<unsigned>(sample.run),
        static_cast<unsigned>(sample.walkMicros), static_cast<unsigned>(sample.wallsMicros),
        static_cast<unsigned>(sample.planesMicros), static_cast<unsigned>(sample.maskedMicros),
        static_cast<unsigned>(sample.residencyMicros), static_cast<unsigned>(sample.frameMicros),
        static_cast<unsigned>(sample.drawsegs), static_cast<unsigned>(sample.planesDrawn),
        static_cast<unsigned>(sample.planesAllocated),
        static_cast<unsigned>(sample.spritesSubmitted), static_cast<unsigned>(sample.spritesDrawn),
        static_cast<unsigned>(sample.texResolves), static_cast<unsigned>(sample.texDecodes),
        static_cast<unsigned>(sample.flatResolves), static_cast<unsigned>(sample.flatDecodes),
        static_cast<unsigned>(sample.spriteResolves), static_cast<unsigned>(sample.spriteDecodes),
        static_cast<unsigned>(sample.bytesDecoded),
        static_cast<unsigned>(sample.drawsegOverflowed), hash != nullptr ? hash : "");

    // snprintf reports the length it would have written; anything at or past the
    // capacity was truncated, and a truncated line is not written at all.
    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

std::size_t frameProfileFormatSummary(const FrameProfileStats& stats, std::uint32_t views,
                                      std::uint32_t runs, char* out, std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const std::uint32_t frameMean = frameProfileMean(stats.frameSum, stats.samples);
    const std::uint32_t walk = frameProfileMean(stats.walkSum, stats.samples);
    const std::uint32_t walls = frameProfileMean(stats.wallsSum, stats.samples);
    const std::uint32_t planes = frameProfileMean(stats.planesSum, stats.samples);
    const std::uint32_t masked = frameProfileMean(stats.maskedSum, stats.samples);
    const std::uint32_t passes = walk + walls + planes + masked;
    // Integer means can round the phases above the whole by a microsecond or two;
    // the remainder is a count of work outside the four passes, never negative.
    const std::uint32_t other = frameMean > passes ? frameMean - passes : 0u;
    const std::uint32_t residency = frameProfileMean(stats.residencySum, stats.samples);

    // A summary with no sample has no verdict to give. Reporting "pass" on no
    // evidence would be the one dishonest field in the whole table, and "fail"
    // would blame a build that measured nothing; `none` says exactly that.
    const char* verdict = kGateNone;
    if (stats.samples != 0u) {
        verdict = frameProfileGatePass(frameMean) ? kGatePass : kGateFail;
    }

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,summary,views=%u,runs=%u,samples=%u,frame_mean_us=%u,frame_min_us=%u,"
        "frame_max_us=%u,walk_us=%u,walls_us=%u,planes_us=%u,masked_us=%u,other_us=%u,"
        "residency_us=%u,residency_permille=%u,gate_us=%u,gate=%s\n",
        static_cast<unsigned>(views), static_cast<unsigned>(runs),
        static_cast<unsigned>(stats.samples), static_cast<unsigned>(frameMean),
        static_cast<unsigned>(stats.frameMin), static_cast<unsigned>(stats.frameMax),
        static_cast<unsigned>(walk), static_cast<unsigned>(walls), static_cast<unsigned>(planes),
        static_cast<unsigned>(masked), static_cast<unsigned>(other),
        static_cast<unsigned>(residency),
        static_cast<unsigned>(frameProfilePermille(stats.residencySum, stats.frameSum)),
        static_cast<unsigned>(kFrameGateMicros), verdict);

    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

std::size_t frameProfileFormatBreakdown(const FrameProfileSample& sample, char* out,
                                        std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,breakdown,frame=%02u,run=%u,walk_us=%u,walls_us=%u,planes_us=%u,"
        "masked_us=%u,residency_us=%u,frame_us=%u,residency_permille=%u,drawsegs=%u\n",
        static_cast<unsigned>(sample.frame), static_cast<unsigned>(sample.run),
        static_cast<unsigned>(sample.walkMicros), static_cast<unsigned>(sample.wallsMicros),
        static_cast<unsigned>(sample.planesMicros),
        static_cast<unsigned>(sample.maskedMicros), static_cast<unsigned>(sample.residencyMicros),
        static_cast<unsigned>(sample.frameMicros),
        static_cast<unsigned>(frameProfilePermille(sample.residencyMicros, sample.frameMicros)),
        static_cast<unsigned>(sample.drawsegs));

    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

}  // namespace doomc
}  // namespace doom

// --- The board driver (D3.6) -------------------------------------------------

#if defined(PIXELROOT32_DOOM_FRAME_PROBE) && (defined(ESP32) || defined(ESP_PLATFORM))

#include <cstring>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "doomc/DoomConsole.h"
#include "doomc/WhdSource.h"

#include "doom_map.h"
#include "r_bsp.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_plane.h"
#include "r_segs.h"
#include "r_tables.h"
#include "r_things.h"

// The E1M1 sprite fixture: the DoomEd number -> frame-0 lump table
// tools/gen_e1m1_sprites.py generates for the host harness. Fixture data, not
// renderer code -- lib/doom/ never includes it -- and the frame environment puts
// tools/ on the include path so the board builds the same sprite list the host
// proves, from the same table, or the two hashes describe different frames.
#include "e1m1_sprites.inc"

namespace doom {
namespace doomc {

namespace {

// --- Capacities -------------------------------------------------------------

/*
 * Every capacity below is the caller's, and each was chosen from a measurement
 * rather than from upstream's default. The measured peak is stated next to it so
 * a later change is a decision with evidence.
 */

/// DOOM's MAXDRAWSEGS is 256 (13,312 B). 256 does not fit: with the map working
/// set, the two wall slots, the decode scratch and the walk state resident, the
/// array is one of the last elastic costs and the driver is tens of kilobytes
/// over the board's free DRAM at 256. 40 costs 2,080 B and the peak over the 20
/// sampled views is 39; the walk reports an overflow instead of writing past the
/// array, and the probe prints that flag per frame.
constexpr int kDrawsegCapacity = 40;

/// The measured peak *allocation* over the 20 sampled views is 22 (a plane is
/// allocated by the walk and may stay unmarked for the whole view). 64 is what
/// D3.1's byte counts and the plan's budget row assume, and 64 x 664 B = 42,496 B
/// is 28 KB the board does not have: with the map's working set, the two wall
/// slots and the decode scratch resident, a 64-plane view cannot be built at all.
/// 22 is the measured peak, allocates exactly what the host harness allocated for
/// every one of the 20 views, and leaves every pixel -- and therefore every hash
/// -- identical to the host's. An overflow is reported by the walk, not hidden.
constexpr int kPlaneCapacity = 22;

/// R_MAX_CLIP_SEGS, the minimum r_bsp_init() accepts (upstream's MAXSEGS).
constexpr int kClipRangeCapacity = 32;

/// The D3.4 clip-block pool: 15 blocks of 320 shorts, reserved two included, is
/// the measured peak, so 15 is the smallest correct capacity. One short and the
/// wall pass refuses the frame with R_SEGS_ERR_SMALL_CLIP_POOL instead of
/// writing past the pool. It is still needed without the masked pass: the wall
/// pass snapshots every drawseg's clip arrays before it knows what will be drawn.
constexpr int kClipBlockCapacity = 15;

/// The masked pass's pools, at their measured peaks: 97 accepted vis-sprite
/// entries (100 here) and zero recorded masked-mid columns (one block here, so a
/// view that records one allocates it instead of failing). Both are static,
/// because the heap has nothing to spare.
constexpr int kVisspriteCapacity = 100;
constexpr int kOpeningBlockCapacity = 1;

/// One THINGS record per sprite the level can place; E1M1's fixture resolves 129.
constexpr int kSpriteCapacity = 130;

// --- Buffer sizes -----------------------------------------------------------

/// E1M1's renderer lumps minus THINGS (LINEDEFS 6,650 + SIDEDEFS 19,440 +
/// VERTEXES 1,868 + SEGS 8,784 + SSECTORS 948 + NODES 6,608 + SECTORS 2,210 =
/// 46,508). In the RAM build these are decoded, one block each; in the flash build
/// they are pointers into the container. THINGS (1,380) is a view in both builds.
///
/// `kLevelReleasedBytes` is what a frame stops costing DRAM: the 46,508 B of map
/// plus the 20,742 B of COLORMAP, TEXTURE1 and PNAMES (8,704 + 9,234 + 2,804) in
/// the flash build, and only the directories in the RAM build, which still decodes
/// the map. `FRAMEPROF,mem` prints it, so "the level costs nothing" is a number.
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
constexpr std::size_t kLevelReleasedBytes = 20742u;
#else
constexpr std::size_t kLevelReleasedBytes = 46508u + 20742u;
#endif

/// One static graphics region, big enough for the buffer that has to live in
/// .bss. 36,864 B holds the wall arena exactly and the 35,080 B scratch with
/// 1,784 B to spare, so either can be the static one.
constexpr std::size_t kStaticGraphicsBytes = 36864u;

/// The composed-texture arena. Two slots, because a two-sided fragment resolves
/// its upper and lower tier before the column loop and holds both: one slot would
/// be a wrong frame, not a slow one. The extent is the measured peak pair over the
/// 20 views -- 32,768 B (a 256-wide texture, stored with DOOM's 128-row column)
/// plus 4,096 B (a 32-wide one) -- so 36,864 B total.
///
/// The two slots are packed from opposite ends: slot 0 starts at byte 0 and slot 1
/// ends at the arena's last byte. Any pair whose sum fits therefore coexists,
/// which a fixed 32,768/4,096 split does not: it refused BROWN144 (18,432 B) next
/// to a 16,384 B wall on the board, a pair that needs 34,816 of the 36,864.
constexpr std::size_t kWallArenaBytes = 36864u;

/// One patch of a composite. 35,080 B is SKY1's largest patch, which the plane
/// pass composes now that it runs; it is also larger than every wall texture's
/// largest patch (17,544 B), so the wall pass and the masked pass share it. A
/// flat (4,096 B) and a sprite patch (4,272 B) fit with room to spare.
constexpr std::size_t kScratchBytes = 35080u;

/// The graphics blocks: the wall arena and the decode scratch, side by side in one
/// static region when both are static, or split between .bss and the heap by the
/// placement the probe reports.
constexpr std::size_t kGraphicsBlockBytes = kWallArenaBytes + kScratchBytes;

/// Up to nine extra lump blocks from the heap: the blocks of the decoded level
/// that the static arena does not hold, in the RAM build.
constexpr int kExtraLumpBlocks = 9;

#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
/// How much of the decoded level the static arena holds. `dram0_0_seg` is
/// 124,068 B and this build's other .bss is 101,640 B (the flash build's 97,368
/// plus the 4,272 B sprite patch), so 22,428 B is the hard ceiling; 20,480 leaves
/// slack for the framework. It holds SIDEDEFS (19,448) and SSECTORS (952) and
/// sends the rest -- 30,070 B -- to the heap, which the probe's `heap_before`
/// shows has room.
constexpr std::size_t kStaticLumpBytes = 20480u;
#endif

/// The static region this image can use for data: `dram0_0_seg` is 124,068 B
/// (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c) and the image used ~29 KB of it
/// before this file. Reported so the shortfall line can say what the pool is, not
/// only what was asked of it.
constexpr std::size_t kStaticRegionBytes = 124068u;
/// DOOM's stored column is 128 rows whatever the texture's declared height,
/// because R_DrawColumn() wraps with a hard-coded `& 127`. A composed texture is
/// therefore width * 128 bytes, with the rows above the declared height zeroed
/// (which is what the host harness's calloc'd buffer holds, so the two agree
/// byte for byte).
constexpr std::size_t kStoredColumnRows = 128u;

/// A flat has no header and no length field: 4,096 bytes are the whole format.
constexpr std::size_t kFlatBytes = 4096u;

/// The largest sprite patch the fixture names (ELECA0, type 48). The scratch is
/// bigger than this, so a sprite patch is decoded into the scratch.
constexpr std::size_t kSpritePatchBytes = 4272u;

/// The views, the runs and the colours, exactly as tools/rsegs_check.c has them.
constexpr int kViewCount = 20;
constexpr int kMeasuredRuns = 2;
constexpr int kTotalRuns = 1 + kMeasuredRuns;
constexpr int kPositionCount = 4;
constexpr int kAngleCount = 5;

/// The level's sky flat and sky texture, packed from their WAD names.
constexpr std::uint8_t kSkyFlatName[8] = {'F', '_', 'S', 'K', 'Y', '1', 0, 0};
constexpr std::uint8_t kSkyTextureName[8] = {'S', 'K', 'Y', '1', 0, 0, 0, 0};

/// Which of the level's lumps the driver keeps. E1M1 is the zero-byte marker and
/// the ten data lumps follow it in WAD order (THINGS LINEDEFS SIDEDEFS VERTEXES
/// SEGS SSECTORS NODES SECTORS REJECT BLOCKMAP). REJECT and BLOCKMAP are the
/// simulation's and the renderer never reads them, so the slots from
/// DOOM_MAP_REJECT on are left empty and nothing is bound or decoded for them.
constexpr int kMapLumpCount = 1 + DOOM_MAP_LUMP_COUNT;
constexpr int kFirstSkippedMapLump = DOOM_MAP_REJECT;

// --- Static buffers ---------------------------------------------------------

/*
 * WHERE THE BUFFERS LIVE, AND WHY IT IS STILL TWO POOLS
 *
 * The two pools on this board are not additive and neither alone is enough, so
 * the split below is a measurement, not a preference:
 *
 *   dram0_0_seg   the static data region the linker enforces: 124,068 B
 *                 (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c). D3.6's build used
 *                 120,536 B of it, leaving 3,532 B; removing the 23,200 B decoded
 *                 level arena is what makes room for the 17,536 B the plane pass
 *                 adds to the scratch.
 *   the heap      what is left of DRAM after the static data. With the display
 *                 path live the probe reports ~48 KB free and one hole of ~47 KB;
 *                 that hole is where the scratch goes.
 *
 * `DriverState` is the static half; the wall arena and the scratch are the
 * placement half, reported by `FRAMEPROF,mem` so which pool served them is a
 * printed number. The decoded level is a third case: .bss in the RAM build (its
 * own array), absent in the flash build because the container is the storage.
 */

/// The static half: the walk state, the clip-block pool and the coverage record.
struct DriverState {
    /// The walk's state, all of it caller-owned as r_bsp.h requires.
    alignas(8) r_drawseg_t drawsegs[kDrawsegCapacity];
    alignas(8) r_visplane_t planes[kPlaneCapacity];
    alignas(8) r_clip_range_t clipRanges[kClipRangeCapacity];
    alignas(8) int16_t floorClip[R_SCREEN_WIDTH];
    alignas(8) int16_t ceilingClip[R_SCREEN_WIDTH];
    /// The wall pass's clip-block pool (the D3.4 snapshot).
    alignas(8) int16_t clipBlocks[kClipBlockCapacity * R_SCREEN_WIDTH];
    alignas(8) int16_t openingBlocks[kOpeningBlockCapacity * R_SCREEN_WIDTH];
    alignas(8) r_vissprite_t visSprites[kVisspriteCapacity];
    /// The per-column coverage record the frame hash needs: an uncovered column
    /// is 0, a real palette index writes 1, so the pixels alone cannot say
    /// whether a column was written.
    alignas(8) std::uint8_t covered[R_SCREEN_WIDTH];
};

/// The static half, and the only unconditional new .bss this file adds.
DriverState gState;

/// The two large blocks.
std::uint8_t* gWallBytes = nullptr;
std::uint8_t* gScratch = nullptr;
std::uint8_t* gGraphics = nullptr;

/// The extra lump blocks from the heap: the decoded level's blocks that the static
/// arena does not hold. Each is 8,784 B or smaller.
std::uint8_t* gLumpBlocks[kExtraLumpBlocks] = {};
int gLumpBlockCount = 0;

/// The sprite list, from the heap: 130 records, where the static region has no
/// room left for it.
r_sprite_t* gSprites = nullptr;

/**
 * @brief Releases whatever the probe took. Safe on any subset.
 *
 * The heap has no single hole large enough for every block, so the level and the
 * graphics are allocated one piece at a time -- see heapLump() and the placement
 * block in frameProbeRun() -- and each is released here exactly once.
 */
void releaseWorkingSet() {
    gScratch = nullptr;
    gWallBytes = nullptr;
    if (gGraphics != nullptr) {
        heap_caps_free(gGraphics);
        gGraphics = nullptr;
    }
    if (gSprites != nullptr) {
        heap_caps_free(gSprites);
        gSprites = nullptr;
    }
    for (int index = 0; index < gLumpBlockCount; ++index) {
        heap_caps_free(gLumpBlocks[index]);
        gLumpBlocks[index] = nullptr;
    }
    gLumpBlockCount = 0;
}

/// How many bytes of the level the setup committed to RAM: 0 in the flash build,
/// the decoded map's size in the RAM build. Printed as `level_ram=`.
std::size_t gLevelRamUsed = 0;

#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
/// The static part of the decoded level. `PIXELROOT32_DOOM_MAP_IN_RAM` is the
/// A/B's control: it keeps D3.6's data path (the map decoded into DRAM) so the
/// two builds differ in exactly one thing.
alignas(8) std::uint8_t gLumpArena[kStaticLumpBytes];
#endif

/// The one graphics buffer that has to be static; which one is decided at boot.
alignas(8) std::uint8_t gStaticGraphics[kStaticGraphicsBytes];

/// The placement the probe ended up using, printed by `FRAMEPROF,mem`:
/// "wall-static", "scratch-static" or "heap-both".
const char* gPlacement = "none";

/// The map path this build uses, printed by every `FRAMEPROF,mem` and
/// `FRAMEPROF,setup` line so a captured log says which of the two it is.
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
const char* const kMapPath = "ram";
#else
const char* const kMapPath = "flash";
#endif

// --- Reader and per-frame state ---------------------------------------------

whd_state_t gWhd;

/// How many lumps the setup got as mapped pointers instead of decoded bytes, and
/// how many it asked for and had to refuse because the container is not raw.
/// Printed by `FRAMEPROF,setup`, so a container regenerated without the raw
/// section says so instead of quietly costing RAM.
std::uint32_t gRawViews = 0;
std::uint32_t gNotRaw = 0;

const std::uint8_t* gTexture1 = nullptr;
std::size_t gTexture1Bytes = 0;
const std::uint8_t* gPnames = nullptr;
std::size_t gPnamesBytes = 0;
const std::uint8_t* gColormap = nullptr;
std::size_t gColormapBytes = 0;
r_light_t gLight = {};

/// The measured sample being filled. Every resolver folds into it, so a counter
/// cannot be written by one pass and forgotten by the next.
FrameProfileSample gSample;

/// The error code of the pass that refused the last frame, printed by
/// `FRAMEPROF,error` so a refusal names the code instead of only the stage.
int gStageCode = 0;

/// How deep the decode call stack is. Only the outermost decode adds to
/// residencyMicros, so a compose that decodes six patches does not count their
/// time twice.
int gDecodeDepth = 0;

/// One composed-texture slot. `offset` is inside gWallBytes; `bytes` is
/// width * 128; `stamp` orders the two so a resolve never evicts the entry the
/// caller is about to use.
struct WallSlot {
    r_walltex_t tex = {};
    r_name_t name = R_NAME_NONE;
    std::uint32_t offset = 0;
    std::uint32_t bytes = 0;
    std::uint32_t stamp = 0;
    bool used = false;
};

WallSlot gWall[2];
std::uint32_t gStamp = 0;
int gNewest = -1;

/// The shared scratch's owner. A hit requires the owner *and* the name, so a
/// pointer can never be handed back after another decoder overwrote it.
///
/// The sprite patch is NOT part of this sharing any more: R_DrawMasked() renders
/// the masked mid textures a sprite hides *inside* that sprite's draw, so a
/// compose can run while a sprite patch is live, and one 35,080 B scratch cannot
/// hold both. D3.6 never saw it because the masked pass never ran; the board
/// counts it in `hazards=`. The sprite patch therefore has its own buffer.
enum ScratchOwner : std::uint8_t {
    kOwnerNone = 0,   ///< nothing valid in the scratch
    kOwnerPatch = 1,  ///< a texture compose's patch bytes
    kOwnerFlat = 2    ///< the flat slot
};

std::uint8_t gScratchOwner = kOwnerNone;
r_name_t gFlatName = R_NAME_NONE;
r_flat_t gFlat = {};

/// The sprite patch slot, separate from the shared scratch for the reason the
/// owner tag documents. 4,272 B: the largest sprite patch the fixture names
/// (ELECA0, type 48).
alignas(8) std::uint8_t gSpritePatchBytes[kSpritePatchBytes];
r_sprite_patch_t gSpritePatch = {nullptr, 0u};
r_name_t gSpriteName = R_NAME_NONE;

/// 1 while r_plane_draw() is running. The plane pass resolves flats and composes
/// the sky in the same scan, and it re-resolves every flat immediately before it
/// reads it, so a compose that finds a flat in the scratch there is the design.
/// Anywhere else it is an aliasing violation, and `hazards=` counts it.
int gPlanePass = 0;

/// The aliasing violations the probe saw: a texture compose that took the shared
/// scratch while a flat was live outside the plane pass. Must stay 0; a non-zero
/// value means a pixel could have come from another buffer's bytes.
std::uint32_t gHazards = 0;

// --- Timing -----------------------------------------------------------------

std::int64_t nowMicros() {
    // The ESP-IDF microsecond counter: integer, 64-bit, monotonic for the life of
    // a boot. A difference of two samples never wraps, which is why the sample is
    // taken in this width and only the per-phase results are narrowed to 32 bits.
    return esp_timer_get_time();
}

std::uint32_t elapsedMicros(std::int64_t start) {
    const std::int64_t elapsed = nowMicros() - start;
    return elapsed > 0 ? static_cast<std::uint32_t>(elapsed) : 0u;
}

// --- Console ----------------------------------------------------------------

/// One line, through the port's console, with the growth check every formatter
/// in this project uses: a truncated line is worse than no line.
void emitLine(const char* line) {
    if (line != nullptr && line[0] != '\0') {
        writeLine(line);
    }
}

void emitFormatted(std::size_t length, const char* line) {
    if (length > 0u) {
        emitLine(line);
    }
}

// --- Lump decoding ----------------------------------------------------------

/**
 * @brief Decodes one container lump into a buffer and folds it into the sample.
 *
 * This is the only place a lump is read, so `bytes_decoded` and `residency_us`
 * have exactly one writer per decode. A refusal is reported to the caller and
 * still costs the time it cost.
 */
bool decodeLump(int lump, std::uint8_t* out, std::size_t capacity, std::uint32_t* used) {
    const bool outermost = (gDecodeDepth == 0);
    const std::int64_t start = nowMicros();

    std::uint32_t required = 0u;
    ++gDecodeDepth;
    const int code = doom_whd_source_read_lump(&gWhd, lump, out, capacity, &required);
    --gDecodeDepth;

    if (outermost) {
        gSample.residencyMicros += elapsedMicros(start);
    }
    if (code != DOOM_WHD_SOURCE_OK) {
        return false;
    }
    if (used != nullptr) {
        *used = required;
    }
    gSample.bytesDecoded += required;
    return true;
}

/**
 * @brief Binds one lump as a pointer into the mapping, or reports that it is not raw.
 *
 * No decode, no copy, no allocation: this is the whole reason the container grew
 * a raw section. It costs no measured time either -- a mapped read is as cheap as
 * a RAM read until the flash cache misses, and the miss is charged to whichever
 * pass touches the bytes, which is the number the A/B is after.
 */
bool viewLump(int lump, const std::uint8_t** data, std::size_t* size) {
    std::uint32_t bytes = 0u;
    if (doom_whd_source_lump_view(&gWhd, lump, data, &bytes) != DOOM_WHD_SOURCE_OK) {
        ++gNotRaw;
        *data = nullptr;
        *size = 0u;
        return false;
    }
    *size = static_cast<std::size_t>(bytes);
    ++gRawViews;
    return true;
}

// --- Wall textures ----------------------------------------------------------

/// r_texture_compose()'s patch resolver: a PNAMES name is a lump name.
r_error_t fetchPatch(void* user, const char* name, const std::uint8_t** data, std::size_t* size) {
    (void)user;

    const int lump = doom_whd_source_lump_index(&gWhd, name);
    if (lump < 0) {
        return R_ERR_PATCH_FETCH;
    }

    // The plane pass resolves every flat again immediately before it reads it, so
    // a flat in the scratch there is the design. Anywhere else a flat still in the
    // scratch means a compose is about to overwrite bytes someone may read.
    if (gScratchOwner == kOwnerFlat && gPlanePass == 0) {
        ++gHazards;
    }
    gScratchOwner = kOwnerPatch;

    std::uint32_t required = 0u;
    if (!decodeLump(lump, gScratch, kScratchBytes, &required)) {
        return R_ERR_PATCH_FETCH;
    }
    *data = gScratch;
    *size = required;
    return R_ERR_OK;
}

/// True when a texture of @p bytes can live in @p slot beside whatever the other
/// slot holds. A texture never has to fit beside itself: placing it releases the
/// slot it is replacing.
bool slotFits(int slot, std::uint32_t bytes) {
    const int other = 1 - slot;
    const std::uint32_t otherBytes = gWall[other].used ? gWall[other].bytes : 0u;
    return bytes + otherBytes <= static_cast<std::uint32_t>(kWallArenaBytes);
}

/// Chooses the slot a texture of @p bytes goes in, or -1 when neither can hold it.
/// The most recently returned slot is never the victim: a fragment resolves its
/// tiers one after the other and then reads both.
int wallVictim(std::uint32_t bytes) {
    // An unused slot first, then the older of the two. `gNewest` is the slot
    // handed out most recently and is skipped whenever there is an alternative,
    // because a two-sided fragment resolves its upper and lower tier one after the
    // other and reads both in the same column loop.
    const int preferred = !gWall[0].used ? 0 : (!gWall[1].used ? 1 : (gNewest != 0 ? 0 : 1));
    const int fallback = 1 - preferred;

    if (slotFits(preferred, bytes)) {
        return preferred;
    }
    if (slotFits(fallback, bytes)) {
        return fallback;
    }
    return -1;
}

/**
 * @brief Composes one texture into a slot, in DOOM's column-major stored form.
 *
 * r_texture_compose() writes `pixel(x, y)` at `x * height + y`, which is the
 * composite layout; the column writer wants DOOM's stored 128-row column, so the
 * columns are expanded in place, backwards, and the rows above the declared
 * height are zeroed. Backwards works because every destination starts at or above
 * every remaining source, and the zeroing above a column's declared height cannot
 * reach a column not yet moved.
 */
bool composeInto(int slot, const char* name, const r_texture_info_t& info) {
    const std::size_t height = static_cast<std::size_t>(info.height);
    const std::uint32_t bytes =
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(info.width) * kStoredColumnRows);
    // Slot 0 grows up from the arena's first byte, slot 1 down from its last, so
    // the pair only has to add up to the arena, not match a fixed split.
    const std::uint32_t offset =
        slot == 0 ? 0u : static_cast<std::uint32_t>(kWallArenaBytes - bytes);
    std::uint8_t* base = gWallBytes + offset;

    const bool outermost = (gDecodeDepth == 0);
    const std::int64_t start = nowMicros();

    ++gDecodeDepth;
    const r_error_t error =
        r_texture_compose(&info, gPnames, gPnamesBytes, fetchPatch, nullptr, base,
                          static_cast<std::size_t>(info.width) * height, nullptr, 0u, nullptr);
    --gDecodeDepth;

    if (outermost) {
        gSample.residencyMicros += elapsedMicros(start);
    }
    if (error != R_ERR_OK) {
        return false;
    }
    gSample.bytesDecoded += static_cast<std::uint32_t>(static_cast<std::uint32_t>(info.width)
                                                       * static_cast<std::uint32_t>(height));

    int x;
    for (x = info.width - 1; x >= 0; --x) {
        std::uint8_t* destination = base + static_cast<std::size_t>(x) * kStoredColumnRows;
        const std::uint8_t* source = base + static_cast<std::size_t>(x) * height;

        if (destination != source) {
            std::memmove(destination, source, height);
        }
        std::memset(destination + height, 0, kStoredColumnRows - height);
    }

    WallSlot& entry = gWall[slot];
    entry.name = r_pack_name(reinterpret_cast<const std::uint8_t*>(name));
    entry.offset = offset;
    entry.bytes = bytes;
    entry.used = true;
    entry.stamp = ++gStamp;
    entry.tex.pixels = base;
    entry.tex.mask = nullptr;
    entry.tex.width = static_cast<std::int16_t>(info.width);
    entry.tex.height = static_cast<std::int16_t>(info.height);
    entry.tex.widthMask = r_texture_width_mask(info.width);
    entry.tex.heightMask = static_cast<std::int16_t>(kStoredColumnRows - 1u);
    gNewest = slot;
    return true;
}

/// The most recent texture a resolve refused, and why: `lookup` (the name is not
/// in the container's TEXTURE1), `slot` (the two live textures do not fit the
/// arena) or `compose` (the patch fetch or the composite failed). Printed by
/// `FRAMEPROF,error`, because "a texture is missing" is not a diagnosis.
char gFailTexture[9] = {0};
const char* gFailWhy = "none";

/// r_tex_source_t::resolve: the naive wall policy, documented in the header.
const r_walltex_t* resolveWall(void* context, r_name_t name) {
    (void)context;

    ++gSample.texResolves;
    for (int index = 0; index < 2; ++index) {
        if (gWall[index].used && gWall[index].name == name) {
            gNewest = index;
            return &gWall[index].tex;
        }
    }

    ++gSample.texDecodes;
    char text[9];
    r_unpack_name(name, text);
    std::memcpy(gFailTexture, text, sizeof text);

    r_texture_info_t info;
    if (r_texture_lookup(gTexture1, gTexture1Bytes, nullptr, 0u, text, &info) != R_ERR_OK) {
        gFailWhy = "lookup";
        return nullptr;
    }

    const std::uint32_t bytes = static_cast<std::uint32_t>(info.width) * kStoredColumnRows;
    const int slot = wallVictim(bytes);
    if (slot < 0) {
        // The frame's two live textures do not fit the arena. Reported by the
        // caller as R_SEGS_ERR_TEXTURE_MISSING, which is honest: nothing was
        // written for that tier.
        gFailWhy = "slot";
        return nullptr;
    }
    if (!composeInto(slot, text, info)) {
        gFailWhy = "compose";
        return nullptr;
    }
    return &gWall[slot].tex;
}

// --- Flats and sprite patches -----------------------------------------------

/// r_tex_source_t::resolveFlat: decode into the shared scratch, nothing more.
///
/// The scratch is also the patch buffer, so a compose takes it and invalidates the
/// flat. That is safe by the order of the passes, not by luck: the plane pass
/// resolves one flat per drawable plane and reads it before the next resolve, and
/// the only compose it can reach is the sky's, in its own preflight. A violation
/// is counted in `hazards=`, not assumed away.
const r_flat_t* resolveFlat(void* context, r_name_t name) {
    (void)context;

    ++gSample.flatResolves;
    if (gScratchOwner == kOwnerFlat && gFlatName == name) {
        return &gFlat;
    }

    ++gSample.flatDecodes;
    char text[9];
    r_unpack_name(name, text);
    const int lump = doom_whd_source_lump_index(&gWhd, text);
    if (lump < 0) {
        return nullptr;
    }
    std::uint32_t used = 0u;
    if (!decodeLump(lump, gScratch, kFlatBytes, &used)) {
        return nullptr;
    }
    if (used != kFlatBytes) {
        return nullptr;
    }

    gScratchOwner = kOwnerFlat;
    gFlatName = name;
    gFlat.pixels = gScratch;
    gFlat.width = R_FLAT_SIZE;
    gFlat.height = R_FLAT_SIZE;
    gFlat.widthMask = R_FLAT_SIZE - 1;
    gFlat.heightMask = R_FLAT_SIZE - 1;
    return &gFlat;
}

/// r_sprite_source_t::resolve: a sprite lump is its raw patch bytes, decoded into
/// the sprite patch slot. It cannot share the scratch: R_DrawMasked() renders the
/// masked mid textures a sprite hides inside that sprite's own draw, so a compose
/// can take the scratch while the patch is live. The slot is one buffer because
/// the pass resolves one sprite at a time; `gSpriteName` is the cache key.
const r_sprite_patch_t* resolveSprite(void* context, r_name_t lump) {
    (void)context;

    ++gSample.spriteResolves;
    if (gSpriteName == lump) {
        return &gSpritePatch;
    }

    ++gSample.spriteDecodes;
    char text[9];
    r_unpack_name(lump, text);
    const int index = doom_whd_source_lump_index(&gWhd, text);
    if (index < 0) {
        return nullptr;
    }
    std::uint32_t used = 0u;
    if (!decodeLump(index, gSpritePatchBytes, kSpritePatchBytes, &used)) {
        return nullptr;
    }
    gSpriteName = lump;
    gSpritePatch.data = gSpritePatchBytes;
    gSpritePatch.size = static_cast<std::size_t>(used);
    return &gSpritePatch;
}

// --- The 20 sampled viewpoints ----------------------------------------------

/// One viewpoint, as tools/rsegs_check.c's frame_view_t holds it.
struct FrameView {
    fixed_t x;
    fixed_t y;
    fixed_t z;
    angle_t angle;
};

bool isPlayerStart(std::int16_t type) {
    return type == 1 || type == 2 || type == 3 || type == 4 || type == 11;
}

/// The spawn rule's z: the floor of the subsector at (x, y) plus VIEWHEIGHT.
bool positionView(const doom_map_t* map, fixed_t x, fixed_t y, FrameView* out) {
    std::uint16_t subsector = 0u;
    doom_subsector_t sub;
    doom_sector_t record;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return false;
    }
    doom_map_get_subsector(map, subsector, &sub);
    doom_map_get_sector(map, doom_map_subsector_sector(map, &sub), &record);
    out->x = x;
    out->y = y;
    out->z = record.floorHeight + R_VIEWHEIGHT;
    out->angle = 0;
    return true;
}

/// Fixture code: R_PointToAngle(), copied from the walk's private helper because
/// the sprite rotation rule needs it and the driver must not reach into lib/doom's
/// internals. The renderer never selects a rotation.
angle_t pointToAngle(const r_view_params_t* view, fixed_t x, fixed_t y) {
    fixed_t dx = static_cast<fixed_t>(static_cast<std::uint32_t>(x) - static_cast<std::uint32_t>(view->x));
    fixed_t dy = static_cast<fixed_t>(static_cast<std::uint32_t>(y) - static_cast<std::uint32_t>(view->y));

    if (dx == 0 && dy == 0) {
        return 0;
    }
    if (dx >= 0) {
        if (dy >= 0) {
            if (dx > dy) {
                return r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dy), static_cast<std::uint32_t>(dx))];
            }
            return ANG90 - 1u - r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy))];
        }
        dy = static_cast<fixed_t>(0u - static_cast<std::uint32_t>(dy));
        if (dx > dy) {
            return 0u - r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dy), static_cast<std::uint32_t>(dx))];
        }
        return ANG270 + r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy))];
    }
    dx = static_cast<fixed_t>(0u - static_cast<std::uint32_t>(dx));
    if (dy >= 0) {
        if (dx > dy) {
            return ANG180 - 1u - r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dy), static_cast<std::uint32_t>(dx))];
        }
        return ANG90 + r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy))];
    }
    dy = static_cast<fixed_t>(0u - static_cast<std::uint32_t>(dy));
    if (dx > dy) {
        return ANG180 + r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dy), static_cast<std::uint32_t>(dx))];
    }
    return ANG270 - 1u - r_tan_to_angle[slope_div(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy))];
}

const e1m1_sprite_rule_t* spriteRule(int type) {
    for (int index = 0; index < E1M1_SPRITE_RULE_COUNT; ++index) {
        if (kE1M1Sprites[index].type == type) {
            return &kE1M1Sprites[index];
        }
    }
    return nullptr;
}

bool sectorAt(const doom_map_t* map, fixed_t x, fixed_t y, std::uint16_t* sector) {
    std::uint16_t subsector = 0u;
    doom_subsector_t sub;

    if (r_bsp_point_in_subsector(map, x, y, &subsector) != R_BSP_OK) {
        return false;
    }
    doom_map_get_subsector(map, subsector, &sub);
    *sector = doom_map_subsector_sector(map, &sub);
    return true;
}

/// Builds the frame's sprite list: every THINGS record the fixture covers, standing
/// on its sector's floor, with the rotation R_ProjectSprite's rule selects for this
/// viewpoint -- exactly the list the host harness builds.
int buildSprites(const doom_map_t* map, const r_view_params_t* view) {
    int count = 0;

    for (std::uint32_t index = 0; index < map->lumps[DOOM_MAP_THINGS].count; ++index) {
        doom_thing_t thing;
        doom_map_get_thing(map, index, &thing);
        const e1m1_sprite_rule_t* rule = spriteRule(thing.type);
        if (rule == nullptr) {
            continue;
        }
        if (count >= kSpriteCapacity) {
            return -1;
        }

        const fixed_t x = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.x)) << FRACBITS);
        const fixed_t y = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.y)) << FRACBITS);
        std::uint16_t sectorIndex = 0u;
        if (!sectorAt(map, x, y, &sectorIndex)) {
            return -1;
        }
        doom_sector_t sector;
        doom_map_get_sector(map, sectorIndex, &sector);

        const angle_t angle = ANG45 * static_cast<angle_t>(thing.angle / 45);
        const angle_t facing = pointToAngle(view, x, y);
        const int rotation = static_cast<int>((facing - angle + ((ANG45 / 2u) * 9u)) >> 29);
        const int lump = rule->rotations ? rotation : 0;

        char lumpName[9];
        for (int i = 0; i < 8; ++i) {
            lumpName[i] = rule->lumps[lump].name[i];
        }
        lumpName[8] = '\0';

        gSprites[count].x = x;
        gSprites[count].y = y;
        gSprites[count].z = sector.floorHeight;
        gSprites[count].angle = angle;
        gSprites[count].lump = r_pack_name(reinterpret_cast<const std::uint8_t*>(lumpName));
        gSprites[count].lightLevel = sector.lightLevel;
        gSprites[count].flip = rule->lumps[lump].flip;
        gSprites[count].fullbright = rule->fullbright;
        gSprites[count].noClip = 0;
        ++count;
    }
    return count;
}

/// The same rule tools/rsegs_check.c's buildFrames() uses, so the two describe
/// the same 20 views.
bool buildFrames(const doom_map_t* map, FrameView frames[kViewCount]) {
    FrameView positions[kPositionCount];
    int found = 0;
    doom_thing_t thing;

    for (std::uint32_t index = 0; index < map->lumps[DOOM_MAP_THINGS].count
                                  && found < kPositionCount;
         ++index) {
        doom_map_get_thing(map, index, &thing);
        if (!isPlayerStart(thing.type)) {
            continue;
        }
        const fixed_t x = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.x)) << FRACBITS);
        const fixed_t y = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.y)) << FRACBITS);
        if (!positionView(map, x, y, &positions[found])) {
            return false;
        }
        ++found;
    }

    if (found < kPositionCount) {
        r_view_params_t spawn;
        if (r_bsp_spawn_view(map, 0, &spawn) != R_BSP_OK) {
            return false;
        }
        while (found < kPositionCount) {
            positions[found].x = spawn.x;
            positions[found].y = spawn.y;
            positions[found].z = spawn.z;
            positions[found].angle = 0;
            ++found;
        }
    }

    for (int position = 0; position < kPositionCount; ++position) {
        for (int k = 0; k < kAngleCount; ++k) {
            FrameView* frame = &frames[position * kAngleCount + k];
            *frame = positions[position];
            frame->angle = static_cast<angle_t>((static_cast<std::uint64_t>(k) << 32) / kAngleCount);
        }
    }
    return true;
}

// --- SHA-256 (FIPS 180-4) ---------------------------------------------------

/*
 * The host harness hashes a frame as the 320*200 pixel bytes followed by the 320
 * covered bytes, and the board prints the same number so a data-path divergence
 * has one place to show up. The implementation is the harness's, unchanged: three
 * published vectors are checked at boot before the first hash is trusted.
 */

typedef struct sha256_context {
    std::uint32_t state[8];
    std::uint64_t bits;
    std::uint8_t buffer[64];
    std::size_t used;
} sha256_context_t;

const std::uint32_t kRound[64] = {
    0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul, 0x3956C25Bul, 0x59F111F1ul,
    0x923F82A4ul, 0xAB1C5ED5ul, 0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
    0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul, 0xE49B69C1ul, 0xEFBE4786ul,
    0x0FC19DC6ul, 0x240CA1CCul, 0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
    0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul, 0xC6E00BF3ul, 0xD5A79147ul,
    0x06CA6351ul, 0x14292967ul, 0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
    0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul, 0xA2BFE8A1ul, 0xA81A664Bul,
    0xC24B8B70ul, 0xC76C51A3ul, 0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
    0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul, 0x391C0CB3ul, 0x4ED8AA4Aul,
    0x5B9CCA4Ful, 0x682E6FF3ul, 0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
    0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul};

std::uint32_t rotateRight(std::uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

void sha256Block(sha256_context_t* context, const std::uint8_t* block) {
    std::uint32_t words[64];
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t c;
    std::uint32_t d;
    std::uint32_t e;
    std::uint32_t f;
    std::uint32_t g;
    std::uint32_t h;
    unsigned int index;

    for (index = 0; index < 16u; ++index) {
        words[index] = (static_cast<std::uint32_t>(block[index * 4u]) << 24)
                       | (static_cast<std::uint32_t>(block[index * 4u + 1u]) << 16)
                       | (static_cast<std::uint32_t>(block[index * 4u + 2u]) << 8)
                       | static_cast<std::uint32_t>(block[index * 4u + 3u]);
    }
    for (index = 16u; index < 64u; ++index) {
        const std::uint32_t s0 = rotateRight(words[index - 15u], 7u)
                                 ^ rotateRight(words[index - 15u], 18u)
                                 ^ (words[index - 15u] >> 3);
        const std::uint32_t s1 = rotateRight(words[index - 2u], 17u)
                                 ^ rotateRight(words[index - 2u], 19u)
                                 ^ (words[index - 2u] >> 10);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0; index < 64u; ++index) {
        const std::uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const std::uint32_t choose = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = h + s1 + choose + kRound[index] + words[index];
        const std::uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void sha256Init(sha256_context_t* context) {
    context->state[0] = 0x6A09E667ul;
    context->state[1] = 0xBB67AE85ul;
    context->state[2] = 0x3C6EF372ul;
    context->state[3] = 0xA54FF53Aul;
    context->state[4] = 0x510E527Ful;
    context->state[5] = 0x9B05688Cul;
    context->state[6] = 0x1F83D9ABul;
    context->state[7] = 0x5BE0CD19ul;
    context->bits = 0u;
    context->used = 0u;
}

void sha256Update(sha256_context_t* context, const void* data, std::size_t size) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t index = 0u;

    context->bits += static_cast<std::uint64_t>(size) * 8u;

    while (index < size) {
        const std::size_t room = 64u - context->used;
        const std::size_t take = (size - index) < room ? (size - index) : room;

        std::memcpy(context->buffer + context->used, bytes + index, take);
        context->used += take;
        index += take;

        if (context->used == 64u) {
            sha256Block(context, context->buffer);
            context->used = 0u;
        }
    }
}

void sha256Final(sha256_context_t* context, std::uint8_t out[32]) {
    const std::uint64_t bits = context->bits;
    static const std::uint8_t pad = 0x80u;
    std::uint8_t zero = 0u;
    std::uint8_t length[8];
    unsigned int index;

    sha256Update(context, &pad, 1u);
    while (context->used != 56u) {
        sha256Update(context, &zero, 1u);
    }
    for (index = 0; index < 8u; ++index) {
        length[7u - index] = static_cast<std::uint8_t>((bits >> (index * 8u)) & 0xFFu);
    }
    sha256Update(context, length, sizeof length);

    for (index = 0; index < 8u; ++index) {
        out[index * 4u] = static_cast<std::uint8_t>((context->state[index] >> 24) & 0xFFu);
        out[index * 4u + 1u] = static_cast<std::uint8_t>((context->state[index] >> 16) & 0xFFu);
        out[index * 4u + 2u] = static_cast<std::uint8_t>((context->state[index] >> 8) & 0xFFu);
        out[index * 4u + 3u] = static_cast<std::uint8_t>(context->state[index] & 0xFFu);
    }
}

void digestToHex(const std::uint8_t digest[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";

    for (unsigned int index = 0; index < 32u; ++index) {
        hex[index * 2u] = digits[(digest[index] >> 4) & 0x0Fu];
        hex[index * 2u + 1u] = digits[digest[index] & 0x0Fu];
    }
    hex[64] = '\0';
}

/// Three published vectors, so a broken round function or padding rule is caught
/// before any comparison is trusted. The million-'a' vector is left to the host:
/// it is a second of work for a check the three short ones already make.
bool sha256SelfTest() {
    static const char* vectors[] = {"", "abc", "The quick brown fox jumps over the lazy dog"};
    static const char* expected[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592",
    };
    char hex[65];
    std::uint8_t digest[32];

    for (unsigned int index = 0; index < 3u; ++index) {
        sha256_context_t one;
        sha256Init(&one);
        sha256Update(&one, vectors[index], std::strlen(vectors[index]));
        sha256Final(&one, digest);
        digestToHex(digest, hex);
        if (std::strcmp(hex, expected[index]) != 0) {
            return false;
        }
    }
    return true;
}

// --- Setup ------------------------------------------------------------------

/// Allocates one block for a decoded lump and accounts for it.
///
/// In the RAM build the first blocks come from the static arena and the rest from
/// the heap; in the flash build the map is not decoded at all, so nothing calls
/// this. Blocks are handed out in descending size order by the caller: the heap's
/// holes are not interchangeable, and a block order that leaves a small hole in
/// front of a bigger request fails
/// while the same bytes fit.
std::uint8_t* heapLump(std::size_t bytes) {
    const std::size_t aligned = (bytes + 7u) & ~static_cast<std::size_t>(7u);

#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
    if (gLevelRamUsed + aligned <= kStaticLumpBytes) {
        std::uint8_t* at = gLumpArena + gLevelRamUsed;
        gLevelRamUsed += aligned;
        return at;
    }
#endif
    if (gLumpBlockCount >= kExtraLumpBlocks) {
        return nullptr;
    }
    std::uint8_t* at = static_cast<std::uint8_t*>(
        heap_caps_malloc(aligned, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (at == nullptr) {
        return nullptr;
    }
    gLumpBlocks[gLumpBlockCount] = at;
    ++gLumpBlockCount;
    gLevelRamUsed += aligned;
    return at;
}

/// The map lump the setup could not bind or decode, printed by `FRAMEPROF,setup`
/// so a shortfall names the request that failed instead of only counting it.
const char* const kMapLumpNames[DOOM_MAP_LUMP_COUNT] = {
    "THINGS", "LINEDEFS", "SIDEDEFS", "VERTEXES", "SEGS",
    "SSECTORS", "NODES", "SECTORS", "REJECT", "BLOCKMAP"};
int gFailLump = -1;

/// Loads one E1M1 map lump the way this build reads the level.
///
/// The slot is the `doom_map_lump_t` value itself and the lump is one past the
/// E1M1 marker plus it. Getting that wrong is silent: doom_map_open() only checks
/// record sizes, so a shifted set of lumps that happens to be aligned opens fine
/// and renders garbage.
///
/// REJECT and BLOCKMAP are empty in both builds: the renderer never reads them,
/// and in the RAM build decoding them would spend 7,826 B of DRAM for nothing. In
/// the flash build the container still holds them raw, so the loader could bind
/// them, and does not -- the probe measures the renderer.
///
/// THINGS is loaded like the rest and, unlike D3.6, stays loaded: the masked pass
/// builds the sprite list from it once per view, so it is read 20 times, not once.
bool loadMapLump(int marker, int dataIndex, doom_map_source_t* source,
                 std::int32_t* failures) {
    const int slot = dataIndex;
    const int lump = marker + 1 + dataIndex;

    if (dataIndex >= kFirstSkippedMapLump) {
        source->data[slot] = nullptr;
        source->size[slot] = 0u;
        return true;
    }
    const std::int32_t size = doom_whd_source_lump_size(&gWhd, lump);
    if (size < 0) {
        ++(*failures);
        gFailLump = dataIndex;
        return false;
    }
    if (size == 0) {
        source->data[slot] = nullptr;
        source->size[slot] = 0u;
        return true;
    }
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
    std::uint8_t* destination = heapLump(static_cast<std::size_t>(size));
    if (destination == nullptr) {
        ++(*failures);
        gFailLump = dataIndex;
        return false;
    }
    std::uint32_t used = 0u;
    if (!decodeLump(lump, destination, static_cast<std::size_t>(size), &used)) {
        ++(*failures);
        gFailLump = dataIndex;
        return false;
    }
    source->data[slot] = destination;
    source->size[slot] = static_cast<std::size_t>(used);
#else
    const std::uint8_t* view = nullptr;
    std::size_t viewSize = 0u;
    if (!viewLump(lump, &view, &viewSize)) {
        ++(*failures);
        gFailLump = dataIndex;
        return false;
    }
    source->data[slot] = view;
    source->size[slot] = viewSize;
#endif
    return true;
}

/// Binds one map lump as a view. THINGS goes through this in *both* builds: it is
/// the sprite list's input (read once per view by the fixture), not part of the map
/// graph the walk traverses, so holding it constant keeps the A/B about the walk
/// and it is the 1,380 B that lets the RAM build fit at all.
bool viewMapLump(int marker, int dataIndex, doom_map_source_t* source) {
    const int lump = marker + 1 + dataIndex;
    const std::uint8_t* view = nullptr;
    std::size_t viewSize = 0u;
    if (!viewLump(lump, &view, &viewSize)) {
        return false;
    }
    source->data[dataIndex] = view;
    source->size[dataIndex] = viewSize;
    return true;
}

/// Binds a whole directory lump as a view. The three directories are read-only
/// tables -- a texture lookup, a patch fetch and a colormap read -- and the
/// container keeps them raw in both builds, so neither has to hold 20,742 B.
bool loadLumpViewByName(const char* name, const std::uint8_t** data, std::size_t* bytes) {
    const int lump = doom_whd_source_lump_index(&gWhd, name);
    if (lump < 0) {
        return false;
    }
    return viewLump(lump, data, bytes);
}

// --- One frame --------------------------------------------------------------

/// The state the four passes share for the whole run.
struct Driver {
    doom_map_t map = {};
    r_view_storage_t storage = {};
    r_view_state_t view = {};
    r_draw_surface_t surface = {};
    r_light_t light = {};
    r_tex_source_t textures = {};
    r_sprite_source_t sprites = {};
    const char* stage = "none";
};

/// Builds the surface descriptor over the engine's framebuffer.
void bindSurface(Driver* driver, std::uint8_t* pixels, int width, int height) {
    driver->surface.pixels = pixels;
    driver->surface.width = width;
    driver->surface.height = height;
    driver->surface.covered = gState.covered;
    driver->surface.planeMask = nullptr;
    driver->surface.spriteMask = nullptr;
}

/**
 * @brief Renders one view once and fills @p sample.
 *
 * `frameMicros` brackets the surface prefill and the four passes; each pass is
 * timed on its own inside that window. Residency is the sum of the decode time
 * the passes incurred, accumulated by the resolvers.
 *
 * THE FOUR PHASES
 *
 * `walk`, `walls`, `planes` and `masked` all run. D3.6 could only afford the
 * first two: the plane pass needs a 35,080 B sky-patch scratch and the masked
 * pass its 9,168 B of pools, and neither fit next to a decoded level. With the
 * level in the container's raw section both fit, so this is the whole frame the
 * renderer defines, in the order r_bsp.h states, and the hash is the finished
 * frame rather than the wall pass's half of it.
 *
 * The sprite list is built inside the masked window: upstream builds it during
 * the walk, but this port's caller owns the thing list, so the build is the
 * caller's work for that pass and is measured with it rather than hidden in
 * `other_us`.
 */
bool renderOnce(Driver* driver, const FrameView& view, std::uint8_t* pixels, int width,
                int height, FrameProfileSample* sample) {
    const std::int64_t frameStart = nowMicros();

    r_view_params_t params;
    params.x = view.x;
    params.y = view.y;
    params.z = view.z;
    params.angle = view.angle;
    params.extralight = 0;
    params.skyPicNum = r_pack_name(kSkyFlatName);

    *sample = FrameProfileSample{};
    // The resolvers fold into gSample, so it is the per-run accumulator and is
    // cleared here rather than at the call site: one frame, one set of counters.
    gSample = FrameProfileSample{};

    std::memset(pixels, 0xFF, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    std::memset(gState.covered, 0, sizeof gState.covered);
    driver->surface.tierPixels[R_DRAW_TIER_MID] = 0u;
    driver->surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
    driver->surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    driver->surface.planePixels = 0u;
    driver->surface.planeSpans = 0u;
    driver->surface.spritePixels = 0u;

    std::int64_t start = nowMicros();
    driver->stage = "walk";
    const r_bsp_error_t walk = r_bsp_render(&driver->view, &params);
    sample->walkMicros = elapsedMicros(start);
    if (walk != R_BSP_OK) {
        return false;
    }
    sample->drawsegs = driver->view.drawsegCount;
    sample->drawsegOverflowed = driver->view.drawsegOverflowed;

    // A pass owns the scratch for its duration only. Clearing the owner here is
    // what keeps `hazards=` honest: the plane pass leaves a flat tagged in the
    // scratch, and without this the masked pass's first sky/mid compose would look
    // like an aliasing violation when it is only a stale tag.
    gScratchOwner = kOwnerNone;
    gFlatName = R_NAME_NONE;

    start = nowMicros();
    driver->stage = "walls";
    const r_segs_error_t walls =
        r_wall_emit(&driver->view, &driver->surface, &driver->textures, &driver->light);
    sample->wallsMicros = elapsedMicros(start);
    if (walls != R_SEGS_OK) {
        gStageCode = static_cast<int>(walls);
        return false;
    }

    // The wall pass owns the two texture slots for its duration, and it is over.
    // Clearing them is what lets the plane pass compose the sky: SKY1 is 32,768 B
    // and only fits the 36,864 B arena beside something of at most 4,096 B, so a
    // wall texture left cached from the previous pass would refuse it. The policy
    // is "nothing beyond the current call", and a pass is a call; recomposing the
    // sky or a masked mid texture costs decode time that `residency_us` reports.
    gWall[0].used = false;
    gWall[1].used = false;
    gNewest = -1;

    start = nowMicros();
    driver->stage = "planes";
    gPlanePass = 1;
    const r_plane_error_t planes =
        r_plane_draw(&driver->view, &driver->surface, &driver->textures, &driver->light);
    gPlanePass = 0;
    sample->planesMicros = elapsedMicros(start);
    if (planes != R_PLANE_OK) {
        gStageCode = static_cast<int>(planes);
        return false;
    }
    for (std::uint16_t index = 0u; index < driver->view.storage.planes.count; ++index) {
        if (driver->view.storage.planes.planes[index].minX
            <= driver->view.storage.planes.planes[index].maxX) {
            ++sample->planesDrawn;
        }
    }
    sample->planesAllocated = driver->view.storage.planes.count;

    // The plane pass is over: its flat is dead and the tags go with it.
    gScratchOwner = kOwnerNone;
    gFlatName = R_NAME_NONE;

    start = nowMicros();
    driver->stage = "sprites";
    const int spriteCount = buildSprites(&driver->map, &params);
    if (spriteCount < 0) {
        return false;
    }

    driver->stage = "masked";
    r_masked_stats_t maskedStats;
    const r_things_error_t masked = r_masked_emit(
        &driver->view, &driver->surface, &driver->textures, &driver->light, gSprites,
        static_cast<std::uint16_t>(spriteCount), &driver->sprites, &maskedStats);
    sample->maskedMicros = elapsedMicros(start);
    if (masked != R_THINGS_OK) {
        gStageCode = static_cast<int>(masked);
        return false;
    }
    sample->spritesSubmitted = maskedStats.submitted;
    sample->spritesDrawn = maskedStats.drawn;

    sample->residencyMicros = gSample.residencyMicros;
    sample->texResolves = gSample.texResolves;
    sample->texDecodes = gSample.texDecodes;
    sample->flatResolves = gSample.flatResolves;
    sample->flatDecodes = gSample.flatDecodes;
    sample->spriteResolves = gSample.spriteResolves;
    sample->spriteDecodes = gSample.spriteDecodes;
    sample->bytesDecoded = gSample.bytesDecoded;
    sample->frameMicros = elapsedMicros(frameStart);
    driver->stage = "done";
    return true;
}

/// Hashes the finished frame exactly as the host harness does.
void hashSurface(const std::uint8_t* pixels, std::size_t pixelBytes, char hex[65]) {
    sha256_context_t context;
    std::uint8_t digest[32];

    sha256Init(&context);
    sha256Update(&context, pixels, pixelBytes);
    sha256Update(&context, gState.covered, sizeof gState.covered);
    sha256Final(&context, digest);
    digestToHex(digest, hex);
}

bool isBreakdownView(int frame) {
    for (int index = 0; index < kFrameBreakdownCount; ++index) {
        if (kFrameBreakdownFrames[index] == frame) {
            return true;
        }
    }
    return false;
}

/// One `FRAMEPROF,mem` field, from `sizeof`: the driver's RAM cost is a printed
/// number, never an estimate. `state_static` is the .bss half, `level_ram` is the
/// decoded level (0 in the flash build) and `heap_before/after` bound the rest.
void formatMemoryLine(char* line, std::size_t capacity, std::uint32_t heapBefore,
                      std::uint32_t heapAfterAlloc) {
    const std::size_t drawsegs = sizeof(gState.drawsegs);
    const std::size_t planes = sizeof(gState.planes);
    const std::size_t clipRanges = sizeof(gState.clipRanges);
    const std::size_t clipArrays = sizeof(gState.floorClip) + sizeof(gState.ceilingClip);
    const std::size_t clipBlocks = sizeof(gState.clipBlocks);
    const std::size_t spritePools = sizeof(gState.visSprites) + sizeof(gState.openingBlocks);
    const std::size_t total = sizeof(DriverState) + gLevelRamUsed + kWallArenaBytes
                              + kScratchBytes + sizeof(gSpritePatchBytes);

    std::snprintf(line, capacity,
                  "FRAMEPROF,mem,surface=engine8bpp,index_writes=1,present=no,no_rgb332=1,"
                  "arena=%s,lump_blocks=%u,wall_arena=%u,scratch=%u,sprite_patch=%u,"
                  "state_static=%u,drawsegs=%u,planes=%u,clip_ranges=%u,clip_arrays=%u,"
                  "clip_blocks=%u,sprite_pools=%u,covered=%u,driver_total=%u,drawseg_cap=%d,"
                  "plane_cap=%d,clip_block_cap=%d,vissprite_cap=%d,heap_before=%u,"
                  "heap_after_alloc=%u,map=%s,map_in_ram=%d,raw_views=%u,level_ram=%u,"
                  "level_released=%u,phases=4\n",
                  gPlacement, static_cast<unsigned>(gLumpBlockCount),
                  static_cast<unsigned>(kWallArenaBytes), static_cast<unsigned>(kScratchBytes),
                  static_cast<unsigned>(sizeof(gSpritePatchBytes)),
                  static_cast<unsigned>(sizeof(DriverState)), static_cast<unsigned>(drawsegs),
                  static_cast<unsigned>(planes), static_cast<unsigned>(clipRanges),
                  static_cast<unsigned>(clipArrays), static_cast<unsigned>(clipBlocks),
                  static_cast<unsigned>(spritePools),
                  static_cast<unsigned>(sizeof(gState.covered)), static_cast<unsigned>(total),
                  kDrawsegCapacity, kPlaneCapacity, kClipBlockCapacity, kVisspriteCapacity,
                  static_cast<unsigned>(heapBefore), static_cast<unsigned>(heapAfterAlloc),
                  kMapPath,
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
                  1,
#else
                  0,
#endif
                  static_cast<unsigned>(gRawViews), static_cast<unsigned>(gLevelRamUsed),
                  static_cast<unsigned>(kLevelReleasedBytes));
}

}  // namespace

void frameProbeRun(std::uint8_t* surface, int width, int height) {
    char line[kFrameProfileLineCapacity];

    if (surface == nullptr || width < R_SCREEN_WIDTH || height < R_VIEW_HEIGHT) {
        std::snprintf(line, sizeof line, "FRAMEPROF,mem,surface=missing,need=%dx%d,have=%dx%d\n",
                      R_SCREEN_WIDTH, R_VIEW_HEIGHT, width, height);
        writeLine(line);
        return;
    }
    if (!sha256SelfTest()) {
        writeLine("FRAMEPROF,setup,status=fail,reason=sha256\n");
        return;
    }

    const int openCode = doom_whd_source_open(&gWhd);
    if (openCode != DOOM_WHD_SOURCE_OK) {
        std::snprintf(line, sizeof line, "FRAMEPROF,setup,status=fail,reason=open,code=%d\n",
                      openCode);
        writeLine(line);
        return;
    }

    // --- The working set ----------------------------------------------------
    //
    // The graphics blocks are taken here; the map lumps are bound inside the setup
    // below. `FRAMEPROF,mem` prints the heap before and after, and the placement
    // the driver chose, so the split is a measured number.
    const std::uint32_t heapBefore = static_cast<std::uint32_t>(esp_get_free_heap_size());
    const std::uint32_t largestBefore = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    // --- Placement, tried in order and then reported ------------------------
    //
    // The two graphics buffers are separate regions -- a compose reads a patch and
    // writes a slot at the same time, but they are not one array -- so each can go
    // where it fits instead of both competing for one hole. The admissible
    // placements are tried in order and the one that succeeds is printed by
    // `FRAMEPROF,mem`, so the choice is evidence and not luck:
    //
    //   wall-static     wall arena in .bss (36,864), scratch in the heap (35,080)
    //   scratch-static  scratch in .bss (35,080), wall arena in the heap (36,864)
    //   heap-both       both from the heap, one request each
    //
    // No buffer is shrunk to make one of them fit; a placement either succeeds at
    // the measured size or it does not.
    gWallBytes = gStaticGraphics;
    gScratch = static_cast<std::uint8_t*>(
        heap_caps_malloc(kScratchBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (gScratch != nullptr) {
        gPlacement = "wall-static";
    } else {
        gWallBytes = nullptr;
        gScratch = gStaticGraphics;
        gWallBytes = static_cast<std::uint8_t*>(
            heap_caps_malloc(kWallArenaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (gWallBytes != nullptr) {
            gPlacement = "scratch-static";
        } else {
            gScratch = nullptr;
            gWallBytes = static_cast<std::uint8_t*>(
                heap_caps_malloc(kWallArenaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            gScratch = static_cast<std::uint8_t*>(
                heap_caps_malloc(kScratchBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (gWallBytes != nullptr && gScratch != nullptr) {
                gPlacement = "heap-both";
            }
        }
    }
    if (gPlacement[0] == 'n') {
        // Nothing is measured: say exactly which allocation is short and by how
        // much, because that is the result this board produced.
        std::snprintf(line, sizeof line,
                      "FRAMEPROF,setup,status=fail,reason=arena,placement=%s,"
                      "wall=%u,scratch=%u,heap=%u,largest=%u\n",
                      gPlacement, static_cast<unsigned>(kWallArenaBytes),
                      static_cast<unsigned>(kScratchBytes),
                      static_cast<unsigned>(heapBefore), static_cast<unsigned>(largestBefore));
        writeLine(line);
        releaseWorkingSet();
        doom_whd_source_close();
        return;
    }
    const std::uint32_t heapAfterAlloc = static_cast<std::uint32_t>(esp_get_free_heap_size());


    // --- Setup: the working set, timed once and reported separately ----------
    const std::int64_t setupStart = nowMicros();
    gLevelRamUsed = 0u;
    gRawViews = 0u;
    gNotRaw = 0u;
    gSample = FrameProfileSample{};
    gDecodeDepth = 0;

    const int marker = doom_whd_source_lump_index(&gWhd, "E1M1");
    if (marker < 0) {
        writeLine("FRAMEPROF,setup,status=fail,reason=marker\n");
        doom_whd_source_close();
        return;
    }

    doom_map_source_t source = {};
    std::int32_t mapFailures = 0;
    // Descending block size, which is the order the RAM build's heap needs: the
    // holes are not interchangeable, so a small request in front of a big one can
    // fail while the same bytes fit when the requests descend. In the flash build
    // every one of these is a pointer and the order costs nothing.
    (void)loadMapLump(marker, DOOM_MAP_SIDEDEFS, &source, &mapFailures);   // 19,440
    (void)loadMapLump(marker, DOOM_MAP_SEGS, &source, &mapFailures);       //  8,784
    bool directoriesLoaded = loadLumpViewByName("COLORMAP", &gColormap, &gColormapBytes);
    (void)loadMapLump(marker, DOOM_MAP_LINEDEFS, &source, &mapFailures);   //  6,650
    (void)loadMapLump(marker, DOOM_MAP_NODES, &source, &mapFailures);      //  6,608
    directoriesLoaded = loadLumpViewByName("PNAMES", &gPnames, &gPnamesBytes) && directoriesLoaded;
    // THINGS is a view in both builds: see viewMapLump(). It stays alive for the
    // whole run because the masked pass builds one sprite list per view from it.
    if (!viewMapLump(marker, DOOM_MAP_THINGS, &source)) {
        ++mapFailures;
    }
    (void)loadMapLump(marker, DOOM_MAP_SECTORS, &source, &mapFailures);    //  2,210
    (void)loadMapLump(marker, DOOM_MAP_VERTEXES, &source, &mapFailures);   //  1,868
    (void)loadMapLump(marker, DOOM_MAP_SSECTORS, &source, &mapFailures);   //    948
    (void)loadMapLump(marker, DOOM_MAP_REJECT, &source, &mapFailures);     // skipped
    (void)loadMapLump(marker, DOOM_MAP_BLOCKMAP, &source, &mapFailures);   // skipped
    directoriesLoaded = directoriesLoaded
                        && loadLumpViewByName("TEXTURE1", &gTexture1, &gTexture1Bytes);

    // The sprite list. D3.6 never allocated it because the masked pass never ran;
    // it is the caller's thing list, so it is the caller's block. It is taken
    // *after* the level's blocks because those are the big requests: the scratch
    // has already eaten the middle of the one large hole, so 8,784 B of SEGS has to
    // find a hole before a 4,160 B sprite list splits what is left of it.
    gSprites = static_cast<r_sprite_t*>(
        heap_caps_malloc(static_cast<std::size_t>(kSpriteCapacity) * sizeof(r_sprite_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (gSprites == nullptr) {
        std::snprintf(line, sizeof line,
                      "FRAMEPROF,setup,status=fail,reason=sprites,need=%u,heap=%u,"
                      "largest=%u\n",
                      static_cast<unsigned>(kSpriteCapacity * sizeof(r_sprite_t)),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        writeLine(line);
        releaseWorkingSet();
        doom_whd_source_close();
        return;
    }

    doom_map_t map;
    const doom_map_error_t mapError =
        mapFailures == 0 ? doom_map_open(&map, &source) : DOOM_MAP_ERR_REQUIRED_LUMP;
    if (mapFailures != 0 || !directoriesLoaded || mapError != DOOM_MAP_OK) {
        std::snprintf(line, sizeof line,
                      "FRAMEPROF,setup,status=fail,reason=lump,map=%s,map_failures=%d,"
                      "fail_lump=%s,not_raw=%u,directories=%d,map_error=%d,pnames=%u,"
                      "colormap=%u,texture1=%u,heap=%u,largest=%u,level_ram=%u\n",
                      kMapPath, static_cast<int>(mapFailures),
                      (gFailLump >= 0 && gFailLump < DOOM_MAP_LUMP_COUNT)
                          ? kMapLumpNames[gFailLump] : "none",
                      static_cast<unsigned>(gNotRaw), directoriesLoaded ? 1 : 0,
                      static_cast<int>(mapError), static_cast<unsigned>(gPnamesBytes),
                      static_cast<unsigned>(gColormapBytes),
                      static_cast<unsigned>(gTexture1Bytes),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(gLevelRamUsed));
        writeLine(line);
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    if (r_light_init(&gLight, gColormap, gColormapBytes) != R_LIGHT_OK) {
        writeLine("FRAMEPROF,setup,status=fail,reason=colormap\n");
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    // The block sizes and the directory sizes are the residency decision, so they
    // are printed with the setup that made it.
    std::snprintf(line, sizeof line,
                  "FRAMEPROF,setup,status=ok,map=%s,map_in_ram=%d,raw_views=%u,level_ram=%u,"
                  "texture1=%u,pnames=%u,colormap=%u,map_lumps=%d,map_skipped=%d,"
                  "setup_us=%u\n",
                  kMapPath,
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
                  1,
#else
                  0,
#endif
                  static_cast<unsigned>(gRawViews), static_cast<unsigned>(gLevelRamUsed),
                  static_cast<unsigned>(gTexture1Bytes),
                  static_cast<unsigned>(gPnamesBytes),
                  static_cast<unsigned>(gColormapBytes),
                  DOOM_MAP_LUMP_COUNT, kMapLumpCount - kFirstSkippedMapLump,
                  static_cast<unsigned>(elapsedMicros(setupStart)));
    writeLine(line);

    // --- The walk's storage and the pass contracts --------------------------
    FrameView frames[kViewCount];
    if (!buildFrames(&map, frames)) {
        writeLine("FRAMEPROF,setup,status=fail,reason=views\n");
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    Driver driver;
    driver.map = map;
    driver.storage.drawsegs = gState.drawsegs;
    driver.storage.drawsegCapacity = kDrawsegCapacity;
    driver.storage.planes.planes = gState.planes;
    driver.storage.planes.capacity = kPlaneCapacity;
    driver.storage.planes.count = 0;
    driver.storage.clipRanges = gState.clipRanges;
    driver.storage.clipRangeCapacity = kClipRangeCapacity;
    driver.storage.floorClip = gState.floorClip;
    driver.storage.ceilingClip = gState.ceilingClip;
    driver.storage.clipBlocks = gState.clipBlocks;
    driver.storage.clipBlockCapacity = kClipBlockCapacity;
    // All four phases run, so the D3.4 pools are part of the frame: the masked
    // pass projects into visSprites and records its deferred columns in
    // openingBlocks.
    driver.storage.visSprites = gState.visSprites;
    driver.storage.visSpriteCapacity = kVisspriteCapacity;
    driver.storage.openingBlocks = gState.openingBlocks;
    driver.storage.openingBlockCapacity = kOpeningBlockCapacity;

    if (r_bsp_init(&driver.view, &driver.map, &driver.storage) != R_BSP_OK) {
        writeLine("FRAMEPROF,setup,status=fail,reason=storage\n");
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    driver.light = gLight;
    driver.textures.resolve = resolveWall;
    driver.textures.context = nullptr;
    driver.textures.resolveFlat = resolveFlat;
    driver.textures.flatContext = nullptr;
    driver.textures.skyTexture = r_pack_name(kSkyTextureName);
    driver.sprites.resolve = resolveSprite;
    driver.sprites.context = nullptr;
    bindSurface(&driver, surface, width, height);

    // --- The table ----------------------------------------------------------
    formatMemoryLine(line, sizeof line, heapBefore, heapAfterAlloc);
    writeLine(line);

    FrameProfileStats stats;
    frameProfileReset(&stats);
    const std::int64_t probeStart = nowMicros();

    for (int frame = 0; frame < kViewCount; ++frame) {
        for (int run = 1; run <= kTotalRuns; ++run) {
            FrameProfileSample sample;
            gStageCode = 0;
            if (!renderOnce(&driver, frames[frame], surface, width, height, &sample)) {
                std::snprintf(line, sizeof line,
                              "FRAMEPROF,error,frame=%02d,run=%d,map=%s,stage=%s,code=%d,"
                              "tex=%s,why=%s\n", frame,
                              run, kMapPath, driver.stage, gStageCode, gFailTexture, gFailWhy);
                writeLine(line);
                doom_whd_source_close();
                releaseWorkingSet();
                return;
            }
            if (run == 1) {
                // The warm-up is not discarded silently: it is the cold number --
                // the first touch of this view's map bytes in the flash cache --
                // and mixing it into the steady state would make the table
                // incomparable with itself. `FRAMEPROF,cold` prints it alone.
                std::snprintf(line, sizeof line,
                              "FRAMEPROF,cold,frame=%02d,map=%s,walk_us=%u,walls_us=%u,"
                              "planes_us=%u,masked_us=%u,frame_us=%u,drawsegs=%u\n",
                              frame, kMapPath, static_cast<unsigned>(sample.walkMicros),
                              static_cast<unsigned>(sample.wallsMicros),
                              static_cast<unsigned>(sample.planesMicros),
                              static_cast<unsigned>(sample.maskedMicros),
                              static_cast<unsigned>(sample.frameMicros),
                              static_cast<unsigned>(sample.drawsegs));
                writeLine(line);
                continue;
            }

            sample.frame = static_cast<std::uint16_t>(frame);
            sample.run = static_cast<std::uint16_t>(run - 1);

            char hash[65];
            hashSurface(surface, static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                        hash);

            frameProfileRecord(&stats, sample);
            (void)FrameProfileReport<kFrameProbeEnabled>::frame(sample, hash, &writeLine);
            if (isBreakdownView(frame)) {
                emitFormatted(frameProfileFormatBreakdown(sample, line, sizeof line), line);
            }
        }
    }

    const std::uint32_t probeMicros = elapsedMicros(probeStart);
    emitFormatted(frameProfileFormatSummary(stats, kViewCount, kMeasuredRuns, line, sizeof line),
                  line);

    std::snprintf(line, sizeof line,
                  "FRAMEPROF,probe,total_us=%u,hazards=%u,sha256=ok,views=%d,runs=%d,"
                  "warmup=%d,map=%s,phases=4\n",
                  static_cast<unsigned>(probeMicros), static_cast<unsigned>(gHazards), kViewCount,
                  kMeasuredRuns, kTotalRuns - kMeasuredRuns, kMapPath);
    writeLine(line);

    doom_whd_source_close();

    // The blocks go back before the shell's first frame: a probe that left the
    // graphics and the decoded level held would be a state its caller never asked
    // for, exactly as the D2.3 probe releases its mapping.
    releaseWorkingSet();

    std::snprintf(line, sizeof line, "FRAMEPROF,heap,after_free=%u\n",
                  static_cast<unsigned>(esp_get_free_heap_size()));
    writeLine(line);
}

}  // namespace doomc
}  // namespace doom

#elif defined(PIXELROOT32_DOOM_FRAME_PROBE)

// Host build with the flag set: there is no partition to map and no
// `esp_timer.h`, so the probe has nothing to measure. The accumulator and the
// formatters above are what a host test drives.
namespace doom {
namespace doomc {

void frameProbeRun(std::uint8_t* surface, int width, int height) {
    (void)surface;
    (void)width;
    (void)height;
}

}  // namespace doomc
}  // namespace doom

#endif


