#pragma once

/*
 * The frame driver's platform seam.
 *
 * FrameDriver.cpp is a renderer session (plus the boot probe) that is otherwise
 * portable C++: the ~4,450 lines that walk E1M1, bind the baked graphics and
 * run the four passes reach the board through exactly six services --
 *
 *   clock        a monotonic microsecond counter;
 *   allocator    an internal-DRAM block (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
 *   census       free heap, total heap and the largest free internal block;
 *   arena        how large a world the allocator may be asked for;
 *   stack        the loop task's stack high-water mark;
 *   cycles       the CPU cycle counter r_bsp.h declares as r_probe_cycles().
 *
 * This header names each one once, so the driver body compiles untouched on
 * both targets instead of carrying a second, diverging copy of itself.
 *
 * ON THE BOARD every function forwards to exactly the call FrameDriver.cpp made
 * before this header existed, with the same arguments, so the shipped
 * `esp32dev_ILI9341` image and the probe environments do the same work they
 * always did.
 *
 * ON THE HOST (PLATFORM_NATIVE, the SDL2 simulator) the clock is
 * std::chrono::steady_clock and the allocator is std::malloc/std::free. The
 * census and the stack watermark report 0: the desktop has no ESP32 heap and no
 * FreeRTOS loop task, and 0 is the sentinel the native census branch already
 * uses for "not measurable here" (DoomScene::reportMemoryCensus()). A native
 * build that wants an open world therefore takes its arena from the separate
 * budget below (kNativeArenaBudgetBytes) instead of from that zero, and every
 * other consumer of the census keeps reading "not measurable".
 *
 * It is a seam, not a framework: nine functions, no state, no policy.
 */

#include <cstddef>
#include <cstdint>

#if defined(ESP32) || defined(ESP_PLATFORM)

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#else

#include <chrono>
#include <cstdlib>

#endif

namespace doom {
namespace doomc {

/// The monotonic microsecond clock the phase table and the tic clock sample.
///
/// Board: the ESP-IDF counter, integer, 64-bit, monotonic for the life of a
/// boot (a difference of two samples never wraps). Host: steady_clock, also
/// monotonic, narrowed by the caller exactly as the board's value is.
inline std::int64_t driverMicros() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return esp_timer_get_time();
#else
    using SteadyClock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::microseconds>(
               SteadyClock::now().time_since_epoch())
        .count();
#endif
}

/// Takes @p bytes of internal DRAM, 8-bit capable, or returns nullptr.
///
/// Board: heap_caps_malloc() with MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT. Host:
/// std::malloc, where "internal DRAM" has no meaning and the only requirement
/// the callers rely on -- an aligned, writable block -- is malloc's.
inline void* driverAllocInternal(std::size_t bytes) {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}

/// Returns a block from driverAllocInternal(). A null pointer is a no-op.
inline void driverFreeInternal(void* block) {
#if defined(ESP32) || defined(ESP_PLATFORM)
    heap_caps_free(block);
#else
    std::free(block);
#endif
}

/// Free bytes in the internal pool the driver allocates from; 0 on the host.
inline std::uint32_t driverFreeHeapBytes() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return static_cast<std::uint32_t>(esp_get_free_heap_size());
#else
    return 0u;
#endif
}

/// Total bytes of that internal pool; 0 on the host.
inline std::uint32_t driverHeapTotalBytes() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return static_cast<std::uint32_t>(
        heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    return 0u;
#endif
}

/// The largest single free block in that pool; 0 on the host.
///
/// The census half of the printed arithmetic (`heap_largest_before`,
/// `heap_largest_after`), not the sizing input: the world is sized from
/// driverArenaBudgetBytes()'s answer. On the board the two are the same
/// measurement; on the host there is no ESP32 heap to read, so this one is 0.
inline std::uint32_t driverLargestFreeBlockBytes() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    return 0u;
#endif
}

#if !defined(ESP32) && !defined(ESP_PLATFORM)
/// The arena budget the SDL2 simulator adopts, in bytes: 256 KB.
///
/// 256 KB is the arena the host tools already tick the world over (see the
/// "THE WORLD" comment in FrameDriver.cpp), so the simulator reuses the gates'
/// own figure instead of reading how much RAM the desktop happens to have. The
/// arithmetic behind the value:
///
///   - the container is a 3,078,396 B file mapping, so it costs the process
///     address space and page cache, not this arena's heap;
///   - E1M1's whole world -- every mobj, thinker, sector and the linedef-special
///     copy -- is what the board's 4 MB image opens in `chosen=18432` B, so
///     262,144 B is fourteen times the figure the shipped build adopts;
///   - the driver's rule (measured peak + 25 %, rounded up to 1 KB) still picks
///     the kept arena from its own probe, so the budget only has to be large
///     enough not to be the constraint -- which at ~14x it is not. On the host
///     the probe measures 23,760 B and the kept arena comes out at 30,720 B.
constexpr std::uint32_t kNativeArenaBudgetBytes = 256u * 1024u;
#endif

/// How large an arena the world may be given, in bytes.
///
/// This is the sizing input, and it is not the same question as the census
/// above. Board: exactly the largest free internal block -- the same call and
/// the same caps mask driverLargestFreeBlockBytes() makes, so the board's fit
/// check still runs against real free memory and its capacity is unchanged.
///
/// Host: kNativeArenaBudgetBytes, because a desktop heap has no cap to measure
/// and the census's 0 is "not measurable here". Returning 0 here would refuse
/// every world on the simulator, which is a portability gap, not a decision
/// about capacity.
inline std::uint32_t driverArenaBudgetBytes() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return static_cast<std::uint32_t>(
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    return kNativeArenaBudgetBytes;
#endif
}

/// Bytes the loop task's stack still had at its deepest point; 0 on the host.
///
/// Board: uxTaskGetStackHighWaterMark() in StackType_t units (one byte on this
/// port). The host has no loop task; frameStackFree() reports 0 there.
inline std::uint32_t driverStackFreeBytes() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    return static_cast<std::uint32_t>(uxTaskGetStackHighWaterMark(nullptr)) *
           static_cast<std::uint32_t>(sizeof(StackType_t));
#else
    return 0u;
#endif
}

/// The CPU cycle counter r_bsp.h's raster probe reads.
///
/// Board: the Xtensa CCOUNT special register, 32 bits at 240 MHz, wrapping
/// every 17.9 s -- far longer than a frame, and unsigned subtraction is still
/// correct across a wrap for any interval shorter than that. Host: 0, because
/// only `PIXELROOT32_DOOM_RASTER_PROBE` asks for it and that environment is a
/// board measurement; no host build defines the flag.
inline std::uint32_t driverProbeCycles() {
#if defined(ESP32) || defined(ESP_PLATFORM)
    std::uint32_t value;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(value));
    return value;
#else
    return 0u;
#endif
}

}  // namespace doomc
}  // namespace doom
