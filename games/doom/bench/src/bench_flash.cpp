// D0.5 - flash partition and mmap.
//
// PLAN Phase 2 puts the shareware WAD in flash and reads it through a memory
// map. This probe answers the two things that decision needs before it is
// taken: does a custom `wad` partition exist and map, and what do sequential
// and scattered 4-byte reads out of that mapping actually cost on this board.
//
// A missing partition or a failed mmap prints an explicit failure line. It
// never reports a zero, because a zero would look like a measured result.

#include "bench_flash.h"

#include "bench_common.h"

#include <cstddef>
#include <cstdint>
#include <esp_idf_version.h>
#include <esp_partition.h>

#if ESP_IDF_VERSION_MAJOR >= 5
using BenchMmapHandle = esp_partition_mmap_handle_t;
#define BENCH_MMAP_DATA ESP_PARTITION_MMAP_DATA
#define BENCH_MUNMAP(handle) esp_partition_munmap(handle)
#else
#include <esp_spi_flash.h>
using BenchMmapHandle = spi_flash_mmap_handle_t;
#define BENCH_MMAP_DATA SPI_FLASH_MMAP_DATA
#define BENCH_MUNMAP(handle) spi_flash_munmap(handle)
#endif

namespace bench {
namespace {

constexpr int kStridedReads = 4096;
constexpr double kStridedGateUs = 1.5;

} // namespace

void runFlashProbe() {
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "wad");

    if (partition == nullptr) {
        csvPrefix("D0.5");
        Serial.printf(",status=partition_missing,label=wad,result=fail\n");
        return;
    }

    const void* mapped = nullptr;
    BenchMmapHandle handle = 0;
    const esp_err_t err = esp_partition_mmap(partition,
                                             0,
                                             partition->size,
                                             BENCH_MMAP_DATA,
                                             &mapped,
                                             &handle);

    if (err != ESP_OK || mapped == nullptr) {
        csvPrefix("D0.5");
        Serial.printf(",status=mmap_failed,label=wad,size=%lu,err=%d,result=fail\n",
                      static_cast<unsigned long>(partition->size),
                      static_cast<int>(err));
        return;
    }

    csvPrefix("D0.5");
    Serial.printf(",status=mapped,label=wad,address=%p,size=%lu,subtype=0x%02X\n",
                  mapped,
                  static_cast<unsigned long>(partition->size),
                  static_cast<unsigned>(partition->subtype));

    const uint8_t* base = static_cast<const uint8_t*>(mapped);
    const size_t bytes = partition->size & ~static_cast<size_t>(3u);
    volatile uint32_t sink = 0;

    // Sequential: one 32-bit load per word over the whole mapping. The volatile
    // accumulator keeps every load; the loop overhead is negligible next to a
    // flash read.
    const int64_t sequentialStart = esp_timer_get_time();
    for (size_t offset = 0; offset < bytes; offset += 4) {
        sink += *reinterpret_cast<const uint32_t*>(base + offset);
    }
    const int64_t sequentialUs = esp_timer_get_time() - sequentialStart;
    const double seconds = static_cast<double>(sequentialUs) / 1000000.0;
    const double mbPerSecond = seconds > 0.0
        ? (static_cast<double>(bytes) / (1024.0 * 1024.0)) / seconds
        : 0.0;

    csvPrefix("D0.5");
    Serial.printf(",kind=sequential,bytes=%lu,elapsed_us=%lld,mb_s=%.2f\n",
                  static_cast<unsigned long>(bytes),
                  static_cast<long long>(sequentialUs),
                  mbPerSecond);

    // Strided: the access pattern a BSP/column walk makes through cached lumps.
    // Deterministic xorshift so two runs on the same build sample the same
    // addresses.
    uint32_t rng = 0x12345678u;
    const size_t words = bytes / 4;
    const int64_t stridedStart = esp_timer_get_time();
    for (int i = 0; i < kStridedReads; ++i) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        const size_t word = words > 0 ? static_cast<size_t>(rng) % words : 0;
        sink += *reinterpret_cast<const uint32_t*>(base + word * 4);
    }
    const int64_t stridedUs = esp_timer_get_time() - stridedStart;
    const double meanUs = static_cast<double>(stridedUs) / kStridedReads;

    csvPrefix("D0.5");
    Serial.printf(",kind=strided4,reads=%d,elapsed_us=%lld,mean_us=%.3f\n",
                  kStridedReads,
                  static_cast<long long>(stridedUs),
                  meanUs);

    csvPrefix("D0.5");
    Serial.printf(",gate=strided_mean_us,threshold_us=%.3f,value_us=%.3f,result=%s\n",
                  kStridedGateUs,
                  meanUs,
                  meanUs <= kStridedGateUs ? "pass" : "fail");

    BENCH_MUNMAP(handle);
    (void)sink;
}

} // namespace bench
