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

/// The live mapping, if any. The reader state keeps its own copy of base/size,
/// so this file only has to remember the handle, the mapped size and whether
/// the mapping is live.
WhdMmapHandle gHandle = 0;
std::size_t gMappedSize = 0;
bool gMappingOpen = false;

uint32_t readUint32(const uint8_t* base, std::size_t offset) {
    return static_cast<uint32_t>(base[offset])
           | (static_cast<uint32_t>(base[offset + 1]) << 8)
           | (static_cast<uint32_t>(base[offset + 2]) << 16)
           | (static_cast<uint32_t>(base[offset + 3]) << 24);
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

    const void* mapped = nullptr;
    WhdMmapHandle handle = 0;
#if ESP_IDF_VERSION_MAJOR >= 5
    const esp_err_t error = esp_partition_mmap(partition,
                                               0,
                                               partition->size,
                                               ESP_PARTITION_MMAP_DATA,
                                               &mapped,
                                               &handle);
#else
    const esp_err_t error = esp_partition_mmap(partition,
                                               0,
                                               partition->size,
                                               SPI_FLASH_MMAP_DATA,
                                               &mapped,
                                               &handle);
#endif

    if (error != ESP_OK || mapped == nullptr) {
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    const uint8_t* base = static_cast<const uint8_t*>(mapped);
    const std::size_t mappedSize = static_cast<std::size_t>(partition->size);

    // From here on every failure has to release the mapping: the caller is not
    // handed a mapping it cannot name, so this file owns the only handle.
    if (mappedSize < static_cast<std::size_t>(WHD_HEADER_SIZE)) {
        releaseMapping(handle);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    // The container states its own length, and whd_open() requires it to be
    // exactly the size offered. The partition is larger than the container, so
    // that field is what tells this adapter where the container ends; a value
    // that cannot describe this mapping is a header this image does not have.
    const uint32_t containerSize = readUint32(base, kTotalSizeOffset);
    if (containerSize < static_cast<uint32_t>(WHD_HEADER_SIZE)
        || static_cast<uint64_t>(containerSize) > static_cast<uint64_t>(mappedSize)) {
        releaseMapping(handle);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
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
    gMappingOpen = false;
}

#else

// Host build: src/doomc/WhdSource.cpp is compiled by env:link_test, which
// installs no ESP-IDF headers. There is no partition to find and nothing to map,
// so the flash entry points report deterministically that this build has none.
extern "C" uint32_t doom_whd_source_mapped_size(void) {
    return 0u;
}

extern "C" int doom_whd_source_open(whd_state_t* state) {
    (void)state;
    return DOOM_WHD_SOURCE_ERR_UNAVAILABLE;
}

extern "C" void doom_whd_source_close(void) {}

#endif
