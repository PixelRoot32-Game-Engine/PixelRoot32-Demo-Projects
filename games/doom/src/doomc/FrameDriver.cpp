/*
 * The frame driver: see FrameDriver.h for the four phases, the two map paths,
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
 * renders a whole frame on the board. Every wall texture, the sky and every flat
 * is a raw payload baked at build time by tools/bake_graphics.py and named by the
 * container's ~PWBAKE~ manifest, so a resolve is a manifest lookup and a pointer
 * into the mapping: there is no texture cache and no decoder on this path.
 *
 * WHAT THE FRAME COSTS, IN BYTES
 *
 * Every large buffer below is a reported size, not an estimate: `FRAMEPROF,mem`
 * prints each one from `sizeof` or from the bootstrap that allocated it.
 *
 *   walk state      68,672 B   drawsegs 6,656, visplanes 42,496, clip posts 256,
 *                              floorclip/ceilingclip 1,280, the unified clip pool
 *                              6,400, vis sprites 11,264, covered 320 (the
 *                              walkable capacities; the sampled set is smaller)
 *   baked records   12,312 B   the 187-entry manifest adapter, static .bss
 *   level                0 B   in the flash build the map is a pointer into the
 *                              mapping; 47,888 B in the RAM build
 *
 * A run-time texture cache (a 53,248 B static arena, up to 24,576 B of heap
 * arenas, 1,920 B of entries and a 35,080 B decode scratch, ~114,824 B in all)
 * is what baking avoids.
 *
 * THE TWO MAP PATHS
 *
 * `PIXELROOT32_DOOM_MAP_IN_RAM=1` decodes the map lumps into RAM, which is the
 * A/B's control; without it the map's lumps are raw container entries and
 * `doom_whd_source_lump_view()` hands out the mapped bytes with no copy at all.
 * The graphics are baked in both builds on purpose: the A/B isolates the map
 * path.
 *
 * NO FALLBACK, NO DECODE
 *
 * A name the manifest does not hold is a reported frame error
 * (R_SEGS_ERR_TEXTURE_MISSING, R_PLANE_ERR_FLAT_MISSING or
 * R_THINGS_ERR_SPRITE_MISSING), never a silent decode. The C referee in
 * test/unit/test_baked/tool recomposes every baked wall and sky from the
 * container's own patches through lib/doom/r_data.c and compares pixels and
 * mask byte for byte, which is the only proof the bake is correct.
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

    // The attribution: every field is folded here so no summary can read a
    // counter the frame path forgot to fill.
    stats->wallPixelsSum += sample.wallPixels;
    stats->planePixelsSum += sample.planePixels;
    stats->spritePixelsSum += sample.spritePixels;
    stats->pixelsSum += sample.pixels;
    stats->columnsSum += sample.wallColumns;
    stats->fragmentsSum += sample.wallFragments;
    stats->spansSum += sample.planeSpans;
    stats->skyColumnsSum += sample.skyColumns;
    stats->spriteColumnsSum += sample.spriteColumns;
    stats->decodeSum += sample.decodeMicros;
    stats->composeSum += sample.composeMicros;
    stats->lookupSum += sample.lookupMicros;
    stats->wallDecodeSum += sample.wallDecodeMicros;
    stats->planeDecodeSum += sample.planeDecodeMicros;
    stats->maskedDecodeSum += sample.maskedDecodeMicros;
    stats->wallComposeSum += sample.wallComposeMicros;
    stats->planeComposeSum += sample.planeComposeMicros;
    stats->maskedComposeSum += sample.maskedComposeMicros;
    stats->wallLookupSum += sample.wallLookupMicros;
    stats->planeLookupSum += sample.planeLookupMicros;
    stats->maskedLookupSum += sample.maskedLookupMicros;
    stats->texHitsSum += sample.texHits;
    stats->texMissesSum += sample.texMisses;
    stats->flatHitsSum += sample.flatHits;
    stats->flatMissesSum += sample.flatMisses;
    stats->spriteHitsSum += sample.spriteHits;
    stats->spriteMissesSum += sample.spriteMisses;
    stats->texSetSum += sample.texSetCount;
    stats->texSetBytesSum += sample.texSetBytes;
    stats->flatSetSum += sample.flatSetCount;
    stats->spriteSetSum += sample.spriteSetCount;
    stats->spriteSetBytesSum += sample.spriteSetBytes;
    if (sample.cacheBytes > stats->cacheBytesMax) {
        stats->cacheBytesMax = sample.cacheBytes;
    }
    if (sample.cachePeakSlots > stats->cachePeakMax) {
        stats->cachePeakMax = sample.cachePeakSlots;
    }
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

bool frameMapMarker(std::uint32_t level, char out[5]) {
    if (out == nullptr || level < 1u || level > 9u) {
        return false;
    }
    out[0] = 'E';
    out[1] = '1';
    out[2] = 'M';
    out[3] = static_cast<char>('0' + static_cast<char>(level));
    out[4] = '\0';
    return true;
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

std::size_t frameProfileFormatAttribution(const FrameProfileSample& sample, char* out,
                                          std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,attr,frame=%02u,run=%u,pixels=%u,wall_pixels=%u,plane_pixels=%u,"
        "sprite_pixels=%u,columns=%u,fragments=%u,plane_spans=%u,sky_columns=%u,"
        "sprite_columns=%u,decode_us=%u,compose_us=%u,lookup_us=%u,wall_decode_us=%u,"
        "plane_decode_us=%u,masked_decode_us=%u,wall_compose_us=%u,plane_compose_us=%u,"
        "masked_compose_us=%u,wall_lookup_us=%u,plane_lookup_us=%u,masked_lookup_us=%u,"
        "tex_hit=%u,tex_miss=%u,flat_hit=%u,flat_miss=%u,sprite_hit=%u,sprite_miss=%u,"
        "tex_set=%u,tex_set_bytes=%u,flat_set=%u,sprite_set=%u,sprite_set_bytes=%u,"
        "cache_bytes=%u,cache_peak=%u\n",
        static_cast<unsigned>(sample.frame), static_cast<unsigned>(sample.run),
        static_cast<unsigned>(sample.pixels), static_cast<unsigned>(sample.wallPixels),
        static_cast<unsigned>(sample.planePixels), static_cast<unsigned>(sample.spritePixels),
        static_cast<unsigned>(sample.wallColumns), static_cast<unsigned>(sample.wallFragments),
        static_cast<unsigned>(sample.planeSpans), static_cast<unsigned>(sample.skyColumns),
        static_cast<unsigned>(sample.spriteColumns), static_cast<unsigned>(sample.decodeMicros),
        static_cast<unsigned>(sample.composeMicros), static_cast<unsigned>(sample.lookupMicros),
        static_cast<unsigned>(sample.wallDecodeMicros),
        static_cast<unsigned>(sample.planeDecodeMicros),
        static_cast<unsigned>(sample.maskedDecodeMicros),
        static_cast<unsigned>(sample.wallComposeMicros),
        static_cast<unsigned>(sample.planeComposeMicros),
        static_cast<unsigned>(sample.maskedComposeMicros),
        static_cast<unsigned>(sample.wallLookupMicros),
        static_cast<unsigned>(sample.planeLookupMicros),
        static_cast<unsigned>(sample.maskedLookupMicros),
        static_cast<unsigned>(sample.texHits), static_cast<unsigned>(sample.texMisses),
        static_cast<unsigned>(sample.flatHits), static_cast<unsigned>(sample.flatMisses),
        static_cast<unsigned>(sample.spriteHits), static_cast<unsigned>(sample.spriteMisses),
        static_cast<unsigned>(sample.texSetCount), static_cast<unsigned>(sample.texSetBytes),
        static_cast<unsigned>(sample.flatSetCount), static_cast<unsigned>(sample.spriteSetCount),
        static_cast<unsigned>(sample.spriteSetBytes), static_cast<unsigned>(sample.cacheBytes),
        static_cast<unsigned>(sample.cachePeakSlots));

    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

std::size_t frameProfileFormatAttributionSummary(const FrameProfileStats& stats, char* out,
                                                 std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }

    const std::uint32_t samples = stats.samples;
    // The three raster remainders, each exclusive: the pass time less the decode,
    // compose and lookup time measured inside that same pass. A negative result
    // cannot happen (the buckets are subsets of the pass), but the clamp keeps a
    // future miscount from printing a wrapped 32-bit number.
    const std::uint32_t wallRaster = frameProfileMean(
        stats.wallsSum > stats.wallDecodeSum + stats.wallComposeSum + stats.wallLookupSum
            ? stats.wallsSum - stats.wallDecodeSum - stats.wallComposeSum - stats.wallLookupSum
            : 0u,
        samples);
    const std::uint32_t planeRaster = frameProfileMean(
        stats.planesSum > stats.planeDecodeSum + stats.planeComposeSum + stats.planeLookupSum
            ? stats.planesSum - stats.planeDecodeSum - stats.planeComposeSum
                  - stats.planeLookupSum
            : 0u,
        samples);
    const std::uint32_t maskedRaster = frameProfileMean(
        stats.maskedSum > stats.maskedDecodeSum + stats.maskedComposeSum + stats.maskedLookupSum
            ? stats.maskedSum - stats.maskedDecodeSum - stats.maskedComposeSum
                  - stats.maskedLookupSum
            : 0u,
        samples);
    const std::uint32_t pixels = frameProfileMean(stats.pixelsSum, samples);
    const std::uint32_t wallPixels = frameProfileMean(stats.wallPixelsSum, samples);
    const std::uint32_t columns = frameProfileMean(stats.columnsSum, samples);
    const std::uint32_t fragments = frameProfileMean(stats.fragmentsSum, samples);
    const std::uint32_t spans = frameProfileMean(stats.spansSum, samples);
    const std::uint32_t skyColumns = frameProfileMean(stats.skyColumnsSum, samples);
    const std::uint32_t spriteColumns = frameProfileMean(stats.spriteColumnsSum, samples);

    // Thousandths of a microsecond: 1,100 means 1.100 us per unit. A zero
    // denominator reports 0, never a divide.
    const std::uint32_t pixelNanos = wallPixels != 0u
                                         ? frameProfilePermille(wallRaster, wallPixels) : 0u;
    const std::uint32_t columnNanos = columns != 0u
                                          ? frameProfilePermille(wallRaster, columns) : 0u;
    const std::uint32_t fragmentNanos = fragments != 0u
                                            ? frameProfilePermille(wallRaster, fragments) : 0u;

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,attrsum,samples=%u,pixels=%u,wall_pixels=%u,plane_pixels=%u,"
        "sprite_pixels=%u,columns=%u,fragments=%u,plane_spans=%u,sky_columns=%u,"
        "sprite_columns=%u,decode_us=%u,compose_us=%u,lookup_us=%u,wall_decode_us=%u,"
        "plane_decode_us=%u,masked_decode_us=%u,wall_compose_us=%u,plane_compose_us=%u,"
        "masked_compose_us=%u,wall_lookup_us=%u,plane_lookup_us=%u,masked_lookup_us=%u,"
        "wall_raster_us=%u,plane_raster_us=%u,masked_raster_us=%u,px_us_permille=%u,"
        "col_us_permille=%u,frag_us_permille=%u,tex_hit=%u,tex_miss=%u,flat_hit=%u,"
        "flat_miss=%u,sprite_hit=%u,sprite_miss=%u,tex_hit_permille=%u,flat_hit_permille=%u,"
        "sprite_hit_permille=%u,tex_set=%u,tex_set_bytes=%u,flat_set=%u,sprite_set=%u,"
        "sprite_set_bytes=%u,cache_bytes=%u,cache_peak=%u\n",
        static_cast<unsigned>(samples), static_cast<unsigned>(pixels),
        static_cast<unsigned>(wallPixels),
        static_cast<unsigned>(frameProfileMean(stats.planePixelsSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.spritePixelsSum, samples)),
        static_cast<unsigned>(columns), static_cast<unsigned>(fragments), static_cast<unsigned>(spans),
        static_cast<unsigned>(skyColumns), static_cast<unsigned>(spriteColumns),
        static_cast<unsigned>(frameProfileMean(stats.decodeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.composeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.lookupSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.wallDecodeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.planeDecodeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.maskedDecodeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.wallComposeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.planeComposeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.maskedComposeSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.wallLookupSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.planeLookupSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.maskedLookupSum, samples)),
        static_cast<unsigned>(wallRaster), static_cast<unsigned>(planeRaster),
        static_cast<unsigned>(maskedRaster), static_cast<unsigned>(pixelNanos),
        static_cast<unsigned>(columnNanos), static_cast<unsigned>(fragmentNanos),
        static_cast<unsigned>(frameProfileMean(stats.texHitsSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.texMissesSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.flatHitsSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.flatMissesSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.spriteHitsSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.spriteMissesSum, samples)),
        static_cast<unsigned>(frameProfilePermille(stats.texHitsSum,
                                                   stats.texHitsSum + stats.texMissesSum)),
        static_cast<unsigned>(frameProfilePermille(stats.flatHitsSum,
                                                   stats.flatHitsSum + stats.flatMissesSum)),
        static_cast<unsigned>(frameProfilePermille(stats.spriteHitsSum,
                                                   stats.spriteHitsSum + stats.spriteMissesSum)),
        static_cast<unsigned>(frameProfileMean(stats.texSetSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.texSetBytesSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.flatSetSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.spriteSetSum, samples)),
        static_cast<unsigned>(frameProfileMean(stats.spriteSetBytesSum, samples)),
        static_cast<unsigned>(stats.cacheBytesMax), static_cast<unsigned>(stats.cachePeakMax));

    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

}  // namespace doomc
}  // namespace doom

// --- The board driver --------------------------------------------------------

#if (defined(PIXELROOT32_DOOM_FRAME_PROBE) || defined(PIXELROOT32_DOOM_SHOW_E1M1)) && \
    (defined(ESP32) || defined(ESP_PLATFORM))

#include <cstring>
#include <cstddef>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
// The loop task's stack margin. The firing path is the deepest call chain the
// game has, so its margin is printed to rule a stack overflow in or out.
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "doomc/BakedGraphics.h"
#include "doomc/DoomConsole.h"
#include "doomc/InputMap.h"
#include "doomc/WhdSource.h"

/*
 * `LevelFailure.cpp` restates the adapter's open-path numbers so the taxonomy
 * compiles without lib/doom (the host gate builds it with `-I src -I src/doomc`
 * only). This is the one translation unit that sees both vocabularies, so this is
 * where the restatement is pinned: a renumbered adapter code stops the build
 * instead of putting the wrong reason on the panel.
 */
static_assert(DOOM_WHD_SOURCE_OK == 0, "levelFailureFromWhd: OK must stay 0");
static_assert(DOOM_WHD_SOURCE_ERR_PARTITION_MISSING == 3,
              "levelFailureFromWhd: PARTITION_MISSING must stay 3");
static_assert(DOOM_WHD_SOURCE_ERR_MMAP_FAILED == 4,
              "levelFailureFromWhd: MMAP_FAILED must stay 4");
static_assert(DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID == 5,
              "levelFailureFromWhd: CONTAINER_INVALID must stay 5");
static_assert(DOOM_WHD_SOURCE_ERR_UNAVAILABLE == 6,
              "levelFailureFromWhd: UNAVAILABLE must stay 6");

#include "doom_map.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#if defined(DOOM_LEVEL_TEST)
// The board proof's exit fixture: P_CheckPosition's standability and
// P_PointToAngle's facing, the same two calls tools/level_check.c uses.
#include "m_fixed.h"
#include "p_map.h"
#endif
#include "palflash.h"
#if defined(DOOM_PALFLASH_SCRIPT)
#include "p_inter.h"
#endif
#if defined(DOOM_HUD)
#include "am_map.h"
#endif
#include "r_bsp.h"
#include "r_data.h"
#include "r_defs.h"
#include "r_light.h"
#include "r_plane.h"
#include "r_psprite.h"
#include "r_segs.h"
#include "r_tables.h"
#include "r_things.h"
#include "r_view_diag.h"
#if defined(DOOM_AUDIO)
#include "s_sound.h"
#endif
#if defined(DOOM_HUD)
#include "st_stuff.h"
#endif
#if defined(DOOM_MENU)
#if !defined(DOOM_HUD)
#error "DOOM_MENU draws over the HUD build: define DOOM_HUD too"
#endif
#include "m_menu.h"
#endif
#if defined(DOOM_MENU) || DOOM_PERSISTENCE
// G_InitNew/G_DoReborn: the reload a New Game and a loaded save share.
#include "g_game.h"
#endif
// The level transition. The composition of G_DoCompleted's switch with
// G_DoWorldDone's load is engine-free and host-tested, so the game frame and
// test/unit/test_level run one implementation. g_game.h's declarations are part
// of it, and the include above is the menu's own reason for them.
#include "doomc/LevelTransition.h"

// The E1M1 sprite fixture: the DoomEd number -> frame-0 lump table
// tools/gen_e1m1_sprites.py generates for the host harness. Fixture data, not
// renderer code -- lib/doom/ never includes it -- and the frame environment puts
// tools/ on the include path so the board builds the same sprite list the host
// proves, from the same table, or the two hashes describe different frames.
#include "e1m1_sprites.inc"

/*
 * The CPU cycle counter the cycle attribution reads (r_bsp.h declares it).
 *
 * The Xtensa CCOUNT special register is the only counter with a cycle of its
 * own; esp_timer_get_time() is the reference for the phase table and these two
 * must agree on the total, which is what makes the per-pixel numbers
 * trustworthy. CCOUNT is 32 bits at 240 MHz, so it wraps every 17.9 s -- far
 * longer than a frame, and `now - start` on unsigned values is still correct
 * across a wrap as long as one interval is under that.
 *
 * It is defined outside the anonymous namespace and with C linkage because
 * lib/doom/ is C and links against this symbol.
 */
extern "C" std::uint32_t r_probe_cycles(void) {
    std::uint32_t value;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(value));
    return value;
}

namespace doom {
namespace doomc {

namespace {

// --- Capacities -------------------------------------------------------------

/*
 * Every capacity below is the caller's, and each was chosen from a measurement
 * rather than from upstream's default. The measured peak is stated next to it so
 * a later change is a decision with evidence.
 */

/*
 * WHICH CAPACITY SET THIS BUILD COMPILES
 *
 * The shipped set is sized for every *walkable* viewpoint of E1M1 and E1M2, the
 * sweep tools/capacity_check.py takes over both levels. `_frameram` cannot carry
 * it: that environment is the map-path A/B control, has 256 B of `dram0_0_seg`
 * headroom, and only ever renders the 20 sampled views the frame probe defines.
 * So it selects the sampled set by operation, not by board: a build that renders
 * the sampled views compiles the capacities those 20 views need, and every
 * other build compiles the walkable set. The #if is written "not defined" first
 * so capacity_check.py's reader (which takes the first declaration of each
 * constant) always measures the shipped set.
 */
#if !defined(PIXELROOT32_DOOM_SAMPLED_VIEW_CAPACITIES)
/// DOOM's MAXDRAWSEGS is 256 (13,312 B). The walkable sweep peaks at 99 for
/// E1M1 at ss202/a4 (2,752,-2,980) and 98 for E1M2 at ss168/a3. An overflow is a
/// walk error, so the number must be right: 99 with the +25% margin is 124,
/// rounded to 128. 52 B each is 6,656 B.
constexpr int kDrawsegCapacity = 128;
#else
constexpr int kDrawsegCapacity = 64;  /* the 20 sampled views need 39 */
#endif

/// A plane is allocated by the walk and may stay unmarked for the whole view, so
/// the allocation peak is above the drawn peak. The walkable sweep measures 51
/// allocated planes at E1M1 ss202/a4 (E1M2: 47 at ss220/a7); 40, enough for the
/// sampled views, refuses 19 walkable E1M1 viewpoints with
/// R_BSP_ERR_VISPLANE_OVERFLOW. 51 with the +25% margin is 64, 664 B each,
/// 42,496 B.
#if !defined(PIXELROOT32_DOOM_SAMPLED_VIEW_CAPACITIES)
constexpr int kPlaneCapacity = 64;
#else
constexpr int kPlaneCapacity = 40;  /* the 20 sampled views allocate 22 */
#endif

/// R_MAX_CLIP_SEGS, the minimum r_bsp_init() accepts (upstream's MAXSEGS).
/// The walkable sweep never needs more than 2 interior posts on any viewpoint of
/// either level, so the minimum is enough.
constexpr int kClipRangeCapacity = 32;

/// The unified clip/opening pool, in int16_t *entries*: upstream's `openings[]`,
/// where a clipped edge costs its own x2-x1+1 span instead of a whole 320-entry
/// block. The measured walkable peak is 2,459 entries for E1M1 (ss95/a0,
/// 128,-3,232) and 2,060 for E1M2 (ss35/a5), from tools/capacity_check.py's
/// `clippool` row. +25% is 3,073.75, rounded up to a multiple of 128 is 3,200
/// entries = 6,400 B. It holds both the sprite clip arrays and the masked mid
/// columns; the two "everything" answers are negative codes and cost nothing.
#if !defined(PIXELROOT32_DOOM_SAMPLED_VIEW_CAPACITIES)
constexpr int kClipPoolCapacity = 3200;
#else
/// The 20 sampled views need `clip_blocks_peak=15`, which includes the two
/// reserved constants; the 13 real edges are at most 13 x 320 = 4,160 entries.
/// 4,608 entries (9,216 B) is that peak with about 10% margin, rounded to a
/// multiple of 128. The earlier 5,120 (10,240 B) was sized by the control's link
/// budget, and engine 1.14.0 grew `dram0_0_seg` past it by 488 B; the unused
/// margin is the cheapest thing to give back, because the measured peak still
/// fits and this environment never renders a walkable view.
constexpr int kClipPoolCapacity = 4608;
#endif

/// The masked pass's sprite pool, sized for the whole episode: E1M2 submits 243
/// sprites, and its sampled frame 02 overflows a 100-entry pool. 256 is the
/// episode's peak with headroom, 44 B each. The walkable sweep measures 220
/// projected sprites at E1M2 ss151/a4 (E1M1: 125 at ss76/a0), so 256 covers it.
constexpr int kVisspriteCapacity = 256;

/// One THINGS record per sprite the level can place. E1M1's fixture resolves
/// 129 and the episode's busiest map (E1M6) 389, so 512 is the episode's peak
/// with headroom; the allocation below takes exactly the level's THINGS count.
constexpr int kSpriteCapacity = 512;

// --- Buffer sizes -----------------------------------------------------------

/// A level's renderer lumps minus THINGS are what the RAM build decodes and the
/// flash build maps in place: LINEDEFS, SIDEDEFS, VERTEXES, SEGS, SSECTORS, NODES
/// and SECTORS (E1M1's are 46,508 B; E1M2's are 94,984 B). THINGS is a view in
/// both builds. `gLevelReleasedBytes` is what the opened level stops costing
/// DRAM: those lumps plus the 20,742 B of COLORMAP, TEXTURE1 and PNAMES that the
/// flash build maps instead of decoding. It is computed from the level actually
/// opened, so `FRAMEPROF,mem` is a per-level number, not E1M1's.
std::size_t gLevelReleasedBytes = 0u;

/// Every wall texture, the sky and every flat is a pointer into the container's
/// raw section, so the only graphics state is the manifest adapter, a fixed
/// record array.
///
/// Up to nine extra lump blocks from the heap: the blocks of the decoded level
/// that the static arena does not hold, in the RAM build.
constexpr int kExtraLumpBlocks = 9;

#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
/// How much of the decoded level the static arena holds. `dram0_0_seg` is
/// 124,068 B and the sampled-view walk state plus the 187-entry manifest adapter
/// leave room for 17,408 B; that keeps the link and sends SIDEDEFS (19,448 B) to
/// the heap, which `heap_before` shows has room for it. The control still decodes
/// every map lump the flash build maps.
constexpr std::size_t kStaticLumpBytes = 17408u;
#endif

/// The static region this image can use for data: `dram0_0_seg` is 124,068 B
/// (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c) and the image used ~29 KB of it
/// before this file. Reported so the shortfall line can say what the pool is, not
/// only what was asked of it.
constexpr std::size_t kStaticRegionBytes = 124068u;
/// DOOM's stored column is 128 rows whatever the texture's declared height,
/// because R_DrawColumn() wraps with a hard-coded `& 127`. The bake writes that
/// form; the adapter derives its heightMask from it.
constexpr std::size_t kStoredColumnRows = 128u;

/// The views, the runs and the colours, exactly as tools/rsegs_check.c has them.
constexpr int kViewCount = 20;
constexpr int kMeasuredRuns = 2;
constexpr int kTotalRuns = 1 + kMeasuredRuns;
constexpr int kPositionCount = 4;
constexpr int kAngleCount = 5;

/// The level's sky flat and sky texture, packed from their WAD names.
constexpr std::uint8_t kSkyFlatName[8] = {'F', '_', 'S', 'K', 'Y', '1', 0, 0};
constexpr std::uint8_t kSkyTextureName[8] = {'S', 'K', 'Y', '1', 0, 0, 0, 0};

/*
 * WHICH LEVEL THIS BUILD OPENS
 *
 * The container carries the whole shareware episode, so the startup level is a
 * build decision. Most environments compile the default; the E1M2 measurement
 * build sets
 * PIXELROOT32_DOOM_FRAME_LEVEL=2 and renders the same 20 views of the largest
 * bake in the episode. The marker name is built by frameMapMarker(), which the
 * host suite pins, so the number and the container's lump name cannot drift.
 */
#ifndef PIXELROOT32_DOOM_FRAME_LEVEL
#define PIXELROOT32_DOOM_FRAME_LEVEL 1
#endif

constexpr std::uint32_t kFrameLevel = PIXELROOT32_DOOM_FRAME_LEVEL;
static_assert(kFrameLevel >= 1u && kFrameLevel <= 9u,
              "the shareware episode has nine levels: E1M1..E1M9");

/*
 * THE LEVEL THE GAME IS ON
 *
 * `kFrameLevel` is the *build's* map: the environment variable that selects which
 * level a measurement build opens, and therefore the level a session starts on.
 * A completed level names the next one and the world loads it, so the driver
 * also needs the number the world is actually on. `gRuntimeLevel` is that number.
 *
 *   - it starts at `kFrameLevel`, the startup default;
 *   - New Game returns it to `kFrameLevel` (a run starts at the build's map);
 *   - a transition moves it to the map the load named;
 *   - the save record carries it, so a save written on E1M2 names E1M2.
 *
 * It is one byte of .bss in the probe builds too, where nothing but
 * openLevelSession() reads it.
 */
std::uint8_t gRuntimeLevel = static_cast<std::uint8_t>(kFrameLevel);

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
 *                 (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c). The walk state
 *                 and the manifest adapter are the only large statics.
 *   the heap      what is left of DRAM after the static data. The sprite list
 *                 (130 records) and, in the RAM build, the decoded level come
 *                 from it.
 *
 * `DriverState` is the static half and `gBaked` is the manifest adapter; the
 * graphics themselves are pointers into the mapping, so no pool holds them.
 * The decoded level is the RAM build's own array and heap blocks; the flash
 * build stores it in the container.
 */

/// The static half: the walk state, the unified clip/opening pool and the
/// coverage record.
///
/// THE CAPACITY MEASUREMENT
///
/// tools/capacity_check.py renders every subsector midpoint and every sector
/// centre of E1M1 and E1M2 at 8 angles (2,520 walkable E1M1 viewpoints; 4,768
/// E1M2) through the same four passes this driver runs, then reports the peak
/// each capacity consumes and every viewpoint a pass refused. The walkable
/// peaks and the +25% margin the capacities are rounded from:
///
///   capacity      peak (E1M1)      +25%   shipped
///   drawsegs      99               128    128
///   planes        51                64     64
///   clip ranges    2                32     32
///   clip pool   2,459 entries     3,074  3,200
///   vis sprites  125 (E1M2 220)    256    256
///
/// In env:esp32dev_ILI9341_show the census prints
/// `static_used=120,408,seg_free=4172` and the link map's `_bss_end`
/// (0x3ffdb1b8) leaves 4,168 B of `dram0_0_seg`. env:esp32dev_ILI9341_frameram
/// has 256 B and cannot carry the walkable set: it is the A/B control, renders
/// only the 20 sampled views, and selects the sampled capacity set with
/// PIXELROOT32_DOOM_SAMPLED_VIEW_CAPACITIES (see the capacities block above).
///
/// The unified pool is what makes this fit. A layout that charges a 320-entry
/// block per clipped edge and per masked mid needs 71 blocks = 45,440 B for
/// E1M1's walkable peak -- more than the whole free static region. Upstream's
/// openings[] charges the fragment's own span, and 3,200 entries is 6,400 B for
/// the whole walkable set.
struct DriverState {
    /// The walk's state, all of it caller-owned as r_bsp.h requires.
    alignas(8) r_drawseg_t drawsegs[kDrawsegCapacity];
    alignas(8) r_visplane_t planes[kPlaneCapacity];
    alignas(8) r_clip_range_t clipRanges[kClipRangeCapacity];
    alignas(8) int16_t floorClip[R_SCREEN_WIDTH];
    alignas(8) int16_t ceilingClip[R_SCREEN_WIDTH];
    /// The wall pass's unified clip/opening pool, in int16_t entries: upstream's
    /// openings[]. It holds both the per-drawseg sprite clip arrays and the
    /// deferred masked mid columns; see r_bsp.h.
    alignas(8) int16_t clipPool[kClipPoolCapacity];
    alignas(8) r_vissprite_t visSprites[kVisspriteCapacity];
    /// The per-column coverage record the frame hash needs: an uncovered column
    /// is 0, a real palette index writes 1, so the pixels alone cannot say
    /// whether a column was written.
    alignas(8) std::uint8_t covered[R_SCREEN_WIDTH];
};

/// The static half, and the only unconditional new .bss this file adds.
DriverState gState;

/// The baked graphics adapter: the manifest and one prebuilt record per name.
BakedGraphics gBaked;

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
 * The heap has no single hole large enough for every block, so the level is
 * allocated one piece at a time -- see heapLump() and the placement block in
 * frameProbeRun() -- and each is released here exactly once.
 */
void releaseWorkingSet() {
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
/// A/B's control: it decodes the map into DRAM so the two builds differ in
/// exactly one thing.
alignas(8) std::uint8_t gLumpArena[kStaticLumpBytes];
#endif

/// The placement the probe ended up using, printed by `FRAMEPROF,mem`.
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

const std::uint8_t* gColormap = nullptr;
std::size_t gColormapBytes = 0;
r_light_t gLight = {};

/// The residency-scoped patch proof cache. The bake is part of the read-only
/// container mapping and BakedGraphics' records are file-scope statics, so a
/// patch at a given address cannot change or become malformed while the run
/// holds: the two promises r_things.h's r_sprite_proofs_t requires both hold
/// here, and the 32 distinct sprite lumps are proved once per run instead of
/// once per frame. 800 B of .bss.
r_sprite_proofs_t gSpriteProofs;

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

    // The leaf time, always: a compose that decodes six patches wants each
    // patch's inflate counted once, while `residency_us` counts the outermost
    // decoder only.
    gSample.decodeMicros += elapsedMicros(start);
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
 * @brief One frame's distinct-name working set.
 *
 * Filled by the resolvers on a miss, cleared at the top of every frame. It is
 * what says how many distinct textures, flats and sprite lumps a view actually
 * asks for and how many bytes their baked payloads cost. The capacities are
 * measured peaks (4 textures, 4 flats, 32 sprite lumps in the 40 measured
 * frames) with headroom, and they are template parameters so the three sets do
 * not each pay for the largest one.
 */
template <int Capacity>
struct NameSet {
    r_name_t names[Capacity];
    std::uint32_t bytes[Capacity];
    int count;
    std::uint32_t totalBytes;

    void reset() {
        count = 0;
        totalBytes = 0u;
    }

    /// Adds @p name on its first sighting; returns true when it was new.
    bool add(r_name_t name, std::uint32_t entryBytes) {
        for (int index = 0; index < count; ++index) {
            if (names[index] == name) {
                return false;
            }
        }
        if (count >= Capacity) {
            return false;
        }
        names[count] = name;
        bytes[count] = entryBytes;
        ++count;
        totalBytes += entryBytes;
        return true;
    }
};

NameSet<16> gTexSet;
NameSet<12> gFlatSet;
NameSet<40> gSpriteSet;

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

// --- Baked graphics ---------------------------------------------------------

/// The most recent name a resolve refused, and why. A baked container either has
/// the name or it does not; there is no fallback decode, so `baked` is the whole
/// diagnosis. Printed by `FRAMEPROF,error`.
char gFailTexture[9] = {0};
const char* gFailWhy = "none";

#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
/// The first wall texture the run resolved, kept for the raster memory probe so
/// the benchmark reads the bytes the frame actually reads.
const r_walltex_t* gProbeTexture = nullptr;
#endif

void noteMissing(r_name_t name, const char* why) {
    r_unpack_name(name, gFailTexture);
    gFailWhy = why;
}

/// r_tex_source_t::resolve: the manifest's wall/sky record, or NULL.
///
/// The sky reaches this resolver through r_plane.c, which asks the same source
/// for textures->skyTexture, so kind 1 and kind 2 are both served here.
const r_walltex_t* resolveWall(void* context, r_name_t name) {
    (void)context;

    ++gSample.texResolves;
    const r_walltex_t* texture = gBaked.resolveWall(name);
    if (texture == nullptr) {
        ++gSample.texMisses;
        noteMissing(name, "baked");
        return nullptr;
    }
    ++gSample.texHits;
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    if (gProbeTexture == nullptr) {
        gProbeTexture = texture;
    }
#endif
    (void)gTexSet.add(name, static_cast<std::uint32_t>(texture->width) * kStoredColumnRows);
    return texture;
}

/// r_tex_source_t::resolveFlat: the manifest's flat record, or NULL.
const r_flat_t* resolveFlat(void* context, r_name_t name) {
    (void)context;

    ++gSample.flatResolves;
    const r_flat_t* flat = gBaked.resolveFlat(name);
    if (flat == nullptr) {
        ++gSample.flatMisses;
        noteMissing(name, "baked");
        return nullptr;
    }
    ++gSample.flatHits;
    (void)gFlatSet.add(name, static_cast<std::uint32_t>(R_FLAT_BYTES));
    return flat;
}

/// r_sprite_source_t::resolve: the manifest's raw sprite patch, or NULL.
const r_sprite_patch_t* resolveSprite(void* context, r_name_t lump) {
    (void)context;

    ++gSample.spriteResolves;
    const r_sprite_patch_t* patch = gBaked.resolveSprite(lump);
    if (patch == nullptr) {
        ++gSample.spriteMisses;
        noteMissing(lump, "baked");
        return nullptr;
    }
    ++gSample.spriteHits;
    (void)gSpriteSet.add(lump, static_cast<std::uint32_t>(patch->size));
    return patch;
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

/*
 * THE LIVE FRAME, NOT THE TYPE
 *
 * A static row per type can only name the type's spawn frame, so a pain frame,
 * a death animation, a corpse or a puff could never be drawn from it. The world
 * supplies the frame: r_mobj_t already carries the current state's `sprite` and
 * `frame`, so the lump is looked up by the *pair*, and the fixture
 * (tools/e1m1_sprites.inc) answers which frames the container baked.
 *
 * `frame` is the state's FF_FRAMEMASKed letter index; FF_FULLBRIGHT is carried
 * separately from the mobj's own frame word, exactly as R_ProjectSprite folds
 * it into `fullbright`.
 */
const e1m1_sprite_rule_t* spriteFrameRule(const char* sprite, int frame) {
    for (int index = 0; index < E1M1_SPRITE_RULE_COUNT; ++index) {
        if (kE1M1Sprites[index].frame == static_cast<signed char>(frame)
            && std::strncmp(kE1M1Sprites[index].sprite, sprite, 4) == 0) {
            return &kE1M1Sprites[index];
        }
    }
    return nullptr;
}

/*
 * A mobj whose live frame was not baked falls back to its type's spawn-state
 * row -- the frame the static THINGS list shows. The alternative is a
 * whole-frame refusal for one unbakeable frame (R_THINGS_ERR_SPRITE_MISSING),
 * which would blank the panel, or a silent drop, which would make an animated
 * item flicker out of existence. This keeps every mobj drawn and every pixel
 * where it was; the gate (tools/sprite_check.c) reports how many submissions
 * took the fallback, so the gap is a number on the host side. NULL means even
 * the spawn row is absent (the player avatar's S_PLAY_STND is not in the bake),
 * and the caller skips that mobj: it is the camera at the viewpoint, which
 * R_ProjectSprite culls anyway.
 */
const e1m1_sprite_rule_t* spawnRuleForType(int mobjType) {
    if (mobjType < 0 || mobjType >= NUM_MOBJ_TYPES) {
        return nullptr;
    }
    const int spawnState = mobjinfo[mobjType].spawnstate;
    if (spawnState < 0 || spawnState >= NUM_STATES) {
        return nullptr;
    }
    const doom_state_t& spawn = states[spawnState];
    return spriteFrameRule(sprnames[spawn.sprite], static_cast<int>(spawn.frame & 0x7FFF));
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

/*
 * THE SPRITE LIST'S CONSTANT HALF
 *
 * Re-reading every THINGS record, descending the BSP for its sector and copying
 * the sector for all 129 sprites *per view* costs 4,932 us a frame on the board
 * -- 37% of the masked window, more than the sprite drawing itself -- for 242
 * mapped-flash record reads, all for values that cannot change while the level
 * is loaded.
 *
 * Only two fields are view-dependent: the rotation R_ProjectSprite's rule picks
 * for this viewpoint, and the lump and flip that follow from it. So the base
 * record is built once per run into gSprites itself (no second array of records)
 * and the per-view pass rewrites the lump, the flip and the fullbright flag.
 * The rule pointers are the 520 bytes this needs.
 *
 * The frame is the same as a per-view rebuild: the same records, the same
 * values, the same order -- only the reads that produce them happen once.
 */
const e1m1_sprite_rule_t* gSpriteRules[kSpriteCapacity] = {};
int gSpriteBaseCount = -1;

bool buildSpriteBase(const doom_map_t* map) {
    int count = 0;

    for (std::uint32_t index = 0; index < map->lumps[DOOM_MAP_THINGS].count; ++index) {
        doom_thing_t thing;
        doom_map_get_thing(map, index, &thing);
        const e1m1_sprite_rule_t* rule = spriteRule(thing.type);
        if (rule == nullptr) {
            continue;
        }
        if (count >= kSpriteCapacity) {
            return false;
        }

        const fixed_t x = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.x)) << FRACBITS);
        const fixed_t y = static_cast<fixed_t>(static_cast<std::uint32_t>(static_cast<std::uint16_t>(thing.y)) << FRACBITS);
        std::uint16_t sectorIndex = 0u;
        if (!sectorAt(map, x, y, &sectorIndex)) {
            return false;
        }
        doom_sector_t sector;
        doom_map_get_sector(map, sectorIndex, &sector);

        gSprites[count].x = x;
        gSprites[count].y = y;
        gSprites[count].z = sector.floorHeight;
        gSprites[count].angle = ANG45 * static_cast<angle_t>(thing.angle / 45);
        gSprites[count].lightLevel = sector.lightLevel;
        gSprites[count].noClip = 0;
        gSpriteRules[count] = rule;
        ++count;
    }
    gSpriteBaseCount = count;
    return true;
}

/// Builds the frame's sprite list: every THINGS record the fixture covers, standing
/// on its sector's floor, with the rotation R_ProjectSprite's rule selects for this
/// viewpoint -- exactly the list the host harness builds.
int buildSprites(const doom_map_t* map, const r_view_params_t* view) {
    if (gSpriteBaseCount < 0 && !buildSpriteBase(map)) {
        return -1;
    }
    if (gSpriteBaseCount < 0) {
        return -1;
    }

    for (int index = 0; index < gSpriteBaseCount; ++index) {
        const e1m1_sprite_rule_t* rule = gSpriteRules[index];
        const angle_t facing = pointToAngle(view, gSprites[index].x, gSprites[index].y);
        const int rotation =
            static_cast<int>((facing - gSprites[index].angle + ((ANG45 / 2u) * 9u)) >> 29);
        const int lump = rule->rotations ? rotation : 0;

        char lumpName[9];
        for (int i = 0; i < 8; ++i) {
            lumpName[i] = rule->lumps[lump].name[i];
        }
        lumpName[8] = '\0';

        gSprites[index].lump = r_pack_name(reinterpret_cast<const std::uint8_t*>(lumpName));
        gSprites[index].flip = rule->lumps[lump].flip;
        gSprites[index].fullbright = rule->fullbright;
    }
    return gSpriteBaseCount;
}

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
/*
 * THE LIVE LIST
 *
 * For the game frame the sprite list is the world's mobjs, walked per frame
 * through p_level_first_mobj()/p_level_next_mobj(). The static THINGS list
 * above is only a stand-in for this path: it is kept for the two measurement
 * probes (FRAMEPROF, which measures a renderer and owns no world) and is never
 * built in the `_show` build while the world is open, so nothing is drawn
 * twice.
 *
 * Each mobj contributes its own `x`, `y`, `z`, `angle` and the light of the
 * sector it stands in (`p_level_t::sectors`, upstream's mutable copy -- the
 * same one every mover and light writer changes). Its lump comes from its own
 * state, not from its type: `sprite` indexes sprnames[] and `frame` is the
 * FF_FRAMEMASKed letter index. `FF_FULLBRIGHT` is taken from the same frame
 * word. The rotation is R_ProjectSprite's rule, unchanged from the static list.
 *
 * The name is rebuilt per mobj on every frame, because one static row per type
 * can express one frame and a live mobj can show any frame of its chain.
 *
 * The bound is the masked pass's own vis-sprite capacity (256): a longer list
 * cannot project to fewer vis sprites, so exceeding it is
 * R_THINGS_ERR_VISSPRITE_OVERFLOW -- the pass's existing refusal code -- rather
 * than a silent truncation. There is no per-frame allocation: the array is the
 * one `openLevelSession()` took.
 */
const p_level_t* gFrameWorld = nullptr;

/// Set by buildWorldSprites() so renderOnce() can report *why* the list refused
/// instead of the generic -1 the static build's failure uses.
int gSpriteBuildCode = -1;

int buildWorldSprites(const doom_map_t* map, const r_view_params_t* view) {
    if (gFrameWorld == nullptr) {
        return buildSprites(map, view);
    }

    int count = 0;
    for (const r_mobj_t* mobj = p_level_first_mobj(gFrameWorld); mobj != nullptr;
         mobj = p_level_next_mobj(mobj)) {
        if (mobj->thinker.removed != 0u) {
            continue;
        }
        if (count >= kVisspriteCapacity) {
            gSpriteBuildCode = static_cast<int>(R_THINGS_ERR_VISSPRITE_OVERFLOW);
            return -1;
        }

        const int frame = static_cast<int>(mobj->frame & 0x7FFF);
        const int spriteIndex = static_cast<int>(mobj->sprite);
        const e1m1_sprite_rule_t* rule =
            (spriteIndex >= 0 && spriteIndex < NUM_SPRITES)
                ? spriteFrameRule(sprnames[spriteIndex], frame)
                : nullptr;
        if (rule == nullptr) {
            rule = spawnRuleForType(static_cast<int>(mobj->type));
            if (rule == nullptr) {
                // Even the type's spawn frame was not baked (the avatar's
                // S_PLAY_STND): nothing can be drawn for it. It stands at the
                // viewpoint, where R_ProjectSprite culls it anyway.
                continue;
            }
        }

        const angle_t facing = pointToAngle(view, mobj->x, mobj->y);
        const int rotation =
            static_cast<int>((facing - mobj->angle + ((ANG45 / 2u) * 9u)) >> 29);
        const int lump = rule->rotations ? rotation : 0;
        // The per-mobj lump: rebuilt from this mobj's frame, never a static row
        // per type. Two mobjs of one type can name two different frames, so the
        // eight characters live here for this submission and nowhere else.
        char lumpName[9];
        for (int i = 0; i < 8; ++i) {
            lumpName[i] = rule->lumps[lump].name[i];
        }
        lumpName[8] = '\0';

        gSprites[count].x = mobj->x;
        gSprites[count].y = mobj->y;
        gSprites[count].z = mobj->z;
        gSprites[count].angle = mobj->angle;
        gSprites[count].lump =
            r_pack_name(reinterpret_cast<const std::uint8_t*>(lumpName));
        gSprites[count].lightLevel = gFrameWorld->sectors[mobj->sector].lightLevel;
        gSprites[count].flip = rule->lumps[lump].flip;
        gSprites[count].fullbright = (mobj->frame & 0x8000) != 0;
        gSprites[count].noClip = 0;
        ++count;
    }
    return count;
}
#endif  // PIXELROOT32_DOOM_SHOW_E1M1

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
/// front of a bigger request fails while the same bytes fit.
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
/// THINGS stays loaded: the masked pass builds the sprite list from it once per
/// view, so it is read 20 times, not once.
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

/*
 * ONE MAP'S LUMPS, BECAUSE TWO CALLERS NEED THE SAME SET
 *
 * `openLevelSession()` binds the level a session starts on, and the level
 * transition binds the level an exit names. Both must hand the rest of the file
 * exactly the same `doom_map_t` from the same container bytes, or the frame the
 * board draws after a transition would come from a map no probe measured. The
 * lump order is part of that sameness: the RAM build's heap only fits when the
 * requests descend (see the call-site comment in openLevelSession), so the
 * sequence below is one list and not two copies of it.
 *
 * THINGS is a view in both builds, because the masked pass builds one sprite list
 * per view from it; REJECT and BLOCKMAP are skipped, because the renderer never
 * reads them and the simulation does not need them loaded here.
 *
 * @param marker           The level's zero-byte marker lump index.
 * @param loadColormap     Read COLORMAP into gColormap in its place in the
 *                         sequence. The session's first map does; a transition
 *                         does not, because the palette table's view is already
 *                         bound and must not be taken again under a level that is
 *                         about to be replaced.
 * @param outMap           Zeroed, then filled by doom_map_open().
 * @param mapFailures      Out: the lumps that did not resolve.
 * @param directoriesLoaded Out: whether COLORMAP resolved; true when not asked.
 * @return DOOM_MAP_OK, or the first refusal's code.
 */
doom_map_error_t readMapLumps(int marker, bool loadColormap, doom_map_t* outMap,
                              std::int32_t* mapFailures, bool* directoriesLoaded) {
    doom_map_source_t source = {};
    *mapFailures = 0;
    *directoriesLoaded = true;

    // Descending block size, which is the order the RAM build's heap needs: the
    // holes are not interchangeable, so a small request in front of a big one can
    // fail while the same bytes fit when the requests descend. In the flash build
    // every one of these is a pointer and the order costs nothing.
    (void)loadMapLump(marker, DOOM_MAP_SIDEDEFS, &source, mapFailures);   // 19,440
    (void)loadMapLump(marker, DOOM_MAP_SEGS, &source, mapFailures);       //  8,784
    if (loadColormap) {
        *directoriesLoaded = loadLumpViewByName("COLORMAP", &gColormap, &gColormapBytes);
    }
    (void)loadMapLump(marker, DOOM_MAP_LINEDEFS, &source, mapFailures);   //  6,650
    (void)loadMapLump(marker, DOOM_MAP_NODES, &source, mapFailures);      //  6,608
    // TEXTURE1 and PNAMES are build-time inputs: the bake used them and the
    // runtime reads the manifest. They stay in the container for the C check.
    if (!viewMapLump(marker, DOOM_MAP_THINGS, &source)) {
        ++(*mapFailures);
    }
    (void)loadMapLump(marker, DOOM_MAP_SECTORS, &source, mapFailures);    //  2,210
    (void)loadMapLump(marker, DOOM_MAP_VERTEXES, &source, mapFailures);   //  1,868
    (void)loadMapLump(marker, DOOM_MAP_SSECTORS, &source, mapFailures);   //    948
    (void)loadMapLump(marker, DOOM_MAP_REJECT, &source, mapFailures);     // skipped
    (void)loadMapLump(marker, DOOM_MAP_BLOCKMAP, &source, mapFailures);   // skipped

    std::memset(outMap, 0, sizeof *outMap);
    if (*mapFailures != 0) {
        return DOOM_MAP_ERR_REQUIRED_LUMP;
    }
    return doom_map_open(outMap, &source);
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
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    /// The cycle attribution for one frame. Zeroed at the top of every
    /// frame and handed to the view state so the four passes can fold into it.
    r_raster_probe_t probe = {};
    /// The sprite-list build, timed on the caller's side because the fixture,
    /// not the renderer, owns that loop.
    std::uint32_t spriteListCycles = 0u;
    std::uint32_t spriteListSectors = 0u;
    /// The whole frame in cycles, so the cycle table and the microsecond table
    /// can be checked against each other.
    std::uint32_t frameCycles = 0u;
#endif
};

#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
/*
 * THE CYCLE ACCUMULATOR
 *
 * r_raster_probe_t is 32-bit fields because the passes live in C99 and adding
 * a second width there buys nothing. Forty frames of 240 MHz is 9.6e9 cycles,
 * which does not fit, so the folding below is 64-bit: one field per bucket,
 * summed over the measured frames only (the cold run is not part of the table).
 *
 * The fold walks the probe as a flat array of 32-bit words. Every member is a
 * uint32_t, so there is no interior padding the array walk could misread; the
 * static_assert below is the proof of that, not a wish.
 */
static_assert(sizeof(r_raster_probe_t) % sizeof(std::uint32_t) == 0,
              "the probe's fields must be one uniform word size");

constexpr std::size_t kProbeWords = sizeof(r_raster_probe_t) / sizeof(std::uint32_t);

struct ProbeSum {
    std::uint64_t words[kProbeWords] = {};
    std::uint32_t frames = 0u;
    std::uint64_t frameCycles = 0u;
    std::uint64_t spriteListCycles = 0u;

    void add(const r_raster_probe_t& probe, std::uint32_t cycles,
             std::uint32_t spriteCycles) {
        const std::uint32_t* source = reinterpret_cast<const std::uint32_t*>(&probe);
        for (std::size_t index = 0; index < kProbeWords; ++index) {
            words[index] += source[index];
        }
        ++frames;
        frameCycles += cycles;
        spriteListCycles += spriteCycles;
    }

    std::uint32_t field(std::size_t offset) const { return static_cast<std::uint32_t>(words[offset]); }
};

/// The accumulator and the line buffer are file-scope statics, not locals: the
/// probe runs on the Arduino loop task, whose stack the four passes already
/// nearly fill, and 1,536 bytes of formatter buffer there is enough to trip the
/// canary. Nothing else reads them.
ProbeSum gProbeSum;
ProbeSum gProbeFrameSum;
char gProbeLine[1536];

/// Compile-time offsets, so the formatter cannot print the wrong bucket under a
/// name: a field added to the struct and forgotten here is a build error at the
/// next name that shifts.
#define PROBE_FIELD(name) (offsetof(r_raster_probe_t, name) / sizeof(std::uint32_t))
#define PROBE_SUM(field_) (sum.words[PROBE_FIELD(field_)])

/*
 * HOW MANY CYCLES IS A PIXEL, AND HOW MUCH OF IT IS THE FLASH
 *
 * The phase table says a wall pixel costs 236 ns; the pass table says the store
 * is X of it. Neither says whether that X is the loop's own shape or the mapped
 * flash read of the texel and the colormap entry. This benchmark separates the
 * two by running the *identical* inner statement over the same byte patterns
 * from three residencies, so the difference between two rows is the cost of
 * moving one operand from RAM to flash.
 *
 * It uses a real baked texture column and the real mapped COLORMAP, so the
 * bytes are the ones the frame reads, and a RAM copy of the same 256-byte
 * colormap, so the only variable is where the bytes live.
 */
struct MemoryProbe {
    std::uint32_t ramRamNanos = 0u;   ///< store + texel and map both in RAM
    std::uint32_t ramFlashNanos = 0u; ///< store + colormap in flash
    std::uint32_t flashFlashNanos = 0u; ///< store + texel and map both in flash
    std::uint32_t storeNanos = 0u;    ///< the store alone
    std::uint32_t pixels = 0u;
};

/// One pass of the micro-benchmark. `column` and `colormap` are read through the
/// same expression the wall pass uses, one pixel per row of a 200-row column.
void benchInner(uint8_t* destination, int stride, const uint8_t* column,
                const uint8_t* colormap, fixed_t iscale, int rows, int repeats,
                std::uint64_t* sink) {
    for (int pass = 0; pass < repeats; ++pass) {
        fixed_t frac = 0;
        uint8_t* at = destination;
        for (int row = 0; row < rows; ++row) {
            *at = colormap[column[(int)((frac >> FRACBITS) & 127)]];
            if (sink != nullptr && (row & 63) == 0) {
                *sink += *at;
            }
            at += stride;
            frac += iscale;
        }
    }
}
#endif
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
 * `walk`, `walls`, `planes` and `masked` all run: this is the whole frame the
 * renderer defines, in the order r_bsp.h states, and the hash is the finished
 * frame. The plane and masked passes fit only because the level and the
 * graphics are read from the container's raw section instead of being decoded
 * into RAM.
 *
 * The sprite list is built inside the masked window: upstream builds it during
 * the walk, but this port's caller owns the thing list, so the build is the
 * caller's work for that pass and is measured with it rather than hidden in
 * `other_us`.
 */
#if defined(DOOM_FRAME_DUMP)
/// The submitted sprite count of the last completed frame, for
/// frameDumpViewDiag(). The view state itself is gSession.driver->view.
static std::uint16_t gDumpSpriteCount = 0u;
#endif

bool renderOnce(Driver* driver, const FrameView& view, std::uint8_t* pixels, int width,
                int height, FrameProfileSample* sample) {
    const std::int64_t frameStart = nowMicros();
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    const std::uint32_t frameCycleStart = r_probe_cycles();
#endif

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
    gTexSet.reset();
    gFlatSet.reset();
    gSpriteSet.reset();

    std::memset(gState.covered, 0, sizeof gState.covered);
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    driver->probe = r_raster_probe_t{};
    driver->spriteListCycles = 0u;
    driver->spriteListSectors = 0u;
#endif
    driver->surface.tierPixels[R_DRAW_TIER_MID] = 0u;
    driver->surface.tierPixels[R_DRAW_TIER_TOP] = 0u;
    driver->surface.tierPixels[R_DRAW_TIER_BOTTOM] = 0u;
    driver->surface.planePixels = 0u;
    driver->surface.planeSpans = 0u;
    driver->surface.spritePixels = 0u;

    std::uint32_t decodeMark = 0u;
    std::uint32_t composeMark = 0u;
    std::uint32_t lookupMark = 0u;

    std::int64_t start = nowMicros();
    driver->stage = "walk";
    const r_bsp_error_t walk = r_bsp_render(&driver->view, &params);
    sample->walkMicros = elapsedMicros(start);
    if (walk != R_BSP_OK) {
        // The walk has its own vocabulary (clip list, plane capacity, depth,
        // drawseg capacity); the error line names the stage and this code, so a
        // level that outgrows a capacity says which one instead of reporting a
        // bare `stage=walk`.
        //
        // The surface is not touched before the walk succeeds, and it is not
        // prefilled until the wall preflight below passes either: a refusal
        // writes no pixel, so the previous frame stays in the surface and the
        // scene keeps the last good picture instead of a white or half-drawn
        // frame.
        gStageCode = static_cast<int>(walk);
        return false;
    }
    sample->drawsegs = driver->view.drawsegCount;
    sample->drawsegOverflowed = driver->view.drawsegOverflowed;

    // The wall pass's denominators, from the walk's own output: a drawseg is one
    // fragment and its [x1, x2] span is the columns the loop will visit.
    sample->wallFragments = driver->view.drawsegCount;
    for (std::uint16_t index = 0u; index < driver->view.drawsegCount; ++index) {
        const r_drawseg_t& drawseg = driver->view.storage.drawsegs[index];
        if (drawseg.x2 >= drawseg.x1) {
            sample->wallColumns =
                static_cast<std::uint16_t>(sample->wallColumns + (drawseg.x2 - drawseg.x1 + 1));
        }
    }

    // The graphics are already baked and mapped: the passes below resolve names
    // through the manifest and read pixels straight from flash. There is no
    // decode scratch and no cache to clear.

    decodeMark = gSample.decodeMicros;
    composeMark = gSample.composeMicros;
    lookupMark = gSample.lookupMicros;
    start = nowMicros();
    driver->stage = "walls";
    // The wall pass is preflight then draw. The preflight proves every
    // texture and counts the clip-pool demand before a pixel exists; this is the
    // last gate before the prefill, so a refusal here still leaves the previous
    // frame intact instead of a 0xFF background with nothing drawn over it.
    r_wall_plan_t wallPlan{};
    const r_segs_error_t wallGate =
        r_wall_preflight(&driver->view, &driver->textures, &wallPlan);
    if (wallGate != R_SEGS_OK) {
        sample->wallsMicros = elapsedMicros(start);
        gStageCode = static_cast<int>(wallGate);
        return false;
    }
    // Every capacity and residency gate that runs before the first pixel has
    // passed. From here the surface is the frame being built: 0xFF is what the
    // wall and plane passes leave uncovered, which is what makes a hole visible
    // in the oracle comparison.
    std::memset(pixels, 0xFF, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    const r_segs_error_t walls = r_wall_draw(
        &driver->view, &driver->surface, &driver->textures, &driver->light, &wallPlan);
    sample->wallsMicros = elapsedMicros(start);
    if (walls != R_SEGS_OK) {
        // Unreachable by construction: the preflight proved the pool and the
        // textures, and this returns only for a surface/argument the caller
        // built wrong. Kept as the pass's own report rather than a crash.
        gStageCode = static_cast<int>(walls);
        return false;
    }
    sample->wallDecodeMicros = gSample.decodeMicros - decodeMark;
    sample->wallComposeMicros = gSample.composeMicros - composeMark;
    sample->wallLookupMicros = gSample.lookupMicros - lookupMark;
    sample->wallPixels = driver->surface.tierPixels[R_DRAW_TIER_MID]
                         + driver->surface.tierPixels[R_DRAW_TIER_TOP]
                         + driver->surface.tierPixels[R_DRAW_TIER_BOTTOM];
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    driver->probe.wallPixels = sample->wallPixels;
#endif

    decodeMark = gSample.decodeMicros;
    composeMark = gSample.composeMicros;
    lookupMark = gSample.lookupMicros;
    start = nowMicros();
    driver->stage = "planes";
    const r_plane_error_t planes =
        r_plane_draw(&driver->view, &driver->surface, &driver->textures, &driver->light);
    sample->planesMicros = elapsedMicros(start);
    if (planes != R_PLANE_OK) {
        gStageCode = static_cast<int>(planes);
        return false;
    }
    sample->planeDecodeMicros = gSample.decodeMicros - decodeMark;
    sample->planeComposeMicros = gSample.composeMicros - composeMark;
    sample->planeLookupMicros = gSample.lookupMicros - lookupMark;
    sample->planePixels = driver->surface.planePixels;
    sample->planeSpans = driver->surface.planeSpans;
    for (std::uint16_t index = 0u; index < driver->view.storage.planes.count; ++index) {
        const r_visplane_t& plane = driver->view.storage.planes.planes[index];
        if (plane.minX <= plane.maxX) {
            ++sample->planesDrawn;
        }
        // A sky plane draws one column per marked column, not a span, so its
        // columns are the second denominator the plane pass needs.
        if (params.skyPicNum != R_PLANE_NO_SKY && plane.picNum == params.skyPicNum) {
            for (int column = plane.minX; column <= (int)plane.maxX; ++column) {
                if (plane.top[column] != R_PLANE_EMPTY
                    && plane.top[column] <= plane.bottom[column]) {
                    ++sample->skyColumns;
                }
            }
        }
    }
    sample->planesAllocated = driver->view.storage.planes.count;

    decodeMark = gSample.decodeMicros;
    composeMark = gSample.composeMicros;
    lookupMark = gSample.lookupMicros;
    start = nowMicros();
    driver->stage = "sprites";
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    const std::uint32_t spriteListStart = r_probe_cycles();
#endif
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The game frame submits the world; the probes (which own no world) keep the
    // static THINGS list. This is the one switch between the two, so the game
    // can never draw both lists and double-draw a thing.
    const int spriteCount = buildWorldSprites(&driver->map, &params);
#else
    const int spriteCount = buildSprites(&driver->map, &params);
#endif
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    driver->spriteListCycles = r_probe_cycles() - spriteListStart;
#endif
    if (spriteCount < 0) {
        // The list build is the caller's, so it has no renderer error code; the
        // live builder sets one when the world exceeds the vis-sprite bound, and
        // -1 (no code) otherwise, instead of leaving the previous frame's error
        // to be reported as this one's.
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
        gStageCode = gSpriteBuildCode;
#else
        gStageCode = -1;
#endif
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
    sample->maskedDecodeMicros = gSample.decodeMicros - decodeMark;
    sample->maskedComposeMicros = gSample.composeMicros - composeMark;
    sample->maskedLookupMicros = gSample.lookupMicros - lookupMark;
    sample->spritePixels = driver->surface.spritePixels;
    sample->spritesSubmitted = maskedStats.submitted;
    sample->spritesDrawn = maskedStats.drawn;
#if defined(DOOM_FRAME_DUMP)
    /* The records this frame was drawn from, for frameDumpViewDiag(). */
    gDumpSpriteCount = static_cast<std::uint16_t>(spriteCount);
#endif
    for (std::uint16_t index = 0u; index < maskedStats.drawn; ++index) {
        const r_vissprite_t& vis = driver->view.storage.visSprites[index];
        if (vis.x2 >= vis.x1) {
            sample->spriteColumns =
                static_cast<std::uint16_t>(sample->spriteColumns + (vis.x2 - vis.x1 + 1));
        }
    }

    sample->pixels = sample->wallPixels + sample->planePixels + sample->spritePixels;
    sample->cacheBytes = gBaked.payloadBytes();
    sample->cachePeakSlots = static_cast<std::uint16_t>(gBaked.entryCount());
    sample->texSetCount = static_cast<std::uint16_t>(gTexSet.count);
    sample->texSetBytes = gTexSet.totalBytes;
    sample->flatSetCount = static_cast<std::uint16_t>(gFlatSet.count);
    sample->spriteSetCount = static_cast<std::uint16_t>(gSpriteSet.count);
    sample->spriteSetBytes = gSpriteSet.totalBytes;

    sample->residencyMicros = gSample.residencyMicros;
    sample->decodeMicros = gSample.decodeMicros;
    sample->composeMicros = gSample.composeMicros;
    sample->lookupMicros = gSample.lookupMicros;
    sample->texResolves = gSample.texResolves;
    sample->texDecodes = gSample.texDecodes;
    sample->texHits = gSample.texHits;
    sample->texMisses = gSample.texMisses;
    sample->flatResolves = gSample.flatResolves;
    sample->flatDecodes = gSample.flatDecodes;
    sample->flatHits = gSample.flatHits;
    sample->flatMisses = gSample.flatMisses;
    sample->spriteResolves = gSample.spriteResolves;
    sample->spriteDecodes = gSample.spriteDecodes;
    sample->spriteHits = gSample.spriteHits;
    sample->spriteMisses = gSample.spriteMisses;
    sample->bytesDecoded = gSample.bytesDecoded;
    sample->frameMicros = elapsedMicros(frameStart);
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    driver->frameCycles = r_probe_cycles() - frameCycleStart;
#endif
    driver->stage = "done";
    return true;
}

#if defined(DOOM_WEAPON_DIAG)
/// SHA-256 over a w x h rectangle of the surface, row by row (no coverage bytes).
/// The weapon check uses it: a whole-view hash also covers the animated items and
/// torches a host rebuild cannot know the phase of, a rectangle around the gun
/// does not.
void hashRect(const std::uint8_t* pixels, int stride, int x0, int y0, int w, int h,
              char hex[65]) {
    sha256_context_t context;
    std::uint8_t digest[32];

    sha256Init(&context);
    for (int row = 0; row < h; ++row) {
        sha256Update(&context, pixels + static_cast<std::size_t>(y0 + row) * stride + x0,
                     static_cast<std::size_t>(w));
    }
    sha256Final(&context, digest);
    digestToHex(digest, hex);
}
#endif  // DOOM_WEAPON_DIAG

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

#if defined(DOOM_HUD)
/**
 * @brief The automap frame's hash: the map window's rows alone.
 *
 * `hashSurface()` appends the wall pass's coverage record, which a map frame
 * never writes (it clears every pixel of the window itself), so a map frame has
 * no coverage to report and a stale one would make the number depend on the last
 * world frame instead of on the picture. This hashes exactly the 320x168 index
 * bytes `am_draw()` owns, which is the number `tools/am_check.py` prints for the
 * same state and the one the board's `SHOW,map` line carries.
 */
void hashWindow(const std::uint8_t* pixels, std::size_t pixelBytes, char hex[65]) {
    sha256_context_t context;
    std::uint8_t digest[32];

    sha256Init(&context);
    sha256Update(&context, pixels, pixelBytes);
    sha256Final(&context, digest);
    digestToHex(digest, hex);
}
#endif

bool isBreakdownView(int frame) {
    for (int index = 0; index < kFrameBreakdownCount; ++index) {
        if (kFrameBreakdownFrames[index] == frame) {
            return true;
        }
    }
    return false;
}

#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
/*
 * The cycle attribution line (`FRAMEPROF,d39`): what the cycles were spent on, per unit, so the
 * microsecond table can be read as work rather than as a phase name. Every
 * number here is a sum over the measured frames of one bucket the passes fill,
 * divided by the count that names it.
 */
void formatProbeLine(const ProbeSum& sum, char* out, std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return;
    }

    const int written = std::snprintf(
        out, capacity,
        "FRAMEPROF,d39,frames=%u,frame_cyc=%u,"
        "walk_nodes=%u,walk_subsectors=%u,walk_segs=%u,walk_bbox=%u,walk_clipscan=%u,"
        "walk_planefind_cyc=%u,walk_planefind_calls=%u,"
        "walk_line_cyc=%u,walk_store_cyc=%u,walk_segloop_cyc=%u,walk_planecheck_cyc=%u,"
        "walk_subsector_cyc=%u,walk_mapreads=%u,wall_stores=%u,"
        "wall_pre_cyc=%u,wall_setup_cyc=%u,wall_store_cyc=%u,wall_generic_cyc=%u,"
        "wall_open_cyc=%u,wall_pool_cyc=%u,wall_clip_cyc=%u,wall_emit_cyc=%u,"
        "wall_prologue_cyc=%u,"
        "wall_columns=%u,wall_px=%u,"
        "plane_pre_cyc=%u,plane_setup_cyc=%u,plane_px_cyc=%u,plane_cover_cyc=%u,"
        "plane_loop_cyc=%u,plane_spans=%u,"
        "masked_proof_cyc=%u,masked_proofs=%u,masked_proof_calls=%u,masked_memo_hits=%u,"
        "masked_proj_cyc=%u,"
        "masked_range_cyc=%u,masked_seed_cyc=%u,masked_seed_steps=%u,masked_sort_cyc=%u,"
        "masked_sprite_cyc=%u,masked_posts=%u,masked_tail_cyc=%u,spritelist_cyc=%u\n",
        static_cast<unsigned>(sum.frames),
        static_cast<unsigned>(sum.frameCycles),
        static_cast<unsigned>(PROBE_SUM(nodeVisits)),
        static_cast<unsigned>(PROBE_SUM(subsectorVisits)),
        static_cast<unsigned>(PROBE_SUM(segVisits)),
        static_cast<unsigned>(PROBE_SUM(bboxTests)),
        static_cast<unsigned>(PROBE_SUM(clipScanSteps)),
        static_cast<unsigned>(PROBE_SUM(planeFindCycle)),
        static_cast<unsigned>(PROBE_SUM(planeFindCalls)),
        static_cast<unsigned>(PROBE_SUM(walkLineCycle)),
        static_cast<unsigned>(PROBE_SUM(walkStoreCycle)),
        static_cast<unsigned>(PROBE_SUM(walkSegLoopCycle)),
        static_cast<unsigned>(PROBE_SUM(walkPlaneCheckCycle)),
        static_cast<unsigned>(PROBE_SUM(walkSubsectorCycle)),
        static_cast<unsigned>(PROBE_SUM(walkMapReads)),
        static_cast<unsigned>(PROBE_SUM(wallRangeStores)),
        static_cast<unsigned>(PROBE_SUM(wallPreflightCycle)),
        static_cast<unsigned>(PROBE_SUM(wallSetupCycle)),
        static_cast<unsigned>(PROBE_SUM(wallStoreCycle)),
        static_cast<unsigned>(PROBE_SUM(wallGenericCycle)),
        static_cast<unsigned>(PROBE_SUM(wallOpenCycle)),
        static_cast<unsigned>(PROBE_SUM(wallPoolCycle)),
        static_cast<unsigned>(PROBE_SUM(wallClipSaveCycle)),
        static_cast<unsigned>(PROBE_SUM(wallEmitCycle)),
        static_cast<unsigned>(PROBE_SUM(wallPrologueCycle)),
        static_cast<unsigned>(PROBE_SUM(wallColumns)),
        static_cast<unsigned>(PROBE_SUM(wallPixels)),
        static_cast<unsigned>(PROBE_SUM(planePreflightCycle)),
        static_cast<unsigned>(PROBE_SUM(planeSetupCycle)),
        static_cast<unsigned>(PROBE_SUM(planePixelCycle)),
        static_cast<unsigned>(PROBE_SUM(planeCoverCycle)),
        static_cast<unsigned>(PROBE_SUM(planeLoopCycle)),
        static_cast<unsigned>(PROBE_SUM(planeSpans)),
        static_cast<unsigned>(PROBE_SUM(maskedPatchProofCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedPatchProofs)),
        static_cast<unsigned>(PROBE_SUM(maskedProofCalls)),
        static_cast<unsigned>(PROBE_SUM(maskedPatchMemoHits)),
        static_cast<unsigned>(PROBE_SUM(maskedProjectionCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedRangeCheckCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedClipSeedCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedClipSeedSteps)),
        static_cast<unsigned>(PROBE_SUM(maskedSortCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedSpriteCycle)),
        static_cast<unsigned>(PROBE_SUM(maskedPostCalls)),
        static_cast<unsigned>(PROBE_SUM(maskedTailCycle)),
        static_cast<unsigned>(static_cast<std::uint32_t>(sum.spriteListCycles)));

    if (written <= 0 || static_cast<std::size_t>(written) >= capacity) {
        out[0] = '\0';
    }
}

/**
 * @brief Runs the inner-loop benchmark and prints where the operand bytes live.
 *
 * Three residencies of the same statement, then the difference. `us_permille`
 * units are thousandths of a microsecond (1,100 = 1.100 us), so a row here is
 * directly comparable with the frame table's `px_us_permille`.
 */
void runMemoryProbe(std::uint8_t* scratch, int stride, const r_walltex_t* texture,
                    const std::uint8_t* colormap, char* out, std::size_t capacity) {
    MemoryProbe memory;

    if (texture == nullptr || colormap == nullptr || scratch == nullptr) {
        std::snprintf(out, capacity, "FRAMEPROF,d39mem,status=skip\n");
        return;
    }

    // 200 rows is one full-height column; 200 passes gives 40,000 pixels per
    // row, which is above the 34,129 a frame draws, and the timer's own
    // resolution stops mattering.
    constexpr int kRows = 200;
    constexpr int kPasses = 200;
    constexpr fixed_t kIscale = 1 << 12;  // one texel every 16 rows

    static std::uint8_t ramColormap[256];
    static std::uint8_t ramColumn[128];
    std::uint64_t sink = 0u;

    for (int index = 0; index < 256; ++index) {
        ramColormap[index] = colormap[index];
    }
    for (int index = 0; index < 128; ++index) {
        ramColumn[index] = texture->pixels[index];
    }

    struct Row {
        const char* name;
        const std::uint8_t* column;
        const std::uint8_t* map;
        std::uint32_t* nanos;
    } rows[3] = {
        {"ram_ram", ramColumn, ramColormap, &memory.ramRamNanos},
        {"ram_flash", ramColumn, colormap, &memory.ramFlashNanos},
        {"flash_flash", texture->pixels, colormap, &memory.flashFlashNanos},
    };

    for (Row& row : rows) {
        const std::int64_t start = nowMicros();
        benchInner(scratch, stride, row.column, row.map, kIscale, kRows, kPasses,
                   &sink);
        const std::uint32_t micros = elapsedMicros(start);
        // The frame table's unit: permille of a microsecond.
        *row.nanos = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(micros) * 1000u)
            / static_cast<std::uint64_t>(kRows * kPasses));
    }

    {
        const std::int64_t start = nowMicros();
        for (int pass = 0; pass < kPasses; ++pass) {
            std::uint8_t* at = scratch;

            for (int row = 0; row < kRows; ++row) {
                *at = static_cast<std::uint8_t>(row);
                at += stride;
            }
        }
        const std::uint32_t micros = elapsedMicros(start);
        memory.storeNanos = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(micros) * 1000u)
            / static_cast<std::uint64_t>(kRows * kPasses));
    }
    memory.pixels = static_cast<std::uint32_t>(kRows * kPasses);

    std::snprintf(out, capacity,
                  "FRAMEPROF,d39mem,pixels=%u,store_us_permille=%u,ram_ram_us_permille=%u,"
                  "ram_flash_us_permille=%u,flash_flash_us_permille=%u,sink=%u\n",
                  static_cast<unsigned>(memory.pixels),
                  static_cast<unsigned>(memory.storeNanos),
                  static_cast<unsigned>(memory.ramRamNanos),
                  static_cast<unsigned>(memory.ramFlashNanos),
                  static_cast<unsigned>(memory.flashFlashNanos),
                  static_cast<unsigned>(static_cast<std::uint32_t>(sink)));
}
#endif /* PIXELROOT32_DOOM_RASTER_PROBE */

// --- The level session ------------------------------------------------------

/// Palette 0 of the WAD's PLAYPAL lump: 256 entries of R, G, B.
constexpr std::size_t kPlaypalBytes = 768u;

/**
 * @brief What the probe and the game frame share: one open level.
 *
 * `Driver` already holds the map, the walk state, the surface and the three
 * resolvers, and it is the caller's: the probe keeps its copy on the stack,
 * because `env:esp32dev_ILI9341_frameram` has
 * less than 1 KB of `dram0_0_seg` headroom and a 464 B file-scope copy would
 * overflow it. The game frame owns one static copy (`gShowDriver`) instead. The
 * session adds only what has to outlive one render call: the spawn viewpoint,
 * the marker name and the heap sample the memory line needs.
 */
struct LevelSession {
    Driver* driver;
    FrameView spawn;
    char marker[5];
    bool open;
    bool spawnValid;
    std::uint32_t heapBefore;
};

LevelSession gSession;

/// One formatted-line buffer shared by the session setup and the probe's own
/// output. They never run at the same time, and one 512-byte buffer instead of
/// two is what keeps `env:esp32dev_ILI9341_frameram` inside `dram0_0_seg`.
char gSessionLine[kFrameProfileLineCapacity];

/*
 * The setup's failure taxonomy. Every branch of `openLevelSession()` (and of
 * the world open it calls) records one value beside the line it prints, so the
 * scene can paint the reason on the panel. The serial line keeps its exact text
 * and fields -- log parsers and tests read it -- and this record only adds the
 * machine-readable form the screen consumes.
 *
 * `None` is both "no failure yet" and "the open succeeded"; it is reset at the
 * top of every open and left untouched by a successful one.
 */
LevelFailure gLevelFailure;

/// Records one classified failure. @p detail may be NULL; a longer string is
/// truncated by the record's own fixed capacity, never by a second buffer.
void recordLevelFailure(LevelFailureReason reason, int code = 0, const char* detail = nullptr) {
    gLevelFailure = LevelFailure{};
    gLevelFailure.reason = reason;
    gLevelFailure.code = code;
    if (detail != nullptr) {
        std::snprintf(gLevelFailure.detail, sizeof gLevelFailure.detail, "%s", detail);
    }
}

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
/// The game frame's Driver. The probe passes its own stack copy instead; the
/// two paths never run in one build, and keeping the probe's off .bss is what
/// leaves the RAM control environment linkable.
Driver gShowDriver;
#endif

/// PLAYPAL's palette 0, decoded once at open. The scene turns it into the
/// index -> RGB332 table; this file only carries the 768 bytes. Compiled only
/// for the game frame: the probe renders palette indices and must not spend
/// 768 B of `dram0_0_seg` its RAM control environment does not have.
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
std::uint8_t gPlaypal[kPlaypalBytes];
constexpr std::size_t kPlaypalStaticBytes = kPlaypalBytes;
#else
constexpr std::size_t kPlaypalStaticBytes = 0u;
#endif
bool gPlaypalValid = false;

// --- The world ----------------------------------------------------------------

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

/*
 * THE WORLD: one level, one arena, one tic at a time.
 *
 * The host tools tick the world over a 256 KB malloc'd arena. On the board there
 * is no 256 KB to spend, so the arena is a number this file measures instead of
 * a number it assumes.
 *
 * THE ARENA'S SIZE IS A MEASUREMENT, THEN A RULE
 *
 * The world is loaded twice at open. The first load runs over a generous arena
 * -- the largest free block less a small reserve -- with nothing to squeeze it,
 * and reports z_stats()'s peak and used bytes. `p_level_shutdown()` then frees
 * every PU_LEVEL block and the block goes back to the heap. The second load runs
 * over the size the rule chose: the measured peak plus at most 25 %, rounded up
 * to 1 KB. The map is read-only through both loads -- p_level_init() copies
 * SECTORS and LINEDEFS' special into the level, and the spawns allocate from the
 * level's own zone -- so the trial cannot disturb the load that is kept.
 *
 * WHY THE WORLD IS NOT IN `LevelSession`
 *
 * `LevelSession` and `Driver` are compiled in the probe builds too, and
 * `env:esp32dev_ILI9341_frameram` has under 1 KB of `dram0_0_seg` headroom: a
 * p_level_t in the shared session would overflow it. The world, like
 * `gShowDriver` and `gPlaypal`, therefore lives under the game's own flag, and
 * the probe builds carry neither its state nor its code.
 *
 * WHO OWNS WHAT
 *
 *   the zone        every mobj, every special thinker, the sector array and the
 *                   linedef-special copy -- all of it inside the arena below.
 *   the world       `gWorld.level`, a `p_level_t` whose own fields point into
 *                   the arena, so it must not move after the load: it is a
 *                   file-scope static for that reason and not for style.
 *   the tics        the scene's tic clock decides how many elapsed; this file
 *                   runs exactly that many `p_level_tic()` calls.
 */

/// The headroom the sizing rule allows over the measured peak, in percent.
constexpr std::uint32_t kArenaHeadroomPercent = 25u;

/// The granularity the chosen size is rounded up to: 1 KB.
constexpr std::uint32_t kArenaRoundBytes = 1024u;

/// Bytes left in the heap when the measuring arena asks for the largest block.
/// The reserve is not for the world: it keeps the probe from consuming a hole a
/// later allocation (a level change, a sprite list) would need to see at all.
constexpr std::uint32_t kArenaProbeReserveBytes = 4096u;

/// The game's one world: the level state, its arena, and the heap block behind it.
struct LevelWorld {
    p_level_t level;
    std::uint8_t* arena;   ///< What z_init() was given: `block` rounded up to 8.
    void* block;           ///< What heap_caps_free() must get back.
    std::uint32_t arenaBytes;
    bool open;
};

LevelWorld gWorld;

/*
 * The level-transition game state and the one-shot the scene reads.
 *
 * `gLevelState` is upstream's gameepisode/gamemap/secretexit, which this port
 * keeps as a value because it has no globals (g_game.h). It is written by
 * g_do_world_done() before each load, so it always names the level that is open
 * and the exit that requested it.
 *
 * `gVictoryPending` is "the run ended and nothing has looked at it yet": E1M8's
 * ending has no finale screen to draw (this port has none), so the driver
 * records the fact, loads the run's start so the frame the menu draws over is a
 * live one, and the scene opens the main menu on the next frame. One-shot because
 * the scene must not reopen the menu every frame after a victory.
 */
g_level_state_t gLevelState = {1u, static_cast<std::uint8_t>(kFrameLevel), 0u};
bool gVictoryPending = false;

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)

/*
 * THE STATUS BAR
 *
 * With DOOM_HUD the view is 320x168 and the bottom 32 rows belong to
 * lib/doom/st_stuff.c. The bar's state is the face machine's ~60 bytes; every
 * number, weapon, key and the pain level are read from the player the world
 * already owns, so nothing here duplicates game state.
 *
 * The patches are raw, name-addressed lumps the container keeps (not manifest
 * records). whd_lump_index_for_name() is a linear scan of the name table, which
 * is flash-resident and about 2,000 entries long, so a frame that asks for forty
 * names pays for it forty times; the cache below remembers each name's lump
 * index by a 32-bit FNV-1a hash. It has 128 slots (768 B of DRAM) because the
 * weapon widget asks for its patches through the same cache: 86 bar names plus
 * the 28 weapon frames are 114 distinct names a long session can touch, and a
 * weapon name that missed would pay the linear name scan every frame. A name
 * that finds the table full is still looked up each time, never dropped.
 *
 * DOOM_HUD_NOFACES is the CYD fall-back: the bar without the face widget
 * (STBAR's empty face box stays), kept for the case the full bar misses its
 * frame-rate target on the 4 MB board.
 */
constexpr std::size_t kHudCacheSlots = 128u;
st_state_t gStatus;
std::uint32_t gStatusLastTime = 0u;
std::uint32_t gHudHash[kHudCacheSlots];
std::int16_t gHudLump[kHudCacheSlots];
std::uint32_t gHudMicros = 0u;
int gHudMissing = 0;
bool gHudPartsDone = false;

#if defined(DOOM_HUD_NOFACES)
constexpr int kHudFaces = 0;
#else
constexpr int kHudFaces = 1;
#endif

const std::uint8_t* hudLump(void* /*context*/, const char* name, std::size_t* size) {
    std::uint32_t hash = 2166136261u;
    for (const char* c = name; *c != '\0'; ++c) {
        hash = (hash ^ static_cast<std::uint8_t>(*c)) * 16777619u;
    }
    hash |= 1u;  // 0 marks an empty slot

    int lump = -1;
    std::size_t slot = hash % kHudCacheSlots;
    std::size_t free = kHudCacheSlots;
    for (std::size_t probe = 0u; probe < kHudCacheSlots; ++probe) {
        const std::size_t at = (slot + probe) % kHudCacheSlots;
        if (gHudHash[at] == hash) {
            lump = gHudLump[at];
            break;
        }
        if (gHudHash[at] == 0u) {
            free = at;
            break;
        }
    }
    if (lump < 0) {
        lump = doom_whd_source_lump_index(&gWhd, name);
        if (lump < 0) {
            return nullptr;
        }
        if (free < kHudCacheSlots) {
            gHudHash[free] = hash;
            gHudLump[free] = static_cast<std::int16_t>(lump);
        }
    }
    const std::uint8_t* data = nullptr;
    std::uint32_t bytes = 0u;
    if (doom_whd_source_lump_view(&gWhd, lump, &data, &bytes) != DOOM_WHD_SOURCE_OK) {
        return nullptr;
    }
    *size = static_cast<std::size_t>(bytes);
    return data;
}

/// ST_Start(): the bar begins with the level.
void hudStart() {
    st_start(&gStatus, &gWorld.level.playerState);
    gStatusLastTime = static_cast<std::uint32_t>(gWorld.level.leveltime);
}

/// ST_Ticker(): once per world tic, after p_level_tic(). A level time that went
/// backwards is a level reload (G_DoReborn), which upstream answers with
/// ST_Start().
void hudTick() {
    const std::uint32_t now = static_cast<std::uint32_t>(gWorld.level.leveltime);
    if (now < gStatusLastTime) {
        hudStart();
    }
    gStatusLastTime = now;
    st_ticker(&gStatus, &gWorld.level.playerState);
}

/// ST_Drawer(): repaints the bottom 32 rows of the finished frame.
void hudDraw(std::uint8_t* surface, int width) {
    if (!gWorld.open) {
        return;
    }
    const std::int64_t start = nowMicros();
    gHudMissing = st_draw(&gStatus, &gWorld.level.playerState, hudLump, nullptr, surface,
                          width, kHudFaces);
    gHudMicros = elapsedMicros(start);
}

/*
 * THE WEAPON
 *
 * R_DrawPlayerSprites(): the player's psprites, drawn over the finished 3D view
 * and under the bar. The patches are raw, name-addressed lumps listed in
 * tools/weapon_lumps.txt, looked up through the same cache as the bar's.
 * DOOM_HUD_NOWEAPON leaves the call out: it exists only so the cost of the
 * weapon can be measured by an A/B build, never as a product option.
 */
std::uint32_t gWeaponMicros = 0u;
int gWeaponMissing = 0;

#if defined(DOOM_WEAPON_DIAG)
// DOOM_WEAPON_DIAG (env esp32dev_ILI9341_hud_weaponcheck): once, when the pistol
// rests, print SHA-256 hashes of a box around it before and after it is drawn, so
// the host can rebuild both (tools/weapon_draw_check.py --board-rect). Off in the
// shipped HUD build: the extra code moves the flash layout, which costs frame time.
bool gWeaponPartsDone = false;
char gPreWeaponHash[65] = "";
// The box the resting pistol (PISGA0 at sx 1.0, sy 32.0) fits in: it paints
// x 132..183, y 122..167.
constexpr int kWeaponRectX = 128;
constexpr int kWeaponRectY = 116;
constexpr int kWeaponRectW = 64;
constexpr int kWeaponRectH = 52;
#endif  // DOOM_WEAPON_DIAG

void weaponDraw(Driver* driver) {
#if !defined(DOOM_HUD_NOWEAPON)
    if (!gWorld.open) {
        return;
    }
    const p_player_t& player = gWorld.level.playerState;
    if (player.mo == nullptr) {
        return;
    }
    const std::int64_t start = nowMicros();
    gWeaponMissing = r_psprites_draw(&driver->view, &driver->surface, &driver->light, &player,
                                     gWorld.level.sectors[player.mo->sector].lightLevel,
                                     player.extralight, hudLump, nullptr, nullptr);
    gWeaponMicros = elapsedMicros(start);
#else
    (void)driver;
#endif
}

#if defined(DOOM_WEAPON_DIAG)
/// True once the weapon has finished rising and rests at the ready state with no
/// bob, which is the one steady state a stationary player reaches: the board then
/// prints the hashes a host rebuild can match.
bool weaponAtRest() {
    const p_pspdef_t& weapon = gWorld.level.playerState.psprites[0];
    return gWorld.open && weapon.state == S_PISTOL && weapon.sx == FRACUNIT
           && weapon.sy == 32 * FRACUNIT;
}
#endif  // DOOM_WEAPON_DIAG

#if defined(DOOM_MENU)
/*
 * THE MENU
 *
 * lib/doom/m_menu.c owns the state machine and the drawing; this file only
 * paints it over the finished frame and carries out its New Game. The menu is
 * the scene's, so the driver holds a pointer, not a copy, and a NULL pointer or
 * a closed menu costs one comparison per frame.
 *
 * The patches are raw, name-addressed lumps listed in tools/menu_lumps.txt and
 * are looked up through the HUD's cache. The HUD and
 * the weapon already use 114 of its 128 slots in a long session and the menu asks
 * for about 70 more names, so the cache is emptied whenever the menu opens or
 * closes: while it is open the frame needs only the names it actually draws (the
 * bar, the weapon and the active screen), and none of them pays the linear scan of
 * the container's name table that a full cache would charge every frame.
 */
static_assert(sizeof(gHudHash) / sizeof(gHudHash[0]) == kHudCacheSlots, "HUD cache layout");
const m_menu_t* gMenu = nullptr;
bool gMenuWasActive = false;
std::uint32_t gMenuMicros = 0u;
int gMenuMissing = 0;

void hudCacheReset() {
    for (std::size_t slot = 0u; slot < kHudCacheSlots; ++slot) {
        gHudHash[slot] = 0u;
        gHudLump[slot] = 0;
    }
}

/// Paints the open menu over the finished frame (menu, then nothing above it).
void menuDraw(std::uint8_t* surface, int width) {
    const bool active = m_menu_active(gMenu) != 0;
    if (active != gMenuWasActive) {
        gMenuWasActive = active;
        hudCacheReset();
    }
    if (!active) {
        return;
    }
    const std::int64_t start = nowMicros();
    gMenuMissing = m_menu_draw(gMenu, hudLump, nullptr, surface, width);
    gMenuMicros = elapsedMicros(start);
}
#endif  // DOOM_MENU

/*
 * THE AUTOMAP
 *
 * lib/doom/am_map.c is upstream's am_map.c translated; this block is the session
 * side of it and nothing else:
 *
 *   the bitmap   one shadow bit per linedef, `(lines + 7) / 8` bytes of heap,
 *                taken when the level opens and freed when the session closes.
 *                r_segs.c sets a bit for every wall range the wall pass stores,
 *                which is upstream's `linedef->flags |= ML_MAPPED`.
 *   the state    `gMap`, the caller-owned am_state_t: AM_LevelInit once per
 *                level load, AM_Stop on a reload, the window kept in tics.
 *   the frame    renderSessionView() draws it over the 168 view rows instead of
 *                the world, then the bar and the menu on top, exactly the order
 *                upstream's D_Display uses (the automap, then the status bar).
 *
 * Why the map can be up without breaking anything: the map is drawn *instead of*
 * renderOnce(), so no pass runs and the surface's rows 168..199 are the bar's as
 * usual; the hash block below is unchanged, so `SHOW,frame` still reports a
 * number; and `SHOW,map` is the map's own line, printed only while the map is up.
 *
 * THE TIC
 *
 * AM_Ticker runs from frameLevelRunTics(), once per 35 Hz tic and after
 * p_level_tic -- upstream's G_Ticker order (P_Ticker, then the automap) -- so the
 * pan is 140 px a second and a zoom is 2x a second whatever the frame rate is.
 * Driving it from the frame instead would make both a function of the panel.
 */
am_state_t gMap;
std::uint8_t* gMapped = nullptr;
std::uint32_t gMappedBytes = 0u;
std::uint32_t gMapMicros = 0u;
int gMapLinesDrawn = 0;

/// Takes the shadow bitmap and initialises the automap for the open level.
bool openAutomap() {
    const doom_map_t& map = gSession.driver->map;
    const std::uint32_t lines = map.lumps[DOOM_MAP_LINEDEFS].count;
    const std::uint32_t bytes = (lines + 7u) / 8u;
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;

    if (lines == 0u || bytes == 0u) {
        std::snprintf(line, sizeof line, "SHOW,map,init,status=fail,reason=lines,lines=%u\n",
                      static_cast<unsigned>(lines));
        writeLine(line);
        return false;
    }
    gMapped = static_cast<std::uint8_t*>(
        heap_caps_malloc(static_cast<std::size_t>(bytes), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (gMapped == nullptr) {
        std::snprintf(line, sizeof line,
                      "SHOW,map,init,status=fail,reason=alloc,bytes=%u,heap=%u\n",
                      static_cast<unsigned>(bytes),
                      static_cast<unsigned>(esp_get_free_heap_size()));
        writeLine(line);
        return false;
    }
    std::memset(gMapped, 0, static_cast<std::size_t>(bytes));
    gMappedBytes = bytes;

    am_clear_state(&gMap);
    if (!am_level_init(&gMap, &map, gMapped, gMappedBytes)) {
        heap_caps_free(gMapped);
        gMapped = nullptr;
        gMappedBytes = 0u;
        std::snprintf(line, sizeof line, "SHOW,map,init,status=fail,reason=level\n");
        writeLine(line);
        return false;
    }
    am_stop(&gMap);

    // Both copies on purpose: the wall pass reads the state's copy (r_bsp_init()
    // copied the storage into the view when the session opened) and the driver
    // keeps the descriptor it was given.
    gSession.driver->storage.mapped = gMapped;
    gSession.driver->storage.mappedBytes = gMappedBytes;
    gSession.driver->view.storage.mapped = gMapped;
    gSession.driver->view.storage.mappedBytes = gMappedBytes;

    std::snprintf(line, sizeof line,
                  "SHOW,map,init,status=ok,lines=%u,bytes=%u,scale=%ld,min=%ld,max=%ld\n",
                  static_cast<unsigned>(lines), static_cast<unsigned>(gMappedBytes),
                  static_cast<long>(gMap.scaleMtof), static_cast<long>(gMap.minScaleMtof),
                  static_cast<long>(gMap.maxScaleMtof));
    writeLine(line);
    return true;
}

/// Releases the shadow bitmap. Safe on a closed automap.
void closeAutomap() {
    am_stop(&gMap);
    gMap.mapped = nullptr;
    gMap.mappedBytes = 0u;
    if (gMapped != nullptr) {
        heap_caps_free(gMapped);
        gMapped = nullptr;
    }
    gMappedBytes = 0u;
}

/// A reloaded level (New Game) has seen nothing yet, and P_SetupLevel re-reading
/// the linedefs would have cleared upstream's ML_MAPPED bits the same way.
void resetAutomap() {
    am_stop(&gMap);
    frameLevelMapForget();
}

#endif  // SHOW && DOOM_HUD

/*
 * The live seams: p_level_bind_render() installs three callbacks on
 * the map the renderer walks, so what the simulation wrote is what the frame
 * draws.
 *
 *   readSector     p_sector_t's mutable sector state (heights, flats, light).
 *                  Without it a door that opened in the simulation still drew
 *                  closed, and standing inside its 16-unit sector left the whole
 *                  320x200 surface at the 0xFF prefill. A sector's flats arrive
 *                  already carrying P_UpdateSpecials' animation frame.
 *   adjustSide     the scroll offset P_UpdateSpecials accumulates on a line
 *                  special 48's front side.
 *   translateName  the animation tables' frame for a *texture* name; installed
 *                  only on a map whose SIDEDEFS name one -- the flat half
 *                  rides with readSector.
 *
 * All three are p_spec.c's; this file only binds them, so the arithmetic the
 * board runs is the arithmetic tools/specials_check.c measures per tic and
 * tools/rsegs_check.c proves in pixels.
 */

/// Rounds an arena size up to the sizing rule's 1 KB.
std::uint32_t roundArenaUp(std::uint32_t bytes) {
    return (bytes + kArenaRoundBytes - 1u) / kArenaRoundBytes * kArenaRoundBytes;
}

/**
 * @brief Takes @p bytes of internal DRAM and aligns the payload to the zone.
 *
 * `heap_caps_malloc()`'s alignment is the allocator's promise, not this file's,
 * and the zone refuses an arena that is not ZONE_ALIGNMENT-aligned. One extra
 * alignment unit is requested and the payload is aligned here, so the world
 * opens on any heap implementation; @p block is what must go back.
 */
bool takeArena(std::uint32_t bytes, std::uint8_t** arena, void** block) {
    void* raw = heap_caps_malloc(static_cast<std::size_t>(bytes) + ZONE_ALIGNMENT,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (raw == nullptr) {
        *arena = nullptr;
        *block = nullptr;
        return false;
    }
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(raw);
    const std::uintptr_t mask = static_cast<std::uintptr_t>(ZONE_ALIGNMENT) - 1u;
    *arena = reinterpret_cast<std::uint8_t*>((address + mask) & ~mask);
    *block = raw;
    return true;
}

/**
 * @brief Loads the world once over @p map and reports the zone peak.
 *
 * @param map      The session's open map.
 * @param arena    The arena to load into.
 * @param bytes    Its size.
 * @param level    Receives the load; left zeroed when the load is refused.
 * @param zoneOut  Receives z_stats()'s snapshot; zeroed when the load is refused.
 * @return P_SETUP_OK with @p zoneOut filled, or the p_level_init() code.
 */
p_setup_error_t probeWorld(const doom_map_t* map, std::uint8_t* arena, std::uint32_t bytes,
                           p_level_t* level, zone_stats_t* zoneOut,
                           p_skill_t skill = P_SKILL_MEDIUM) {
    *zoneOut = zone_stats_t{};
    p_spawn_rules_t rules = p_spawn_rules_default();
    rules.skill = skill;
    const p_setup_error_t setup = p_level_init(level, map, arena, bytes, &rules);
    if (setup != P_SETUP_OK) {
        return setup;
    }
    // P_SetupLevel's order: the THINGS run first, then the sector specials. The
    // specials create the light and door thinkers the tic walks, so they are
    // part of the peak this number is about.
    if (p_spawn_specials(level) != P_SPEC_OK) {
        p_level_shutdown(level);
        return P_SETUP_ERR_ALLOC;
    }
    *zoneOut = z_stats(&level->zone);
    return P_SETUP_OK;
}

/// Shuts the world down and releases its arena. Safe on a closed world.
void closeWorldSession();

/// Releases the whole level session. Defined after the open path it unwinds.
void closeLevelSession();

/*
 * A loaded level has no eye until its first tic, and that frame must not be
 * frozen.
 *
 * p_player_wire() is G_PlayerReborn's `memset (p, 0, sizeof(*p))`, so
 * `playerState.viewheight` is 0 and `viewz` with it; P_PlayerThink's
 * P_CalcHeight is what gives the camera a height (and its half-step ramp). Every
 * load therefore draws its first frame from the sector floor. That is
 * tolerable only while the frame is a transient under a fade, and it stops
 * being tolerable the moment something freezes it: the victory opens the main
 * menu on the frame after the reload, the world never ticks while the menu is
 * up, and the camera stays at z = 0 for as long as the menu is open.
 *
 * A camera at the floor is not a low view. The wall pass's bottom clips and the
 * plane pass's spans no longer meet in the columns at the view's edges, and the
 * gap between them keeps the surface's 0xFF prefill -- palette 255 is
 * (167,107,107) -- so two magenta bars run down the frame over a floor stretched
 * to the horizon.
 *
 * The value written is the one the level's own tics reach within half a second
 * and then keep -- P_CalcHeight clamps `viewheight` to P_VIEWHEIGHT and stops
 * growing it -- so the simulation is unchanged: the ramp simply is not visible
 * in a frame that never advances. `rsegs_check`'s spawn rule is the same number
 * (`sector.floorHeight + R_VIEWHEIGHT`), which is why its player-start frames
 * are drawn from it.
 */
static void settleLevelView() {
    p_level_t& level = gWorld.level;
    if (level.playerState.mo == nullptr) {
        return;
    }
    fixed_t viewz = level.playerState.mo->z + P_VIEWHEIGHT;
    const fixed_t ceiling = level.playerState.mo->ceilingz - 4 * FRACUNIT;
    if (viewz > ceiling) {
        // P_CalcHeight's own clamp, so a low ceiling lands on the same pixel the
        // first tic would produce.
        viewz = ceiling;
    }
    level.playerState.viewheight = P_VIEWHEIGHT;
    level.playerState.deltaviewheight = 0;
    level.playerState.viewz = viewz;
}

/**
 * @brief Measures, sizes and opens the world; prints two `SHOW,arena,...` lines.
 *
 * Runs only for the game frame, from `openLevelSession()` after the session is
 * open, because the frame path's own allocations (the display sprite, the sprite
 * list) are what the arena has to fit next to. The arithmetic is printed instead
 * of being asserted so a board with a different heap can be judged from its log.
 *
 * @return true when the world is open. On false nothing is left allocated and
 *         the reason is already on the serial line.
 */
bool openWorldSession() {
    gWorld = LevelWorld{};
    gFrameWorld = nullptr;
    // A fresh session's game state is the level being opened, with no exit
    // requested. gRuntimeLevel was set by openLevelSession() before the map was
    // read, so the marker and this record cannot disagree.
    gLevelState.episode = 1u;
    gLevelState.map = gRuntimeLevel;
    gLevelState.secretExit = 0u;
    gVictoryPending = false;

    if (!gSession.open || gSession.driver == nullptr) {
        return false;
    }
    const doom_map_t* map = &gSession.driver->map;

    // The session's one formatted-line buffer. openLevelSession() has already
    // written its setup line, and nothing in this call graph re-enters it, so
    // the 512 bytes are reused instead of bought twice.
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;

    const int caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const std::uint32_t heapTotal = static_cast<std::uint32_t>(heap_caps_get_total_size(caps));
    const std::uint32_t heapBefore = static_cast<std::uint32_t>(esp_get_free_heap_size());
    const std::uint32_t largestBefore =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(caps));

    // A generous arena: everything the largest hole has, less the reserve. This
    // is the "does it fit at all" number, so it is deliberately not the size the
    // session will keep.
    std::uint32_t probeBytes =
        largestBefore > kArenaProbeReserveBytes ? largestBefore - kArenaProbeReserveBytes : 0u;
    probeBytes &= ~(static_cast<std::uint32_t>(ZONE_ALIGNMENT) - 1u);

    std::uint8_t* probeArena = nullptr;
    void* probeBlock = nullptr;
    if (probeBytes < kArenaRoundBytes || !takeArena(probeBytes, &probeArena, &probeBlock)) {
        recordLevelFailure(LevelFailureReason::World, 0, "heap");
        std::snprintf(line, sizeof line,
                      "SHOW,arena,probe,status=fail,reason=heap,peak=0,used=0,"
                      "arena=%u,heap_free_before=%u,heap_largest_before=%u\n",
                      static_cast<unsigned>(probeBytes), static_cast<unsigned>(heapBefore),
                      static_cast<unsigned>(largestBefore));
        writeLine(line);
        return false;
    }
    const std::uint32_t heapDuringProbe = static_cast<std::uint32_t>(esp_get_free_heap_size());
    const std::uint32_t largestDuringProbe =
        static_cast<std::uint32_t>(heap_caps_get_largest_free_block(caps));

    zone_stats_t zone;
#if defined(DOOM_MENU)
    // The menu's New Game can reload this level at any skill, and a harder
    // skill spawns more things (E1M1: 92 mobjs at medium, 116 at ultra-violence
    // and nightmare). The arena must hold the largest of them, so the peak is
    // measured at nightmare; the world itself still starts at medium below.
    const p_skill_t probeSkill = P_SKILL_NIGHTMARE;
#else
    const p_skill_t probeSkill = P_SKILL_MEDIUM;
#endif
    const p_setup_error_t probeSetup =
        probeWorld(map, probeArena, probeBytes, &gWorld.level, &zone, probeSkill);
    const std::uint32_t things = gWorld.level.stats.things;
    const std::uint32_t spawned = gWorld.level.stats.spawned;
    const std::uint32_t spawnFailed = gWorld.level.stats.spawnFailed;
    const std::uint32_t thinkers = gWorld.level.thinkers.count;
    const std::uint32_t specials = gWorld.level.specStats.spawned;
    const std::uint32_t mobjs = probeSetup == P_SETUP_OK ? p_level_mobj_count(&gWorld.level) : 0u;

    if (probeSetup == P_SETUP_OK) {
        std::snprintf(line, sizeof line,
                      "SHOW,arena,probe,status=ok,peak=%u,used=%u,free=%u,overhead=%u,"
                      "failures=%u,arena=%u,things=%u,spawned=%u,spawn_failed=%u,"
                      "thinkers=%u,specials=%u,mobjs=%u,heap_total=%u,"
                      "heap_free_before=%u,heap_largest_before=%u,heap_free_after=%u,"
                      "heap_largest_after=%u\n",
                      static_cast<unsigned>(zone.peakInUse), static_cast<unsigned>(zone.inUse),
                      static_cast<unsigned>(zone.freeBytes),
                      static_cast<unsigned>(zone.overhead),
                      static_cast<unsigned>(zone.failures),
                      static_cast<unsigned>(probeBytes), static_cast<unsigned>(things),
                      static_cast<unsigned>(spawned), static_cast<unsigned>(spawnFailed),
                      static_cast<unsigned>(thinkers), static_cast<unsigned>(specials),
                      static_cast<unsigned>(mobjs), static_cast<unsigned>(heapTotal),
                      static_cast<unsigned>(heapBefore), static_cast<unsigned>(largestBefore),
                      static_cast<unsigned>(heapDuringProbe),
                      static_cast<unsigned>(largestDuringProbe));
    } else {
        // A refused load is the one case where the peak means nothing: the zone
        // never held a world. The arithmetic is printed with the code that
        // refused it, so "too small" and "the map is wrong" are different lines.
        std::snprintf(line, sizeof line,
                      "SHOW,arena,probe,status=fail,reason=world,setup=%d,peak=0,used=0,"
                      "arena=%u,things=%u,spawned=%u,spawn_failed=%u,heap_free_after=%u,"
                      "heap_largest_after=%u\n",
                      static_cast<int>(probeSetup), static_cast<unsigned>(probeBytes),
                      static_cast<unsigned>(things), static_cast<unsigned>(spawned),
                      static_cast<unsigned>(spawnFailed), static_cast<unsigned>(heapDuringProbe),
                      static_cast<unsigned>(largestDuringProbe));
    }
    writeLine(line);

    p_level_shutdown(&gWorld.level);
    heap_caps_free(probeBlock);

    if (probeSetup != P_SETUP_OK) {
        recordLevelFailure(LevelFailureReason::World, static_cast<int>(probeSetup), "probe");
        return false;
    }

    // The rule: peak + 25 %, rounded up to 1 KB.
    const std::uint32_t chosenBytes =
        roundArenaUp(zone.peakInUse + zone.peakInUse * kArenaHeadroomPercent / 100u);
    if (chosenBytes == 0u || chosenBytes > largestBefore) {
        recordLevelFailure(LevelFailureReason::World, 0, "fit");
        std::snprintf(line, sizeof line,
                      "SHOW,arena,fit,status=fail,reason=no_fit,peak=%u,chosen=%u,"
                      "heap_largest_before=%u,heap_largest=%u\n",
                      static_cast<unsigned>(zone.peakInUse), static_cast<unsigned>(chosenBytes),
                      static_cast<unsigned>(largestBefore),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
        writeLine(line);
        return false;
    }

    if (!takeArena(chosenBytes, &gWorld.arena, &gWorld.block)) {
        recordLevelFailure(LevelFailureReason::World, 0, "alloc");
        std::snprintf(line, sizeof line,
                      "SHOW,arena,fit,status=fail,reason=alloc,peak=%u,chosen=%u,"
                      "heap_free=%u,heap_largest=%u\n",
                      static_cast<unsigned>(zone.peakInUse), static_cast<unsigned>(chosenBytes),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
        writeLine(line);
        return false;
    }
    gWorld.arenaBytes = chosenBytes;

    zone_stats_t live;
    const p_setup_error_t setup =
        probeWorld(map, gWorld.arena, gWorld.arenaBytes, &gWorld.level, &live);
    if (setup != P_SETUP_OK) {
        // The measured peak did not fit its own arena: report both numbers and
        // leave nothing open rather than trimming the renderer to make room.
        recordLevelFailure(LevelFailureReason::World, static_cast<int>(setup), "fit");
        std::snprintf(line, sizeof line,
                      "SHOW,arena,fit,status=fail,reason=world,setup=%d,peak=%u,chosen=%u,"
                      "arena=%u,heap_free=%u,heap_largest=%u\n",
                      static_cast<int>(setup), static_cast<unsigned>(zone.peakInUse),
                      static_cast<unsigned>(chosenBytes),
                      static_cast<unsigned>(gWorld.arenaBytes),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
        writeLine(line);
        closeWorldSession();
        return false;
    }

    gWorld.open = true;
    gFrameWorld = &gWorld.level;
    // The renderer walks gSession.driver->map. Bind the level's live
    // state to it -- the mutable sectors, the scroll offsets P_UpdateSpecials
    // accumulates and the animation tables -- so what the simulation wrote is
    // what the frame draws. The probe paths call openLevelSession() without
    // loadPlaypal, so they never take this branch and keep rendering the
    // immutable map.
    p_level_bind_render(&gWorld.level, &gSession.driver->map);
    // Boot is a load too, and its frame is drawn before the first tic.
    settleLevelView();
#if defined(DOOM_AUDIO)
    // G_DoLoadLevel's S_Start: kill the previous level's sounds, tell the layer
    // which level this is (S_Start's music number and the map-8 rule) and let it
    // start the music. The listener exists from the first tic; setting it here
    // keeps a sound started before that tick distance-adjusted.
    S_SetGameLevel(1, static_cast<int>(gRuntimeLevel));
    S_SetListener(gWorld.level.playerState.mo);
    S_Start();
#endif
#if defined(DOOM_HUD)
    hudStart();
    // The automap's shadow bitmap and its level state. A refusal is
    // reported (`SHOW,map,init,status=fail`) and leaves the map unavailable; the
    // world frame is untouched either way.
    (void)openAutomap();
#endif
    std::snprintf(line, sizeof line,
                  "SHOW,arena,fit,status=ok,chosen=%u,peak=%u,used=%u,headroom=25,round=1024,"
                  "arena=%u,world_static=%u,heap_free=%u,heap_largest=%u\n",
                  static_cast<unsigned>(chosenBytes), static_cast<unsigned>(zone.peakInUse),
                  static_cast<unsigned>(live.inUse), static_cast<unsigned>(gWorld.arenaBytes),
                  static_cast<unsigned>(sizeof(LevelWorld)),
                  static_cast<unsigned>(esp_get_free_heap_size()),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(caps)));
    writeLine(line);
    return true;
}

void closeWorldSession() {
    gFrameWorld = nullptr;
    if (gSession.driver != nullptr) {
        p_level_unbind_render(&gSession.driver->map);
    }
    if (gWorld.open) {
        p_level_shutdown(&gWorld.level);
    }
    if (gWorld.block != nullptr) {
        heap_caps_free(gWorld.block);
    }
    gWorld = LevelWorld{};
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1


/// One `FRAMEPROF,mem` field, from `sizeof`: the driver's RAM cost is a printed
/// number, never an estimate. `state_static` is the .bss half, `level_ram` is the
/// decoded level (0 in the flash build) and `heap_before/after` bound the rest.
/// The `cache_*` and `scratch` fields are constants: the bytes a run-time texture
/// cache and decode scratch would cost, kept as the reference baking is
/// measured against.
void formatMemoryLine(const char* tag, bool sceneFrame, char* line, std::size_t capacity,
                      std::uint32_t heapBefore, std::uint32_t heapAfterAlloc) {
    const std::size_t drawsegs = sizeof(gState.drawsegs);
    const std::size_t planes = sizeof(gState.planes);
    const std::size_t clipRanges = sizeof(gState.clipRanges);
    const std::size_t clipArrays = sizeof(gState.floorClip) + sizeof(gState.ceilingClip);
    const std::size_t clipPool = sizeof(gState.clipPool);
    const std::size_t spritePools = sizeof(gState.visSprites);
    const std::size_t bakedRecords = sizeof(BakedGraphics);
    const std::size_t total = sizeof(DriverState) + gLevelRamUsed + bakedRecords;
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
    constexpr unsigned kRemovedCacheStatic = 36864u;
    constexpr unsigned kRemovedCacheHeap = 0u;
#else
    constexpr unsigned kRemovedCacheStatic = 53248u;
    constexpr unsigned kRemovedCacheHeap = 24576u;
#endif

    std::snprintf(line, capacity,
                  "%s,mem,surface=engine8bpp,index_writes=1,%s,"
                  "arena=%s,lump_blocks=%u,baked_records=%u,baked_entries=%d,baked_bytes=%u,"
                  "state_static=%u,drawsegs=%u,planes=%u,clip_ranges=%u,clip_arrays=%u,"
                  "clip_pool=%u,sprite_pools=%u,covered=%u,driver_total=%u,drawseg_cap=%d,"
                  "plane_cap=%d,clip_pool_cap=%d,vissprite_cap=%d,heap_before=%u,"
                  "heap_after_alloc=%u,map=%s,map_in_ram=%d,raw_views=%u,level_ram=%u,"
                  "level_released=%u,cache_static=%u,scratch=35080,cache_entries=1920,"
                  "cache_heap_max=%u,session_static=%u,driver_static=%u,playpal=%u,phases=4\n",
                  tag,
                  // The probe measures renderer CPU only; the game frame converts
                  // the same surface to RGB332 and the engine presents it.
                  sceneFrame ? "present=engine,rgb332=1" : "present=no,no_rgb332=1",
                  gPlacement, static_cast<unsigned>(gLumpBlockCount),
                  static_cast<unsigned>(bakedRecords), gBaked.entryCount(),
                  static_cast<unsigned>(gBaked.payloadBytes()),
                  static_cast<unsigned>(sizeof(DriverState)), static_cast<unsigned>(drawsegs),
                  static_cast<unsigned>(planes), static_cast<unsigned>(clipRanges),
                  static_cast<unsigned>(clipArrays), static_cast<unsigned>(clipPool),
                  static_cast<unsigned>(spritePools),
                  static_cast<unsigned>(sizeof(gState.covered)), static_cast<unsigned>(total),
                  kDrawsegCapacity, kPlaneCapacity, kClipPoolCapacity, kVisspriteCapacity,
                  static_cast<unsigned>(heapBefore), static_cast<unsigned>(heapAfterAlloc),
                  kMapPath,
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
                  1,
#else
                  0,
#endif
                  static_cast<unsigned>(gRawViews), static_cast<unsigned>(gLevelRamUsed),
                  static_cast<unsigned>(gLevelReleasedBytes), kRemovedCacheStatic,
                  kRemovedCacheHeap, static_cast<unsigned>(sizeof(LevelSession)),
                  static_cast<unsigned>(sizeof(Driver)),
                  static_cast<unsigned>(kPlaypalStaticBytes));
}

// --- The level session ------------------------------------------------------

/**
 * @brief Opens the container, binds E1M1 and leaves the mapping live.
 *
 * The probe and the shipped scene both open the level through this one setup, so
 * the scene renders through the same code the probe measures. A second copy would
 * be the place a frame hash could start disagreeing with the host.
 *
 * @param session   Caller-owned session record; it points at @p driver.
 * @param driver    The caller-owned Driver to fill. The probe passes a stack
 *                  local, the game frame its `gShowDriver`.
 * @param tag      "FRAMEPROF" or "SHOW": the prefix of the one record it prints.
 * @param loadPlaypal Decode PLAYPAL's palette 0 for the game frame. The probe
 *                 passes false: it renders palette indices and must not spend
 *                 heap the RAM build needs for the decoded map.
 * @param markerOut Receives the level marker ("E1M1").
 * @return true when the session is open. On false nothing is left open and the
 *         reason has already been printed as `<tag>,setup,status=fail,...`.
 */
bool openLevelSession(LevelSession* session, Driver* driver, const char* tag,
                      bool loadPlaypal, char markerOut[5]) {
    *session = LevelSession{};
    session->driver = driver;
    *driver = Driver{};
    gLevelRamUsed = 0u;
    gRawViews = 0u;
    gNotRaw = 0u;
    gLevelReleasedBytes = 0u;
    gSample = FrameProfileSample{};
    gDecodeDepth = 0;
    gPlaypalValid = false;
    gPlacement = "baked-flash";
    // A new open starts with no failure recorded: the scene paints an error
    // screen only when this call (or the palette step that follows it) says so.
    gLevelFailure = LevelFailure{};
    // A session starts on the build's map. gRuntimeLevel names the level the run
    // is on, so it is set before the marker and the game state are built from it
    // -- a second open cannot inherit the previous run's transition.
    gRuntimeLevel = static_cast<std::uint8_t>(kFrameLevel);

    // The reason line writes into the shared session buffer: the setup call
    // graph below already runs near the loop task's stack, and a second 512-byte
    // static is enough to overflow the RAM control environment. The reference
    // keeps `sizeof line` meaningful at every snprintf below.
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;

    session->heapBefore = static_cast<std::uint32_t>(esp_get_free_heap_size());
    const std::int64_t setupStart = nowMicros();

    const int openCode = doom_whd_source_open(&gWhd);
    if (openCode != DOOM_WHD_SOURCE_OK) {
        gLevelFailure = levelFailureFromWhd(openCode);
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=open,code=%d\n", tag,
                      openCode);
        writeLine(line);
        return false;
    }
    // A new container, a new manifest: the adapter validates every raw payload
    // once, builds one record per name and allocates nothing. A container built
    // without the bake is a reported failure here, not a fallback decode.
    const BakedError baked = gBaked.open(&gWhd);
    if (baked != BakedError::Ok) {
        recordLevelFailure(LevelFailureReason::Baked, static_cast<int>(baked),
                           bakedErrorName(baked));
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=baked,code=%s\n", tag,
                      bakedErrorName(baked));
        writeLine(line);
        doom_whd_source_close();
        return false;
    }

    // Palette 0 of PLAYPAL, for the frame's index -> RGB332 table. The probe
    // never needs it, so a missing or short lump is recorded and left for the
    // caller to report: it must not turn a measurement build into a failure.
    //
    // PLAYPAL is 14 palettes (10,752 B), not the 768 the frame converts with, and
    // the reader has no partial-decode entry point, so the lump is decoded into one
    // transient heap block and the first palette is copied out. The block is
    // released before the map is bound, so it costs no steady-state RAM and does
    // not compete with the sprite list's allocation.
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    const int playpalLump = loadPlaypal ? doom_whd_source_lump_index(&gWhd, "PLAYPAL") : -1;
    if (playpalLump >= 0) {
        const std::int32_t playpalLumpBytes = doom_whd_source_lump_size(&gWhd, playpalLump);
        if (playpalLumpBytes >= static_cast<std::int32_t>(kPlaypalBytes)) {
            std::uint8_t* decode = static_cast<std::uint8_t*>(
                heap_caps_malloc(static_cast<std::size_t>(playpalLumpBytes),
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            std::uint32_t decoded = 0u;
            if (decode != nullptr
                && doom_whd_source_read_lump(&gWhd, playpalLump, decode,
                                             static_cast<std::size_t>(playpalLumpBytes),
                                             &decoded)
                       == DOOM_WHD_SOURCE_OK
                && decoded >= kPlaypalBytes) {
                std::memcpy(gPlaypal, decode, kPlaypalBytes);
                gPlaypalValid = true;
            }
            if (decode != nullptr) {
                heap_caps_free(decode);
            }
        }
    }
#else
    (void)loadPlaypal;
#endif

    char mapMarker[5] = {0};
    if (!frameMapMarker(gRuntimeLevel, mapMarker)) {
        // `frameMapMarker()` writes nothing on failure; the level number is the
        // diagnostic, so the detail names the map that could not be spelled.
        char expected[5];
        std::snprintf(expected, sizeof expected, "E1M%u", static_cast<unsigned>(gRuntimeLevel));
        recordLevelFailure(LevelFailureReason::Level, 0, expected);
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=level\n", tag);
        writeLine(line);
        doom_whd_source_close();
        return false;
    }

    const int marker = doom_whd_source_lump_index(&gWhd, mapMarker);
    if (marker < 0) {
        recordLevelFailure(LevelFailureReason::Level, 0, mapMarker);
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=marker,level=%s\n", tag,
                      mapMarker);
        writeLine(line);
        doom_whd_source_close();
        return false;
    }

    doom_map_t map;
    std::int32_t mapFailures = 0;
    bool directoriesLoaded = true;
    const doom_map_error_t mapError =
        readMapLumps(marker, true, &map, &mapFailures, &directoriesLoaded);
    if (mapFailures != 0 || !directoriesLoaded || mapError != DOOM_MAP_OK) {
        const char* failedLump = (gFailLump >= 0 && gFailLump < DOOM_MAP_LUMP_COUNT)
                                     ? kMapLumpNames[gFailLump]
                                     : (!directoriesLoaded ? "COLORMAP" : "map");
        recordLevelFailure(LevelFailureReason::MapLump, static_cast<int>(mapError), failedLump);
        std::snprintf(line, sizeof line,
                      "%s,setup,status=fail,reason=lump,map=%s,map_failures=%d,"
                      "fail_lump=%s,not_raw=%u,directories=%d,map_error=%d,"
                      "colormap=%u,heap=%u,largest=%u,level_ram=%u\n",
                      tag, kMapPath, static_cast<int>(mapFailures),
                      (gFailLump >= 0 && gFailLump < DOOM_MAP_LUMP_COUNT)
                          ? kMapLumpNames[gFailLump] : "none",
                      static_cast<unsigned>(gNotRaw), directoriesLoaded ? 1 : 0,
                      static_cast<int>(mapError),
                      static_cast<unsigned>(gColormapBytes),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                      static_cast<unsigned>(gLevelRamUsed));
        writeLine(line);
        doom_whd_source_close();
        releaseWorkingSet();
        return false;
    }

    gLevelReleasedBytes = 20742u;
#if !defined(PIXELROOT32_DOOM_MAP_IN_RAM)
    // The map's own raw bytes, read from the container by name: in the flash
    // build every one of them is a pointer into the mapping, so this is the RAM
    // the workload did not have to hold. (readMapLumps() owns the source record;
    // the number is the same one it bound.)
    for (int lump = 0; lump < DOOM_MAP_LUMP_COUNT; ++lump) {
        if (lump == DOOM_MAP_THINGS || lump >= kFirstSkippedMapLump) {
            continue;
        }
        const std::int32_t size = doom_whd_source_lump_size(&gWhd, marker + 1 + lump);
        if (size > 0) {
            gLevelReleasedBytes += static_cast<std::uint32_t>(size);
        }
    }
#endif

    if (r_light_init(&gLight, gColormap, gColormapBytes) != R_LIGHT_OK) {
        recordLevelFailure(LevelFailureReason::Colormap, 0);
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=colormap\n", tag);
        writeLine(line);
        doom_whd_source_close();
        releaseWorkingSet();
        return false;
    }

    // The sprite list. It is the caller's thing list, so it is the caller's
    // block. It is taken *after* the map is open and the level's blocks are
    // bound. The probe's static list is exactly one record per THINGS entry
    // (E1M1 needs 129, E1M6 389); the game's live list is bounded by the masked
    // pass's own vis-sprite capacity (256), because a longer submission cannot
    // project to fewer vis sprites.
    const std::uint32_t thingCount = map.lumps[DOOM_MAP_THINGS].count;
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    const std::uint32_t spriteSlots = static_cast<std::uint32_t>(kVisspriteCapacity);
#else
    const std::uint32_t spriteSlots = thingCount;
#endif
    if (thingCount == 0u || thingCount > static_cast<std::uint32_t>(kSpriteCapacity)) {
        recordLevelFailure(LevelFailureReason::Sprites, static_cast<int>(thingCount), "things");
        std::snprintf(line, sizeof line,
                      "%s,setup,status=fail,reason=sprites,things=%u,capacity=%d\n", tag,
                      static_cast<unsigned>(thingCount), kSpriteCapacity);
        writeLine(line);
        doom_whd_source_close();
        releaseWorkingSet();
        return false;
    }
    gSprites = static_cast<r_sprite_t*>(
        heap_caps_malloc(static_cast<std::size_t>(spriteSlots) * sizeof(r_sprite_t),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (gSprites == nullptr) {
        recordLevelFailure(LevelFailureReason::Sprites, 0, "heap");
        std::snprintf(line, sizeof line,
                      "%s,setup,status=fail,reason=sprites,need=%u,heap=%u,"
                      "largest=%u\n",
                      tag, static_cast<unsigned>(spriteSlots * sizeof(r_sprite_t)),
                      static_cast<unsigned>(esp_get_free_heap_size()),
                      static_cast<unsigned>(heap_caps_get_largest_free_block(
                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
        writeLine(line);
        releaseWorkingSet();
        doom_whd_source_close();
        return false;
    }



    driver->map = map;
    driver->storage.drawsegs = gState.drawsegs;
    driver->storage.drawsegCapacity = kDrawsegCapacity;
    driver->storage.planes.planes = gState.planes;
    driver->storage.planes.capacity = kPlaneCapacity;
    driver->storage.planes.count = 0;
    driver->storage.clipRanges = gState.clipRanges;
    driver->storage.clipRangeCapacity = kClipRangeCapacity;
    driver->storage.floorClip = gState.floorClip;
    driver->storage.ceilingClip = gState.ceilingClip;
    // All four phases run, so the clip and sprite pools are part of the frame: the wall
    // pass snapshots each drawseg's clip spans and records its deferred masked
    // mid columns in the same unified pool (upstream's openings[]), and the
    // masked pass projects into visSprites.
    driver->storage.clipPool = gState.clipPool;
    driver->storage.clipPoolCapacity = kClipPoolCapacity;
    driver->storage.visSprites = gState.visSprites;
    driver->storage.visSpriteCapacity = kVisspriteCapacity;
    // The baked patches cannot change under an unchanged record address,
    // so their proof belongs to the residency and not to the frame.
    gSpriteProofs.count = 0;
    driver->storage.spriteProofs = &gSpriteProofs;

    const r_bsp_error_t bspCode = r_bsp_init(&driver->view, &driver->map, &driver->storage);
    if (bspCode != R_BSP_OK) {
        recordLevelFailure(LevelFailureReason::Storage, static_cast<int>(bspCode));
        std::snprintf(line, sizeof line, "%s,setup,status=fail,reason=storage\n", tag);
        writeLine(line);
        doom_whd_source_close();
        releaseWorkingSet();
        return false;
    }

    driver->light = gLight;
    driver->textures.resolve = resolveWall;
    driver->textures.context = nullptr;
    driver->textures.resolveFlat = resolveFlat;
    driver->textures.flatContext = nullptr;
    driver->textures.skyTexture = r_pack_name(kSkyTextureName);
    driver->textures.beginFragment = nullptr;
    driver->sprites.resolve = resolveSprite;
    driver->sprites.context = nullptr;
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    // The four passes fold their cycle buckets into this state. Assigned
    // after r_bsp_init() because that call zeroes the state.
    driver->view.probe = &driver->probe;
#endif

    for (int index = 0; index < 5; ++index) {
        session->marker[index] = mapMarker[index];
        markerOut[index] = mapMarker[index];
    }
    session->open = true;

    // The bake and the map path are the setup's two decisions, so they are
    // printed with the setup that made them.
    std::snprintf(line, sizeof line,
                  "%s,setup,status=ok,map=%s,map_in_ram=%d,level=%s,raw_views=%u,"
                  "level_ram=%u,"
                  "baked_entries=%d,baked_walls=%d,baked_flats=%d,baked_sprites=%d,"
                  "baked_bytes=%u,colormap=%u,map_lumps=%d,map_skipped=%d,"
                  "setup_us=%u\n",
                  tag, kMapPath,
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
                  1,
#else
                  0,
#endif
                  mapMarker, static_cast<unsigned>(gRawViews),
                  static_cast<unsigned>(gLevelRamUsed), gBaked.entryCount(),
                  gBaked.count(BakedKind::kBakedKindWall),
                  gBaked.count(BakedKind::kBakedKindFlat),
                  gBaked.count(BakedKind::kBakedKindSprite),
                  static_cast<unsigned>(gBaked.payloadBytes()),
                  static_cast<unsigned>(gColormapBytes), DOOM_MAP_LUMP_COUNT,
                  kMapLumpCount - kFirstSkippedMapLump,
                  static_cast<unsigned>(elapsedMicros(setupStart)));
    writeLine(line);

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // The game frame owns a world as well; the probe does not, because
    // it measures a renderer and the RAM control environment has no room for a
    // p_level_t. `loadPlaypal` is the flag that says "this is the game": the
    // probe passes false for it and for the palette for the same reason.
    //
    // A world that does not fit closes the session instead of leaving a frame
    // without a simulation: the scene's fallback is its shell, and the failure
    // arithmetic is already on the serial line.
    if (loadPlaypal && !openWorldSession()) {
        closeLevelSession();
        return false;
    }
#endif
    return true;
}

/// Releases everything the session holds. Safe on a closed session.
void closeLevelSession() {
    if (!gSession.open) {
        return;
    }
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    // Before the mapping: the world's mobjs and thinkers live in its arena, and
    // p_level_shutdown() walks the level's zone to release them.
    closeWorldSession();
#endif
#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)
    // The shadow bitmap is the session's heap block, so it goes with it.
    closeAutomap();
#endif
    doom_whd_source_close();
    releaseWorkingSet();
    gSession.open = false;
    gSession.spawnValid = false;
}

/**
 * @brief Renders one explicit viewpoint into an index surface.
 *
 * The one render implementation: `renderSessionSpawn()` calls it with the
 * harness's cached spawn viewpoint, and the game calls it with the player's.
 * Everything after the viewpoint -- the surface bind, the four passes, the
 * measurement and the hash -- is therefore shared, which is what keeps the
 * frame-00 hash identical across the two callers.
 */
bool renderSessionView(std::uint8_t* surface, int width, int height, std::int32_t x,
                       std::int32_t y, std::int32_t z, std::uint32_t angle,
                       FrameProfileSample* sample, char hash[65], bool wantHash,
                       std::uint32_t* hashMicros) {
    if (hashMicros != nullptr) {
        *hashMicros = 0u;
    }
    if (!gSession.open || surface == nullptr || width < R_SCREEN_WIDTH
        || height < R_VIEW_HEIGHT) {
        return false;
    }

    FrameView view;
    view.x = x;
    view.y = y;
    view.z = z;
    view.angle = angle;

    bindSurface(gSession.driver, surface, width, height);
    FrameProfileSample local;
    FrameProfileSample* out = sample != nullptr ? sample : &local;
    std::uint32_t mapMicros = 0u;
    bool mapFrame = false;
#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)
    // The automap replaces the world frame. Upstream's D_Display draws the
    // automap *instead of* the view (R_RenderPlayerView is skipped while
    // automapactive) and the status bar and the menu on top of it; that is the
    // order below. No pass runs, so nothing is marked as seen while the map is
    // up, and the weapon (a psprite, drawn between the view and the bar) is not
    // drawn either -- upstream's R_DrawPlayerSprites is part of the view it
    // skipped.
    if (am_active(&gMap)) {
        const std::int64_t mapStart = nowMicros();
        const int drawn = am_draw(&gMap, surface, width);
        mapMicros = elapsedMicros(mapStart);
        gMapMicros = mapMicros;
        gMapLinesDrawn = drawn;
        *out = FrameProfileSample{};
        out->frameMicros = mapMicros;
        hudDraw(surface, width);
#if defined(DOOM_MENU)
        menuDraw(surface, width);
#endif
        mapFrame = true;
    }
#endif
    if (!mapFrame) {
        if (!renderOnce(gSession.driver, view, surface, width, height, out)) {
            return false;
        }
#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)
        // The bar is part of the frame and of its hash: the HUD frame-00 hash is a
        // different number from the 200-row (no HUD) frame-00 hash, by construction.
#if defined(DOOM_WEAPON_DIAG)
        if (wantHash && !gWeaponPartsDone && weaponAtRest()) {
            // The rectangle around the resting pistol before it is drawn, for the
            // one-shot below: it separates a world mismatch from a weapon mismatch when
            // the host rebuilds the numbers.
            hashRect(surface, width, kWeaponRectX, kWeaponRectY, kWeaponRectW, kWeaponRectH,
                     gPreWeaponHash);
        }
#endif  // DOOM_WEAPON_DIAG
        weaponDraw(gSession.driver);
        hudDraw(surface, width);
#if defined(DOOM_MENU)
        menuDraw(surface, width);
#endif
#endif
    }

    if (hash != nullptr) {
        if (wantHash) {
            const std::int64_t hashStart = nowMicros();
            hashSurface(surface,
                        static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                        hash);
            if (hashMicros != nullptr) {
                *hashMicros = elapsedMicros(hashStart);
            }
#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && defined(DOOM_HUD)
            // One line per reported frame: what the bar and the weapon cost and whether
            // every patch drew. gSessionLine is the session's shared buffer; no setup runs
            // now. The line deliberately carries no hash (the `frame` line that follows
            // has the same one): this line is written inside the frame, so it is charged
            // to the frame period, and past about 135 characters the UART's transmit
            // buffer blocks (62 extra characters measured ~12 ms on the board, 134 vs
            // 157 fps_x10). Keep it short.
            std::snprintf(gSessionLine, sizeof gSessionLine,
                          "SHOW,hud,hud_us=%u,missing=%d,faces=%d,view_h=%d,face=%d,"
                          "weapon_us=%u,weapon_missing=%d,weapon_state=%d,weapon_sy=%d\n",
                          static_cast<unsigned>(gHudMicros), gHudMissing, kHudFaces,
                          R_VIEW_HEIGHT, static_cast<int>(gStatus.faceIndex),
                          static_cast<unsigned>(gWeaponMicros), gWeaponMissing,
                          static_cast<int>(gWorld.level.playerState.psprites[0].state),
                          static_cast<int>(gWorld.level.playerState.psprites[0].sy >> FRACBITS));
            writeLine(gSessionLine);
            if (mapFrame) {
                // The map's own line, once per reported frame and only while
                // the map is up, so no map-off log grows a field. The hash is the
                // 168-window bytes alone (`hashWindow`), which is exactly what
                // tools/am_check.py prints for the same state, so the board and the
                // host compare one number. `scale` is `scale_mtof` raw: the zoom in
                // the driver's own 16.16 unit, not a rounded ratio.
                char mapHash[65];
                hashWindow(surface, static_cast<std::size_t>(width) * R_VIEW_HEIGHT, mapHash);
                std::snprintf(gSessionLine, sizeof gSessionLine,
                              "SHOW,map,us=%u,active=%d,follow=%d,scale=%ld,grid=%d,hash=%s\n",
                              static_cast<unsigned>(mapMicros), am_active(&gMap),
                              am_follow(&gMap), static_cast<long>(am_scale_mtof(&gMap)),
                              am_grid(&gMap), mapHash);
                writeLine(gSessionLine);
            }
#if defined(DOOM_MAP_SCRIPT)
            if (mapFrame) {
                // Diagnostic build only: the automap's own numbers, so a board
                // picture that disagrees with tools/am_check.py can be localised to
                // the window, the mapped set or the drawing. The second line dumps
                // the shadow bitmap itself, LSB first, capped at 64 bytes -- E1M1's
                // 60 all fit, E1M2's 130 would truncate.
                std::uint32_t marked = 0u;
                for (std::uint32_t line = 0u; line < gMap.lineCount; ++line) {
                    if (am_mapped(&gMap, line) != 0) {
                        ++marked;
                    }
                }
                std::snprintf(gSessionLine, sizeof gSessionLine,
                              "SHOW,mapdbg,mx=%ld,my=%ld,mw=%ld,mh=%ld,drawn=%d,mapped=%u\n",
                              static_cast<long>(gMap.mX), static_cast<long>(gMap.mY),
                              static_cast<long>(gMap.mW), static_cast<long>(gMap.mH),
                              static_cast<int>(gMapLinesDrawn), static_cast<unsigned>(marked));
                writeLine(gSessionLine);
                {
                    char bits[2u * 64u + 1u];
                    std::size_t at = 0u;
                    for (std::uint32_t byte = 0u; byte < gMap.mappedBytes && at + 2u < sizeof bits;
                         ++byte) {
                        static const char kHex[] = "0123456789abcdef";
                        bits[at++] = kHex[(gMapped[byte] >> 4) & 0x0Fu];
                        bits[at++] = kHex[gMapped[byte] & 0x0Fu];
                    }
                    bits[at] = '\0';
                    std::snprintf(gSessionLine, sizeof gSessionLine, "SHOW,mapbits,%s\n", bits);
                    writeLine(gSessionLine);
                }
            }
#endif
#if defined(DOOM_WEAPON_DIAG)
            if (!gWeaponPartsDone && weaponAtRest()) {
                // Once, when the pistol rests: the view alone (the bar's face is
                // random) so a host rebuild of oracle frame 00 plus the oracle
                // weapon can be compared with the number the board computed.
                gWeaponPartsDone = true;
                char viewHash[65];
                char rectHash[65];
                hashSurface(surface, static_cast<std::size_t>(width) * R_VIEW_HEIGHT, viewHash);
                hashRect(surface, width, kWeaponRectX, kWeaponRectY, kWeaponRectW,
                         kWeaponRectH, rectHash);
                std::snprintf(gSessionLine, sizeof gSessionLine,
                              "SHOW,weaponparts,tics=%u,rect_pre=%s,rect=%s,view=%s\n",
                              static_cast<unsigned>(gWorld.level.leveltime), gPreWeaponHash,
                              rectHash, viewHash);
                writeLine(gSessionLine);
            }
#endif  // DOOM_WEAPON_DIAG
            if (!gHudPartsDone) {
                // The first reported frame also prints the view and the bar apart, so
                // a host rebuild of the hash can be localised to one half.
                gHudPartsDone = true;
                char viewHash[65];
                char barHash[65];
                hashSurface(surface, static_cast<std::size_t>(width) * R_VIEW_HEIGHT, viewHash);
                hashSurface(surface + static_cast<std::size_t>(width) * R_VIEW_HEIGHT,
                            static_cast<std::size_t>(width) * (height - R_VIEW_HEIGHT), barHash);
                std::snprintf(gSessionLine, sizeof gSessionLine,
                              "SHOW,hudparts,view=%s,bar=%s\n", viewHash, barHash);
                writeLine(gSessionLine);
            }
#endif
        } else {
            hash[0] = '\0';
        }
    }
    return true;
}

/// Computes and caches `buildFrames()`'s `frames[0]` for this session.
bool ensureSpawnView() {
    if (!gSession.open) {
        return false;
    }
    if (gSession.spawnValid) {
        return true;
    }
    FrameView frames[kViewCount];
    if (!buildFrames(&gSession.driver->map, frames)) {
        return false;
    }
    gSession.spawn = frames[0];
    gSession.spawnValid = true;
    return true;
}

/**
 * @brief Renders the host harness's frame 0 into an index surface.
 *
 * The viewpoint is `frames[0]` from the same `buildFrames()` rule the probe and
 * `tools/rsegs_check.py` use, computed once and cached: the first player-start
 * THING's position, its sector's floor plus R_VIEWHEIGHT, and angle 0. It is
 * *not* `r_bsp_spawn_view()`, which would use the THING record's own facing and
 * therefore hash differently from the host's frame 00.
 *
 * This delegates to `renderSessionView()` with that cached viewpoint, so the
 * frame-00 hash is produced by the same code the game frame
 * runs rather than by a copy kept in step by hand.
 */
bool renderSessionSpawn(std::uint8_t* surface, int width, int height,
                        FrameProfileSample* sample, char hash[65], bool wantHash,
                        std::uint32_t* hashMicros) {
    if (hashMicros != nullptr) {
        *hashMicros = 0u;
    }
    if (!ensureSpawnView()) {
        return false;
    }
    const FrameView& spawn = gSession.spawn;
    return renderSessionView(surface, width, height, spawn.x, spawn.y, spawn.z, spawn.angle,
                             sample, hash, wantHash, hashMicros);
}

}  // namespace

void frameLevelLastRefusal(const char** stage, int* code) {
    if (stage != nullptr) {
        *stage = (gSession.open && gSession.driver != nullptr) ? gSession.driver->stage
                                                               : "none";
    }
    if (code != nullptr) {
        *code = gStageCode;
    }
}

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

bool frameLevelOpen(const char* tag, char marker[5]) {
    if (tag == nullptr || marker == nullptr) {
        return false;
    }
    return openLevelSession(&gSession, &gShowDriver, tag, true, marker);
}

bool frameLevelRenderSpawn(std::uint8_t* surface, int width, int height,
                           FrameProfileSample* sample, char hash[65], bool wantHash,
                           std::uint32_t* hashMicros) {
    return renderSessionSpawn(surface, width, height, sample, hash, wantHash, hashMicros);
}

void frameLevelClose() { closeLevelSession(); }

const LevelFailure& frameLevelFailure() { return gLevelFailure; }

bool frameLevelRenderView(std::uint8_t* surface, int width, int height, std::int32_t x,
                          std::int32_t y, std::int32_t z, std::uint32_t angle,
                          FrameProfileSample* sample, char hash[65], bool wantHash,
                          std::uint32_t* hashMicros) {
    return renderSessionView(surface, width, height, x, y, z, angle, sample, hash, wantHash,
                             hashMicros);
}

#if defined(DOOM_HUD)
static_assert(kMapZoomRelease == AM_ZOOM_RELEASE && kMapZoomIn == AM_ZOOM_IN
                  && kMapZoomOut == AM_ZOOM_OUT,
              "the map's zoom directions are am_map.h's");
static_assert(kMapZoomIn == 1 && kMapZoomOut == -1,
              "the map's zoom directions are the two sign values the driver documents");

bool frameLevelMapActive() { return am_active(&gMap) != 0; }

bool frameLevelMapStart() {
    if (!gWorld.open || gWorld.level.playerState.mo == nullptr) {
        return false;
    }
    const r_mobj_t* avatar = gWorld.level.playerState.mo;
    if (!am_start(&gMap, avatar->x, avatar->y)) {
        return false;
    }
    // The arrow's facing, from the first frame. am_ticker() keeps it current once
    // the tics run, but a map opened between two tics (or before the first one)
    // would otherwise draw the arrow at angle 0 -- east -- whatever way the
    // player is facing (upstream reads `plr->mo->angle` at draw time, so it never
    // has this gap).
    gMap.playerAngle = avatar->angle;
    return true;
}

void frameLevelMapStop() { am_stop(&gMap); }

bool frameLevelMapPan(std::uint8_t bits) { return am_pan(&gMap, bits) != 0; }

bool frameLevelMapZoom(int direction) { return am_zoom(&gMap, direction) != 0; }

void frameLevelMapForget() {
    if (gMapped != nullptr) {
        std::memset(gMapped, 0, static_cast<std::size_t>(gMappedBytes));
    }
}
#endif  // DOOM_HUD

bool frameLevelSpawnView(LevelView* out) {
    if (out == nullptr || !ensureSpawnView()) {
        return false;
    }
    out->x = gSession.spawn.x;
    out->y = gSession.spawn.y;
    out->z = gSession.spawn.z;
    out->angle = gSession.spawn.angle;
    return true;
}

/*
 * --- Opening a level at runtime and changing to another one ----------------
 *
 * A completed level names the next one and the world has to load it, so the
 * driver needs two things the probe never does -- a way to build any
 * level's map view into the session, and a per-load presentation reset that
 * knows whether the map changed.
 *
 * The composition itself (read the request, name the next level, load it with the
 * player carried) is `doomc/LevelTransition.*`, which is engine-free and
 * host-tested; this block is the board half: the container's map view, the
 * renderer's live reader, the HUD, the automap, the sound layer and the serial
 * line.
 */

/**
 * @brief Builds @p levelNumber's map view into @p outMap from the open container.
 *
 * The container is mapped for the whole session, so a level's lumps are a second
 * set of pointers into the same mapping -- no allocation, nothing to release, and
 * the previous level's view simply stops being referenced.
 *
 * A refusal is printed here (`SHOW,level,error=...`) and leaves @p outMap zeroed:
 * the caller decides whether the world keeps the level it was on.
 *
 * @param levelNumber 1..9.
 * @param outMap      Receives the opened view.
 * @return true when the marker, all ten lumps and doom_map_open() accepted it.
 */
bool openSessionMap(std::uint8_t levelNumber, doom_map_t* outMap) {
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;
    char marker[5] = {0};

    if (!gSession.open || !frameMapMarker(levelNumber, marker)) {
        std::snprintf(line, sizeof line, "SHOW,level,error=number,level=%u\n",
                      static_cast<unsigned>(levelNumber));
        writeLine(line);
        return false;
    }
    const int lump = doom_whd_source_lump_index(&gWhd, marker);
    if (lump < 0) {
        std::snprintf(line, sizeof line, "SHOW,level,error=marker,level=%s\n", marker);
        writeLine(line);
        return false;
    }

    std::int32_t failures = 0;
    bool directoriesLoaded = true;
    const doom_map_error_t error = readMapLumps(lump, false, outMap, &failures, &directoriesLoaded);
    if (failures != 0 || error != DOOM_MAP_OK) {
        std::snprintf(line, sizeof line,
                      "SHOW,level,error=lump,level=%s,map_failures=%d,fail_lump=%s,"
                      "map_error=%d\n",
                      marker, static_cast<int>(failures),
                      (gFailLump >= 0 && gFailLump < DOOM_MAP_LUMP_COUNT) ? kMapLumpNames[gFailLump]
                                                                         : "none",
                      static_cast<int>(error));
        writeLine(line);
        std::memset(outMap, 0, sizeof *outMap);
        return false;
    }
    return true;
}

/**
 * @brief G_DoLoadLevel's presentation reset, for a load that just happened.
 *
 * `hudStart()` and the sound layer's restart are the same for every load. The
 * automap is the one that depends on the answer: a reload of the *same* map only
 * has to forget what was seen (`resetAutomap`), while a *different* map has a
 * different linedef count and vertex box, and `am_level_init()` is the call that
 * reads them into the state (am_map.h: the state keeps the map pointer, the line
 * count and both zoom limits). Stopping alone would draw the level that just
 * ended under the new player.
 *
 * @param differentMap Whether the load bound a map the automap has not seen.
 */
void restartLevelPresentation(bool differentMap) {
    // Every caller has just loaded a level and is about to draw it, so the eye
    // is set here instead of waiting for a tic that a menu may never allow.
    settleLevelView();
#if defined(DOOM_HUD)
    hudStart();
    if (differentMap) {
        closeAutomap();
        (void)openAutomap();
    } else {
        resetAutomap();
    }
#else
    (void)differentMap;
#endif
#if defined(DOOM_AUDIO)
    // G_DoLoadLevel's S_Start: the music choice and S_AdjustSoundParams' map-8
    // exception both read gamemap, so the level is handed over before the music
    // is asked for; the listener is the new avatar.
    S_SetGameLevel(static_cast<int>(gLevelState.episode), static_cast<int>(gLevelState.map));
    S_SetListener(gWorld.level.playerState.mo);
    S_Start();
#endif
}

/**
 * @brief Lays @p map's world out over the session's arena, binds it and presents it.
 *
 * This is the recovery path: a level change whose load refused leaves the level
 * zeroed (p_setup.h), so the map that load left behind is laid out again through
 * `p_level_init()` + `p_spawn_specials()` -- the same pair every load uses, never
 * a second loader. `probeWorld()` is that pair with the zone snapshot, and the
 * renderer's live reader is unbound first for the reason `reloadWorldAtLevel`
 * documents (p_level_init copies SECTORS through it).
 *
 * @param map   The map to lay out; its views must be live.
 * @param skill 0..4, the map's own rules' skill.
 * @return true when the world is open again. On false the world is closed, so the
 *         frame keeps its last picture instead of drawing a zeroed level.
 */
bool layOutWorld(const doom_map_t* map, int skill) {
    zone_stats_t unused;
    gFrameWorld = nullptr;
    p_level_unbind_render(&gSession.driver->map);
    if (probeWorld(map, gWorld.arena, gWorld.arenaBytes, &gWorld.level, &unused,
                   static_cast<p_skill_t>(skill)) != P_SETUP_OK) {
        gWorld.open = false;
        gFrameWorld = nullptr;
        return false;
    }
    gFrameWorld = &gWorld.level;
    p_level_bind_render(&gWorld.level, &gSession.driver->map);
    restartLevelPresentation(true);
    return true;
}

/**
 * @brief Carries out the exit the level requested, once.
 *
 * Upstream's order, without the intermission this port never built:
 *
 *   1. `levelExitNext()` reads `exitRequest` and names the next level (or the
 *      episode's victory).
 *   2. A level: open its map view, unbind the renderer's live reader, load with
 *      the player carried (`levelChangeLoad(..., carry=true)`) and bind the new
 *      map's seams.
 *   3. The victory (E1M8's sector special 11 has no exit line): load the run's
 *      start with `carry=false` -- G_InitNew is a reborn -- and raise the one-shot
 *      the scene turns into the main menu.
 *
 * The request is consumed before anything is attempted, so a level whose exit
 * cannot be answered does not ask again every frame.
 *
 * A refusal always leaves a world: a next map that cannot be opened leaves the
 * current level untouched, and a load that refused is re-laid from the map the
 * world was on, or -- if even that fails -- the world is closed.
 *
 * @return true when a level (or the run's reset) is loaded and ticking.
 */
bool transitionLevel() {
    p_level_t& level = gWorld.level;
    doom_map_t* map = &gSession.driver->map;
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;

    const std::uint8_t fromLevel = gRuntimeLevel;
    const int skill = static_cast<int>(level.rules.skill);
    const std::uint32_t ticsBefore = static_cast<std::uint32_t>(level.leveltime);
    char fromMarker[5] = {0};
    (void)frameMapMarker(fromLevel, fromMarker);

    g_level_next_t next;
    const int kind = levelExitNext(level, gLevelState, next);
    level.exitRequest = static_cast<std::uint8_t>(P_EXIT_NONE);

    if (kind < 0) {
        // Outside upstream's table: there is no level to name, and the world is
        // still exactly the one the tic left. Reported, consumed, not retried.
        std::snprintf(line, sizeof line, "SHOW,level,error=no_next,from=%s,skill=%d,tics=%u\n",
                      fromMarker, skill, static_cast<unsigned>(ticsBefore));
        writeLine(line);
        return true;
    }

    // The victory loads the run's start through the same call, with a record that
    // has no victory flag: g_do_world_done() refuses a victory record because
    // there is no map in it.
    const bool victory = kind == 0;
    const std::uint8_t target = victory ? static_cast<std::uint8_t>(kFrameLevel) : next.map;
    g_level_next_t load = next;
    if (victory) {
        load.victory = 0u;
        load.episode = 1u;
        load.map = target;
        load.secret = 0u;
    }

    char toMarker[5] = {0};
    (void)frameMapMarker(target, toMarker);

    doom_map_t nextView = {};
    if (!openSessionMap(target, &nextView)) {
        // Nothing was unbound and nothing was loaded: the world keeps the level
        // it was on, which is the honest answer to "the next map is not in the
        // container".
        return true;
    }

    const doom_map_t previous = *map;
    *map = nextView;
    // p_level_change() copies SECTORS through the renderer's live reader, and the
    // level it would answer from is the one being rebuilt -- see the long comment
    // in reloadWorldAtLevel for the board failure that made this mandatory.
    p_level_unbind_render(map);
    const g_level_error_t changed = levelChangeLoad(level, gLevelState, load, *map, !victory);

    if (changed != G_LEVEL_OK) {
        // p_setup.h: a refused level change leaves the level zeroed, so the map
        // the world was on is laid out again through the one loader every load
        // uses. A world that cannot be re-laid is closed, never drawn zeroed.
        *map = previous;
        const bool recovered = layOutWorld(map, skill);
        std::snprintf(line, sizeof line,
                      "SHOW,level,error=load,from=%s,to=%s,code=%d,recovered=%d\n", fromMarker,
                      toMarker, static_cast<int>(changed), recovered ? 1 : 0);
        writeLine(line);
        return recovered;
    }

    p_level_bind_render(&level, map);
    gRuntimeLevel = target;
    gVictoryPending = victory;

    // The line is printed before the presentation reset: openAutomap() prints its
    // own `SHOW,map,init` for the new map, and the level change reading first is
    // what makes a captured log readable top to bottom.
    if (victory) {
        // E1M8's ending: this port has no finale screen, so the run ends here
        // and the scene opens the menu over the level the run started on.
        std::snprintf(line, sizeof line, "SHOW,level,victory=1,episode=%u,map=%u\n",
                      static_cast<unsigned>(next.episode), static_cast<unsigned>(next.map));
        writeLine(line);
        restartLevelPresentation(true);
        return true;
    }

    std::snprintf(line, sizeof line,
                  "SHOW,level,from=%s,to=%s,skill=%d,tics=%u,health=%ld,ammo=%ld\n", fromMarker,
                  toMarker, skill, static_cast<unsigned>(ticsBefore),
                  static_cast<long>(level.playerState.health),
                  static_cast<long>(level.playerState.ammo[P_AM_CLIP]));
    writeLine(line);
    restartLevelPresentation(true);
    return true;
}

std::uint8_t frameLevelNumber() { return gRuntimeLevel; }

const char* frameLevelMarker() {
    // One buffer: the caller reads it and copies what it needs. `gRuntimeLevel`
    // is the only source, so the marker cannot disagree with the number.
    static char marker[5] = {0};
    if (!frameMapMarker(gRuntimeLevel, marker)) {
        marker[0] = '\0';
    }
    return marker;
}

bool frameLevelTakeVictory() {
    const bool pending = gVictoryPending;
    gVictoryPending = false;
    return pending;
}

#if defined(DOOM_LEVEL_TEST)
/*
 * The level-exit board proof (env esp32dev_ILI9341_hud_leveltest): the panel's buttons
 * cannot be pressed from the build host, so this finds E1M1's own exit switch
 * (line special 11) and stands the avatar in front of it, on the front sector's
 * side facing the line. The scene then holds USE through the real button byte, so
 * the press travels P_PlayerThink -> p_use_lines -> P_UseSpecialLine exactly as a
 * B press does; nothing here calls the line trigger.
 *
 * The search is tools/level_check.c's fixture (and test/unit/test_level's), in
 * DOOM's own fixed point: a third of the line, 16/24/32 units out along each of
 * the two normals, and the first candidate that P_CheckPosition accepts inside
 * the line's front sector. A use press from the back side is refused by
 * P_UseSpecialLine for every number but 124, so the side is not an optimisation.
 *
 * @return true when the avatar was moved and the fixture printed.
 */
bool frameLevelExitScript(int special) {
    char (&out)[kFrameProfileLineCapacity] = gSessionLine;
    if (!gWorld.open || gWorld.level.player == nullptr) {
        std::snprintf(out, sizeof out, "SHOW,level,script=stand,status=fail,reason=world\n");
        writeLine(out);
        return false;
    }

    p_level_t& level = gWorld.level;
    const doom_map_t& map = *level.map;
    r_mobj_t* mobj = level.player;
    static const fixed_t kGaps[3] = {32 * FRACUNIT, 24 * FRACUNIT, 16 * FRACUNIT};
    static const int kThirds[5] = {6, 4, 8, 3, 9};

    for (std::uint32_t index = 0u; index < level.lineCount; ++index) {
        if (level.lineSpecial[index] != (uint8_t)special) {
            continue;
        }
        doom_line_t line;
        uint16_t front = 0;
        doom_map_get_line(&map, static_cast<uint16_t>(index), &line);
        if (!p_line_front_sector(&map, &line, &front)) {
            continue;
        }
        doom_vertex_t v1;
        doom_vertex_t v2;
        doom_map_get_vertex(&map, line.v1, &v1);
        doom_map_get_vertex(&map, line.v2, &v2);
        const fixed_t dx = v2.x - v1.x;
        const fixed_t dy = v2.y - v1.y;
        if (dx == 0 && dy == 0) {
            continue;
        }
        const std::uint32_t lineAngle = p_point_to_angle(dx, dy);

        for (int third = 0; third < 5; ++third) {
            const fixed_t mx =
                v1.x + static_cast<fixed_t>((static_cast<std::int64_t>(dx) * kThirds[third]) / 12);
            const fixed_t my =
                v1.y + static_cast<fixed_t>((static_cast<std::int64_t>(dy) * kThirds[third]) / 12);
            for (int gapIndex = 0; gapIndex < 3; ++gapIndex) {
                for (int sign = -1; sign <= 1; sign += 2) {
                    const std::uint32_t normal =
                        sign > 0 ? (lineAngle + ANG90) : (lineAngle + ANG270);
                    const fixed_t px = mx + fixed_mul(kGaps[gapIndex], r_fine_cosine(normal));
                    const fixed_t py =
                        my + fixed_mul(kGaps[gapIndex], r_fine_sine[normal >> ANGLETOFINESHIFT]);
                    p_position_result_t position;
                    if (!p_check_position(&level.context, mobj, px, py, &position)
                        || position.sector != front) {
                        continue;
                    }
                    mobj->x = px;
                    mobj->y = py;
                    mobj->z = position.floorz;
                    mobj->momx = 0;
                    mobj->momy = 0;
                    mobj->floorz = position.floorz;
                    mobj->ceilingz = position.ceilingz;
                    mobj->subsector = position.subsector;
                    mobj->sector = position.sector;
                    mobj->angle = p_point_to_angle(mx - px, my - py);
                    std::snprintf(out, sizeof out,
                                  "SHOW,level,script=stand,status=ok,level=%s,line=%u,"
                                  "special=%d,standoff=%ld,x=%ld,y=%ld,angle=%lu\n",
                                  frameLevelMarker(), static_cast<unsigned>(index), special,
                                  static_cast<long>(kGaps[gapIndex]), static_cast<long>(px),
                                  static_cast<long>(py),
                                  static_cast<unsigned long>(mobj->angle));
                    writeLine(out);
                    return true;
                }
            }
        }
    }

    std::snprintf(out, sizeof out, "SHOW,level,script=stand,status=fail,reason=line,level=%s\n",
                  frameLevelMarker());
    writeLine(out);
    return false;
}
#endif  // DOOM_LEVEL_TEST

bool frameLevelRunTics(std::uint32_t tics, std::uint8_t buttons) {
    if (!gWorld.open) {
        return false;
    }

    // One command for the whole call: a held button is a level, and P_PlayerThink
    // latches BT_USE itself, so a two-tic frame is two tics of the same input
    // rather than two reads of a device that updates at frame rate.
    const p_ticcmd_t cmd = mapButtons(buttons);
    for (std::uint32_t tic = 0; tic < tics; ++tic) {
#if defined(DOOM_AUDIO)
        // The listener is the console player's avatar; upstream's G_Ticker
        // hands `players[0].mo` to S_UpdateSounds once per tic, after P_Ticker.
        S_SetListener(gWorld.level.playerState.mo);
#endif
        // No walk statistics: the scene reports the world's state, and the
        // combat code owns the counters that would read them.
        if (p_level_tic(&gWorld.level, &cmd, nullptr) != P_TICK_OK) {
            return false;
        }
        // G_Ticker runs G_DoCompleted/G_DoWorldDone immediately after
        // P_Ticker and before ST_Ticker, AM_Ticker and S_UpdateSounds, so this
        // frame's HUD and sound work belong to the level the transition loaded --
        // not to the one that just ended.
        if (gWorld.level.exitRequest != static_cast<std::uint8_t>(P_EXIT_NONE)) {
            if (!transitionLevel()) {
                return false;
            }
            // The command was built for the level that ended. Upstream's action
            // loop builds the next tic's command from the next input sample; a
            // second tic of the same held button in the new level is not that, so
            // the call stops here and the frame's next update asks again.
            break;
        }
#if defined(DOOM_AUDIO)
        // One tick per simulated tic: cue lifetimes, distance re-attenuation and
        // the channel table all advance at DOOM's 35 Hz, not the panel's rate.
        S_UpdateSounds(gWorld.level.playerState.mo);
#endif
#if defined(DOOM_HUD)
        hudTick();
#endif
#if defined(DOOM_HUD)
        // Upstream's G_Ticker order -- P_Ticker, then ST_Ticker, then the
        // automap's AM_Ticker -- one call per 35 Hz tic, so the pan and the zoom
        // are the reference's per-second rates and not the panel's frame rate.
        if (am_active(&gMap) && gWorld.level.playerState.mo != nullptr) {
            const r_mobj_t* avatar = gWorld.level.playerState.mo;
            am_ticker(&gMap, avatar->x, avatar->y, avatar->angle);
        }
#endif
    }
    return true;
}

#if defined(DOOM_MENU) || DOOM_PERSISTENCE
/*
 * G_InitNew()'s reload, shared by the menu's New Game, by a loaded save and by
 * the run's reset after a victory.
 *
 * The skill goes into the rules, the random position is cleared (M_ClearRandom),
 * then G_DoLoadLevel's reload of @p levelNumber over the same arena -- the path
 * the demo gate already proves (`g_demo_reborn()`). Every THING is run again from
 * the map's own records under the new skill, the player is G_PlayerReborn's,
 * leveltime is 0.
 *
 * @p levelNumber is the level the world should be on, not "the one map the
 * session opened". A New Game and the post-victory reset ask for `kFrameLevel`; a
 * loaded record asks for the map its header names. When it differs from the map
 * already bound, the session's map view is rebuilt from the container first, so
 * `g_demo_reborn()` reloads the map the caller asked for rather than the one the
 * previous level left in the struct. Nothing is allocated: the container is
 * mapped for the whole session and a map view is a set of pointers into it.
 *
 * The renderer's live reader is UNBOUND for the reload and bound again after.
 * The reload copies the map's sectors with doom_map_get_sector(), which answers
 * through a bound reader (p_level_read_sector) -- and the level it would read is
 * the one being rebuilt, so with the reader bound the copy is stack garbage
 * (every frame after a New Game is refused with FLAT_MISSING, 85 of 85 sectors
 * wrong). tools/menu_check.py --arena reproduces that with --perturb.
 *
 * A refused reload leaves the level zeroed (p_setup.h), so the fallback lays it
 * out again through `layOutWorld()`, or closes the world so the frame keeps its
 * last picture. tools/menu_check.py --arena proves E1M1 fits at every skill in
 * this arena; the fallback is for a build whose measurement was changed.
 *
 * This does NOT run the idle tic a New Game needs: a save is applied to the
 * freshly built world first, and its restored state must not advance.
 *
 * @param levelNumber 1..9, the map the caller wants the world on.
 * @param skill       0..4.
 * @return true when the level is laid out and presented at @p skill.
 */
static bool reloadWorldAtLevel(std::uint8_t levelNumber, int skill) {
    if (!gWorld.open || levelNumber < 1u || levelNumber > 9u || skill < 0
        || skill > P_SKILL_NIGHTMARE) {
        return false;
    }
    p_level_t& level = gWorld.level;
    doom_map_t* map = &gSession.driver->map;
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;

    // A same-level reload (New Game on the build's map, the post-victory reset)
    // keeps the map view it already has: rebuilding it would be the same pointer
    // set, and this path is the one the board's menu proof runs six times.
    const std::uint8_t previousLevel = gRuntimeLevel;
    const bool mapChanged = levelNumber != previousLevel;
    if (mapChanged) {
        doom_map_t nextView = {};
        if (!openSessionMap(levelNumber, &nextView)) {
            // openSessionMap() printed the reason; the world is the level it was
            // on, untouched.
            return false;
        }
        *map = nextView;
    }

    gRuntimeLevel = levelNumber;
    gLevelState.episode = 1u;
    gLevelState.map = levelNumber;
    gLevelState.secretExit = 0u;
    gVictoryPending = false;

    level.rules.skill = static_cast<p_skill_t>(skill);
    std::memset(&level.random, 0, sizeof level.random);
    p_level_unbind_render(map);
    const bool reloaded = g_demo_reborn(&level) == G_DEMO_OK;
    p_level_bind_render(&level, map);

    if (reloaded) {
        restartLevelPresentation(mapChanged);
        return true;
    }

    // A refused reload left the level zeroed; the same map is laid out again
    // through the one loader. The caller asked for this level, and a fresh copy
    // of it is the promise every refusal makes.
    if (!layOutWorld(map, skill)) {
        std::snprintf(line, sizeof line, "SHOW,level,error=reload,level=%u,skill=%d\n",
                      static_cast<unsigned>(levelNumber), skill);
        writeLine(line);
    }
    return false;
}
#endif  // DOOM_MENU || DOOM_PERSISTENCE

#if defined(DOOM_MENU)
bool frameLevelNewGame(int skill) {
    // A run starts on the build's map, whatever level the last run ended on.
    if (!reloadWorldAtLevel(static_cast<std::uint8_t>(kFrameLevel), skill)) {
        return false;
    }
    // One idle tic, so the level's own first tic has run before the first frame
    // of the new game: P_PlayerThink is where P_SetupPsprites' deferred call
    // happens, which is what puts the weapon in hand. The eye does not depend
    // on it -- settleLevelView() gives a loaded level its height -- but the
    // psprite timeline does.
    (void)frameLevelRunTics(1u, 0u);
    return true;
}

void frameLevelSetMenu(const m_menu* menu) { gMenu = menu; }

bool frameLevelArenaStats(std::uint32_t* inUse, std::uint32_t* freeBytes,
                          std::uint32_t* failures, std::uint32_t* mobjs) {
    *inUse = 0u;
    *freeBytes = 0u;
    *failures = 0u;
    *mobjs = 0u;
    if (!gWorld.open) {
        return false;
    }
    const zone_stats_t zone = z_stats(&gWorld.level.zone);
    *inUse = zone.inUse;
    *freeBytes = zone.freeBytes;
    *failures = zone.failures;
    *mobjs = p_level_mobj_count(&gWorld.level);
    return true;
}

const char* frameLevelLastMissing() { return gFailTexture; }

#if defined(DOOM_LEVEL_TEST)
/*
 * The E1M8 ending, from a map the board proof can reach.
 *
 * The leveltest image plays E1M1 and never walks the other seven maps, so the
 * victory branch would otherwise be proven only by test/unit/test_level's own
 * worlds. This puts the world on E1M8 at the skill it is already running (the
 * last map's, so the rules travel the way a transition's do) and raises the
 * request sector special 11 raises -- the level's own field, which the next
 * tic's transitionLevel() reads exactly as it reads a line's. What the branch
 * then does is the shipped code: `g_completed_next` answers the victory, the run
 * is reloaded on its first map and the scene opens the menu.
 *
 * It also answers a question no host gate can: whether E1M8 fits the arena the
 * session sized from its own startup map. A refusal prints
 * `SHOW,level,error=reload,level=8` and closes the world, which is the honest
 * report of a dead end.
 *
 * Diagnostic only; never in the shipped image.
 */
bool frameLevelVictoryScript() {
    if (!gWorld.open) {
        return false;
    }
    if (!reloadWorldAtLevel(8u, static_cast<int>(gWorld.level.rules.skill))) {
        return false;
    }
    gWorld.level.exitRequest = static_cast<std::uint8_t>(P_EXIT_NORMAL);
    return true;
}
#endif  // DOOM_LEVEL_TEST
#endif  // DOOM_MENU

#if DOOM_PERSISTENCE
SaveStatus frameLevelSave(std::uint8_t* out, std::size_t capacity, std::size_t& written) {
    if (!gWorld.open) {
        // No world is not a save: a menu opened over a failed boot must report a
        // refusal rather than archive a zeroed level.
        return SaveStatus::ApplyFailed;
    }
    // The record names the level the world is actually on. `p_level_t` does
    // not carry its number, the container's level marker is what the session
    // loaded, and `gRuntimeLevel` is the one variable a transition and a load
    // both move -- so a save written in E1M2 stamps 2 and loads back into E1M2.
    return saveWrite(gWorld.level, gWorld.level.playerState, gRuntimeLevel, out, capacity,
                     written);
}

SaveStatus frameLevelLoad(const std::uint8_t* in, std::size_t size, std::uint8_t levelNumber,
                          std::uint8_t skill) {
    if (!gWorld.open || in == nullptr) {
        return SaveStatus::ApplyFailed;
    }
    // The record names the map, so the reload opens *that* map before
    // applying it -- not whatever level the world happens to be on. `carry=false`
    // is implicit here: g_demo_reborn() is G_PlayerReborn's, and a restored
    // player is a reborn one, never a carried one.
    if (!reloadWorldAtLevel(levelNumber, static_cast<int>(skill))) {
        return SaveStatus::ApplyFailed;
    }
    const SaveStatus status =
        saveApply(in, size, levelNumber, gWorld.level, gWorld.level.playerState);
    if (status != SaveStatus::Ok) {
        // saveApply() documents that a value which only fails while it is being
        // consumed leaves the level partially rebuilt. The caller is promised a
        // *freshly loaded* level on any failure, so lay it out again through the
        // same path -- never a second loader, and never a half-restored world.
        (void)reloadWorldAtLevel(levelNumber, static_cast<int>(skill));
        return status;
    }
    // The restored leveltime and player are what the status bar starts from; the
    // reload above ran hudStart() against the fresh level (leveltime 0).
#if defined(DOOM_HUD)
    hudStart();
#endif
    return SaveStatus::Ok;
}
#endif  // DOOM_PERSISTENCE

bool frameLevelPlayer(LevelPlayer* out) {
    if (out == nullptr) {
        return false;
    }
    *out = LevelPlayer{};
    if (!gWorld.open) {
        return false;
    }

    const r_mobj_t* mobj = gWorld.level.playerState.mo;
    if (mobj == nullptr) {
        return false;
    }
    out->view.x = mobj->x;
    out->view.y = mobj->y;
    // The eye, not the floor: P_CalcHeight's viewz is the mobj's floor height
    // plus the view height plus the bob. A load settles it before the first
    // frame is drawn (settleLevelView()), so 0 only ever means the world has no
    // player.
    out->view.z = gWorld.level.playerState.viewz;
    out->view.angle = mobj->angle;
    out->health = gWorld.level.playerState.health;
    out->leveltime = static_cast<std::uint32_t>(gWorld.level.leveltime);
    out->valid = true;
    return true;
}

const std::uint8_t* frameLevelPlaypal() { return gPlaypalValid ? gPlaypal : nullptr; }

std::uint32_t frameStackFree() {
    // uxTaskGetStackHighWaterMark() reports the *smallest* margin the task has
    // ever had, in StackType_t units (one byte on this port), so a value near
    // zero after firing is the deepest chain the game has reached.
    return static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) *
           static_cast<std::uint32_t>(sizeof(StackType_t));
}

#if defined(DOOM_FRAME_DUMP)
/*
 * The board-side frame dump. The protocol is in FrameDriver.h; what matters here is
 * that this writes the surface the panel is about to expand, not a re-render:
 * a pixel that looks wrong in the dump looked wrong on the glass, so the host
 * can rebuild this exact frame (same palette index bytes) and diff it against
 * its own render of the same viewpoint.
 *
 * One pair per line group of 24, because a console line has to stay short
 * enough for the writer and the capture to keep up; 255 is the longest run a
 * byte can carry, so a longer stretch is several pairs.
 */
void frameDumpSurface(const std::uint8_t* pixels, int width, int height, std::uint32_t frame) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return;
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    char line[128];
    LevelPlayer player;

    std::snprintf(line, sizeof line, "DUMP,begin,frame=%u,w=%d,h=%d\n",
                  static_cast<unsigned>(frame), width, height);
    writeLine(line);
    if (frameLevelPlayer(&player)) {
        std::snprintf(line, sizeof line, "DUMP,view,x=%ld,y=%ld,z=%ld,angle=%lu,dark=%u\n",
                      static_cast<long>(player.view.x), static_cast<long>(player.view.y),
                      static_cast<long>(player.view.z), static_cast<unsigned long>(player.view.angle),
                      static_cast<unsigned>(frameLevelPaletteIndex()));
        writeLine(line);
    }

    // Count the pairs first: the reader allocates once and can tell a truncated
    // dump from a complete one.
    std::size_t at = 0u;
    std::size_t runs = 0u;
    while (at < count) {
        const std::uint8_t value = pixels[at];
        std::size_t length = 1u;
        while (at + length < count && pixels[at + length] == value && length < 255u) {
            ++length;
        }
        at += length;
        ++runs;
    }
    std::snprintf(line, sizeof line, "DUMP,runs=%u\n", static_cast<unsigned>(runs));
    writeLine(line);

    at = 0u;
    std::snprintf(line, sizeof line, "DUMP,r,");
    std::size_t written = std::strlen(line);
    while (at < count) {
        const std::uint8_t value = pixels[at];
        std::size_t length = 1u;
        while (at + length < count && pixels[at + length] == value && length < 255u) {
            ++length;
        }
        // 4 hex chars per pair plus room for the newline and the NUL.
        if (written + 6u >= sizeof line) {
            line[written] = '\n';
            line[written + 1u] = '\0';
            writeLine(line);
            std::snprintf(line, sizeof line, "DUMP,r,");
            written = std::strlen(line);
        }
        std::snprintf(line + written, sizeof line - written, "%02X%02X",
                      static_cast<unsigned>(length), static_cast<unsigned>(value));
        written += 4u;
        at += length;
    }
    if (written > 7u) {
        line[written] = '\n';
        line[written + 1u] = '\0';
        writeLine(line);
    }
    writeLine("DUMP,end\n");
}

void frameDumpSectors() {
    char line[128];

    if (!gWorld.open || gWorld.level.sectors == nullptr) {
        writeLine("SHOW,sectors,status=closed\n");
        return;
    }
    if (gSession.driver != nullptr) {
        const r_view_state_t& view = gSession.driver->view;
        std::snprintf(line, sizeof line, "SHOW,bsp,drawsegs=%u,planes=%u\n",
                      static_cast<unsigned>(view.drawsegCount),
                      static_cast<unsigned>(view.storage.planes.count));
        writeLine(line);
    }
    std::snprintf(line, sizeof line, "SHOW,sectors,count=%u\n",
                  static_cast<unsigned>(gWorld.level.sectorCount));
    writeLine(line);
    for (std::uint16_t index = 0u; index < gWorld.level.sectorCount; ++index) {
        const p_sector_t& sector = gWorld.level.sectors[index];
        std::snprintf(line, sizeof line, "SHOW,s,i=%u,f=%ld,c=%ld,l=%d,sp=%d\n",
                      static_cast<unsigned>(index), static_cast<long>(sector.floorHeight),
                      static_cast<long>(sector.ceilingHeight), static_cast<int>(sector.lightLevel),
                      static_cast<int>(sector.special));
        writeLine(line);
    }
}

#if defined(DOOM_FRAME_DUMP)
/* FNV-1a over a texture record's stored bytes. The host draws its own copy
 * of this loop in tools/rsegs_check.c; the two must agree or the comparison is
 * meaningless. */
static std::uint32_t fnv1a32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t hash = 2166136261u;
    for (std::size_t index = 0u; index < size; ++index) {
        hash ^= data[index];
        hash *= 16777619u;
    }
    return hash;
}
#endif

void frameDumpViewDiag() {
    char line[R_VIEW_DIAG_LINE];

    if (!gWorld.open || gSession.driver == nullptr) {
        writeLine("VD,status=closed\n");
        return;
    }
    {
        const r_view_state_t& view = gSession.driver->view;
        std::uint16_t index;

        if (r_view_diag_config(&view.storage, R_VIEW_HEIGHT, R_CENTER_Y, line,
                               sizeof line) > 0) {
            writeLine(line);
            writeLine("\n");
        }
        if (r_view_diag_pool(&view, line, sizeof line) > 0) {
            writeLine(line);
            writeLine("\n");
        }
        for (index = 0u; index < view.drawsegCount; ++index) {
            if (r_view_diag_drawseg(&view, static_cast<unsigned>(index), line,
                                    sizeof line) > 0) {
                writeLine(line);
                writeLine("\n");
            }
        }
        // The bytes the masked pass will read. Resolving the fragment's mid
        // name and hashing the record answers "is the board drawing the same
        // texture the host composes" without sending the frame.
        for (index = 0u; index < view.drawsegCount; ++index) {
            const r_drawseg_t& drawseg = view.storage.drawsegs[index];
            r_wall_fragment_t fragment;
            const r_walltex_t* texture;
            std::uint32_t pixelsHash = 0u;
            std::uint32_t maskHash = 0u;
            std::size_t bytes;

            if (drawseg.maskedTextureCol == R_DRAWSEG_NO_BLOCK) {
                continue;
            }
            r_wall_fragment_open(view.map, &view.params, drawseg.seg, &fragment);
            texture = gSession.driver->textures.resolve(gSession.driver->textures.context,
                                                        fragment.midTexture);
            if (texture == nullptr || texture->pixels == nullptr) {
                std::snprintf(line, sizeof line, "VD,t,%u,0,0,0,0,0,0\n",
                              static_cast<unsigned>(index));
                writeLine(line);
                continue;
            }
            bytes = static_cast<std::size_t>(texture->width) *
                    static_cast<std::size_t>(texture->height);
            pixelsHash = fnv1a32(texture->pixels, bytes);
            maskHash = texture->mask != nullptr ? fnv1a32(texture->mask, bytes) : 0u;
            std::snprintf(line, sizeof line, "VD,t,%u,%08X,%08X,%d,%d,%d,%d\n",
                          static_cast<unsigned>(index), static_cast<unsigned>(pixelsHash),
                          static_cast<unsigned>(maskHash), static_cast<int>(texture->width),
                          static_cast<int>(texture->height),
                          static_cast<int>(texture->widthMask),
                          static_cast<int>(texture->heightMask));
            writeLine(line);
        }
        for (index = 0u; index < view.storage.planes.count; ++index) {
            if (r_view_diag_plane(&view, static_cast<unsigned>(index), line,
                                  sizeof line) > 0) {
                writeLine(line);
                writeLine("\n");
            }
        }
        for (index = 0u; index < gDumpSpriteCount; ++index) {
            if (r_view_diag_sprite(gSprites, static_cast<unsigned>(gDumpSpriteCount),
                                   static_cast<unsigned>(index), line,
                                   sizeof line) > 0) {
                writeLine(line);
                writeLine("\n");
            }
        }
    }
    writeLine("VD,end\n");
}
#endif

int frameLevelPaletteIndex() {
    return gWorld.open ? palflash_index(&gWorld.level.playerState) : 0;
}

bool frameLevelPlaypalPage(int page, std::uint8_t* out) {
    if (out == nullptr || page < 0 || page >= PALFLASH_NUMPALS) {
        return false;
    }
    if (page == 0) {
        // Palette 0 stays resident (768 B): the normal picture is every frame
        // that has no flash and every gamma change at rest, so it must cost no
        // flash read.
        if (!gPlaypalValid) {
            return false;
        }
        std::memcpy(out, gPlaypal, kPlaypalBytes);
        return true;
    }
    // PLAYPAL is raw in the container, so a flash page is a pointer into the
    // mapping -- page @p p at `view + p * 768`, one 768 B copy to the caller's
    // buffer. Decoding all 10,752 B into a transient internal-RAM block cannot
    // work in game: with the save buffer allocated the largest free heap block
    // is about 7,924 B. A view needs no heap and no decode.
    //
    // A container whose PLAYPAL is stored deflated answers ERR_NOT_RAW here.
    // There is deliberately no allocation fallback: the caller keeps the page it
    // already has rather than taking RAM the heap cannot give.
    const int lump = doom_whd_source_lump_index(&gWhd, "PLAYPAL");
    if (lump < 0) {
        return false;
    }
    const std::uint8_t* view = nullptr;
    std::uint32_t viewBytes = 0u;
    if (doom_whd_source_lump_view(&gWhd, lump, &view, &viewBytes) != DOOM_WHD_SOURCE_OK) {
        return false;
    }
    const std::size_t need = (static_cast<std::size_t>(page) + 1u) * kPlaypalBytes;
    if (view == nullptr || static_cast<std::size_t>(viewBytes) < need) {
        return false;
    }
    std::memcpy(out, view + static_cast<std::size_t>(page) * kPlaypalBytes, kPlaypalBytes);
    return true;
}

#if defined(DOOM_PALFLASH_SCRIPT)
bool frameLevelPalflashScript(int step) {
    if (!gWorld.open || gWorld.level.player == nullptr) {
        return false;
    }
    p_level_t& level = gWorld.level;
    // The same two real paths the host gate drives: P_DamageMobj on the avatar,
    // and P_TouchSpecialThing on an item placed 20 units ahead of it.
    mobjtype_t item = MT_MISC2;
    switch (step) {
        case 0:
            p_damage_mobj(&level.context, level.player, nullptr, nullptr, 40);
            return true;
        case 1:
            item = MT_MISC2;
            break;
        case 2:
            p_damage_mobj(&level.context, level.player, nullptr, nullptr, 15);
            return true;
        case 3:
            item = MT_MISC14;
            break;
        default:
            return false;
    }
    r_mobj_t* special = p_spawn_mobj(&level.context, level.player->x,
                                     level.player->y + 20 * FRACUNIT, ONFLOORZ, item, nullptr);
    if (special == nullptr) {
        return false;
    }
    p_touch_special_thing(&level.context, special, level.player);
    return true;
}
#endif

std::size_t frameLevelFormatMemory(char* out, std::size_t capacity) {
    if (out == nullptr || capacity == 0u) {
        return 0u;
    }
    out[0] = '\0';
    if (!gSession.open) {
        return 0u;
    }
    formatMemoryLine("SHOW", true, out, capacity, gSession.heapBefore,
                     static_cast<std::uint32_t>(esp_get_free_heap_size()));
    return std::strlen(out);
}

#endif  // PIXELROOT32_DOOM_SHOW_E1M1

void frameProbeRun(std::uint8_t* surface, int width, int height) {
    // The three line buffers are the probe's largest stack locals and the call
    // graph below them -- the BSP recursion, the wall column emitter and the
    // masked pass's two clip arrays -- already runs close to the loop task's
    // stack; a few more words in several of those frames are enough to trip the
    // canary, so the buffers live in .bss: they are written once per line and
    // never reentrant. `line` is the session's shared
    // buffer; this function runs only after the session has finished setting up.
    char (&line)[kFrameProfileLineCapacity] = gSessionLine;
    static char attrLine[kFrameAttributionLineCapacity];
    static char memLine[kFrameAttributionLineCapacity];
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

    // The one setup implementation, shared with the game frame: it maps the
    // container, binds E1M1 and leaves the mapping live. Every line it prints is
    // this probe's own `FRAMEPROF,setup,...` record.
    // The probe keeps its Driver on the stack: the RAM
    // control environment has under 1 KB of `dram0_0_seg` headroom and a 464 B
    // file-scope copy is enough to overflow it. The session points at this
    // object; only the game frame uses its own static `gShowDriver`.
    Driver driver;
    char mapMarker[5];
    if (!openLevelSession(&gSession, &driver, "FRAMEPROF", false, mapMarker)) {
        return;
    }

    LevelSession& session = gSession;

    // --- The walk's storage and the pass contracts --------------------------
    FrameView frames[kViewCount];
    if (!buildFrames(&driver.map, frames)) {
        writeLine("FRAMEPROF,setup,status=fail,reason=views\n");
        closeLevelSession();
        return;
    }

    // --- The table ----------------------------------------------------------
    formatMemoryLine("FRAMEPROF", false, memLine, sizeof memLine, session.heapBefore,
                     session.heapBefore);
    writeLine(memLine);

    bindSurface(&driver, surface, width, height);

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
                closeLevelSession();
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
            emitFormatted(frameProfileFormatAttribution(sample, attrLine, sizeof attrLine),
                          attrLine);
            if (isBreakdownView(frame)) {
                emitFormatted(frameProfileFormatBreakdown(sample, line, sizeof line), line);
            }
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
            gProbeSum.add(driver.probe, driver.frameCycles, driver.spriteListCycles);
            if (isBreakdownView(frame)) {
                gProbeFrameSum = ProbeSum{};
                gProbeFrameSum.add(driver.probe, driver.frameCycles,
                                   driver.spriteListCycles);
                formatProbeLine(gProbeFrameSum, gProbeLine, sizeof gProbeLine);
                emitLine(gProbeLine);
            }
#endif
        }
    }

    const std::uint32_t probeMicros = elapsedMicros(probeStart);
    emitFormatted(frameProfileFormatSummary(stats, kViewCount, kMeasuredRuns, line, sizeof line),
                  line);
    emitFormatted(frameProfileFormatAttributionSummary(stats, attrLine, sizeof attrLine),
                  attrLine);
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    formatProbeLine(gProbeSum, gProbeLine, sizeof gProbeLine);
    emitLine(gProbeLine);
    runMemoryProbe(surface, width, gProbeTexture, gColormap, gProbeLine,
                   sizeof gProbeLine);
    emitLine(gProbeLine);
#endif

    std::snprintf(line, sizeof line,
                  "FRAMEPROF,probe,total_us=%u,hazards=%u,sha256=ok,views=%d,runs=%d,"
                  "warmup=%d,map=%s,level=%s,phases=4\n",
                  static_cast<unsigned>(probeMicros), 0u, kViewCount,
                  kMeasuredRuns, kTotalRuns - kMeasuredRuns, kMapPath, mapMarker);
    writeLine(line);

    // The blocks go back before the shell's first frame: a probe that left the
    // graphics and the decoded level held would be a state its caller never asked
    // for.
    closeLevelSession();

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

void frameLevelLastRefusal(const char** stage, int* code) {
    // A host build with no board driver never refuses a frame: there is no
    // session to walk, so the honest answer is "nothing refused".
    if (stage != nullptr) {
        *stage = "none";
    }
    if (code != nullptr) {
        *code = 0;
    }
}

}  // namespace doomc
}  // namespace doom

#elif defined(PIXELROOT32_DOOM_SHOW_E1M1)

// Host build with the game flag set: there is no `wad` partition to map, so the
// session cannot open. The shipped `_show` environment is an ESP32 one; this
// branch exists so a host build that defines the flag still links and reports a
// closed session instead of failing at link time.
namespace doom {
namespace doomc {

bool frameLevelOpen(const char* tag, char marker[5]) {
    (void)tag;
    if (marker != nullptr) {
        marker[0] = '\0';
    }
    return false;
}

bool frameLevelRenderSpawn(std::uint8_t* surface, int width, int height,
                           FrameProfileSample* sample, char hash[65], bool wantHash,
                           std::uint32_t* hashMicros) {
    (void)surface;
    (void)width;
    (void)height;
    (void)sample;
    (void)wantHash;
    if (hash != nullptr) {
        hash[0] = '\0';
    }
    if (hashMicros != nullptr) {
        *hashMicros = 0u;
    }
    return false;
}

bool frameLevelRenderView(std::uint8_t* surface, int width, int height, std::int32_t x,
                          std::int32_t y, std::int32_t z, std::uint32_t angle,
                          FrameProfileSample* sample, char hash[65], bool wantHash,
                          std::uint32_t* hashMicros) {
    (void)surface;
    (void)width;
    (void)height;
    (void)x;
    (void)y;
    (void)z;
    (void)angle;
    (void)sample;
    (void)wantHash;
    if (hash != nullptr) {
        hash[0] = '\0';
    }
    if (hashMicros != nullptr) {
        *hashMicros = 0u;
    }
    return false;
}

bool frameLevelSpawnView(LevelView* out) {
    (void)out;
    return false;
}

bool frameLevelRunTics(std::uint32_t tics, std::uint8_t buttons) {
    (void)tics;
    (void)buttons;
    return false;
}

#if defined(DOOM_MENU)
bool frameLevelNewGame(int skill) {
    (void)skill;
    return false;
}

void frameLevelSetMenu(const m_menu* menu) { (void)menu; }

bool frameLevelArenaStats(std::uint32_t* inUse, std::uint32_t* freeBytes,
                          std::uint32_t* failures, std::uint32_t* mobjs) {
    *inUse = 0u;
    *freeBytes = 0u;
    *failures = 0u;
    *mobjs = 0u;
    return false;
}

const char* frameLevelLastMissing() { return ""; }
#endif

bool frameLevelPlayer(LevelPlayer* out) {
    if (out != nullptr) {
        *out = LevelPlayer{};
    }
    return false;
}

void frameLevelClose() {}

const std::uint8_t* frameLevelPlaypal() { return nullptr; }

int frameLevelPaletteIndex() { return 0; }

bool frameLevelPlaypalPage(int page, std::uint8_t* out) {
    (void)page;
    (void)out;
    return false;
}

#if defined(DOOM_PALFLASH_SCRIPT)
bool frameLevelPalflashScript(int step) {
    (void)step;
    return false;
}
#endif

std::size_t frameLevelFormatMemory(char* out, std::size_t capacity) {
    if (out != nullptr && capacity > 0u) {
        out[0] = '\0';
    }
    return 0u;
}

void frameLevelLastRefusal(const char** stage, int* code) {
    if (stage != nullptr) {
        *stage = "none";
    }
    if (code != nullptr) {
        *code = 0;
    }
}

const LevelFailure& frameLevelFailure() {
    // A host build with the game flag has no `wad` partition, so `open()` fails
    // before anything is classified. The honest record is "no failure in the
    // level session" -- the scene's own error is the closed session, not a
    // fabricated container reason.
    static const LevelFailure kNoFailure;
    return kNoFailure;
}

}  // namespace doomc
}  // namespace doom

#endif


