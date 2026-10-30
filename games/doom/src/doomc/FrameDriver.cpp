/*
 * D3.8's frame driver: see FrameDriver.h for the four phases, the two map paths,
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
 * renders a whole frame on the board. D3.5 added the attribution the frame table
 * is read with; D3.8 removed the residency cache and the three decoders from
 * this path. Every wall texture, the sky and every flat is a raw payload baked
 * at build time by tools/bake_graphics.py and named by the container's ~PWBAKE~
 * manifest, so a resolve is a manifest lookup and a pointer into the mapping.
 *
 * WHAT THE FRAME COSTS, IN BYTES, AND WHERE THOSE BYTES COME FROM NOW
 *
 * Every large buffer below is a reported size, not an estimate: `FRAMEPROF,mem`
 * prints each one from `sizeof` or from the bootstrap that allocated it.
 *
 *   walk state      33,184 B   drawsegs 2,080, visplanes 14,608, clip posts 256,
 *                              floorclip/ceilingclip 1,280, clip blocks 9,600,
 *                              opening blocks 640, vis sprites 4,400, covered 320
 *   baked records    4,632 B   the 90-entry manifest adapter, static .bss
 *   level                0 B   in the flash build the map is a pointer into the
 *                              mapping; 47,888 B in the RAM build
 *
 * The D3.5 graphics state is gone: a 53,248 B static arena, up to 24,576 B of
 * heap arenas, 1,920 B of cache entries and a 35,080 B decode scratch. That is
 * ~114,824 B of graphics RAM released and replaced by the 4,632 B above.
 *
 * THE TWO MAP PATHS
 *
 * `PIXELROOT32_DOOM_MAP_IN_RAM=1` decodes the map lumps into RAM, which is what
 * D3.6 did and what the A/B needs as its control; without it the map's lumps are
 * raw container entries and `doom_whd_source_lump_view()` hands out the mapped
 * bytes with no copy at all. The graphics are baked in both builds on purpose:
 * the A/B isolates the map path, and the bake is the thing this task measures.
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

    // D3.5's attribution: every field is folded here so no summary can read a
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

    // Thousandths of a microsecond: 1,100 means 1.100 us per unit, which is the
    // unit the D3.5 brief uses. A zero denominator reports 0, never a divide.
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

// --- The board driver (D3.6) -------------------------------------------------

#if defined(PIXELROOT32_DOOM_FRAME_PROBE) && (defined(ESP32) || defined(ESP_PLATFORM))

#include <cstring>
#include <cstddef>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "doomc/BakedGraphics.h"
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

/*
 * The CPU cycle counter the D3.9 attribution reads (r_bsp.h declares it).
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

/// One static graphics region. D3.8 removed the cache: every wall texture, the
/// sky and every flat is a pointer into the container's raw section now, so the
/// 53,248 B arena and the 35,080 B decode scratch are gone. The only graphics
/// state left is the manifest adapter, which is a fixed record array.
///
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
 *                 (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c). D3.8 removed the
 *                 cache and scratch, so the walk state and the manifest adapter
 *                 are the only large statics left.
 *   the heap      what is left of DRAM after the static data. The sprite list
 *                 (130 records) and, in the RAM build, the decoded level come
 *                 from it.
 *
 * `DriverState` is the static half and `gBaked` is the manifest adapter; the
 * graphics themselves are pointers into the mapping, so no pool holds them.
 * The decoded level is the RAM build's own array and heap blocks; the flash
 * build stores it in the container.
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
/// A/B's control: it keeps D3.6's data path (the map decoded into DRAM) so the
/// two builds differ in exactly one thing.
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

/// D3.9's residency-scoped patch proof cache. The bake is part of the read-only
/// container mapping and BakedGraphics' records are file-scope statics, so a
/// patch at a given address cannot change or become malformed while the run
/// holds: the two promises r_things.h's r_sprite_proofs_t requires both hold
/// here, and the 32 distinct sprite lumps are proved once per run instead of
/// once per frame. It is the only state this change adds, 800 B of .bss.
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
    // patch's inflate counted once, and `residency_us` keeps its older meaning
    // (the outermost decoder only) so the baseline stays comparable.
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
/// The first wall texture the run resolved, kept for the D3.9 memory probe so
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
 * THE SPRITE LIST'S CONSTANT HALF (D3.9)
 *
 * buildSprites() used to re-read every THINGS record, descend the BSP for its
 * sector and copy the sector for all 129 sprites *per view*, for values that
 * cannot change while the level is loaded. The attribution measured that at
 * 4,932 us a frame -- 37% of the masked window, more than the sprite drawing
 * itself -- for 242 mapped-flash record reads a frame.
 *
 * Only two fields are view-dependent: the rotation R_ProjectSprite's rule picks
 * for this viewpoint, and the lump and flip that follow from it. So the base
 * record is built once per run into gSprites itself (no second array of records)
 * and the per-view pass rewrites the lump, the flip and the fullbright flag.
 * The rule pointer is the 520 bytes this needs, and it is the only new state.
 *
 * The frame is unchanged by construction: the same records, the same values,
 * the same order -- only the reads that produce them happen once.
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
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    /// D3.9's cycle attribution for one frame. Zeroed at the top of every
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
 * THE D3.9 ACCUMULATOR
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

    std::memset(pixels, 0xFF, static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
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
    const r_segs_error_t walls =
        r_wall_emit(&driver->view, &driver->surface, &driver->textures, &driver->light);
    sample->wallsMicros = elapsedMicros(start);
    if (walls != R_SEGS_OK) {
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
    const int spriteCount = buildSprites(&driver->map, &params);
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    driver->spriteListCycles = r_probe_cycles() - spriteListStart;
#endif
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
    sample->maskedDecodeMicros = gSample.decodeMicros - decodeMark;
    sample->maskedComposeMicros = gSample.composeMicros - composeMark;
    sample->maskedLookupMicros = gSample.lookupMicros - lookupMark;
    sample->spritePixels = driver->surface.spritePixels;
    sample->spritesSubmitted = maskedStats.submitted;
    sample->spritesDrawn = maskedStats.drawn;
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

#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
/*
 * The D3.9 attribution line: what the cycles were spent on, per unit, so the
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
 * units are the D3.5 convention (1,100 = 1.100 us), so a row here is directly
 * comparable with the frame table's `px_us_permille`.
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
        // Thousandths of a nanosecond... the frame table's unit is 1/1000 us,
        // so the same unit is used here: permille of a microsecond.
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

/// One `FRAMEPROF,mem` field, from `sizeof`: the driver's RAM cost is a printed/// number, never an estimate. `state_static` is the .bss half, `level_ram` is the
/// decoded level (0 in the flash build) and `heap_before/after` bound the rest.
/// D3.8's `cache_*` fields are the bytes the cache and scratch used to cost;
/// they are constants now because the code that owned them is gone.
void formatMemoryLine(char* line, std::size_t capacity, std::uint32_t heapBefore,
                      std::uint32_t heapAfterAlloc) {
    const std::size_t drawsegs = sizeof(gState.drawsegs);
    const std::size_t planes = sizeof(gState.planes);
    const std::size_t clipRanges = sizeof(gState.clipRanges);
    const std::size_t clipArrays = sizeof(gState.floorClip) + sizeof(gState.ceilingClip);
    const std::size_t clipBlocks = sizeof(gState.clipBlocks);
    const std::size_t spritePools = sizeof(gState.visSprites) + sizeof(gState.openingBlocks);
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
                  "FRAMEPROF,mem,surface=engine8bpp,index_writes=1,present=no,no_rgb332=1,"
                  "arena=%s,lump_blocks=%u,baked_records=%u,baked_entries=%d,baked_bytes=%u,"
                  "state_static=%u,drawsegs=%u,planes=%u,clip_ranges=%u,clip_arrays=%u,"
                  "clip_blocks=%u,sprite_pools=%u,covered=%u,driver_total=%u,drawseg_cap=%d,"
                  "plane_cap=%d,clip_block_cap=%d,vissprite_cap=%d,heap_before=%u,"
                  "heap_after_alloc=%u,map=%s,map_in_ram=%d,raw_views=%u,level_ram=%u,"
                  "level_released=%u,cache_static=%u,scratch=35080,cache_entries=1920,"
                  "cache_heap_max=%u,phases=4\n",
                  gPlacement, static_cast<unsigned>(gLumpBlockCount),
                  static_cast<unsigned>(bakedRecords), gBaked.entryCount(),
                  static_cast<unsigned>(gBaked.payloadBytes()),
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
                  static_cast<unsigned>(kLevelReleasedBytes), kRemovedCacheStatic,
                  kRemovedCacheHeap);
}

}  // namespace

void frameProbeRun(std::uint8_t* surface, int width, int height) {
    // The three line buffers are the probe's largest stack locals and the call
    // graph below them -- the BSP recursion, the wall column emitter and the
    // masked pass's two clip arrays -- already runs close to the loop task's
    // stack. D3.9's attribution added a few words to several of those frames and
    // that was enough to trip the canary, so the buffers moved to .bss: they are
    // written once per line and never reentrant.
    static char line[kFrameProfileLineCapacity];
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

    const int openCode = doom_whd_source_open(&gWhd);
    if (openCode != DOOM_WHD_SOURCE_OK) {
        std::snprintf(line, sizeof line, "FRAMEPROF,setup,status=fail,reason=open,code=%d\n",
                      openCode);
        writeLine(line);
        return;
    }
    // A new container, a new manifest: the adapter validates every raw payload
    // once, builds one record per name and allocates nothing. A container built
    // without the bake is a reported failure here, not a fallback decode.
    const BakedError baked = gBaked.open(&gWhd);
    if (baked != BakedError::Ok) {
        std::snprintf(line, sizeof line,
                      "FRAMEPROF,setup,status=fail,reason=baked,code=%s\n",
                      bakedErrorName(baked));
        writeLine(line);
        doom_whd_source_close();
        return;
    }

    // --- The working set ----------------------------------------------------
    //
    // The graphics are already in flash: the only graphics state this build
    // holds is the manifest adapter. `FRAMEPROF,mem` prints its record bytes and
    // the heap before/after, so the released cache is a measured number.
    const std::uint32_t heapBefore = static_cast<std::uint32_t>(esp_get_free_heap_size());
    const std::uint32_t largestBefore = static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    gPlacement = "baked-flash";
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
    // TEXTURE1 and PNAMES are build-time inputs now: the bake used them and the
    // runtime reads the manifest. They stay in the container for the C check.
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
                      "fail_lump=%s,not_raw=%u,directories=%d,map_error=%d,"
                      "colormap=%u,heap=%u,largest=%u,level_ram=%u\n",
                      kMapPath, static_cast<int>(mapFailures),
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
        return;
    }

    if (r_light_init(&gLight, gColormap, gColormapBytes) != R_LIGHT_OK) {
        writeLine("FRAMEPROF,setup,status=fail,reason=colormap\n");
        doom_whd_source_close();
        releaseWorkingSet();
        return;
    }

    // The bake and the map path are the setup's two decisions, so they are
    // printed with the setup that made them.
    std::snprintf(line, sizeof line,
                  "FRAMEPROF,setup,status=ok,map=%s,map_in_ram=%d,raw_views=%u,level_ram=%u,"
                  "baked_entries=%d,baked_walls=%d,baked_flats=%d,baked_sprites=%d,"
                  "baked_bytes=%u,colormap=%u,map_lumps=%d,map_skipped=%d,"
                  "setup_us=%u\n",
                  kMapPath,
#if defined(PIXELROOT32_DOOM_MAP_IN_RAM)
                  1,
#else
                  0,
#endif
                  static_cast<unsigned>(gRawViews), static_cast<unsigned>(gLevelRamUsed),
                  gBaked.entryCount(),
                  gBaked.count(BakedKind::kBakedKindWall),
                  gBaked.count(BakedKind::kBakedKindFlat),
                  gBaked.count(BakedKind::kBakedKindSprite),
                  static_cast<unsigned>(gBaked.payloadBytes()),
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
    // D3.9: the baked patches cannot change under an unchanged record address,
    // so their proof belongs to the residency and not to the frame. Reset the
    // count here because this is where the run's resolver is bound.
    gSpriteProofs.count = 0;
    driver.storage.spriteProofs = &gSpriteProofs;

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
    driver.textures.beginFragment = nullptr;
    driver.sprites.resolve = resolveSprite;
    driver.sprites.context = nullptr;
#if defined(PIXELROOT32_DOOM_RASTER_PROBE)
    // D3.9: the four passes fold their cycle buckets into this state. Assigned
    // after r_bsp_init() because that call zeroes the state.
    driver.view.probe = &driver.probe;
#endif
    bindSurface(&driver, surface, width, height);

    // --- The table ----------------------------------------------------------
    formatMemoryLine(memLine, sizeof memLine, heapBefore, heapAfterAlloc);
    writeLine(memLine);

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
                  "warmup=%d,map=%s,phases=4\n",
                  static_cast<unsigned>(probeMicros), 0u, kViewCount,
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


