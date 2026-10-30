#pragma once

/*
 * D3.6: the board frame driver, and the per-phase timing table it produces.
 *
 * THE SHORT ANSWER, BECAUSE IT CHANGES HOW THIS FILE SHOULD BE READ
 *
 * On this board the naive frame driver does not fit. Not "fits but is slow": the
 * buffers a *correct* unbuffered frame needs are 142,368 B, and the two pools that
 * can hold them are a 94,916 B static region plus a heap whose largest single free
 * block is 47,092 B -- 360 B short of the minimum configuration, before a single
 * cache exists. The probe measures the pools, prints the shortfall with the
 * numbers that decide it, and stops. That is D3.5's real starting point: a cache
 * is not an optimisation here, it is the only way the frame exists at all.
 * See "WHAT THE PROBE MEASURED" below for the arithmetic.
 *
 * WHY THIS RUNS BEFORE D3.5
 *
 * D3.5 is a texture/flat cache with an A/B measurement. An A/B needs a baseline
 * measured on the target, and the baseline cannot be reconstructed afterwards:
 * once a cache exists, the "no cache" number is a guess. So this file is the
 * deliberately unbuffered driver -- what one view costs when every texture is
 * composed the moment it is asked for -- and it is the number D3.5 has to beat.
 *
 * THE RESIDENCY POLICY, IN ONE SENTENCE
 *
 * Nothing is remembered beyond what the current call holds: a wall texture is
 * composed on every miss into one of two slots (a two-sided fragment holds its
 * upper and lower tier at once, so one slot would be *wrong*, not merely slow),
 * every patch a composite needs is decoded into one scratch buffer, and a flat
 * shares that scratch -- so a frame re-decodes the same texture once per fragment
 * that asks for it.
 *
 * THE FRAME, IN THE ORDER THE RENDERER DEFINES
 *
 *   walk      r_bsp_render()    the BSP walk: drawsegs and plane marks
 *   walls     r_wall_emit()     the three wall tiers, the clip snapshot, the
 *                               masked texture columns
 *   planes    r_plane_draw()    flats and the sky
 *   masked    r_masked_emit()   sprites and the deferred masked mid textures
 *
 * `walk` and `walls` are measured. `planes` and `masked` are not: their buffers
 * (a 35,080 B sky-patch scratch and 9,168 B of sprite pools) are exactly what the
 * board cannot hold on top of the level and the two wall slots. The brief's rule
 * for that case is to measure fewer phases rather than something else, and the two
 * expensive ones are the ones kept. The consequence for the hash is a gain, not a
 * loss: the host's `frame NN:` wall hash is the pixels and coverage after its wall
 * pass, which is precisely what this build produces, so the two are comparable
 * byte for byte.
 *
 * Each pass is timed with `esp_timer_get_time()` around the call alone. `residency`
 * is the part of the measured passes spent *inside a decoder* -- a container lump
 * decode or a texture compose -- reported separately so raster time and decode time
 * cannot be confused, and accumulated only for the outermost decode so a compose
 * that decodes its patches does not count their time twice. `frame_us` is the whole
 * per-view sequence -- surface prefill and the two passes -- so
 * `frame_us - (walk + walls)` is work no pass accounts for, and the summary prints
 * that remainder as `other_us` instead of hiding it inside a phase.
 *
 * WHAT IS DELIBERATELY NOT IN THE FRAME
 *
 * The palette index -> RGB332 conversion and the present. This probe measures
 * renderer CPU and nothing else: the pixels are DOOM palette indices written
 * straight into the engine's 8bpp surface, which is *visually wrong* and
 * timing-correct, and the `FRAMEPROF,mem` line says so (`index_writes=1`,
 * `present=no`, `no_rgb332=1`) so nobody reads a screenshot as a result.
 *
 * WHAT THE PROBE MEASURED, AND WHY THE FRAME DOES NOT RUN
 *
 * Every number below is printed by the probe on the board; none is estimated.
 *
 *   dram0_0_seg   124,068 B (0x3ffb0000 + 0xdb5c, len 0x2c200 - 0xdb5c). The
 *                 shipped image uses ~29 KB, so ~94,916 B is free for this file.
 *   the heap      134,924 B free with the display path live, in holes whose
 *                 largest single block is 47,092 B. Shrinking this file's .bss
 *                 does not enlarge that block: it is a fixed set of pieces left
 *                 by the display path's 64,000 B sprite and its line buffers.
 *
 * and what a correct frame needs:
 *
 *   walk state    28,144 B  drawsegs 2,080 (40 x 52, peak 39), visplanes 14,608
 *                           (22 x 664, peak 22), clip posts 1,536, clip-block
 *                           pool 9,600 (15 x 320 shorts, peak 15), coverage 320
 *   level         59,816 B  E1M1's seven renderer lumps 46,508, TEXTURE1 subset
 *                           1,796, PNAMES 2,804, COLORMAP 8,704
 *   graphics      54,408 B  wall slots 36,864 (measured peak pair: 32,768 + 4,096)
 *                           + decode scratch 17,544 (the largest patch of the 13
 *                           wall textures the 20 views resolve)
 *
 * The graphics block must be one contiguous region (a compose reads a patch and
 * writes a slot at the same time), it is larger than the largest heap block by
 * 7,316 B, and the static region cannot take up the difference by 360 B once the
 * walk state and the level are there. Both halves were tried in every order the
 * heap allows; the probe's `FRAMEPROF,shortfall` line is that failure written down
 * with the numbers in it, and it is a result.
 *
 * WHAT FITS, AND WHAT WAS NOT DONE TO MAKE IT FIT
 *
 * Every capacity is the measured peak: 40 drawsegs (peak 39), 22 visplanes (peak
 * 22), 15 clip blocks (peak 15). The visplane count is not 64 as the plan assumes,
 * because 64 x 664 B is 42,496 B and the driver cannot be built at all with it; 22
 * allocates exactly what the host harness allocated for every one of the 20 views,
 * so no pixel changes. The surface is not a 64,000 B private buffer: it is the
 * engine's own 8bpp framebuffer. REJECT, BLOCKMAP and THINGS are not resident --
 * the renderer never reads the first two and THINGS is read once at setup.
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
 * not a baseline anyone can compare against.
 *
 * THE HASH, AND WHAT IT IS FOR
 *
 * After each measured frame the driver prints `hash=`, the SHA-256 of the 64,000
 * index bytes followed by the 320 covered bytes -- exactly the host harness's
 * `frame NN:` wall hash (tools/rsegs_check.c documents the protocol in both
 * places). It exists to catch a board/host divergence in the data path: a texture
 * decoded differently, a clip array off by one. The pixel gate is host-side and
 * already proven; this hash is a sentinel, not a new gate. Frame numbering is the
 * host's, so `frame=NN` on the board and `frame NN:` on the host are the same
 * viewpoint.
 *
 * THE COMPILE-TIME GATE
 *
 * `kFrameProbeEnabled` is true only when the build defines
 * `PIXELROOT32_DOOM_FRAME_PROBE` (`env:esp32dev_ILI9341_frame`). Every static
 * buffer, every resolver and every `esp_timer_get_time()` call lives inside that
 * `#if`, so a shipped build carries none of them. The arithmetic below -- the
 * accumulator, the three formatters, the gate -- is always compiled because a
 * host test drives it, and `-ffunction-sections` plus `--gc-sections` drops it
 * from a build that never calls it; `FrameProfileReport<false>` is the gate the
 * probe emits through and it writes nothing at all.
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
 * @brief Runs D3.6's probe once, at boot, and prints the 20-view timing table.
 *
 * Opens the `wad` partition, decodes E1M1's renderer working set and the three
 * graphics directories into one static arena, renders the 20 sampled viewpoints
 * three times each (the first render discarded), and prints one `FRAMEPROF,...`
 * line per measured frame, one breakdown line per representative view, the
 * summary with the gate verdict, the RAM line and the hash of every frame.
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
 * map or a lump is missing. Nothing here allocates.
 */
void frameProbeRun(std::uint8_t* surface, int width, int height);

#endif

}  // namespace doomc
}  // namespace doom
