// Shared plumbing for the Phase 0 bench: environment identity, CSV emission,
// heap readings and the fixed pattern the present probes checksum.
//
// Header-only on purpose: the probe translation units stay independent of each
// other, so adding one probe never edits another one's build inputs.
#pragma once

#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <esp_heap_caps.h>
#include <esp_timer.h>

#ifndef BENCH_ENV_NAME
#define BENCH_ENV_NAME "bench_unknown"
#endif

namespace bench {

/// Free internal DRAM, the quantity PLAN §5's B4 gate is written in.
inline uint32_t freeInternal() {
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

/// Largest contiguous internal block. Fragmentation, not just the total, is
/// what decides whether the DOOM zone can be a single allocation.
inline uint32_t largestInternal() {
    return static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

// Captured once in main. Stored here so D0.2/D0.4 report the same post-init
// baseline regardless of which probe the maintainer ran first.
inline uint32_t gFreeInternalBeforeInit = 0;
inline uint32_t gLargestInternalBeforeInit = 0;
inline uint32_t gFreeInternalAfterInit = 0;
inline uint32_t gLargestInternalAfterInit = 0;

/// Keeps a timed loop's result observable so the optimiser cannot delete it.
inline volatile uint32_t gSink = 0;

inline const char* envName() {
    return BENCH_ENV_NAME;
}

/// Every probe line starts with this, so `grep '^BENCH,env='` is the capture.
inline void csvPrefix(const char* probe) {
    Serial.printf("BENCH,env=%s,probe=%s", envName(), probe);
}

/// Bitwise CRC-32 (IEEE 802.3). No 1 KB table: a probe calls it once per frame,
/// where 64,000 bytes is noise next to a 17 ms transfer.
inline uint32_t crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
        }
    }
    return ~crc;
}

/// Fixed full-surface pattern for D0.1/D0.2. The term mix is high-frequency
/// enough that a shifted or sheared frame changes the checksum, and it fills
/// in one pass.
inline void fillPresentPattern(uint8_t* dst, int width, int height) {
    for (int y = 0; y < height; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            row[x] = static_cast<uint8_t>((x * 3 + y * 5 + (x >> 3)) & 0xFF);
        }
    }
}

} // namespace bench
