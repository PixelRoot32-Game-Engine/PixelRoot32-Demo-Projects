#pragma once

/*
 * D2.5: what reading a level's working set out of mapped flash actually costs.
 *
 * THE QUESTION, AND WHY IT IS SHAPED THIS WAY
 *
 * The port never streams pixels out of flash. A lump is decoded whole into RAM
 * (`whd_read_lump`) and the renderer works from RAM afterwards. So the number
 * that decides how Phase 3 is built is not a per-byte cache-miss rate: it is
 *
 *   (a) what one cold decode of E1M1's working set costs,
 *   (b) what the identical sequence costs when repeated immediately (warm),
 *   (c) the ratio between the two.
 *
 * (b) is the control and (c) is the answer to "does the zone cache per lump, or
 * does the port reload per frame": a warm repeat that is nearly as slow as the
 * cold pass means the bytes are being decoded again and no cache helps; a warm
 * repeat that is one or two orders of magnitude cheaper means the cost was the
 * flash read, and a per-lump cache is worth its RAM.
 *
 * WHAT COUNTS AS ONE READ
 *
 * Exactly one `doom_whd_source_read_lump()` call, timed around the call alone:
 * the two `esp_timer_get_time()` samples bracket the read and nothing else.
 * Name resolution (`whd_lump_index_for_name`), the DEFLATE-free parsing this
 * file does between reads, and `r_data` validation all happen outside the timed
 * region, so a "read" here is decode-into-a-buffer and never "find the lump".
 * That is deliberate: a level loader resolves its names once and then reads the
 * working set, and it is the reads that repeat per frame.
 *
 * THE TIMING PRIMITIVE, AND WHAT IT CANNOT SAY
 *
 * `esp_timer_get_time()` -- the ESP-IDF microsecond counter, integer, 64-bit and
 * monotonic for the lifetime of a boot. Resolution is 1 us, so a read that
 * costs less than a microsecond records 0, and every recorded value includes
 * the cost of the closing `esp_timer_get_time()` call (a fraction of a
 * microsecond). Per-lump numbers below ~2 us are therefore indistinguishable
 * from timer noise even though they are not zero. The total is not affected by
 * that in any way that matters: E1M1's working set is ~491 KB, and the cold
 * total lands in the tens of milliseconds, three orders of magnitude above the
 * noise floor. This instrument cannot attribute time inside one decode
 * (directory seek, payload fetch, inflate) and does not try.
 *
 * THE RECORD SET IS BOUNDED ON PURPOSE
 *
 * There is no allocation anywhere here. Each read is appended to a fixed array
 * of `kFlashProfileRecordCapacity` records (lump index, decoded bytes, elapsed
 * microseconds) and folded into an accumulator; reads beyond the capacity are
 * counted in `dropped` instead of being stored. The accumulators are exact
 * whether or not a record fit, so a dropped record loses the per-lump detail
 * and never the summary. The `slowest_*` field of the summary is derived from
 * the records, so on a working set that exceeds the capacity it is the slowest
 * of the *recorded* reads; the summary line says how many that was.
 *
 * THE DISABLED BUILD
 *
 * `kFlashProfileEnabled` is true only when the build defines
 * `PIXELROOT32_DOOM_FLASH_PROFILE` (`env:esp32dev_ILI9341_flashprof`). The probe
 * itself -- the 68,168 B static decode buffer, the plan, the accumulators and
 * every `esp_timer_get_time()` call -- lives inside that `#if`, so a shipped
 * build carries none of it. The arithmetic below is always compiled because a
 * host test drives it, and `-ffunction-sections` plus `--gc-sections` drops it
 * from a build that never calls it; `FlashProfileReport<false>` is the gate the
 * probe uses and it emits nothing at all.
 *
 * THE LINE FORMATS
 *
 * One line per pass, then one line per phase of that pass:
 *
 *   WADPROF,pass=<cold|warm>,phase=<all|map|texdir|patches|flats>,
 *           total_us=<u>,lumps=<u>,bytes=<u>,mbps=<u>.<u>,min_us=<u>,max_us=<u>,
 *           gt1ms=<u>,gt4ms=<u>,slowest_lump=<d>,slowest_us=<u>,records=<u>,
 *           dropped=<u>,failed=<u>
 *
 *   `mbps` is mebibytes per second in tenths, so `mbps=1.0` is 1.0 MiB/s:
 *   bytes * 10 * 1e6 / (micros * 1,048,576), integer only.
 *   `gt1ms` counts reads strictly above 1,000 us and `gt4ms` strictly above
 *   4,000 us, so a read of exactly 1 ms is in neither bucket.
 *   `failed` counts reads the container refused (an index that vanished, a
 *   payload that did not decode); a failed read still contributes its elapsed
 *   time, contributes zero bytes, and is recorded like any other.
 *
 *   WADPROF,ratio,cold_us=<u>,warm_us=<u>,warm_permille=<u>
 *
 *   `warm_permille` is the warm total in thousandths of the cold total, so 1000
 *   means the warm repeat was as expensive as the cold one and 50 means it cost
 *   a twentieth as much.
 *
 *   WADPROF,budget,frame_us=<u>,cold_us=<u>,pct_x10=<u>
 *
 *   The load-bearing line: `pct_x10` is the cold total in tenths of one percent
 *   of a single 35 Hz frame (28,570 us), so 432 means a per-frame reload of the
 *   working set would cost 43.2% of the frame budget. Nothing here argues from
 *   intuition, and neither should anyone reading it.
 *
 * Two bookkeeping lines frame those passes. `WADPROF,probe=open,...` states the
 * container (lump count, length, mapped bytes), prints `buf=68168` -- the static
 * decode buffer, which is this build's RAM cost -- and the frame budget the
 * probe measures against. `WADPROF,workingset,...` states what the plan holds
 * (lumps per phase, distinct textures, patch references) and what discovery
 * could not resolve, so a working set that quietly shrank is visible instead of
 * flattering the numbers.
 *
 * The probe writes these through `doomc::writeLine()`, the same console every
 * other boot line uses; the host test writes them through a capture sink.
 */

#include <cstddef>
#include <cstdint>

namespace doom {
namespace doomc {

#if defined(PIXELROOT32_DOOM_FLASH_PROFILE)
/// True only in the build that compiles the probe.
constexpr bool kFlashProfileEnabled = true;
#else
/// True only in the build that compiles the probe.
constexpr bool kFlashProfileEnabled = false;
#endif

/// Per-read records the accumulator can hold before it starts counting drops.
constexpr int kFlashProfileRecordCapacity = 128;

/// Histogram bins: the first six close at the edges below, the last is open.
constexpr int kFlashProfileHistogramBins = 7;

/// Upper bound of each closed bin, in microseconds. The final entry is the
/// open-ended bin and its value is never compared.
constexpr std::uint32_t kFlashProfileBinUpperMicros[kFlashProfileHistogramBins] = {
    10u, 50u, 100u, 250u, 1000u, 4000u, 0u,
};

/// Bytes a formatted line can need, including the newline and the NUL.
constexpr std::size_t kFlashProfileLineCapacity = 320;

/// One recorded read: which lump, how many bytes it decoded to, how long it took.
struct FlashProfileRead {
    std::int32_t lump;
    std::uint32_t bytes;
    std::uint32_t micros;
};

/**
 * @brief The scalar half of a pass: exact over every read, recorded or dropped.
 *
 * `minMicros` and `maxMicros` are meaningful only when `reads != 0`; both are 0
 * for an empty set, which is also the correct minimum and maximum of nothing.
 *
 * `failed` is incremented by the caller, not by `flashProfileRecord()`: the
 * accumulator folds timing, and only the caller knows whether the read it timed
 * produced bytes.
 */
struct FlashProfileStats {
    std::uint32_t reads = 0;
    std::uint32_t failed = 0;
    std::uint32_t minMicros = 0;
    std::uint32_t maxMicros = 0;
    std::uint32_t over1ms = 0;
    std::uint32_t over4ms = 0;
    std::uint32_t histogram[kFlashProfileHistogramBins] = {};
    std::uint64_t totalMicros = 0;
    std::uint64_t totalBytes = 0;
};

/**
 * @brief The scalar half plus the bounded record set; one per pass.
 *
 * `stats` is the whole pass; `records` is the detail that fits. `recorded` and
 * `dropped` always sum to `stats.reads`.
 */
struct FlashProfileAccumulator {
    FlashProfileStats stats;
    FlashProfileRead records[kFlashProfileRecordCapacity] = {};
    std::uint32_t recorded = 0;
    std::uint32_t dropped = 0;
};

/// Receives one complete line, trailing newline included.
using FlashProfileSink = void (*)(const char* line);

/// Zeroes a stats block. Safe with a null pointer.
void flashProfileReset(FlashProfileStats* stats);

/// Zeroes an accumulator, records included. Safe with a null pointer.
void flashProfileReset(FlashProfileAccumulator* accumulator);

/**
 * @brief Folds one read into a stats block.
 *
 * Updates reads, totals, min/max, both millisecond buckets and the histogram.
 * `bytes` is the decoded size the read reported; pass 0 for a read that failed.
 * A safe no-op with a null pointer.
 */
void flashProfileRecord(FlashProfileStats* stats, std::uint32_t bytes, std::uint32_t micros);

/**
 * @brief Folds one read into an accumulator and stores its record if it fits.
 *
 * Reads past `kFlashProfileRecordCapacity` still update `stats` and increment
 * `dropped`; they are never silently lost from the totals.
 */
void flashProfileRecord(FlashProfileAccumulator* accumulator, std::int32_t lump,
                        std::uint32_t bytes, std::uint32_t micros);

/**
 * @brief The histogram bucket for one elapsed time.
 *
 * Bucket i covers (previous edge, edge[i]] in microseconds, so 0..10 is bucket
 * 0, 11..50 is bucket 1, and everything above 4000 is the last bucket.
 */
std::uint32_t flashProfileBin(std::uint32_t micros);

/**
 * @brief Throughput in tenths of a mebibyte per second.
 *
 * `bytes * 10 * 1e6 / (micros * 1,048,576)`, integer maths only. Answers 0 when
 * `micros` is 0 (nothing was timed, so no rate is claimed) and 0 for 0 bytes.
 */
std::uint32_t flashProfileMiBTenths(std::uint64_t bytes, std::uint64_t micros);

/**
 * @brief `part` in thousandths of `whole`.
 *
 * Answers 0 when `whole` is 0: a ratio to nothing is not a number this port
 * prints, and a zero-length cold pass must not divide by it. A ratio above 1000
 * is reported as measured -- a warm pass slower than the cold one is a result,
 * not an error -- and the multiplication is safe for any microsecond total a
 * boot can produce (decades of microseconds fit in 64 bits).
 */
std::uint32_t flashProfileRatioPermille(std::uint64_t part, std::uint64_t whole);

/**
 * @brief The cold total in tenths of one percent of a frame.
 *
 * 432 means 43.2% of `frameMicros`. Answers 0 when `frameMicros` is 0.
 */
std::uint32_t flashProfileFramePercentTenths(std::uint64_t micros, std::uint32_t frameMicros);

/// Index of the slowest recorded read, or -1 when nothing was recorded.
int flashProfileSlowestRecordIndex(const FlashProfileAccumulator& accumulator);

/**
 * @brief Formats one pass line exactly as the file comment documents it.
 *
 * @return The line length excluding the NUL, or 0 when the line does not fit.
 *         A 0 always means truncation: every line ends with a newline, so a
 *         successfully written line is never empty.
 */
std::size_t flashProfileFormatPass(const FlashProfileAccumulator& accumulator, const char* passLabel,
                                   const char* phaseLabel, char* out, std::size_t capacity);

/// Formats the warm/cold ratio line. Same return contract as above.
std::size_t flashProfileFormatRatio(std::uint64_t coldMicros, std::uint64_t warmMicros, char* out,
                                    std::size_t capacity);

/// Formats the per-frame budget line. Same return contract as above.
std::size_t flashProfileFormatBudget(std::uint64_t coldMicros, std::uint32_t frameMicros, char* out,
                                     std::size_t capacity);

/**
 * @brief The compile-time gate the probe emits through.
 *
 * `FlashProfileReport<kFlashProfileEnabled>::pass()` is the only way the probe
 * writes a line. With `Enabled == false` the specialization below has an empty
 * body: it touches no argument, formats nothing and calls no sink, so the
 * shipped build carries no formatter call site at all.
 *
 * It is a template rather than a runtime flag so the "disabled build reports
 * nothing" property is a property of the compiled code and not of a branch, and
 * so one host test binary can instantiate both forms and observe both.
 */
template <bool Enabled>
struct FlashProfileReport {
    /// Emits one pass line through @p sink; returns 1 when a line was written.
    static std::uint32_t pass(const FlashProfileAccumulator& accumulator, const char* passLabel,
                             const char* phaseLabel, FlashProfileSink sink) {
        if (sink == nullptr) {
            return 0u;
        }
        char line[kFlashProfileLineCapacity];
        if (flashProfileFormatPass(accumulator, passLabel, phaseLabel, line, sizeof line) == 0u) {
            return 0u;
        }
        sink(line);
        return 1u;
    }
};

template <>
struct FlashProfileReport<false> {
    static std::uint32_t pass(const FlashProfileAccumulator&, const char*, const char*,
                              FlashProfileSink) {
        return 0u;
    }
};

#if defined(PIXELROOT32_DOOM_FLASH_PROFILE)

/**
 * @brief Runs D2.5's profile once, at boot: cold pass, warm pass, ratio, budget.
 *
 * Opens the `wad` partition through `doomc/WhdSource.h`, builds E1M1's working
 * set from the container itself -- the map lumps, TEXTURE1, PNAMES, every patch
 * the level's textures name and every flat its sectors name -- decodes all of it
 * into one static buffer, and prints the lines documented at the top of this
 * file. The identical sequence is then replayed from the same buffer.
 *
 * Maps nothing and prints nothing on a host build: there is no partition, and
 * the arithmetic above is what a host test drives.
 */
void flashProfileRunProbe();

#endif

}  // namespace doomc
}  // namespace doom
