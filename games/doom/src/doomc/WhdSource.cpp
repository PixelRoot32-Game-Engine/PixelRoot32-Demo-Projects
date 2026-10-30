/*
 * The device side of the compressed container: see WhdSource.h for the contract
 * and for the validation order this file implements.
 *
 * The flash path is behind the same guard WadSource.cpp uses. On the board this
 * file finds the `wad` partition, maps it read-only and hands the pointer to the
 * C reader; on a host build there is no flash partition, so the same translation
 * unit compiles to a stub that reports DOOM_WHD_SOURCE_ERR_UNAVAILABLE. That
 * stub is not a convenience: env:link_test compiles src/doomc/ on the host with
 * no engine dependency, and it must keep doing so.
 *
 * The accessors are not behind the guard. They only forward to whd.c, which is
 * compiled for the host too (a project library), so a host test can build a
 * container in memory and drive exactly the code the firmware drives from the
 * directory down.
 */
#include "doomc/WhdSource.h"

// --- Directory accessors: the same functions on both targets ---------------

extern "C" int32_t doom_whd_source_lump_count(const whd_state_t* state) {
    return whd_lump_count(state);
}

extern "C" int doom_whd_source_lump_index(const whd_state_t* state, const char* name) {
    return whd_lump_index_for_name(state, name);
}

extern "C" int32_t doom_whd_source_lump_size(const whd_state_t* state, int lump) {
    return whd_lump_size(state, lump);
}

extern "C" int doom_whd_source_lump_is_raw(const whd_state_t* state, int lump, int* out) {
    switch (whd_lump_is_raw(state, lump, out)) {
        case WHD_ERR_OK:
            return DOOM_WHD_SOURCE_OK;
        case WHD_ERR_LUMP_INDEX:
            return DOOM_WHD_SOURCE_ERR_LUMP_INDEX;
        case WHD_ERR_NULL_OUTPUT:
            return DOOM_WHD_SOURCE_ERR_NULL_OUTPUT;
        default:
            return DOOM_WHD_SOURCE_ERR_STATE_NULL;
    }
}

extern "C" int doom_whd_source_lump_view(const whd_state_t* state, int lump,
                                         const uint8_t** data, uint32_t* size) {
    switch (whd_lump_view(state, lump, data, size)) {
        case WHD_ERR_OK:
            return DOOM_WHD_SOURCE_OK;
        // The one answer the caller branches on: decode it, do not map it.
        case WHD_ERR_NOT_RAW:
            return DOOM_WHD_SOURCE_ERR_NOT_RAW;
        case WHD_ERR_LUMP_INDEX:
            return DOOM_WHD_SOURCE_ERR_LUMP_INDEX;
        case WHD_ERR_NULL_OUTPUT:
            return DOOM_WHD_SOURCE_ERR_NULL_OUTPUT;
        default:
            return DOOM_WHD_SOURCE_ERR_STATE_NULL;
    }
}

extern "C" int doom_whd_source_read_lump(const whd_state_t* state, int lump, uint8_t* out,
                                         size_t capacity, uint32_t* requiredSize) {
    const whd_error_t error = whd_read_lump(state, lump, out, capacity, requiredSize);

    // One adapter code per answer the caller can act on differently. The
    // reader's WHD_ERR_NULL_STATE cannot reach the default branch here -- the
    // reader answers a NULL state with WHD_ERR_LUMP_INDEX -- but a future reader
    // change must not turn into a silent success, so it lands on DECODE rather
    // than on a pass-through.
    switch (error) {
        case WHD_ERR_OK:
            return DOOM_WHD_SOURCE_OK;
        case WHD_ERR_LUMP_INDEX:
            return DOOM_WHD_SOURCE_ERR_LUMP_INDEX;
        case WHD_ERR_NULL_OUTPUT:
            return DOOM_WHD_SOURCE_ERR_NULL_OUTPUT;
        case WHD_ERR_BUFFER_TOO_SMALL:
            return DOOM_WHD_SOURCE_ERR_BUFFER_TOO_SMALL;
        default:
            return DOOM_WHD_SOURCE_ERR_DECODE;
    }
}

#if defined(ESP32) || defined(ESP_PLATFORM)

#include <cstddef>
#include <cstdint>
#include <esp_idf_version.h>
#include <esp_partition.h>

#if ESP_IDF_VERSION_MAJOR >= 5
using WhdMmapHandle = esp_partition_mmap_handle_t;
#else
#include <esp_spi_flash.h>
using WhdMmapHandle = spi_flash_mmap_handle_t;
#endif

namespace {

/// The label partitions_wad.csv gives the data partition.
const char* const kPartitionLabel = "wad";

/// The header field whd.h puts totalSize in: magic (4) + version (2) + flags
/// (2) + numLumps (4) + nameCount (4) + directoryOffset (4) + nameTableOffset
/// (4) + dataOffset (4). The field itself is documented there and read with
/// whd_open()'s own rule, little-endian byte by byte, so this file never
/// depends on the target's byte order.
constexpr std::size_t kTotalSizeOffset = 28;

/// The ESP32's data-MMU page: the granularity `esp_partition_mmap()` maps at, and
/// therefore the granularity of every window this file asks for. It is not the
/// CPU page size; it is the flash-cache mapping unit, 64 KB on this SoC.
constexpr std::size_t kMmapPageBytes = 0x10000u;

/// Where the container starts inside the partition. `tools/` writes it at the
/// partition's first byte, which is also page-aligned, so the delta below is 0 --
/// but the arithmetic is written out rather than assumed, because it is exactly
/// the subtraction a second offset would break.
constexpr std::size_t kContainerOffset = 0u;

/// The live mapping, if any. The reader state keeps its own copy of base/size,
/// so this file only has to remember the handle, the mapped size, the base and
/// whether the mapping is live.
WhdMmapHandle gHandle = 0;
std::size_t gMappedSize = 0;
const std::uint8_t* gBase = nullptr;
bool gMappingOpen = false;

std::uint32_t readUint32(const std::uint8_t* base, std::size_t offset) {
    return static_cast<std::uint32_t>(base[offset])
           | (static_cast<std::uint32_t>(base[offset + 1]) << 8)
           | (static_cast<std::uint32_t>(base[offset + 2]) << 16)
           | (static_cast<std::uint32_t>(base[offset + 3]) << 24);
}

std::uint32_t alignUpToPage(std::uint32_t bytes) {
    return static_cast<std::uint32_t>(
        (static_cast<std::size_t>(bytes) + (kMmapPageBytes - 1u)) & ~(kMmapPageBytes - 1u));
}

/// One page-aligned window of @p partition, or false when the mapping is refused.
bool mapWindow(const esp_partition_t* partition, std::size_t length, const void** mapped,
               WhdMmapHandle* handle) {
#if ESP_IDF_VERSION_MAJOR >= 5
    const esp_err_t error = esp_partition_mmap(partition,
                                               static_cast<std::size_t>(kContainerOffset),
                                               length,
                                               ESP_PARTITION_MMAP_DATA,
                                               mapped,
                                               handle);
#else
    const esp_err_t error = esp_partition_mmap(partition,
                                               static_cast<std::size_t>(kContainerOffset),
                                               length,
                                               SPI_FLASH_MMAP_DATA,
                                               mapped,
                                               handle);
#endif
    return error == ESP_OK && *mapped != nullptr;
}

void releaseMapping(WhdMmapHandle handle) {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_partition_munmap(handle);
#else
    spi_flash_munmap(handle);
#endif
}

}  // namespace

extern "C" uint32_t doom_whd_source_mapped_size(void) {
    return gMappingOpen ? static_cast<uint32_t>(gMappedSize) : 0u;
}

extern "C" const uint8_t* doom_whd_source_base(void) {
    return gMappingOpen ? gBase : nullptr;
}

extern "C" int doom_whd_source_open(whd_state_t* state) {
    if (state == nullptr) {
        return DOOM_WHD_SOURCE_ERR_STATE_NULL;
    }
    if (gMappingOpen) {
        return DOOM_WHD_SOURCE_ERR_ALREADY_OPEN;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kPartitionLabel);

    if (partition == nullptr) {
        // The default partition table has no data slot, so this is the expected
        // answer on a board flashed with the shipped environment. Report it; do
        // not pretend a mapping exists.
        return DOOM_WHD_SOURCE_ERR_PARTITION_MISSING;
    }

    if (partition->size < kMmapPageBytes) {
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    // Step one: one page is enough to hold the header, and the header is what
    // says how much of the partition is a container. The mapping has to be
    // page-aligned, so it is taken at the container's page and the pointer into
    // it is the window's start plus the offset inside that page.
    const void* mapped = nullptr;
    WhdMmapHandle handle = 0;
    if (!mapWindow(partition, kMmapPageBytes, &mapped, &handle)) {
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    const std::uint8_t* window = static_cast<const std::uint8_t*>(mapped);
    const std::size_t windowStart = kContainerOffset & ~(kMmapPageBytes - 1u);
    const std::size_t delta = kContainerOffset - windowStart;
    const std::uint8_t* base = window + delta;

    // The container states its own length, and whd_open() requires it to be
    // exactly the size offered. The partition is larger than the container, so
    // that field is what tells this adapter where the container ends; a value
    // that cannot describe this partition is a header this image does not have.
    const std::uint32_t containerSize = readUint32(base, kTotalSizeOffset);
    if (containerSize < static_cast<std::uint32_t>(WHD_HEADER_SIZE)
        || static_cast<std::uint64_t>(containerSize) > static_cast<std::uint64_t>(partition->size)) {
        releaseMapping(handle);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    // Step two: the window that actually covers the container. Mapping the whole
    // 2 MB partition would work and would waste the pages past the container, and
    // a raw view only needs the container itself -- every RAW payload lives
    // inside totalSize. One page already holds the container when it is small
    // enough, and then the first mapping is the only one.
    const std::uint32_t windowBytes = alignUpToPage(containerSize);
    std::size_t mappedSize = kMmapPageBytes;
    if (windowBytes != kMmapPageBytes || delta != 0u) {
        releaseMapping(handle);
        const void* grown = nullptr;
        WhdMmapHandle grownHandle = 0;
        if (!mapWindow(partition, windowBytes, &grown, &grownHandle)) {
            return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
        }
        window = static_cast<const std::uint8_t*>(grown);
        handle = grownHandle;
        mappedSize = windowBytes;
        base = window + delta;
    }

    const whd_error_t whdError = whd_open(state, base, static_cast<size_t>(containerSize));

    if (whdError != WHD_ERR_OK) {
        // A valid partition can still hold erased flash, an older container or a
        // truncated image. Release the mapping instead of handing the caller a
        // reader whose every lookup would fail.
        releaseMapping(handle);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    gHandle = handle;
    gMappedSize = mappedSize;
    gBase = base;
    gMappingOpen = true;

    return DOOM_WHD_SOURCE_OK;
}

extern "C" void doom_whd_source_close(void) {
    if (gMappingOpen == false) {
        return;
    }

    releaseMapping(gHandle);
    gHandle = 0;
    gMappedSize = 0;
    gBase = nullptr;
    gMappingOpen = false;
}

#else

// Host build: src/doomc/WhdSource.cpp is compiled by env:link_test, which
// installs no ESP-IDF headers. There is no partition to find and nothing to map,
// so the flash entry points report deterministically that this build has none.
extern "C" uint32_t doom_whd_source_mapped_size(void) {
    return 0u;
}

extern "C" const uint8_t* doom_whd_source_base(void) {
    return nullptr;
}

extern "C" int doom_whd_source_open(whd_state_t* state) {
    (void)state;
    return DOOM_WHD_SOURCE_ERR_UNAVAILABLE;
}

extern "C" void doom_whd_source_close(void) {}

#endif
