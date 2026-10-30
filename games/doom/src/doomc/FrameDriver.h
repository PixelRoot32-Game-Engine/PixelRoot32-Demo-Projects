#pragma once

/*
 * D3.8: the board frame driver after composition moved to build time.
 *
 * D3.6 measured the wall and could not finish the frame; D3.7 moved the level's
 * lumps into a raw, page-mappable section of the PWHD container (lib/doom/whd.h,
 * method 2) so all four phases run. D3.5 then attributed the frame: 69.1 % of it
 * was inflate/copy, 7.8 % composition and 5.9 % container name scans. D3.8 stops
 * doing all three for graphics: tools/bake_graphics.py writes every wall texture,
 * the sky and every flat as a raw payload at build time, the container carries a
 * ~PWBAKE~ manifest, and this driver resolves a packed name through
 * src/doomc/BakedGraphics.h to a pointer into the mapping. The residency cache,
 * its arenas, its entries and the 35,080 B decode scratch are gone; the only
 * graphics state left is the 4,632 B manifest adapter.
 *
 * The probe still prints, beside the frame line, one `FRAMEPROF,attr` line per
 * measured frame and one `FRAMEPROF,attrsum` summary. They carry:
 *
 *   pixels / columns / fragments   the wall tier pixels, the wall columns the
 *                                  loop visited (sum of x2-x1+1) and the number
 *                                  of drawsegs, plus the plane pixels and spans,
 *                                  the sky columns and the sprite columns.
 *   decode_us                      inflate/copy only; 0 for graphics now, and
 *                                  nonzero only for the RAM build's map setup.
 *   compose_us                     the rest of a composition; 0 now.
 *   lookup_us                      the manifest lookup; a resolve is a binary
 *                                  search over at most 96 packed names.
 *   wall_/plane_/masked_*_us       the same three buckets per pass.
 *   raster_us                      pass time less those buckets, i.e. the column
 *                                  loop, the span loop and the sprite post walk.
 *   px_us_permille                 wall raster per wall pixel, in thousandths
 *                                  of a microsecond (1,100 = 1.100 us/pixel).
 *
 * The cache counters keep their line positions with their D3.8 meaning: a "hit"
 * is a name the manifest answered and a "miss" is one it did not; `cache_bytes`
 * is the total baked payload size and `cache_peak` is the manifest entry count.
 * `tex_dec`, `flat_dec`, `sprite_dec` and `bytes_decoded` are 0 for graphics.
 *
 * THE TWO MAP PATHS (unchanged)
 *
 * The one thing a raw section cannot tell you by itself is what the flash reads
 * cost. So the same driver is built twice:
 *
 *   env:esp32dev_ILI9341_frame      the map is read from the mapping (raw lumps)
 *   env:esp32dev_ILI9341_frameram   PIXELROOT32_DOOM_MAP_IN_RAM=1 decodes the
 *                                   map lumps into RAM, exactly as D3.6 did
 *
 * Both produce the same pixels, so the frame hashes must agree character for
 * character; the difference is `walk_us` and `frame_us`, and that difference is
 * the answer to "does reading the map from flash cost us a frame". The risk is
 * real and is the point of the A/B: D0.5 measured 6.70 MB/s sequential and 4.05
 * us per random 4-byte read from a mapped WAD. The map's hot working set (NODES
 * and SEGS, ~15 KB) should fit the flash cache, but a cold first frame must be
 * reported separately from the steady state instead of being averaged into it,
 * which is why every view's first (discarded) render also prints a
 * `FRAMEPROF,cold,...` line.
 *
 * THE FRAME, IN THE ORDER THE RENDERER DEFINES
 *
 *   walk      r_bsp_render()    the BSP walk: drawsegs and plane marks
 *   walls     r_wall_emit()     the three wall tiers, the clip snapshot, the
 *                               masked texture columns
 *   planes    r_plane_draw()    flats and the sky
 *   masked    r_masked_emit()   sprites and the deferred masked mid textures
 *
 * Each pass is timed with `esp_timer_get_time()` around the call alone; the
 * sprite list is built inside the masked window, because it is the caller's
 * work for that pass and R_DrawMasked() upstream is one function. `residency`
 * is the part of the measured passes spent *inside a decoder* -- a container
 * lump decode or a texture compose -- reported separately so raster time and
 * decode time cannot be confused, and accumulated only for the outermost decode
 * so a compose that decodes its patches does not count their time twice.
 * `frame_us` is the whole per-view sequence -- surface prefill and the four
 * passes -- so `frame_us - (walk + walls + planes + masked)` is work no pass
 * accounts for, and the summary prints that remainder as `other_us` instead of
 * hiding it inside a phase.
 *
 * WHAT IS DELIBERATELY NOT IN THE FRAME
 *
 * The palette index -> RGB332 conversion and the present. This probe measures
 * renderer CPU and nothing else: the pixels are DOOM palette indices written
 * straight into the engine's 8bpp surface, which is *visually wrong* and
 * timing-correct, and the `FRAMEPROF,mem` line says so (`index_writes=1`,
 * `present=no`, `no_rgb332=1`) so nobody reads a screenshot as a result.
 *
 * WHAT ONE FRAME COSTS IN RAM NOW
 *
 *   walk state      53,248 B   drawsegs 64 x 52, visplanes 40 x 664, clip posts
 *                              32 x 8, floorclip/ceilingclip 2 x 320 x 2, clip
 *                              blocks 15 x 320 x 2, opening blocks 1 x 320 x 2,
 *                              vis sprites 256 x 44, covered 320
 *   baked records   12,312 B   the 187-entry manifest adapter; the pixels are in
 *                              flash and no buffer copies them
 *   level                0 B   in the flash path: the map is a pointer into the
 *                              mapping. 47,888 B in the RAM path.
 *
 * 65,560 B of driver RAM in the flash path, against 105,128 B in D3.5: the
 * 53,248 B static arena, up to 24,576 B of heap arenas, 1,920 B of cache entries
 * and the 35,080 B decode scratch are gone. `FRAMEPROF,mem` prints
 * `map=flash` or `map=ram`, every block it still holds, and the constants the
 * removed cache used to cost, so the split is a printed number and not a claim.
 *
 * WHAT FITS, AND WHAT WAS NOT DONE TO MAKE IT FIT
 *
 * Every capacity is a measured peak from the whole shareware episode, not from
 * E1M1 alone (D3.10): 64 drawsegs (E1M2 peak 52, E1M1 39), 40 visplanes (E1M2
 * draws 32, E1M1 20; the E1M1-only 22 reported R_BSP_ERR_VISPLANE_OVERFLOW on
 * E1M2 frame 01), 15 clip blocks (peak 15), 256 vis sprites (E1M2 projects more
 * than E1M1's 97; 100 reported R_THINGS_ERR_VISSPRITE_OVERFLOW on E1M2 frame
 * 02), one opening block (peak 0). Allocating exactly what each view needs is
 * what keeps every pixel -- and therefore every hash -- identical to the
 * host's. The surface is not a 64,000 B private buffer: it is the engine's own
 * 8bpp framebuffer. REJECT, BLOCKMAP and THINGS are in the container's raw
 * section but only THINGS is read, once per view, to place the sprites.
 *
 * THE 20 VIEWS
 *
 * The same rule tools/rsegs_check.c uses, so the board's numbers describe the
 * same 20 views the host pixel gate already proved:
 *
 *   positions  the first four THINGS records, in file order, whose type is 1, 2,
 *              3, 4 or 11, padded by repeating the spawn viewpoint when fewer
 *              than four exist;
 *   angles     k * 2^32 / 5 for k = 0..4;
 *   frame      position * 5 + angle;
 *   z          the floor of the subsector at (x, y) plus R_VIEWHEIGHT.
 *
 * Each view is rendered three times: the first render is discarded, the next two
 * are measured and printed as `run=2` and `run=3`. A warm-up is discarded because
 * the first render of a view touches flash the container has not delivered yet,
 * and a measurement that mixes the first touch of flash with the steady state is
 * not a baseline anyone can compare against. The discarded render is not thrown
 * away, though: it is printed as `FRAMEPROF,cold,frame=NN,...`, which is the cold
 * number the flash path exists to expose.
 *
 * THE HASH, AND WHAT IT IS FOR
 *
 * After each measured frame the driver prints `hash=`, the SHA-256 of the 64,000
 * index bytes followed by the 320 covered bytes -- the host harness's
 * `SPRSTAT ... framehash`, the finished four-pass frame. It exists to catch a
 * board/host divergence in the data path (a texture decoded differently, a clip
 * array off by one) and, between the two board builds, a divergence caused by
 * reading the map from flash instead of RAM. The pixel gate is host-side and
 * already proven; this hash is a sentinel, not a new gate.
 *
 * THE COMPILE-TIME GATE
 *
 * `kFrameProbeEnabled` is true only when the build defines
 * `PIXELROOT32_DOOM_FRAME_PROBE` (`env:esp32dev_ILI9341_frame` and
 * `env:esp32dev_ILI9341_frameram`). Every static buffer, every resolver and
 * every `esp_timer_get_time()` call lives inside that `#if`, so a shipped build
 * carries none of them. The arithmetic below -- the accumulator, the three
 * formatters, the gate -- is always compiled because a host test drives it, and
 * `-ffunction-sections` plus `--gc-sections` drops it from a build that never
 * calls it; `FrameProfileReport<false>` is the gate the probe emits through and
 * it writes nothing at all.
 */

#include <cstddef>
#include <cstdint>

namespace doom {
namespace doomc {

#if defined(PIXELROOT32_DOOM_FRAME_PROBE)
/// True only in the build that compiles the board probe.
constexpr bool kFrameProbeEnabled = true;
#else
/// True only in the build that compiles the board probe.
constexpr bool kFrameProbeEnabled = false;
#endif

/// The Phase 3 renderer-CPU gate: 20,000 us of one 35 Hz frame's 28,571 us.
constexpr std::uint32_t kFrameGateMicros = 20000u;

/// Bytes a formatted line can need, including the newline and the NUL. The
/// frame line carries a 64-byte SHA-256 in hex, so it is the longest one.
constexpr std::size_t kFrameProfileLineCapacity = 512;

/// Bytes the D3.5 attribution line can need. It carries the pixel, column and
/// fragment counts, the exclusive time buckets and the cache counters, so it is
/// longer than the frame line and has its own budget.
constexpr std::size_t kFrameAttributionLineCapacity = 1024;

/// The views whose per-phase breakdown is printed in addition to the table.
///
/// The brief named "frame 01 and frame 16 (the two densest drawseg sets)" and
/// "frame 10 (sky-heavy)"; the measured drawseg counts are 39 for frame 16, 38
/// for frame 06 and 34 for frame 01, so the two densest are 06 and 16. All four
/// named views are printed, which is one more than the brief asked for and
/// strictly more information.
constexpr int kFrameBreakdownCount = 4;
constexpr int kFrameBreakdownFrames[kFrameBreakdownCount] = {0, 6, 10, 16};

/// One measured frame: the four pass times, the residency inside them, the whole
/// sequence, the counts, and the run index.
struct FrameProfileSample {
    std::uint16_t frame = 0;
    std::uint16_t run = 0;
    std::uint32_t walkMicros = 0;
    std::uint32_t wallsMicros = 0;
    std::uint32_t planesMicros = 0;
    std::uint32_t maskedMicros = 0;
    std::uint32_t residencyMicros = 0;
    std::uint32_t frameMicros = 0;
    std::uint16_t drawsegs = 0;
    std::uint16_t planesDrawn = 0;
    std::uint16_t planesAllocated = 0;
    std::uint16_t spritesSubmitted = 0;
    std::uint16_t spritesDrawn = 0;
    std::uint16_t texResolves = 0;
    std::uint16_t texDecodes = 0;
    std::uint16_t flatResolves = 0;
    std::uint16_t flatDecodes = 0;
    std::uint16_t spriteResolves = 0;
    std::uint16_t spriteDecodes = 0;
    std::uint16_t drawsegOverflowed = 0;
    std::uint32_t bytesDecoded = 0;

    // --- D3.5 attribution ---------------------------------------------------
    //
    // The counts that let a microsecond be assigned to a unit rather than to a
    // phase: the pixels each pass wrote, the columns each column loop visited,
    // and the fragments (drawsegs and vis sprites) it iterated. `pixels` is the
    // sum of the three pass records, so a per-pixel cost is one division.
    std::uint32_t wallPixels = 0;
    std::uint32_t planePixels = 0;
    std::uint32_t spritePixels = 0;
    std::uint32_t pixels = 0;
    std::uint16_t wallColumns = 0;
    std::uint16_t wallFragments = 0;
    std::uint16_t planeSpans = 0;
    std::uint16_t skyColumns = 0;
    std::uint16_t spriteColumns = 0;

    // The exclusive time buckets. `decodeMicros` is inflate/copy only and is
    // summed at the leaf, so a compose that decodes six patches counts each
    // patch's inflate once. `composeMicros` is the rest of a compose (validate,
    // blit, the in-place column expansion), `lookupMicros` is the container
    // name -> lump scan, and everything left in a pass is raster.
    std::uint32_t decodeMicros = 0;
    std::uint32_t composeMicros = 0;
    std::uint32_t lookupMicros = 0;
    std::uint32_t wallDecodeMicros = 0;
    std::uint32_t planeDecodeMicros = 0;
    std::uint32_t maskedDecodeMicros = 0;
    std::uint32_t wallComposeMicros = 0;
    std::uint32_t planeComposeMicros = 0;
    std::uint32_t maskedComposeMicros = 0;
    std::uint32_t wallLookupMicros = 0;
    std::uint32_t planeLookupMicros = 0;
    std::uint32_t maskedLookupMicros = 0;

    // The manifest lookup. A hit is a name the manifest answered and a miss is
    // one it did not; a miss is a reported frame error, never a decode, so
    // `texDecodes`, `flatDecodes`, `spriteDecodes` and `bytesDecoded` stay 0 for
    // graphics.
    std::uint16_t texHits = 0;
    std::uint16_t texMisses = 0;
    std::uint16_t flatHits = 0;
    std::uint16_t flatMisses = 0;
    std::uint16_t spriteHits = 0;
    std::uint16_t spriteMisses = 0;
    std::uint32_t cacheBytes = 0;
    std::uint16_t cachePeakSlots = 0;

    // The working set: how many *distinct* wall textures, flats and sprite lumps
    // this frame asked for, and how many bytes their baked payloads occupy.
    // `cacheBytes` is the whole manifest's payload total and `cachePeakSlots`
    // its entry count, so the line keeps the D3.5 field positions with their
    // D3.8 meaning.
    std::uint16_t texSetCount = 0;
    std::uint32_t texSetBytes = 0;
    std::uint16_t flatSetCount = 0;
    std::uint16_t spriteSetCount = 0;
    std::uint32_t spriteSetBytes = 0;
};


/**
 * @brief The accumulator over measured frames.
 *
 * `frameMin` and `frameMax` are meaningful only when `samples != 0`; both are 0
 * for an empty set, which is also the correct minimum and maximum of nothing.
 * The sums are 64-bit because 40 frames of a few hundred milliseconds each
 * already exceed a 32-bit microsecond count on a slow policy.
 */
struct FrameProfileStats {
    std::uint32_t samples = 0;
    std::uint32_t frameMin = 0;
    std::uint32_t frameMax = 0;
    std::uint64_t frameSum = 0;
    std::uint64_t walkSum = 0;
    std::uint64_t wallsSum = 0;
    std::uint64_t planesSum = 0;
    std::uint64_t maskedSum = 0;
    std::uint64_t residencySum = 0;
    std::uint64_t bytesSum = 0;

    // --- D3.5 attribution ---------------------------------------------------
    std::uint64_t wallPixelsSum = 0;
    std::uint64_t planePixelsSum = 0;
    std::uint64_t spritePixelsSum = 0;
    std::uint64_t pixelsSum = 0;
    std::uint64_t columnsSum = 0;
    std::uint64_t fragmentsSum = 0;
    std::uint64_t spansSum = 0;
    std::uint64_t skyColumnsSum = 0;
    std::uint64_t spriteColumnsSum = 0;
    std::uint64_t decodeSum = 0;
    std::uint64_t composeSum = 0;
    std::uint64_t lookupSum = 0;
    std::uint64_t wallDecodeSum = 0;
    std::uint64_t planeDecodeSum = 0;
    std::uint64_t maskedDecodeSum = 0;
    std::uint64_t wallComposeSum = 0;
    std::uint64_t planeComposeSum = 0;
    std::uint64_t maskedComposeSum = 0;
    std::uint64_t wallLookupSum = 0;
    std::uint64_t planeLookupSum = 0;
    std::uint64_t maskedLookupSum = 0;
    std::uint64_t texHitsSum = 0;
    std::uint64_t texMissesSum = 0;
    std::uint64_t flatHitsSum = 0;
    std::uint64_t flatMissesSum = 0;
    std::uint64_t spriteHitsSum = 0;
    std::uint64_t spriteMissesSum = 0;
    std::uint64_t texSetSum = 0;
    std::uint64_t texSetBytesSum = 0;
    std::uint64_t flatSetSum = 0;
    std::uint64_t spriteSetSum = 0;
    std::uint64_t spriteSetBytesSum = 0;
    /// The largest configured cache seen, and the largest slot peak: both are
    /// capacities the run actually ran with, so a summary carries them.
    std::uint32_t cacheBytesMax = 0;
    std::uint32_t cachePeakMax = 0;
};


/// Receives one complete line, trailing newline included.
using FrameProfileSink = void (*)(const char* line);

/// Zeroes an accumulator. Safe with a null pointer.
void frameProfileReset(FrameProfileStats* stats);

/**
 * @brief Folds one measured frame into the accumulator.
 *
 * Updates the sample count, every sum and both frame extremes. A safe no-op with
 * a null pointer.
 */
void frameProfileRecord(FrameProfileStats* stats, const FrameProfileSample& sample);

/**
 * @brief `sum / samples`, integer, truncated; 0 when nothing was sampled.
 *
 * A mean of nothing is not a number this port prints, and a zero-sample summary
 * must not divide by it.
 */
std::uint32_t frameProfileMean(std::uint64_t sum, std::uint32_t samples);

/**
 * @brief `part` in thousandths of `whole`; 0 when `whole` is 0.
 *
 * This is how `residency_permille` is computed: the decode share of the frame in
 * thousandths, so 731 means 73.1 % of the frame was spent inside decoders.
 */
std::uint32_t frameProfilePermille(std::uint64_t part, std::uint64_t whole);

/**
 * @brief The gate verdict for one mean: `mean <= kFrameGateMicros`.
 *
 * The comparison is inclusive on purpose. 19,999 and 20,000 pass; 20,001 fails.
 * The gate is a budget, not a threshold that has to be beaten by one.
 */
bool frameProfileGatePass(std::uint32_t meanMicros);

/**
 * @brief Writes the marker lump name of one shareware level: "E1M1".."E1M9".
 *
 * D3.10 carries all nine levels in one container, so the board driver must be
 * able to say which one it opened instead of hard-coding E1M1. The name is the
 * one the encoder keeps raw (`--raw-level`), and the only levels the shareware
 * episode has: anything outside 1..9 is refused and @p out is left untouched,
 * so a rejected level cannot be mistaken for a name a container might hold.
 *
 * @param level 1..9.
 * @param out   Receives five bytes: four characters and a NUL.
 * @return true when the level is in range and @p out was written.
 */
bool frameMapMarker(std::uint32_t level, char out[5]);

/**
 * @brief Formats one frame line exactly as the file comment documents it.
 *
 * @param hash 64 hex characters plus NUL, or an empty string when no hash was
 *             taken (the host test passes "").
 * @return The line length excluding the NUL, or 0 when the line does not fit.
 *         A 0 always means truncation: every line ends with a newline.
 */
std::size_t frameProfileFormatFrame(const FrameProfileSample& sample, const char* hash, char* out,
                                    std::size_t capacity);

/// Formats the summary line: means, extremes, permille and the gate verdict.
std::size_t frameProfileFormatSummary(const FrameProfileStats& stats, std::uint32_t views,
                                      std::uint32_t runs, char* out, std::size_t capacity);

/// Formats one per-phase breakdown line for a representative view.
std::size_t frameProfileFormatBreakdown(const FrameProfileSample& sample, char* out,
                                        std::size_t capacity);

/**
 * @brief Formats the D3.5 attribution line for one frame.
 *
 * One line per measured frame, beside the frame line, carrying the denominator
 * (pixels, columns, fragments, spans) and the exclusive time buckets that let
 * a microsecond be assigned: `decode_us` (inflate/copy), `compose_us` (the rest
 * of a composition), `lookup_us` (the container name -> lump scan) and, by
 * subtraction from the pass time, the raster remainder. The cache counters and
 * the frame's distinct working set are on the same line because a hit rate is
 * only meaningful beside the bytes it is trying to hold.
 *
 * @return The line length excluding the NUL, or 0 when it does not fit.
 */
std::size_t frameProfileFormatAttribution(const FrameProfileSample& sample, char* out,
                                          std::size_t capacity);

/**
 * @brief Formats the D3.5 attribution summary: means, per-unit costs and hit rates.
 *
 * The per-unit fields are thousandths of a microsecond (nanoseconds), computed
 * from the raster remainder of the whole measured set: `px_us_permille` divides
 * the wall pass's raster time by its pixels, `col_us_permille` by its columns
 * and `frag_us_permille` by its fragments, so the three answers are directly
 * comparable. `raster_us` is the part of the four passes that no decode,
 * compose or lookup accounted for. A zero denominator reports 0, never a
 * division by it.
 */
std::size_t frameProfileFormatAttributionSummary(const FrameProfileStats& stats, char* out,
                                                 std::size_t capacity);

/**
 * @brief The compile-time gate the probe emits through.
 *
 * `FrameProfileReport<kFrameProbeEnabled>::frame()` is the only way the probe
 * writes a measurement line. With `Enabled == false` the specialization below
 * has an empty body: it touches no argument, formats nothing and calls no sink,
 * so the shipped build carries no formatter call site at all. It is a template
 * rather than a runtime flag so "a disabled build reports nothing" is a property
 * of the compiled code, and so one host test binary can instantiate both forms
 * and observe both.
 */
template <bool Enabled>
struct FrameProfileReport {
    /// Emits one frame line through @p sink; returns 1 when a line was written.
    static std::uint32_t frame(const FrameProfileSample& sample, const char* hash,
                               FrameProfileSink sink) {
        if (sink == nullptr) {
            return 0u;
        }
        char line[kFrameProfileLineCapacity];
        if (frameProfileFormatFrame(sample, hash, line, sizeof line) == 0u) {
            return 0u;
        }
        sink(line);
        return 1u;
    }
};

template <>
struct FrameProfileReport<false> {
    static std::uint32_t frame(const FrameProfileSample&, const char*, FrameProfileSink) {
        return 0u;
    }
};

#if defined(PIXELROOT32_DOOM_FRAME_PROBE)

/**
 * @brief Runs the frame probe once, at boot, and prints the 20-view timing table.
 *
 * Opens the `wad` partition, binds E1M1's renderer working set (a pointer into
 * the mapping in the flash build, decoded blocks in the RAM build) and the three
 * graphics directories, renders the 20 sampled viewpoints three times each (the
 * first render discarded), and prints one `FRAMEPROF,cold,...` line for each
 * discarded first render, one `FRAMEPROF,...frame=...` line per measured frame,
 * one breakdown line per representative view, the summary with the gate verdict,
 * the RAM line and the hash of every frame.
 *
 * @param surface The engine's 8bpp logical framebuffer, written with palette
 *                indices instead of RGB332. NULL, or a buffer smaller than
 *                R_SCREEN_WIDTH x R_VIEW_HEIGHT, prints `surface=missing` and
 *                returns: the probe needs a surface it does not own.
 * @param width   Its stride in bytes.
 * @param height  Its rows.
 *
 * The surface is a parameter rather than something this file fetches itself so
 * the driver carries no dependency on the engine: the scene owns the buffer
 * contract, and the driver only writes bytes into it.
 *
 * Prints `FRAMEPROF,setup,status=fail,...` and returns when the container, the
 * map, a lump or the baked manifest is missing. Nothing here allocates from the
 * heap except, in the RAM build, the decoded lumps; all of it is released before
 * this returns. The baked graphics are pointers into the mapping.
 */
void frameProbeRun(std::uint8_t* surface, int width, int height);

#endif

}  // namespace doomc
}  // namespace doom
