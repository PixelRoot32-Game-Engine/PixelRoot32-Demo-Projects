// D0.3 - raw 8bpp fill throughput.
//
// The feature renderer's floor is one byte store per pixel. This probe times
// three ways to put bytes into a 320x200/240x180 surface and reports the cost
// per pixel, so PLAN 6.2's "> 12 ns per byte-store falsifies the CPU estimate"
// can be checked against the same loop shape a rasteriser would use.

#include "bench_memory.h"

#include "bench_common.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>

namespace bench {
namespace {

constexpr int kIterations = 1000;

void reportFill(const char* sizeLabel,
                size_t pixelCount,
                const char* kind,
                int64_t elapsedUs) {
    const double pixelsPerRun = static_cast<double>(pixelCount) * kIterations;
    const double nsPerPixel = pixelsPerRun > 0.0
        ? (static_cast<double>(elapsedUs) * 1000.0) / pixelsPerRun
        : 0.0;
    const double seconds = static_cast<double>(elapsedUs) / 1000000.0;
    const double mbPerSecond = seconds > 0.0
        ? (pixelsPerRun / (1024.0 * 1024.0)) / seconds
        : 0.0;

    csvPrefix("D0.3");
    Serial.printf(",size=%s,pixels=%lu,kind=%s,iterations=%d,total_us=%lld,ns_per_px=%.3f,mb_s=%.2f\n",
                  sizeLabel,
                  static_cast<unsigned long>(pixelCount),
                  kind,
                  kIterations,
                  static_cast<long long>(elapsedUs),
                  nsPerPixel,
                  mbPerSecond);
}

void runFillCase(const char* sizeLabel, int width, int height) {
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    uint8_t* buffer = static_cast<uint8_t*>(
        heap_caps_malloc(pixelCount, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    if (buffer == nullptr) {
        csvPrefix("D0.3");
        Serial.printf(",size=%s,pixels=%lu,status=alloc_failed,bytes=%lu\n",
                      sizeLabel,
                      static_cast<unsigned long>(pixelCount),
                      static_cast<unsigned long>(pixelCount));
        return;
    }

    uint8_t lut[256];
    for (int i = 0; i < 256; ++i) {
        lut[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    }

    // Warm the cache and make sure no timed run starts from a page-fault state.
    std::memset(buffer, 0, pixelCount);
    {
        volatile uint8_t* warm = buffer;
        for (size_t i = 0; i < pixelCount; ++i) {
            warm[i] = 0x5A;
        }
    }
    for (size_t i = 0; i < pixelCount; ++i) {
        buffer[i] = lut[buffer[i]];
    }

    int64_t start = esp_timer_get_time();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::memset(buffer, iteration & 0xFF, pixelCount);
    }
    int64_t elapsed = esp_timer_get_time() - start;
    gSink = crc32(buffer, pixelCount);
    reportFill(sizeLabel, pixelCount, "memset", elapsed);

    // volatile keeps this a real store loop: without it GCC recognises the
    // memset idiom and reports memset's cost under the byte-store name.
    start = esp_timer_get_time();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        volatile uint8_t* dst = buffer;
        const uint8_t value = static_cast<uint8_t>(iteration + 1);
        for (size_t i = 0; i < pixelCount; ++i) {
            dst[i] = value;
        }
    }
    elapsed = esp_timer_get_time() - start;
    gSink = crc32(buffer, pixelCount);
    reportFill(sizeLabel, pixelCount, "byte_store", elapsed);

    // In place, like a colormap remap: one load, one LUT read, one store per
    // pixel. The next iteration reads what this one wrote, so the compiler
    // cannot collapse the loop.
    start = esp_timer_get_time();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        for (size_t i = 0; i < pixelCount; ++i) {
            buffer[i] = lut[buffer[i]];
        }
    }
    elapsed = esp_timer_get_time() - start;
    gSink = crc32(buffer, pixelCount);
    reportFill(sizeLabel, pixelCount, "lut_store", elapsed);

    heap_caps_free(buffer);
}

} // namespace

void runFillProbe() {
    runFillCase("320x200", 320, 200);
    runFillCase("240x180", 240, 180);
}

// D0.4 - DRAM census with the display path live.
//
// The gate is PLAN §5's B4: >= 300 KB free internal DRAM after the drawer is
// initialised and before any DOOM buffer exists. `before_init` is captured by
// main immediately before TFT_eSPI_Drawer::init(); `after_init` therefore
// contains the 64,000 B 8bpp surface, both DMA line buffers and the LUTs.
//
// The third row allocates a second 320x200 8bpp surface on purpose. It is the
// cost of a twin framebuffer (PLAN §6.3 shows the twin does not fit); showing
// it here means the plan's margin can be read without re-deriving it.
void runDramCensus() {
    constexpr uint32_t kGateBytes = 300u * 1024u;
    constexpr size_t kSurfaceBytes = 320u * 200u;

    if (bench::gFreeInternalAfterInit == 0 && bench::gLargestInternalAfterInit == 0) {
        csvPrefix("D0.4");
        Serial.printf(",status=not_initialized\n");
        return;
    }

    csvPrefix("D0.4");
    Serial.printf(",stage=before_init,free_internal=%lu,largest_internal=%lu\n",
                  static_cast<unsigned long>(bench::gFreeInternalBeforeInit),
                  static_cast<unsigned long>(bench::gLargestInternalBeforeInit));

    const int32_t usedByDisplay =
        static_cast<int32_t>(bench::gFreeInternalBeforeInit) -
        static_cast<int32_t>(bench::gFreeInternalAfterInit);

    csvPrefix("D0.4");
    Serial.printf(",stage=after_init,free_internal=%lu,largest_internal=%lu,delta_used=%ld\n",
                  static_cast<unsigned long>(bench::gFreeInternalAfterInit),
                  static_cast<unsigned long>(bench::gLargestInternalAfterInit),
                  static_cast<long>(usedByDisplay));

    uint8_t* extraSurface = static_cast<uint8_t*>(
        heap_caps_malloc(kSurfaceBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    csvPrefix("D0.4");
    Serial.printf(",stage=after_extra_surface,requested_bytes=%lu,status=%s,free_internal=%lu,largest_internal=%lu\n",
                  static_cast<unsigned long>(kSurfaceBytes),
                  extraSurface != nullptr ? "allocated" : "alloc_failed",
                  static_cast<unsigned long>(bench::freeInternal()),
                  static_cast<unsigned long>(bench::largestInternal()));

    if (extraSurface != nullptr) {
        heap_caps_free(extraSurface);
        extraSurface = nullptr;
    }

    csvPrefix("D0.4");
    Serial.printf(",gate=free_internal_after_init,threshold_bytes=%lu,value_bytes=%lu,result=%s\n",
                  static_cast<unsigned long>(kGateBytes),
                  static_cast<unsigned long>(bench::gFreeInternalAfterInit),
                  bench::gFreeInternalAfterInit >= kGateBytes ? "pass" : "fail");
}

} // namespace bench
