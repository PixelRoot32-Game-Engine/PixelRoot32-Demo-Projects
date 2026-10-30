/*
 * The device side of the WAD reader: see WadSource.h for the contract.
 *
 * The whole flash path is behind one preprocessor guard. On the board this file
 * finds the `wad` partition, maps it read-only and hands the pointer to the C
 * reader; on a host build there is no flash partition, so the same translation
 * unit compiles to a stub that reports DOOM_WAD_SOURCE_ERR_UNAVAILABLE. That
 * stub is not a convenience: env:link_test compiles src/doomc/ on the host with
 * no engine dependency, and it must keep doing so.
 *
 * The mmap API moved between ESP-IDF majors and the bench already carries the
 * same two-branch shape (bench/src/bench_flash.cpp). D0.5 measured this exact
 * call on the reference board, so the mapping itself is not speculative.
 */
#include "doomc/WadSource.h"

#if defined(ESP32) || defined(ESP_PLATFORM)

#include "doomc/DoomConsole.h"

#include <cstddef>
#include <cstdio>
#include <esp_idf_version.h>
#include <esp_partition.h>

#if ESP_IDF_VERSION_MAJOR >= 5
using WadMmapHandle = esp_partition_mmap_handle_t;
#else
#include <esp_spi_flash.h>
using WadMmapHandle = spi_flash_mmap_handle_t;
#endif

namespace {

/// The label bench/partitions_wad.csv gives the data partition.
const char* const kPartitionLabel = "wad";

/// One `WAD,...` line is under 72 bytes; the buffer leaves room for an error
/// code and a terminator.
constexpr std::size_t kLineCapacity = 96;

/// The live mapping, if any. The reader state keeps its own copy of base/size,
/// so this file only has to remember the handle and whether it is live.
WadMmapHandle gHandle = 0;
bool gMappingOpen = false;

void emitLine(const char* line) {
    doom::doomc::writeLine(line);
}

void emitMappedLine(unsigned long size) {
    char line[kLineCapacity];

    std::snprintf(line, sizeof line,
                  "WAD,status=mapped,label=wad,size=%lu,result=ok\n", size);
    emitLine(line);
}

void emitPartitionMissingLine() {
    emitLine("WAD,status=partition_missing,label=wad,result=fail\n");
}

void emitMmapFailedLine(int error) {
    char line[kLineCapacity];

    std::snprintf(line, sizeof line,
                  "WAD,status=mmap_failed,label=wad,err=%d,result=fail\n", error);
    emitLine(line);
}

void emitWadInvalidLine(int error) {
    char line[kLineCapacity];

    std::snprintf(line, sizeof line,
                  "WAD,status=wad_invalid,label=wad,error=%d,result=fail\n", error);
    emitLine(line);
}

void releaseMapping(WadMmapHandle handle) {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_partition_munmap(handle);
#else
    spi_flash_munmap(handle);
#endif
}

}  // namespace

extern "C" int doom_wad_source_open(wad_state_t* state) {
    if (state == nullptr) {
        return DOOM_WAD_SOURCE_ERR_STATE_NULL;
    }
    if (gMappingOpen) {
        return DOOM_WAD_SOURCE_ERR_ALREADY_OPEN;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, kPartitionLabel);

    if (partition == nullptr) {
        // The default partition table has no data slot, so this is the expected
        // answer on a board that was flashed without the WAD image. Report it;
        // do not pretend a mapping exists.
        emitPartitionMissingLine();
        return DOOM_WAD_SOURCE_ERR_PARTITION_MISSING;
    }

    const void* mapped = nullptr;
    WadMmapHandle handle = 0;
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
        emitMmapFailedLine(static_cast<int>(error));
        return DOOM_WAD_SOURCE_ERR_MMAP_FAILED;
    }

    const wad_error_t wadError = wad_open(
        state, static_cast<const uint8_t*>(mapped), static_cast<size_t>(partition->size));

    if (wadError != WAD_ERR_OK) {
        // A valid partition can still hold something that is not a WAD, or a
        // truncated image. Release the mapping instead of handing the caller a
        // reader that failed every lookup.
        releaseMapping(handle);
        emitWadInvalidLine(static_cast<int>(wadError));
        return DOOM_WAD_SOURCE_ERR_WAD_INVALID;
    }

    gHandle = handle;
    gMappingOpen = true;
    emitMappedLine(static_cast<unsigned long>(partition->size));

    return DOOM_WAD_SOURCE_OK;
}

extern "C" void doom_wad_source_close(void) {
    if (gMappingOpen == false) {
        return;
    }

    releaseMapping(gHandle);
    gHandle = 0;
    gMappingOpen = false;
}

#else

// Host build: src/doomc/WadSource.cpp is compiled by env:link_test, which
// installs no ESP-IDF headers. The C surface must still exist and link, so it
// reports deterministically that this build has no flash mapping.
extern "C" int doom_wad_source_open(wad_state_t* state) {
    (void)state;
    return DOOM_WAD_SOURCE_ERR_UNAVAILABLE;
}

extern "C" void doom_wad_source_close(void) {}

#endif
