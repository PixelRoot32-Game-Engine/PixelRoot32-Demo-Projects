// D0.5 - custom partition table and memory-mapped WAD reads.
#pragma once

namespace bench {

/// Looks up the custom `wad` data partition, maps it with esp_partition_mmap,
/// reports the pointer, sequential MB/s and strided 4-byte read latency, then
/// unmaps it. A missing partition is an explicit probe failure, not zeros.
void runFlashProbe();

} // namespace bench
