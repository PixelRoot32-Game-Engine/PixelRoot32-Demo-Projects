/*
 * The device side of the compressed container: see WhdSource.h for the contract
 * and for the validation order this file implements.
 *
 * The flash path is behind the same guard WadSource.cpp uses. On the board this
 * file finds the `wad` partition, maps it read-only and hands the pointer to the
 * C reader; on a host build there is no partition, so the container is read from
 * the file WhdSource.h names ($DOOM_WHD, or the compile-time default
 * DOOM_WHD_DEFAULT) into one heap buffer and the same reader is opened over it.
 * The default is tools/DOOM1.whd unless the build defines it, which is how the
 * native environment selects the full-sprite container.
 *
 * The host branch has to keep compiling under env:link_test, which builds every
 * src/doomc/ object with no engine dependency: it uses <cstdint>, <cstdio> and
 * <cstdlib> only, and every failure is a returned code rather than an exception
 * (both builds compile with -fno-exceptions).
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
// installs no ESP-IDF headers, and by env:native. There is no partition to find
// and nothing to map, so the container is a file: open() reads it into one heap
// buffer and opens the same C reader over that buffer. The buffer is what the
// accessors answer over, and what close() releases.
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

/// The file open() reads, `$DOOM_WHD`. Unset or empty selects the default.
const char* const kPathVariable = "DOOM_WHD";

/// Written by tools/make_container.ps1 next to the WAD it encodes, and relative
/// to the working directory: `pio run -e native -t exec` and a run from
/// games/doom both find it there. A host build can name a different default at
/// compile time (`-D DOOM_WHD_DEFAULT=\"...\"`): env:native points it at the
/// full-sprite container, and `$DOOM_WHD` still wins over either.
#ifndef DOOM_WHD_DEFAULT
#define DOOM_WHD_DEFAULT "tools/DOOM1.whd"
#endif

/// The file open() reads when `$DOOM_WHD` is unset or empty.
const char* const kDefaultPath = DOOM_WHD_DEFAULT;

/// The field the device branch reads, at the same offset: magic (4) + version
/// (2) + flags (2) + numLumps (4) + nameCount (4) + directoryOffset (4) +
/// nameTableOffset (4) + dataOffset (4).
constexpr std::size_t kTotalSizeOffset = 28;

/// The live host buffer, if any. Same role as the device branch's mapping.
std::uint8_t* gBuffer = nullptr;
std::size_t gBufferSize = 0;

std::uint32_t readUint32(const std::uint8_t* base, std::size_t offset) {
    return static_cast<std::uint32_t>(base[offset])
           | (static_cast<std::uint32_t>(base[offset + 1]) << 8)
           | (static_cast<std::uint32_t>(base[offset + 2]) << 16)
           | (static_cast<std::uint32_t>(base[offset + 3]) << 24);
}

/// The header's own magic, checked before any field read from it is trusted.
bool hasContainerMagic(const std::uint8_t* header) {
    return header[0] == WHD_MAGIC_0 && header[1] == WHD_MAGIC_1
           && header[2] == WHD_MAGIC_2 && header[3] == WHD_MAGIC_3;
}

const char* containerPath(void) {
    const char* path = std::getenv(kPathVariable);
    return (path != nullptr && path[0] != '\0') ? path : kDefaultPath;
}

/// Drops a buffer an earlier attempt took, so a failed open leaves no state.
void releaseBuffer(void) {
    std::free(gBuffer);
    gBuffer = nullptr;
    gBufferSize = 0;
}

}  // namespace

extern "C" uint32_t doom_whd_source_mapped_size(void) {
    return gBuffer != nullptr ? static_cast<uint32_t>(gBufferSize) : 0u;
}

extern "C" const uint8_t* doom_whd_source_base(void) {
    return gBuffer;
}

extern "C" int doom_whd_source_open(whd_state_t* state) {
    if (state == nullptr) {
        return DOOM_WHD_SOURCE_ERR_STATE_NULL;
    }
    if (gBuffer != nullptr) {
        return DOOM_WHD_SOURCE_ERR_ALREADY_OPEN;
    }

    std::FILE* file = std::fopen(containerPath(), "rb");
    if (file == nullptr) {
        // No container where this build looks for one. The device's answer to
        // the same situation is ERR_PARTITION_MISSING, so a caller's handler
        // does not need a second table.
        return DOOM_WHD_SOURCE_ERR_PARTITION_MISSING;
    }

    // The file's own length is what the device gets from the partition size: the
    // upper bound the header's totalSize is checked against.
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }
    const long end = std::ftell(file);
    if (end < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    const std::size_t fileBytes = static_cast<std::size_t>(end);
    if (fileBytes < static_cast<std::size_t>(WHD_HEADER_SIZE)) {
        // A file too short to hold a header is not a container, which is the
        // device branch's answer to a mapping shorter than the header.
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    std::uint8_t header[WHD_HEADER_SIZE];
    if (std::fread(header, 1, sizeof header, file) != sizeof header) {
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }
    if (!hasContainerMagic(header)) {
        // Rejected here rather than left to whd_open(): the magic is what tells
        // a stray file (a WAD, a partition dump, an empty slot) apart before any
        // size in it is believed.
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    const std::uint32_t containerSize = readUint32(header, kTotalSizeOffset);
    if (containerSize < static_cast<std::uint32_t>(WHD_HEADER_SIZE)
        || static_cast<std::size_t>(containerSize) > fileBytes) {
        // Same rule as the device: the header's length must describe bytes this
        // file actually has. A file larger than the container is accepted, as a
        // partition holding a container is, and only totalSize is passed on.
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    std::uint8_t* buffer = static_cast<std::uint8_t*>(std::malloc(containerSize));
    if (buffer == nullptr) {
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::free(buffer);
        std::fclose(file);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }
    const std::size_t readBytes = std::fread(buffer, 1, containerSize, file);
    std::fclose(file);
    if (readBytes != static_cast<std::size_t>(containerSize)) {
        std::free(buffer);
        return DOOM_WHD_SOURCE_ERR_MMAP_FAILED;
    }

    const whd_error_t whdError = whd_open(state, buffer, static_cast<size_t>(containerSize));
    if (whdError != WHD_ERR_OK) {
        // A right-sized file can still be a corrupted, truncated or older
        // container. Release the buffer instead of leaving a half-open adapter.
        std::free(buffer);
        return DOOM_WHD_SOURCE_ERR_CONTAINER_INVALID;
    }

    gBuffer = buffer;
    gBufferSize = static_cast<std::size_t>(containerSize);

    return DOOM_WHD_SOURCE_OK;
}

extern "C" void doom_whd_source_close(void) {
    releaseBuffer();
}

#endif
