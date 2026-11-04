#pragma once

/*
 * The board frame driver: the level session the shipped scene renders through,
 * and the boot-time frame probe that measures the renderer on the ESP32.
 *
 * Graphics are composed at build time: tools/bake_graphics.py writes every wall
 * texture, the sky and every flat as a raw payload, the PWHD container
 * (lib/doom/whd.h) carries a ~PWBAKE~ manifest, and this driver resolves a packed
 * name through src/doomc/BakedGraphics.h to a pointer into the flash mapping.
 * Nothing is inflated or composed at run time; the only graphics state is the
 * manifest adapter.
 *
 * The probe prints, beside the frame line, one `FRAMEPROF,attr` line per
 * measured frame and one `FRAMEPROF,attrsum` summary. They carry:
 *
 *   pixels / columns / fragments   the wall tier pixels, the wall columns the
 *                                  loop visited (sum of x2-x1+1) and the number
 *                                  of drawsegs, plus the plane pixels and spans,
 *                                  the sky columns and the sprite columns.
 *   decode_us                      inflate/copy only; 0 for graphics, nonzero
 *                                  only for the RAM build's map setup.
 *   compose_us                     the rest of a composition; 0 with baked
 *                                  graphics.
 *   lookup_us                      the manifest lookup; a resolve is a binary
 *                                  search over at most 96 packed names.
 *   wall_/plane_/masked_*_us       the same three buckets per pass.
 *   raster_us                      pass time less those buckets, i.e. the column
 *                                  loop, the span loop and the sprite post walk.
 *   px_us_permille                 wall raster per wall pixel, in thousandths
 *                                  of a microsecond (1,100 = 1.100 us/pixel).
 *
 * The cache counters keep their line positions so existing log parsers still
 * work: a "hit" is a name the manifest answered and a "miss" is one it did not;
 * `cache_bytes` is the total baked payload size and `cache_peak` is the manifest
 * entry count. `tex_dec`, `flat_dec`, `sprite_dec` and `bytes_decoded` are 0 for
 * graphics.
 *
 * THE TWO MAP PATHS
 *
 * The one thing a raw section cannot tell you by itself is what the flash reads
 * cost. So the same driver is built twice:
 *
 *   env:esp32dev_ILI9341_frame      the map is read from the mapping (raw lumps)
 *   env:esp32dev_ILI9341_frameram   PIXELROOT32_DOOM_MAP_IN_RAM=1 decodes the
 *                                   map lumps into RAM
 *
 * Both produce the same pixels, so the frame hashes must agree character for
 * character; the difference is `walk_us` and `frame_us`, and that difference is
 * the answer to "does reading the map from flash cost us a frame". The risk is
 * real: a mapped WAD on the ESP32 board reads about 6.70 MB/s sequentially and
 * takes 4.05 us per random 4-byte read. The map's hot working set (NODES and
 * SEGS, ~15 KB) should fit the flash cache, but a cold first frame must be
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
 * WHAT ONE FRAME COSTS IN RAM
 *
 *   walk state      68,672 B   drawsegs 128 x 52, visplanes 64 x 664, clip posts
 *                              32 x 8, floorclip/ceilingclip 2 x 320 x 2, the
 *                              unified clip pool 3,200 entries x 2, vis sprites
 *                              256 x 44, covered 320
 *   baked records   12,312 B   the 187-entry manifest adapter; the pixels are in
 *                              flash and no buffer copies them
 *   level                0 B   in the flash path: the map is a pointer into the
 *                              mapping. 47,888 B in the RAM path.
 *
 * 80,984 B of driver RAM in the flash path (the census prints it).
 * `FRAMEPROF,mem` prints `map=flash` or `map=ram`, every block it holds, and the
 * constants a run-time texture cache would cost, so the saving from baking is a
 * printed number and not a claim.
 *
 * WHAT FITS
 *
 * Every capacity is a measured peak over every walkable viewpoint of E1M1 and
 * E1M2 (tools/capacity_check.py), not only over the 20 sampled views: 128
 * drawsegs (walkable peak 99), 64 visplanes (peak 51), 3,200 clip-pool entries
 * (peak 2,459), 256 vis sprites (E1M2 220). A drawseg overflow and a pool
 * overflow are both reported walk/pass errors, never silent drops. Allocating
 * exactly what each view needs is what keeps every pixel -- and therefore every
 * hash -- identical to the host's. The surface is not a 64,000 B private buffer:
 * it is the engine's own 8bpp framebuffer.
 *
 * REJECT, BLOCKMAP and THINGS are in the container's raw section but the probes,
 * which own no world, read only THINGS, once per view, to place the sprites. The
 * game frame walks the live mobjs through p_level_first_mobj()/
 * p_level_next_mobj() instead, one submission per mobj, the lump derived from
 * that mobj's state's `sprite` and `frame`; the fixture then says which of those
 * frames the container baked, and an unbakeable one falls back to the type's
 * spawn row rather than refusing the frame. The two lists never run together, so
 * nothing is drawn twice.
 *
 * THE 20 VIEWS
 *
 * The same rule tools/rsegs_check.c uses, so the board's numbers describe the
 * same 20 views the host pixel gate checks:
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
 * reading the map from flash instead of RAM. The pixel gate is host-side; this
 * hash is a sentinel, not a new gate.
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

#include "doomc/LevelFailure.h"

#if DOOM_PERSISTENCE
// The save record's writer/reader, and the status the two entry points below
// return. Including the format here is what keeps the scene from
// ever naming `gWorld`: it asks this driver to archive the live world, not the
// struct behind it.
#include "doomc/SaveGame.h"
#endif

#if defined(DOOM_MENU)
struct m_menu;  // lib/doom/m_menu.h
#endif

namespace doom {
namespace doomc {

#if defined(PIXELROOT32_DOOM_FRAME_PROBE)
/// True only in the build that compiles the board probe.
constexpr bool kFrameProbeEnabled = true;
#else
/// True only in the build that compiles the board probe.
constexpr bool kFrameProbeEnabled = false;
#endif

/// The renderer-CPU gate: 20,000 us of one 35 Hz frame's 28,571 us.
constexpr std::uint32_t kFrameGateMicros = 20000u;

/// Bytes a formatted line can need, including the newline and the NUL. The
/// frame line carries a 64-byte SHA-256 in hex, so it is the longest one.
constexpr std::size_t kFrameProfileLineCapacity = 512;

/// Bytes the attribution line can need. It carries the pixel, column and
/// fragment counts, the exclusive time buckets and the cache counters, so it is
/// longer than the frame line and has its own budget.
constexpr std::size_t kFrameAttributionLineCapacity = 1024;

/// The views whose per-phase breakdown is printed in addition to the table.
///
/// Frames 06 and 16 have the densest drawseg sets (38 and 39 drawsegs), frame 10
/// is sky-heavy, and frame 00 is the spawn view.
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

    // --- attribution -------------------------------------------------------
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
    // its entry count; the field names are kept so the line layout stays stable.
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

    // --- attribution -------------------------------------------------------
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
 * The container carries all nine levels, so the board driver must be able to
 * say which one it opened instead of hard-coding E1M1. The name is the
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
 * @brief Formats the attribution line for one frame.
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
 * @brief Formats the attribution summary: means, per-unit costs and hit rates.
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

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)

/**
 * @brief The level session the shipped scene renders through.
 *
 * The frame probe and the game frame want the same expensive thing: one open
 * container, E1M1's map bound into the walk state, the light table, the sprite
 * list and the four renderer passes. That setup lives behind this API, so there
 * is one implementation of "the level is loaded" and both callers use it:
 *
 *   - `frameProbeRun()` opens it, renders 20 views and closes it;
 *   - the shipped `_show` environment opens it once in `DoomScene::init()`,
 *     ticks the world it owns and renders the player's viewpoint every frame,
 *     and closes it only at shutdown.
 *
 * The session owns no memory of its own beyond the file-scope statics the probe
 * also uses: the walk state, the clip pools and the baked manifest adapter are
 * the same 71 KB of `.bss`, and the mapping stays live until `frameLevelClose()`
 * rather than being released at the end of a boot-time probe.
 *
 * OPEN
 *
 * `frameLevelOpen()` maps the `wad` partition, opens the baked manifest, binds
 * E1M1's map lumps (raw views into the mapping in the flash path), initialises
 * the light table, allocates the sprite list from the heap, initialises the walk
 * state and binds the three resolvers. It prints exactly one
 * `<tag>,setup,status=ok,...` line, or one `<tag>,setup,status=fail,reason=...`
 * line and leaves nothing open. The `tag` is "FRAMEPROF" for the probe and
 * "SHOW" for the game, so a captured log says which caller opened the session.
 *
 * THE WORLD
 *
 * The game frame also owns the level's world: `p_level_init()` over a measured
 * arena, `p_spawn_specials()`, and one `p_level_tic()` per elapsing 35 Hz tic.
 * It is opened by the same call and only for the game: the probe measures a
 * renderer, not a simulation, and `env:esp32dev_ILI9341_frameram` has under 1 KB
 * of `dram0_0_seg` headroom for the level state. Opening prints the arena
 * census -- the zone peak and used bytes, the heap before and after the generous
 * measuring arena, and the size the session chose from the rule "peak plus at
 * most 25 %, rounded up to 1 KB". A world that does not fit is a reported
 * failure: the session closes and the scene keeps its shell, because trimming
 * the renderer to make room would change the proven frame.
 *
 * RENDER
 *
 * `frameLevelRenderView()` renders an arbitrary viewpoint into a caller-owned
 * 8bpp surface as DOOM palette indices. `frameLevelRenderSpawn()` is the same
 * call at the host harness's frame 0 -- the first player-start THING, its
 * sector's floor plus R_VIEWHEIGHT, angle 0, built by the same `buildFrames()`
 * rule the probe and `tools/rsegs_check.py` use -- so its hash is unchanged by
 * construction rather than by a second code path kept in step.
 *
 * Neither converts: colour is the scene's job, because the scene owns the
 * surface contract.
 *
 * When @p wantHash is true it also writes the SHA-256 of the 64,000 index bytes
 * followed by the 320 covered bytes, which is the host's `SPRSTAT ... framehash`
 * for the same viewpoint. It is opt-in because a software SHA-256 over 64 KB
 * costs milliseconds, and a framework that paid it on every frame would report a
 * frame rate nobody gets.
 *
 * @param surface  The 8bpp surface, at least 320x200 bytes.
 * @param width    Its stride in bytes (must be >= R_SCREEN_WIDTH).
 * @param height   Its rows (must be >= R_VIEW_HEIGHT).
 * @param sample   Receives the renderer's own measurements (`frameMicros`,
 *                 `drawsegs` and the pass times); may be NULL.
 * @param hash     65 bytes for the hash and its NUL; untouched when
 *                 @p wantHash is false.
 * @param wantHash Take the SHA-256 of the index surface.
 * @param hashMicros Optional: the time the hash itself took, 0 when not taken.
 * @return false when no session is open or a pass refused the frame.
 */
bool frameLevelOpen(const char* tag, char marker[5]);
bool frameLevelRenderSpawn(std::uint8_t* surface, int width, int height,
                           FrameProfileSample* sample, char hash[65], bool wantHash,
                           std::uint32_t* hashMicros);
void frameLevelClose();

/**
 * @brief One viewpoint in DOOM's units: 16.16 fixed point and a binary angle.
 *
 * The same four numbers `r_view_params_t` takes and `tools/rsegs_check.c`'s
 * `frame_view_t` holds. They are spelled with fixed-width integer types here
 * because this header deliberately includes no DOOM header: the driver must
 * stay compilable in a build that has no `lib/doom` include path.
 */
struct LevelView {
    std::int32_t x = 0;      ///< 16.16 fixed point, world units << 16.
    std::int32_t y = 0;      ///< 16.16 fixed point.
    std::int32_t z = 0;      ///< 16.16 fixed point; the eye height, not the floor.
    std::uint32_t angle = 0; ///< 1/65536 turn; 0 is east, 90 degrees is +y.
};

/**
 * @brief The console player's state, as the frame and the log read it.
 *
 * `view` is what the renderer consumes: `x`/`y` are the avatar's position, `z`
 * is `playerState.viewz` (P_CalcHeight's eye height, including the bob) and
 * `angle` is the avatar's facing. `leveltime` counts the tics the world has run
 * since the level loaded, which is DOOM's own simulation clock and not the
 * scene's frame counter.
 */
struct LevelPlayer {
    LevelView view;            ///< The renderer's inputs.
    std::int32_t health = 0;   ///< playerState.health; 100 at spawn.
    std::uint32_t leveltime = 0; ///< p_level_t::leveltime, the tics simulated.
    bool valid = false;        ///< True when the world is open and has an avatar.
};

/**
 * @brief Renders one explicit viewpoint into a caller-owned index surface.
 *
 * This is the only render entry point the game uses; `frameLevelRenderSpawn()`
 * calls it with the harness's spawn viewpoint. Nothing about the surface, the
 * hash or the measurement changes between the two: the same four passes, the
 * same 8bpp indices, the same SHA-256 over the same 64,320 bytes.
 *
 * @param surface  The 8bpp surface, at least 320x200 bytes.
 * @param width    Its stride in bytes (must be >= R_SCREEN_WIDTH).
 * @param height   Its rows (must be >= R_VIEW_HEIGHT).
 * @param x        Viewpoint x, 16.16 fixed point.
 * @param y        Viewpoint y, 16.16 fixed point.
 * @param z        Eye height, 16.16 fixed point (the floor plus view height).
 * @param angle    Facing, in 1/65536 turn.
 * @param sample   Receives the renderer's measurements; may be NULL.
 * @param hash     65 bytes for the hash and its NUL; untouched when
 *                 @p wantHash is false.
 * @param wantHash Take the SHA-256 of the index surface.
 * @param hashMicros Optional: the time the hash took, 0 when not taken.
 * @return false when no session is open or a pass refused the frame.
 */
bool frameLevelRenderView(std::uint8_t* surface, int width, int height, std::int32_t x,
                          std::int32_t y, std::int32_t z, std::uint32_t angle,
                          FrameProfileSample* sample, char hash[65], bool wantHash,
                          std::uint32_t* hashMicros);

/**
 * @brief Which pass refused the last frame, and with which code.
 *
 * `frameLevelRenderView()` answers only "rendered or not", and a caller that
 * cannot tell a capacity refusal from a closed session replaces a good picture
 * with its fallback screen. This is the second answer: the stage the driver had
 * reached ("walk", "walls", "planes", "sprites" or "masked") and that pass's own
 * error code (`r_bsp_error_t`, `r_segs_error_t`, `r_plane_error_t` or
 * `r_things_error_t`). Both are meaningful only after a render call returned
 * false; before any frame the stage is "none".
 *
 * The strings are static, so the caller may keep the pointer.
 *
 * @param stage Receives the refusing stage; may be NULL.
 * @param code  Receives the error code; may be NULL.
 */
void frameLevelLastRefusal(const char** stage, int* code);

/**
 * @brief The classification of the last failed level open.
 *
 * Every `openLevelSession()` failure branch records the same machine-readable
 * `LevelFailure` beside the line it already prints, so the scene can put the
 * reason on the panel without parsing the serial log. A successful open leaves
 * it `None`; the record is reset at the top of every open.
 *
 * `SceneRenderer::open()` owns the one additional branch the level session
 * cannot see (the missing PLAYPAL) and copies this record into its own member
 * when the frame-level open refuses. The returned reference stays valid for the
 * life of the process.
 *
 * @return The last classified failure; `None` after a successful open or before
 *         the first one.
 */
const LevelFailure& frameLevelFailure();

/**
 * @brief The harness's frame-00 viewpoint, for a caller that wants it explicitly.
 *
 * Computed once from `buildFrames()`'s `frames[0]` and cached, exactly as
 * `frameLevelRenderSpawn()` uses it. Exposed so a verification can render it
 * through `frameLevelRenderView()` and prove the two are the same string.
 *
 * @param out Receives the viewpoint; untouched when the session is closed.
 * @return true when a session is open and @p out was written.
 */
bool frameLevelSpawnView(LevelView* out);

/**
 * @brief Runs @p tics world tics with one command built from @p buttons.
 *
 * This is P_Ticker: `p_level_tic()` per tic, so P_PlayerThink, the thinker walk
 * and `leveltime++` run in upstream's order. The simulation's rate is therefore
 * DOOM's 35 Hz and not the frame rate: a frame that covers two tics calls this
 * with 2, which is what the scene's tic clock reports, and a frame that covers
 * none calls nothing.
 *
 * The command is rebuilt once per call and applied to every tic in it: the
 * scene samples the buttons once per frame, and DOOM's own loop samples input
 * once per tic from a device that updates far slower than 35 Hz. A held button
 * is a level, not an edge, in either case.
 *
 * @param tics    How many tics elapsed since the last call. 0 is a no-op.
 * @param buttons The engine's six bits (`doomc/InputMap.h`).
 * @return false when no world is open or a tic refused to run.
 */
bool frameLevelRunTics(std::uint32_t tics, std::uint8_t buttons);

/**
 * @brief The level the session is on, as a number: 1..9.
 *
 * `gRuntimeLevel` starts at the build's map (`PIXELROOT32_DOOM_FRAME_LEVEL`),
 * moves to the map an exit names, returns to the build's map on a New Game and
 * after the run's victory, and is what a save record stamps and a load opens.
 * Every existing environment therefore opens the map it always did.
 */
std::uint8_t frameLevelNumber();

/**
 * @brief The level the session is on, as its marker ("E1M1").
 *
 * The same source as `frameLevelNumber()` through `frameMapMarker()`, so the two
 * cannot disagree, and the answer is live: a transition moves it. The storage is
 * the driver's one 5-byte buffer; a caller that keeps the string past the next
 * call copies it.
 */
const char* frameLevelMarker();

/**
 * @brief Takes "the run ended" once.
 *
 * E1M8's ending is sector special 11, and this port has no finale screen, so the
 * transition loads the run's start and answers
 * true here exactly once. The scene opens the main menu on that frame; a caller
 * that ignores it loses the report, not the world.
 */
bool frameLevelTakeVictory();

#if defined(DOOM_LEVEL_TEST)
/**
 * @brief Diagnostic (env `esp32dev_ILI9341_hud_leveltest`): stand on E1M1's exit.
 *
 * Finds the level's own line special 11 (the exit switch) and puts the avatar on
 * the front sector's side facing it, so the next USE press travels
 * P_PlayerThink -> p_use_lines -> P_UseSpecialLine. Nothing here triggers the
 * line, and the move is printed (`SHOW,level,script=stand,...`). Never in the
 * shipped image.
 */
bool frameLevelExitScript(int special);

#if defined(DOOM_LEVEL_WALK)
/**
 * @brief Diagnostic (env `esp32dev_ILI9341_hud_levelwalk`): walk every level.
 *
 * The scene calls `frameLevelExitScript(11)` on each level and 51 on E1M3, holds
 * USE through the real button byte, and calls `frameLevelVictoryScript()` on
 * E1M8, so the panel shows E1M1 -> E1M2 -> E1M3 -> E1M9 -> E1M4 -> ... -> E1M8
 * and the ending. Frame-paced on purpose: it is a viewing aid, not a timing
 * measurement. Never in the shipped image.
 */
#endif

#if DOOM_PERSISTENCE
/**
 * @brief Diagnostic: the E1M8 ending, reached from a map the proof can load.
 *
 * Loads E1M8 at the running skill and raises the request sector special 11
 * raises, so the next tic's transition takes the shipped victory branch (the run
 * reloads on its first map and the scene opens the menu). It also proves E1M8
 * fits the arena the session sized from its startup map. Never in the shipped
 * image.
 */
bool frameLevelVictoryScript();
#endif
#endif

#if defined(DOOM_MENU)
/**
 * @brief The menu's New Game: reloads the build's map at @p skill.
 *
 * `rules.skill` takes @p skill, the random position is cleared (G_InitNew's
 * M_ClearRandom) and `g_demo_reborn()` re-lays the build's map over the world's
 * own arena with every THING run again, the status bar restarted. A run always
 * starts on `kFrameLevel`, whatever level the previous run ended on, so
 * this also resets the runtime level number. The arena is sized at nightmare
 * under `DOOM_MENU`, so every skill fits (`tools/menu_check.py --arena`).
 *
 * @param skill 0 (baby) .. 4 (nightmare).
 * @return true when the new game is running. false means the reload was refused:
 *         the level was laid out again at the default skill, or, if that failed
 *         too, the world is closed and the frame keeps its last picture.
 */
bool frameLevelNewGame(int skill);

/**
 * @brief The menu the frame draws over everything else, or NULL.
 *
 * The driver keeps the pointer (the scene owns the struct). A closed menu draws
 * nothing and costs one comparison per frame.
 */
void frameLevelSetMenu(const m_menu* menu);

/**
 * @brief The world arena's state, for the menu's serial line: bytes in use, bytes
 *        free, allocations the zone refused since the last load, live mobjs.
 *
 * The refusal counter is the proof New Game fits: a skill whose THINGS or whose
 * play overflowed the arena shows up as a non-zero count here.
 *
 * @return false when no world is open (every output is 0).
 */
bool frameLevelArenaStats(std::uint32_t* inUse, std::uint32_t* freeBytes,
                          std::uint32_t* failures, std::uint32_t* mobjs);

/// The name of the last picture a resolver could not find ("" if none), for the
/// refusal diagnostic.
const char* frameLevelLastMissing();
#endif

#if defined(PIXELROOT32_DOOM_SHOW_E1M1) && DOOM_PERSISTENCE
/**
 * @brief Archives the live world into @p out.
 *
 * The scene never sees `gWorld`; this is the one seam that turns it into the
 * engine's bytes. The record's map number is `frameLevelNumber()` -- the
 * level the world is actually on -- because a `p_level_t` does not carry its
 * number and a save written in E1M2 has to load back into E1M2.
 *
 * @param out      Destination; the caller owns it (the persistence layer's
 *                 payload buffer, not the stack).
 * @param capacity Bytes available at @p out.
 * @param written  Out: bytes written on Ok; untouched on failure.
 * @return Ok, TooLarge, BadField, or WrongLevel when no world is open.
 */
SaveStatus frameLevelSave(std::uint8_t* out, std::size_t capacity, std::size_t& written);

/**
 * @brief Opens @p levelNumber at @p skill, then applies @p in.
 *
 * The reload opens the level the *record* names -- not whatever map the
 * world is on -- through the same path a New Game drives (`g_demo_reborn()`:
 * the rules' skill, a cleared random position, `p_level_init()` +
 * `p_spawn_specials()` over the world's own arena). There is no second loader,
 * and a restore is a reborn: the player's inventory comes from the record, never
 * from a carried transition. `saveApply()` then overwrites only the mutable
 * state, which is why the record is applied to a *fresh* world.
 *
 * If the reload is refused the function answers `ApplyFailed` and the world is
 * whatever `frameLevelNewGame()` leaves on its own refusal path. If the apply
 * fails, the world is laid out fresh again before returning, so a caller never
 * sees a half-restored level; any non-Ok means "the world is a fresh copy of the
 * map", never "the world is damaged".
 *
 * @param in          The record's payload (already envelope-checked).
 * @param size        Bytes at @p in.
 * @param levelNumber The map the record names (1 for E1M1).
 * @param skill       0 (baby) .. 4 (nightmare), the record's skill.
 * @return Ok or the save layer's status.
 */
SaveStatus frameLevelLoad(const std::uint8_t* in, std::size_t size,
                          std::uint8_t levelNumber, std::uint8_t skill);
#endif

#if defined(DOOM_HUD)
/**
 * @brief The automap's state and controls.
 *
 * `lib/doom/am_map.c` is upstream's `am_map.c`; the session owns its state
 * (`am_state_t`) and its shadow bitmap, and these five calls are the whole seam.
 * They exist only in a HUD build, because the automap fills the 168 rows above
 * the status bar and there is no 168-row window without `DOOM_HUD`.
 *
 * The map is drawn by the one render path, not by these calls: while it is
 * active, `frameLevelRenderView()` paints the map into rows 0..167 instead of the
 * world, then the bar and the menu, and reports the frame as usual. It also
 * prints the map's own line once per reported frame:
 *
 *   SHOW,map,us=<map microseconds>,active=1,follow=<0|1>,scale=<scale_mtof>,
 *   grid=<0|1>,hash=<SHA-256 of the 320x168 window>
 *
 * The hash is the window alone (no coverage record: the map clears every pixel it
 * owns), which is the number `tools/am_check.py` prints for the same state.
 *
 * @return `frameLevelMapActive()`: true while the map is up.
 */
bool frameLevelMapActive();

/**
 * @brief AM_Start(): opens the map centred on the player.
 *
 * Nothing happens when no world is open, the level has no avatar, or the
 * session could not allocate the shadow bitmap at level load (the failure is
 * already on the `SHOW,map,init` line). The follow flag is upstream's: it is
 * kept across a stop/start, so reopening the map resumes the last pan. The
 * arrow's facing is taken from the avatar here as well as per tic, because a map
 * opened between two tics would otherwise draw the arrow pointing east.
 */
bool frameLevelMapStart();

/// AM_Stop(): the map goes down and the world frame comes back.
void frameLevelMapStop();

/**
 * @brief The four d-pad directions, applied once per frame.
 *
 * @param bits Bit 0 up, 1 down, 2 left, 3 right -- the engine's `InputMap.h`
 *             order, which is `am_map.h`'s `AM_PAN_*`. A pan cancels the follow
 *             (the board has no 'f' key) and the window stops at the level's
 *             edges.
 * @return true while the map is up; a stopped map ignores the call.
 */
bool frameLevelMapPan(std::uint8_t bits);

/// The automap's zoom directions, `am_map.h`'s `AM_ZOOM_*`.
constexpr int kMapZoomRelease = 0;
constexpr int kMapZoomIn = 1;
constexpr int kMapZoomOut = -1;

/**
 * @brief The '=' / '-' keys: one zoom step per tic while held.
 *
 * @param direction kMapZoomIn, kMapZoomOut or kMapZoomRelease.
 * @return true while the map is up.
 */
bool frameLevelMapZoom(int direction);

/**
 * @brief Clears the automap's mapped bits.
 *
 * Two callers. The boot frame-00 verification renders the harness's viewpoint,
 * which is not the player's (it faces east while the player faces north), and its
 * wall pass would otherwise leave six lines marked that the player has never
 * seen. And a level reload starts with every line unseen, which is upstream's
 * P_SetupLevel re-reading the linedefs' flags. The window and the map's up/down
 * state are not touched; the bitmap's size never changes after `SHOW,map,init`.
 */
void frameLevelMapForget();
#endif  // DOOM_HUD

/**
 * @brief Fills @p out with the console player's state.
 *
 * @param out Receives the state; @c valid is false when no world is open or the
 *            level has no player start.
 * @return the same @c valid flag.
 */
bool frameLevelPlayer(LevelPlayer* out);

/**
 * @brief Bytes the loop task's stack still had at its deepest point.
 *
 * The firing path is the deepest call chain the game has, so its stack margin
 * is printed on the `SHOW,frame` line, where a stack overflow can be ruled in or
 * out without a debugger.
 */
std::uint32_t frameStackFree();

#if defined(DOOM_FRAME_DUMP)
/**
 * @brief Diagnostic (env `esp32dev_ILI9341_hud_dump`): writes one frame to the
 *        console as run-length pairs.
 *
 * A refused frame, a wrong palette page and a bad colormap row all look like
 * "glitches" in a photograph, and the host renderer cannot reproduce a
 * board-only one. This paints nothing and decides nothing: it copies the bytes
 * the panel is about to show so the same frame can be rebuilt and compared on
 * the host, one pixel at a time.
 *
 * The protocol is `DUMP,begin,...`, then `DUMP,runs=N`, then one
 * `DUMP,r,<count><value>` pair per two hex bytes (count 1..255, value 00..FF),
 * and `DUMP,end`. Never compiled into a shipped image.
 *
 * @param pixels The 8bpp surface, @p width by @p height, row-major.
 * @param frame  The console's frame counter, so the dump names its own frame.
 */
void frameDumpSurface(const std::uint8_t* pixels, int width, int height, std::uint32_t frame);

/**
 * @brief Diagnostic: the level's live sector table, one `SHOW,s` line per sector.
 *
 * A wall drawn with a wrong `ceilingHeight` stretches its texture into a fan over
 * everything else, and the mutable sector record is the only per-level data the
 * renderer reads from the arena. Printing the table lets the host compare it
 * against the WAD's immutable SECTORS and name the corrupted entry.
 */
void frameDumpSectors();

/**
 * @brief Diagnostic: the frame's per-record state, one `VD,...` line each.
 *
 * The pixel dump says the frame is wrong; this says *which record and which
 * field*. The drawing passes' inputs are the drawsegs the walk stored (with the
 * three scales, the masked texture's column block and the two clip codes the
 * masked pass reads back), the visplanes the walk allocated (height, picture,
 * light, and the extrema of the recorded columns) and the submitted sprite
 * list. `r_view_diag_*` formats them (lib/doom/r_view_diag.h) and
 * `tools/viewdiag_compare.py` diffs a capture against the host's render of the
 * same viewpoint -- `rsegs_check --view ... --viewdiag` prints the same lines.
 *
 * Prints the LAST COMPLETED frame, so it belongs in the same press handler as
 * frameDumpSurface(): the records are the ones that frame was drawn from.
 * Never compiled into a shipped image.
 */
void frameDumpViewDiag();
#endif

/**
 * @brief Palette 0 of the WAD's PLAYPAL lump, 768 bytes, or NULL before OPEN.
 *
 * The session decodes the lump out of the container once. The caller turns it
 * into the 256-entry index -> RGB332 conversion table
 * (`palette::initFromPlaypal()`); the session itself does not know the palette
 * module, so the colour contract stays in one place.
 */
const std::uint8_t* frameLevelPlaypal();

/**
 * @brief ST_doPaletteStuff's choice: which PLAYPAL page the screen shows.
 *
 * `palflash_index()` over the open world's player counters; 0 when no world is
 * open. The scene calls it once per rendered frame and re-applies the palette
 * only when the answer (or the gamma) changes.
 */
int frameLevelPaletteIndex();

/**
 * @brief Copies PLAYPAL page @p page (0..13) into @p out (768 bytes).
 *
 * Page 0 is resident (768 B, filled once at open). The other pages are a
 * mapped view of the container's raw PLAYPAL lump: page @p p is
 * `view + p * 768`, copied into @p out with no heap block and no decode: a
 * 10,752 B transient allocation for all 14 pages cannot be served beside the
 * save buffer. Returns false, leaving @p out untouched, when the page is out of
 * range, the lump is absent or short, or the container stores PLAYPAL deflated
 * (`DOOM_WHD_SOURCE_ERR_NOT_RAW`). There is no allocation fallback on that last
 * path; the caller keeps the page it has.
 */
bool frameLevelPlaypalPage(int page, std::uint8_t* out);

#if defined(DOOM_PALFLASH_SCRIPT)
/// Diagnostic build only: plays one scripted event into the real player state
/// (0 damage 40, 1 health-bonus pickup, 2 damage 15, 3 radiation-suit pickup).
bool frameLevelPalflashScript(int step);
#endif

/**
 * @brief Formats one `SHOW,mem,...` line for the open session.
 *
 * The same numbers `FRAMEPROF,mem` prints -- `sizeof` of every static block, the
 * heap before the level was bound, the decoded level bytes (0 in the flash
 * path) -- under the tag the game's log uses. The caller owns the buffer.
 *
 * @return The line length excluding the NUL, or 0 when it does not fit.
 */
std::size_t frameLevelFormatMemory(char* out, std::size_t capacity);

#endif

}  // namespace doomc
}  // namespace doom
