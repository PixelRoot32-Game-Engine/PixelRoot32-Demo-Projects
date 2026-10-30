#pragma once

/*
 * D3.7: the board frame driver, now that a level's lumps no longer cost DRAM.
 *
 * WHAT CHANGED SINCE D3.6, IN ONE PARAGRAPH
 *
 * D3.6 measured the wall and could not finish the frame: a correct four-phase
 * view needed 169,168 B and the image had ~141,992 B for it, so `planes` and
 * `masked` were absent. D3.7 moves the level's data into a raw, page-mappable
 * section of the PWHD container (lib/doom/whd.h, method 2), and the driver reads
 * E1M1's map and the three graphics directories in place with
 * doom_whd_source_lump_view(): 46,508 B of map and 20,742 B of COLORMAP,
 * TEXTURE1 and PNAMES stop being decoded into RAM. All four phases now run --
 * walk, walls, planes and masked -- and the probe measures each one.
 *
 * THE TWO MAP PATHS, AND WHY THERE ARE TWO
 *
 * D3.5: WHAT THE FRAME COSTS, AND WHERE IT GOES
 *
 * The D3.6/D3.7 driver was the naive one: two texture slots, "nothing beyond
 * the current call", and a resolver that re-composed a texture for every
 * fragment that asked. D3.5 replaces that policy with one caller-owned residency
 * cache and instruments the frame so a microsecond can be assigned to a unit.
 *
 * The probe now prints, beside the frame line, one `FRAMEPROF,attr` line per
 * measured frame and one `FRAMEPROF,attrsum` summary. They carry:
 *
 *   pixels / columns / fragments   the wall tier pixels, the wall columns the
 *                                  loop visited (sum of x2-x1+1) and the number
 *                                  of drawsegs, plus the plane pixels and spans,
 *                                  the sky columns and the sprite columns.
 *   decode_us                      inflate/copy only, summed at the leaf.
 *   compose_us                     the rest of a composition: validate, blit and
 *                                  the in-place 128-row column expansion.
 *   lookup_us                      the container's name -> lump scan and the
 *                                  TEXTURE1 directory scan.
 *   wall_/plane_/masked_*_us       the same three buckets per pass.
 *   raster_us                      pass time less those buckets, i.e. the column
 *                                  loop, the span loop and the sprite post walk.
 *   px_us_permille                 wall raster per wall pixel, in thousandths
 *                                  of a microsecond (1,100 = 1.100 us/pixel).
 *
 * The cache: one arena per pool, first fit, 512 B granules, LRU eviction, and a
 * pin ring the renderer scopes with `r_tex_source_t::beginFragment`. Arena 0 is
 * the static 52 KB block; up to three 8 KB heap blocks follow. Capacity, hit
 * counts and the peak live entries are on the same line, so a hit rate is always
 * printed beside the bytes it is trying to hold.
 *
 * THE TWO MAP PATHS, AND WHY THERE ARE TWO (unchanged)
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
 *   walk state   33,184 B  drawsegs 40 x 52, visplanes 22 x 664, clip posts
 *                          32 x 8, floorclip/ceilingclip 2 x 320 x 2, clip
 *                          blocks 15 x 320 x 2, opening blocks 1 x 320 x 2,
 *                          vis sprites 100 x 44, covered 320
 *   graphics     71,944 B  wall arena 36,864 (32,768 + 4,096, the measured peak
 *                          pair) + scratch 35,080 (SKY1's largest patch, now
 *                          that the plane pass runs)
 *   level            0 B   in the flash path: the map and the three directories
 *                          are pointers into the mapping. 47,888 B in the RAM
 *                          path (46,508 map + THINGS 1,380; the directories are
 *                          read-only tables and stay raw in both).
 *
 * 105,128 B of driver RAM in the flash path against the 94,916 B of free
 * `dram0_0_seg` plus the heap: the walk state and the wall arena are .bss
 * (70,048 B) and the scratch comes from the heap (35,080 B, inside the 47,092 B
 * largest hole). The 23,200 B static lump arena D3.6 needed is gone from the
 * flash build; `FRAMEPROF,mem` prints `map=flash` or `map=ram` and both halves of
 * the accounting, so the split is a printed number and not a claim.
 *
 * WHAT FITS, AND WHAT WAS NOT DONE TO MAKE IT FIT
 *
 * Every capacity is the measured peak: 40 drawsegs (peak 39), 22 visplanes (peak
 * 22), 15 clip blocks (peak 15), 100 vis sprites (peak 97), one opening block
 * (peak 0). The visplane count is not 64 as the plan assumes, because 64 x 664 B
 * is 42,496 B and the driver cannot be built at all with it; 22 allocates exactly
 * what the host harness allocated for every one of the 20 views, so no pixel
 * changes. The surface is not a 64,000 B private buffer: it is the engine's own
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

    // The residency cache. A hit is a resolve the cache answered without
    // touching the container; a miss is one that had to decode.
    std::uint16_t texHits = 0;
    std::uint16_t texMisses = 0;
    std::uint16_t flatHits = 0;
    std::uint16_t flatMisses = 0;
    std::uint16_t spriteHits = 0;
    std::uint16_t spriteMisses = 0;
    std::uint32_t cacheBytes = 0;
    std::uint16_t cachePeakSlots = 0;

    // The working set: how many *distinct* wall textures, flats and sprite
    // lumps this frame asked for, and how many bytes their composed forms
    // occupy. That is the number a residency cache has to hold to make the
    // frame's second run free.
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
 * map, a lump or an arena block is missing. Nothing here allocates from the heap
 * except the graphics scratch and, in the RAM build, the decoded lumps; all of it
 * is released before this returns.
 */
void frameProbeRun(std::uint8_t* surface, int width, int height);

#endif

}  // namespace doomc
}  // namespace doom
