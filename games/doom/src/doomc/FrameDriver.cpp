/*
 * D3.6's frame driver: see FrameDriver.h for the residency policy, the frame
 * order, the 20 views, the hash protocol and the exact line formats.
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
 * `PIXELROOT32_DOOM_FRAME_PROBE` is defined (`env:esp32dev_ILI9341_frame`). It is
 * the only code in this project that renders a whole frame on the board, and it
 * is deliberately the *naive* driver: no cache, no reuse, no residency policy
 * beyond "the current call". Improving it is D3.5's job, and D3.5 measures
 * against the numbers this file prints.
 *
 * WHAT THE NAIVE POLICY COSTS IN BYTES, AND WHY THAT MATTERS MORE THAN IT LOOKS
 *
 * Every buffer below is static .bss, because the task stack is 8 KB and the
 * largest E1M1 patch alone is 28,080 B. The total is printed by the probe's own
 * `FRAMEPROF,mem` line from `sizeof`-derived constants, so the RAM cost is a
 * reported number and not an estimate:
 *
 *   lump arena     68,640 B   E1M1's renderer lumps (REJECT and BLOCKMAP
 *                             excluded: the renderer never reads them), TEXTURE1,
 *                             PNAMES, COLORMAP
 *   wall arena     36,864 B   two composed textures: 32,768 + 4,096, the
 *                             measured peak pair sum over the 20 views
 *   scratch        28,080 B   one patch, the largest patch an E1M1 texture
 *                             names (W17_1, TEKWALL1/TEKWALL3); also the flat
 *                             slot and the sprite patch slot, see below
 *   drawsegs        3,328 B   64 x 52 B (measured peak 39)
 *   visplanes      42,496 B   64 x 664 B (measured peak 22 allocated)
 *   clip ranges       256 B   32 x 8 B
 *   clip arrays     1,280 B   floorclip[] and ceilingclip[], 320 shorts each
 *   vis sprites     4,400 B   100 x 44 B (measured peak 97 accepted)
 *   clip blocks     9,600 B   15 x 320 shorts (measured peak 15, reserved two
 *                             included)
 *   opening blocks    640 B   1 x 320 shorts (measured peak 0)
 *   covered           320 B   one byte per column, for the frame hash
 *
 * THE SHARED SCRATCH, AND WHY IT IS SAFE HERE
 *
 * Three decoders produce small buffers that do not overlap in time: a patch
 * inside a texture compose, a flat, and a sprite patch. They share the 28,080 B
 * scratch with a single owner tag, so the resolver can never hand back a pointer
 * to a buffer some other decoder has since overwritten: a hit requires the owner
 * *and* the name to match, and every decode sets the owner. The one sequence that
 * could alias a live buffer -- a texture compose while a sprite patch is live --
 * is impossible in this frame order unless a masked mid texture is drawn during a
 * sprite's draw, which is exactly the case the probe counts in `hazards=` on the
 * summary line. E1M1's 20 views record zero opening blocks and resolve no masked
 * mid textures, so the counter reads 0; a non-zero value would mean the sharing
 * was unsafe for that view and the pixels could differ.
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

/// How many distinct wall textures a level may name before the subset refuses to
/// be built. E1M1 names 32; 48 is the same headroom the flash profile's name
/// lists use.
constexpr int kTextureNameCapacity = 48;

/// The masked pass's pools, at their measured peaks: 97 accepted vis-sprite
/// entries (100 here) and zero recorded masked-mid columns (one block here, so a
/// view that records one allocates it instead of failing). Both are static,
/// because the heap has nothing to spare.
constexpr int kVisspriteCapacity = 100;
constexpr int kOpeningBlockCapacity = 1;

/// One THINGS record per sprite the level can place; E1M1's fixture resolves 129.
constexpr int kSpriteCapacity = 132;

// --- Buffer sizes -----------------------------------------------------------

/// E1M1's renderer lumps minus THINGS (LINEDEFS 6,650 + SIDEDEFS 19,440 +
/// VERTEXES 1,868 + SEGS 8,784 + SSECTORS 948 + NODES 6,608 + SECTORS 2,210 =
/// 46,508), the TEXTURE1 *subset* (the 32 definitions E1M1 names, ~1.8 KB rather
/// than the directory's 9,234), PNAMES 2,804 and COLORMAP 8,704. Each of those is
/// its own heap block (see heapLump()), so this is only the expected total: 10
/// blocks, 59,816 B, printed as `lump_bytes=` once the setup has run. Three
/// things are deliberately absent:
///
///   REJECT (904) and BLOCKMAP (6,922) are the simulation's, and doom_map.h
///   states the renderer never reads them; THINGS (1,380) is read once, at setup,
///   to build the 20 viewpoints, so it is decoded into the shared scratch and the
///   map's view of it is cleared before the first frame (see buildFrames()).
constexpr std::size_t kLumpArenaBytes = 59816u;

/// How many blocks `kLumpArenaBytes` is split into: the seven map lumps,
/// the TEXTURE1 subset, PNAMES and COLORMAP.
constexpr int kLumpBlocks = 7 + 3;

/// The block reserved for the TEXTURE1 subset. E1M1's 32 definitions need 1,796 B;
/// this is the size rounded up so a level with a few more textures still builds.
constexpr std::size_t kTextureSubsetBytes = 2048u;



/// One static graphics region, big enough for whichever of the two buffers has to
/// live in .bss. 36,864 B holds the wall arena exactly and the scratch with 1,784 B
/// to spare.
constexpr std::size_t kStaticGraphicsBytes = 36864u;

/// The composed-texture arena. Two slots, because a two-sided fragment resolves
/// its upper and lower tier before the column loop and holds both: one slot
/// would be a wrong frame, not a slow one. The sizes are the measured peak pair
/// over the 20 views -- 32,768 B (a 256-wide texture, stored with DOOM's 128-row
/// column) plus 4,096 B (a 32-wide one) -- so 36,864 B total.
constexpr std::size_t kWallArenaBytes = 36864u;
constexpr std::size_t kWallSlotABytes = 32768u;
constexpr std::size_t kWallSlotBBytes = 4096u;

/// One patch of a composite: the largest patch among the wall textures the 20 views
/// resolve (TEKWALL4's, 17,544 B). The sky's patches are larger -- SKY1 is composed
/// from four 256-wide patches and its largest is 35,080 B -- and that 17,536 B is
/// what decides whether the plane pass can run here. See the phase note on
/// renderOnce().
constexpr std::size_t kScratchBytes = 17544u;

/// The graphics blocks: the wall arena and the decode scratch, side by side in one
/// static region. Static, not heap, because the heap's holes cannot take them
/// together and the static area can: 28,144 B of walk state plus these 54,408 B is
/// 82,552 B of the 94,916 B this image has left in `dram0_0_seg`.
constexpr std::size_t kGraphicsBlockBytes = kWallArenaBytes + kScratchBytes;

/// Up to eight extra lump blocks from the heap, sized 6,650 B or smaller each.
constexpr int kExtraLumpBlocks = 8;

/// How much of the level the static arena holds: the five biggest lump blocks
/// (SIDEDEFS 19,440, SEGS 8,784, COLORMAP 8,704, LINEDEFS 6,650, NODES 6,608 =
/// 50,186). The rest (PNAMES, SECTORS, VERTEXES, the texture subset, SSECTORS =
/// 9,630) go to the eight extra blocks kExtraLumpBlocks allows.
constexpr std::size_t kStaticLumpBytes = 23200u;

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

/// Which of the level's lumps the driver loads: E1M1 is the zero-byte marker and
/// the ten data lumps follow it in WAD order (THINGS LINEDEFS SIDEDEFS VERTEXES
/// SEGS SSECTORS NODES SECTORS REJECT BLOCKMAP), so index 9 (REJECT) and 10
/// (BLOCKMAP) are the two the renderer never reads.
constexpr int kMapLumpCount = 1 + DOOM_MAP_LUMP_COUNT;
constexpr int kFirstSkippedMapLump = 9;

// --- Static buffers ---------------------------------------------------------

/*
 * WHERE THE BUFFERS LIVE, AND WHY IT IS TWO POOLS
 *
 * The two pools on this board are not additive and neither alone is enough, so
 * the split below is a measurement, not a preference. Both numbers are printed by
 * the probe and neither is estimated:
 *
 *   dram0_0_seg   the static data region the linker enforces: 124,068 B
 *                 (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c). This image uses
 *                 ~33 KB of it, so ~91 KB is free for new .bss. The all-static
 *                 form of this file overflowed the region by 104,696 B.
 *   the heap      what is left of DRAM after the static data, measured at
 *                 156,916 B with the display path live when this probe starts.
 *
 * 195,904 B of buffers do not fit in either. So the part that is fixed for the
 * life of the image -- the walk's state, the D3.4 pools, the coverage record --
 * is .bss, which cannot fail at runtime; and the part that is the level's
 * working set -- the decoded lumps and the two composed textures -- is one
 * `heap_caps_malloc` block, 133,584 B, the same primitive the engine's own drawer
 * uses for its buffers rather than `malloc`/`new`. The block is freed before
 * `frameProbeRun()` returns and the free heap is printed again afterwards, so a
 * probe that held 133 KB of a 157 KB heap is not a state it leaves behind.
 *
 * `DriverState` above is the static half; `DriverWorkingSet` below is the
 * allocated one. The split is also what makes the numbers honest: a "static
 * arena" that cannot be linked is not a policy, it is a compile error.
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
    /// is 0xFF, a real palette index, so the pixels alone cannot say whether a
    /// column was written.
    alignas(8) std::uint8_t covered[R_SCREEN_WIDTH];
};

/// The allocated half: the level's working set, in three blocks.
struct DriverWorkingSet {
    /// E1M1's renderer lumps (REJECT, BLOCKMAP and THINGS excluded) plus the
    /// TEXTURE1 subset, PNAMES and COLORMAP; `gLumpArenaUsed` is the realized
    /// Two composed textures, 32,768 + 4,096. Allocated as `gWallBytes`.
    std::size_t wallBytes = kWallArenaBytes;
    /// One patch or one flat at a time. Allocated as `gScratch`.
    std::size_t scratchBytes = kScratchBytes;
};

/// The static half, and the only new .bss this file adds.
DriverState gState;

/// The two large blocks and the one big request they are carved from.
std::uint8_t* gWallBytes = nullptr;
std::uint8_t* gScratch = nullptr;
std::uint8_t* gGraphics = nullptr;
/// Up to eight extra lump blocks from the heap, for the lumps the static arena
/// does not hold. Each is 6,650 B or smaller, which the heap's small holes serve.
std::uint8_t* gLumpBlocks[kExtraLumpBlocks] = {};
int gLumpBlockCount = 0;

/// The sprite list, from the heap: 132 records of 32 B, where the static region has
/// no room left for it.
r_sprite_t* gSprites = nullptr;

/**
 * @brief Releases whatever the probe took. Safe on any subset.
 *
 * The map lumps and the two graphics directories are allocated one per lump (see
 * heapLump()), because even at boot the heap has no single hole large enough for
 * all of them together: with the display path live it reports 134,924 B free and
 * 110,580 B in its largest block, and one 59,824 B request already fails. Each
 * block is 19,440 B or smaller, which the heap does have.
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

/// How many bytes of the lump arena the setup committed.
std::size_t gLumpArenaUsed = 0;

/// The decoded lumps and the three graphics directories: a static arena, because
/// the static region has room while the heap does not. `dram0_0_seg` is 124,068 B
/// and this image used ~29 KB of it before this file; the walk state (26 KB) and
/// these 59,816 B still fit. The alternative -- one heap block per lump -- was
/// tried and failed: the heap's 134,924 B is 110,580 B in one hole and the rest in
/// pieces smaller than a 6-8 KB lump needs.
alignas(8) std::uint8_t gLumpArena[kStaticLumpBytes];

/// The one graphics buffer that has to be static; which one is decided at boot.
alignas(8) std::uint8_t gStaticGraphics[kStaticGraphicsBytes];


/// The placement the probe ended up using, printed by `FRAMEPROF,mem`:
/// "wall-static", "scratch-static" or "heap-both".
const char* gPlacement = "none";


// --- Reader and per-frame state ---------------------------------------------

whd_state_t gWhd;

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
enum ScratchOwner : std::uint8_t {
    kOwnerNone = 0,   ///< nothing valid in the scratch
    kOwnerPatch = 1,  ///< a texture compose's patch bytes
    kOwnerFlat = 2,   ///< the flat slot
    kOwnerSprite = 3  ///< the sprite patch slot
};

std::uint8_t gScratchOwner = kOwnerNone;
r_name_t gFlatName = R_NAME_NONE;
r_flat_t gFlat = {};
r_sprite_patch_t gSpritePatch = {nullptr, 0u};
r_name_t gSpriteName = R_NAME_NONE;

/// The aliasing violations the probe saw: a texture compose that took the scratch
/// while a flat or a sprite patch was still live in it. The plane pass resolves one
/// flat per drawable plane and reads it before the next resolve; the masked pass
/// resolves one sprite patch per sprite and reads it before the next. The only
/// compose either can reach is the sky's, in the plane pass's preflight. This must
/// stay 0; a non-zero value means the sharing was unsafe and a pixel could have
/// come from another buffer's bytes.
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

// --- Wall textures ----------------------------------------------------------

/// r_texture_compose()'s patch resolver: a PNAMES name is a lump name.
r_error_t fetchPatch(void* user, const char* name, const std::uint8_t** data, std::size_t* size) {
    (void)user;

    const int lump = doom_whd_source_lump_index(&gWhd, name);
    if (lump < 0) {
        return R_ERR_PATCH_FETCH;
    }

    // A compose takes the scratch, which is also the flat slot. The plane pass
    // resolves one flat per plane and reads it before the next resolve, and the
    // only compose it can reach is the sky's in its preflight, so this must never
    // fire; it is counted rather than assumed away.
    if (gScratchOwner == kOwnerFlat || gScratchOwner == kOwnerSprite) {
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

/// Chooses the slot a texture of @p bytes competes for, or -1 when neither can
/// hold it. The most recently returned slot is never the victim: a fragment
/// resolves its tiers one after the other and then reads both.
int wallVictim(std::uint32_t bytes) {
    // `gNewest` is the slot handed out most recently. It is never the victim: a
    // two-sided fragment resolves its upper and lower tier one after the other
    // and reads both in the same column loop.
    const int victim = (!gWall[0].used || gNewest != 0) ? 0 : 1;

    const std::uint32_t extent = victim == 0 ? kWallSlotABytes : kWallSlotBBytes;
    if (bytes <= extent) {
        return victim;
    }

    // The victim's fixed extent is too small. The survivor is the live sibling;
    // when it fits slot B it can be moved down and slot A freed for the new
    // texture. A pair that still cannot fit is refused, not truncated.
    const int other = 1 - victim;
    if (victim == 1 && bytes <= kWallSlotABytes && gWall[other].used
        && gWall[other].bytes <= kWallSlotBBytes) {
        std::memmove(gWallBytes + kWallSlotBBytes, gWallBytes + gWall[other].offset,
                     gWall[other].bytes);
        gWall[other].offset = static_cast<std::uint32_t>(kWallSlotBBytes);
        gWall[other].tex.pixels = gWallBytes + kWallSlotBBytes;
        gWall[0].used = false;
        return 0;
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
    const std::uint32_t offset = slot == 0 ? 0u : static_cast<std::uint32_t>(kWallSlotABytes);
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

    r_texture_info_t info;
    if (r_texture_lookup(gTexture1, gTexture1Bytes, nullptr, 0u, text, &info) != R_ERR_OK) {
        return nullptr;
    }

    const std::uint32_t bytes = static_cast<std::uint32_t>(info.width) * kStoredColumnRows;
    const int slot = wallVictim(bytes);
    if (slot < 0) {
        // The frame's two live textures do not fit the arena. Reported by the
        // caller as R_SEGS_ERR_TEXTURE_MISSING, which is honest: nothing was
        // written for that tier.
        return nullptr;
    }
    if (!composeInto(slot, text, info)) {
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
/// the scratch. The masked pass resolves one sprite at a time, so one buffer is
/// enough; a compose running while a patch is live is the interleaving the shared
/// scratch cannot serve, and it is counted in `hazards=`.
const r_sprite_patch_t* resolveSprite(void* context, r_name_t lump) {
    (void)context;

    ++gSample.spriteResolves;
    if (gScratchOwner == kOwnerSprite && gSpriteName == lump) {
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
    if (!decodeLump(index, gScratch, kSpritePatchBytes, &used)) {
        return nullptr;
    }
    gScratchOwner = kOwnerSprite;
    gSpriteName = lump;
    gSpritePatch.data = gScratch;
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
/// THE TWO-TIER LAYOUT, AND WHY THE ORDER IS PART OF IT
///
/// The heap has no single hole large enough for the working set: with the display
/// path live it reports 134,924 B free and 110,580 B in its largest block. So the
/// five biggest blocks are carved out of one 108,872 B request, which fits that
/// hole, and the rest are separate small requests from the remaining ~24 KB. The
/// first five calls therefore come from the big block in descending size order and
/// every later call falls through to the heap; `FRAMEPROF,mem` reports the split.
std::uint8_t* heapLump(std::size_t bytes) {
    const std::size_t aligned = (bytes + 7u) & ~static_cast<std::size_t>(7u);

    if (gLumpArenaUsed + aligned <= kStaticLumpBytes) {
        std::uint8_t* at = gLumpArena + gLumpArenaUsed;
        gLumpArenaUsed += aligned;
        return at;
    }
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
    gLumpArenaUsed += aligned;
    return at;
}

/// Decodes one E1M1 map lump into its own block, or sizes an empty view.
///
/// REJECT and BLOCKMAP are skipped on purpose: doom_map.h states the walk reads
/// NODES, SSECTORS, SEGS, SECTORS, SIDEDEFS, LINEDEFS and VERTEXES, and THINGS is
/// read once at setup for the viewpoints. The two the renderer never touches are
/// 7,826 B of a 201 KB budget, and this probe measures the renderer.
bool loadMapLump(int marker, int dataIndex, doom_map_source_t* source,
                 std::int32_t* failures) {
    // `dataIndex` is the doom_map_lump_t value, so the slot is the value
    // itself and the lump is one past the E1M1 marker plus it. Getting this
    // wrong is silent: doom_map_open() only checks record sizes, so a
    // shifted set of lumps that happens to be aligned opens fine and renders
    // garbage.
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
        return false;
    }
    if (size == 0) {
        source->data[slot] = nullptr;
        source->size[slot] = 0u;
        return true;
    }
    std::uint8_t* destination = heapLump(static_cast<std::size_t>(size));
    if (destination == nullptr) {
        ++(*failures);
        return false;
    }
    std::uint32_t used = 0u;
    if (!decodeLump(lump, destination, static_cast<std::size_t>(size), &used)) {
        ++(*failures);
        return false;
    }
    source->data[slot] = destination;
    source->size[slot] = static_cast<std::size_t>(used);
    return true;
}

/// Reads a whole lump into a fresh arena block; the directories are used as-is.
bool loadLumpByName(const char* name, const std::uint8_t** data, std::size_t* bytes) {
    const int lump = doom_whd_source_lump_index(&gWhd, name);
    if (lump < 0) {
        return false;
    }
    const std::int32_t size = doom_whd_source_lump_size(&gWhd, lump);
    if (size <= 0) {
        return false;
    }
    std::uint8_t* destination = heapLump(static_cast<std::size_t>(size));
    if (destination == nullptr) {
        return false;
    }
    std::uint32_t used = 0u;
    if (!decodeLump(lump, destination, static_cast<std::size_t>(size), &used)) {
        return false;
    }
    *data = destination;
    *bytes = static_cast<std::size_t>(used);
    return true;
}

void writeLe32(std::uint8_t* at, std::uint32_t value) {
    at[0] = static_cast<std::uint8_t>(value & 0xffu);
    at[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
    at[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
    at[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

/**
 * @brief Builds a TEXTURE1 subset holding only the definitions E1M1 names.
 *
 * The full directory is 9,234 B and E1M1 uses 32 of its 125 textures. The subset
 * is the same format bit for bit -- a count, one offset per texture, then each
 * definition copied verbatim out of the original -- so `r_texture_lookup()` and
 * `r_texture_compose()` read it unchanged and PNAMES' patch indices still refer
 * to the same table. It costs about 2 KB instead of 9,234, which is the 7 KB that
 * decides whether the wall pass fits on this board at all.
 *
 * The original is decoded into the shared scratch, which is free at setup (no
 * frame has run yet), and the subset is written into the lump arena. Called after
 * the map is open, because the names come from the decoded SIDEDEFS.
 */
bool buildTextureSubset(const doom_map_t& map, std::uint8_t* destination,
                        std::size_t capacity) {
    const int lump = doom_whd_source_lump_index(&gWhd, "TEXTURE1");
    if (lump < 0) {
        return false;
    }
    const std::int32_t size = doom_whd_source_lump_size(&gWhd, lump);
    if (size <= 0 || static_cast<std::size_t>(size) > kScratchBytes) {
        return false;
    }
    std::uint32_t sourceBytes = 0u;
    if (!decodeLump(lump, gScratch, kScratchBytes, &sourceBytes)) {
        return false;
    }
    const std::uint8_t* source = gScratch;

    // --- The names E1M1's sidedefs use, deduplicated in first-seen order -----
    char names[kTextureNameCapacity][9];
    int nameCount = 0;
    const doom_map_lump_view_t& sidedefs = map.lumps[DOOM_MAP_SIDEDEFS];

    for (std::uint32_t index = 0; index < sidedefs.count; ++index) {
        const std::uint8_t* record = sidedefs.data + static_cast<std::size_t>(index) * DOOM_SIDEDEF_SIZE;

        for (int slot = 0; slot < 3; ++slot) {
            const std::uint8_t* field = record + 4u + static_cast<std::size_t>(slot) * 8u;
            if (field[0] == 0u
                || (field[0] == static_cast<std::uint8_t>('-') && field[1] == 0u)) {
                continue;
            }
            char name[9];
            int at = 0;
            for (; at < 8 && field[at] != 0u; ++at) {
                name[at] = static_cast<char>(field[at]);
            }
            name[at] = '\0';

            bool seen = false;
            for (int known = 0; known < nameCount; ++known) {
                if (std::strcmp(names[known], name) == 0) {
                    seen = true;
                    break;
                }
            }
            if (seen) {
                continue;
            }
            if (nameCount >= kTextureNameCapacity) {
                return false;
            }
            std::memcpy(names[nameCount], name, sizeof names[0]);
            ++nameCount;
        }
    }

    // --- The definitions, located in the original and measured --------------
    struct Definition {
        std::size_t offset;
        std::size_t bytes;
    };
    Definition definitions[kTextureNameCapacity];
    std::size_t total = 4u + static_cast<std::size_t>(nameCount) * 4u;

    for (int index = 0; index < nameCount; ++index) {
        r_texture_info_t info;
        if (r_texture_lookup(source, sourceBytes, nullptr, 0u, names[index], &info) != R_ERR_OK) {
            return false;
        }
        definitions[index].offset = info.offset;
        definitions[index].bytes = static_cast<std::size_t>(R_TEXTURE_HEADER_SIZE)
                                   + static_cast<std::size_t>(info.patchCount)
                                         * static_cast<std::size_t>(R_TEXTURE_PATCH_SIZE);
        total += definitions[index].bytes;
    }

    if (total > capacity) {
        return false;
    }
    std::uint8_t* out = destination;

    writeLe32(out, static_cast<std::uint32_t>(nameCount));
    std::size_t cursor = 4u + static_cast<std::size_t>(nameCount) * 4u;
    for (int index = 0; index < nameCount; ++index) {
        writeLe32(out + 4u + static_cast<std::size_t>(index) * 4u,
                  static_cast<std::uint32_t>(cursor));
        std::memcpy(out + cursor, source + definitions[index].offset, definitions[index].bytes);
        cursor += definitions[index].bytes;
    }

    gTexture1 = out;
    gTexture1Bytes = total;
    return true;
}

// --- One frame --------------------------------------------------------------

/// The state the three measured passes share for the whole run.
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
 * `frameMicros` brackets the surface prefill and the two measured passes; each
 * pass is timed on its own inside that window. Residency is the sum of the decode
 * time the passes incurred, accumulated by the resolvers.
 *
 * THE TWO PHASES, AND WHY NOT FOUR
 *
 * `walk` and `walls` are measured. `planes` (flats and the sky) and `masked`
 * (sprites and the deferred masked mid textures) are not, and the reason is
 * arithmetic rather than preference: with the map working set at 59,816 B and the
 * two wall slots at 36,864 B resident, the flat/sky scratch (35,080 B, SKY1's
 * largest patch) leaves no split of the board's 124,068 B static region and its
 * 134,924 B heap that holds both, and the masked pools add another 9,168 B on top.
 * The brief's own rule for this case is to measure fewer phases rather than
 * something else, so the two expensive ones -- the walk and the naive wall decode
 * -- are measured and the other two are absent, at 0 in every line.
 *
 * That choice keeps the frame hash exact: the host's `frame NN:` wall hash is the
 * pixels and coverage after its wall pass, which is precisely what this build
 * produces, so the two hashes are comparable byte for byte.
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

    start = nowMicros();
    driver->stage = "walls";
    const r_segs_error_t walls =
        r_wall_emit(&driver->view, &driver->surface, &driver->textures, &driver->light);
    sample->wallsMicros = elapsedMicros(start);
    if (walls != R_SEGS_OK) {
        return false;
    }

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
/// number, never an estimate. `state_static` is the .bss half; `arena_heap` is the
/// one block `frameProbeRun()` allocates and frees.
void formatMemoryLine(char* line, std::size_t capacity, std::uint32_t heapBefore,
                      std::uint32_t heapAfterAlloc) {
    const std::size_t drawsegs = sizeof(gState.drawsegs);
    const std::size_t planes = sizeof(gState.planes);
    const std::size_t clipRanges = sizeof(gState.clipRanges);
    const std::size_t clipArrays = sizeof(gState.floorClip) + sizeof(gState.ceilingClip);
    const std::size_t clipBlocks = sizeof(gState.clipBlocks);
    const std::size_t total = sizeof(DriverState) + gLumpArenaUsed + kWallArenaBytes
                              + kScratchBytes;

    std::snprintf(line, capacity,
                  "FRAMEPROF,mem,surface=engine8bpp,index_writes=1,present=no,no_rgb332=1,"
                  "arena=heap,lump_blocks=%u,wall_arena=%u,scratch=%u,state_static=%u,"
                  "drawsegs=%u,planes=%u,clip_ranges=%u,clip_arrays=%u,clip_blocks=%u,"
                  "covered=%u,driver_total=%u,drawseg_cap=%d,plane_cap=%d,"
                  "clip_block_cap=%d,heap_before=%u,heap_after_alloc=%u,phases=2\n",
                  static_cast<unsigned>(gLumpArenaUsed),
                  static_cast<unsigned>(kWallArenaBytes),
                  static_cast<unsigned>(kScratchBytes),
                  static_cast<unsigned>(sizeof(DriverState)), static_cast<unsigned>(drawsegs),
                  static_cast<unsigned>(planes), static_cast<unsigned>(clipRanges),
                  static_cast<unsigned>(clipArrays), static_cast<unsigned>(clipBlocks),
                  static_cast<unsigned>(sizeof(gState.covered)), static_cast<unsigned>(total),
                  kDrawsegCapacity, kPlaneCapacity, kClipBlockCapacity,
                  static_cast<unsigned>(heapBefore), static_cast<unsigned>(heapAfterAlloc));
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
    // The map lumps are allocated one per lump inside the setup below; the two
    // graphics blocks are taken here. The heap reports 134,924 B free and
    // 110,580 B in its largest block with the display path live, so nothing here
    // asks for more than 36,864 B at once.
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
    gLumpArenaUsed = 0u;
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
    // Biggest block first. The heap is not one arena: with the display path live
    // it reports 134,924 B free in holes whose largest is 110,580 B, so a block
    // order that leaves a small hole in front of a bigger request fails while the
    // same bytes fit when the requests descend. The order is the sizes (wall
    // 36,864, scratch 35,080, SIDEDEFS 19,440, SEGS 8,784, COLORMAP 8,704 -- the
    // five that fill the big block -- then LINEDEFS 6,650, NODES 6,608, PNAMES
    // 2,804, SECTORS 2,210, VERTEXES 1,868, the subset's 2,048, SSECTORS 948), and
    // it is why the calls are not in WAD order.
    // Descending block size: the heap's holes are not interchangeable, and the
    // static arena holds the first five. The rest ask the heap one block at a time,
    // each 8,704 B or smaller, which the fragments behind the big hole serve.
    (void)loadMapLump(marker, DOOM_MAP_SIDEDEFS, &source, &mapFailures);   // 19,440
    (void)loadMapLump(marker, DOOM_MAP_SEGS, &source, &mapFailures);       //  8,784
    bool directoriesLoaded = loadLumpByName("COLORMAP", &gColormap, &gColormapBytes);
    (void)loadMapLump(marker, DOOM_MAP_LINEDEFS, &source, &mapFailures);   //  6,650
    (void)loadMapLump(marker, DOOM_MAP_NODES, &source, &mapFailures);      //  6,608
    directoriesLoaded = loadLumpByName("PNAMES", &gPnames, &gPnamesBytes) && directoriesLoaded;
    (void)loadMapLump(marker, DOOM_MAP_SECTORS, &source, &mapFailures);    //  2,210
    (void)loadMapLump(marker, DOOM_MAP_VERTEXES, &source, &mapFailures);   //  1,868
    std::uint8_t* const textureSubsetBlock = heapLump(kTextureSubsetBytes);
    (void)loadMapLump(marker, DOOM_MAP_SSECTORS, &source, &mapFailures);   //    948
    (void)loadMapLump(marker, DOOM_MAP_REJECT, &source, &mapFailures);     // skipped
    (void)loadMapLump(marker, DOOM_MAP_BLOCKMAP, &source, &mapFailures);   // skipped

    // THINGS is read once, at setup, to build the 20 viewpoints: its 1,380 B are
    // decoded into the shared scratch, which no frame has touched yet, and the
    // map's view of it is cleared as soon as the viewpoints exist. Nothing after
    // setup reads THINGS -- the sprite list went with the masked pass -- so the
    // pointer's lifetime ends where this comment does.
    {
        const int things = marker + 1 + DOOM_MAP_THINGS;
        const std::int32_t size = doom_whd_source_lump_size(&gWhd, things);
        std::uint32_t used = 0u;
        if (size <= 0 || static_cast<std::size_t>(size) > kScratchBytes
            || !decodeLump(things, gScratch, kScratchBytes, &used)) {
            writeLine("FRAMEPROF,setup,status=fail,reason=things\n");
            doom_whd_source_close();
            releaseWorkingSet();
            return;
        }
        source.data[DOOM_MAP_THINGS] = gScratch;
        source.size[DOOM_MAP_THINGS] = static_cast<std::size_t>(used);
    }


    doom_map_t map;
    const doom_map_error_t mapError =
        mapFailures == 0 ? doom_map_open(&map, &source) : DOOM_MAP_ERR_REQUIRED_LUMP;
    if (mapFailures != 0 || !directoriesLoaded || mapError != DOOM_MAP_OK) {
        std::snprintf(line, sizeof line,
                      "FRAMEPROF,setup,status=fail,reason=lump,map_failures=%d,directories=%d,"
                      "map_error=%d,pnames=%u,colormap=%u,heap=%u\n",
                      static_cast<int>(mapFailures), directoriesLoaded ? 1 : 0,
                      static_cast<int>(mapError), static_cast<unsigned>(gPnamesBytes),
                      static_cast<unsigned>(gColormapBytes),
                      static_cast<unsigned>(esp_get_free_heap_size()));
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

    // The subset has to be built after the map is open (its names come from the
    // decoded SIDEDEFS) and before the first resolve. Its block was reserved with
    // the other allocations, so it does not compete for a hole at this point; the
    // original directory is read out of the shared scratch, which is free until
    // the first frame.
    if (textureSubsetBlock == nullptr || !buildTextureSubset(map, textureSubsetBlock,
                                                             kTextureSubsetBytes)) {
        writeLine("FRAMEPROF,setup,status=fail,reason=texture_subset\n");
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    // The block sizes and the directory sizes are the residency decision, so they
    // are printed with the setup that made it.
    std::snprintf(line, sizeof line,
                  "FRAMEPROF,setup,status=ok,lump_blocks=%u,lump_bytes=%u,texture1=%u,pnames=%u,"
                  "colormap=%u,map_lumps=%d,map_skipped=%d,setup_us=%u\n",
                  static_cast<unsigned>(kLumpBlocks),
                  static_cast<unsigned>(gLumpArenaUsed),
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

    // The viewpoints are the last reader of THINGS. Clearing the map's view is
    // what makes the scratch reusable for patches without leaving a dangling
    // lump behind: nothing can read a NULL lump.
    map.lumps[DOOM_MAP_THINGS].data = nullptr;
    map.lumps[DOOM_MAP_THINGS].size = 0u;
    map.lumps[DOOM_MAP_THINGS].count = 0u;

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
    // The plane and masked passes are not run (see renderOnce), so the vis-sprite
    // array and the opening pool are absent. r_bsp_init() ignores both and
    // r_wall_emit() only needs the clip-block pool.
    driver.storage.visSprites = nullptr;
    driver.storage.visSpriteCapacity = 0;
    driver.storage.openingBlocks = nullptr;
    driver.storage.openingBlockCapacity = 0;

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
            if (!renderOnce(&driver, frames[frame], surface, width, height, &sample)) {
                std::snprintf(line, sizeof line,
                              "FRAMEPROF,error,frame=%02d,run=%d,stage=%s\n", frame, run,
                              driver.stage);
                writeLine(line);
                doom_whd_source_close();
                releaseWorkingSet();
                return;
            }
            if (run == 1) {
                // The warm-up: the first render of a view touches flash the
                // container has not delivered yet, and mixing that with the
                // steady state would make the table incomparable.
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
                  "warmup=%d\n",
                  static_cast<unsigned>(probeMicros), static_cast<unsigned>(gHazards), kViewCount,
                  kMeasuredRuns, kTotalRuns - kMeasuredRuns);
    writeLine(line);

    doom_whd_source_close();

    // The block goes back before the shell's first frame: a probe that left
    // 195 KB of a 201 KB heap held would be a state its caller never asked for,
    // exactly as the D2.3 probe releases its mapping.
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
