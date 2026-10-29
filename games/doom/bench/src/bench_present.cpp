// D0.1/D0.2 - cost of the real present path.
//
// The drawer leaves the last DMA block of a frame in flight on purpose, so a
// plain sendBuffer() timing would omit that block's wire time. The probe
// measures both halves: sendBuffer() alone (the engine's per-frame cost when
// the tail overlaps the next frame's draw) and sendBuffer() plus
// flushPendingTransfers() (the complete, serialised frame). `wire_us` is the
// theoretical floor from the requested SPI clock and the byte count.

#include "bench_present.h"

#include "bench_common.h"

#include <cstdint>
#include <cstring>
#include <graphics/DrawSurface.h>
#include <platforms/EngineConfig.h>
#include <platforms/PlatformDefaults.h>

namespace bench {
namespace {

// TFT_eSPI's color8to16() treats an 8-bit sprite value as RGB332:
// RRRGGGBB. These are the indices that become pure red, green, blue,
// yellow, white and black through the drawer's palette LUT.
constexpr uint8_t kBlack = 0x00;
constexpr uint8_t kWhite = 0xFF;
constexpr uint8_t kRed = 0xE0;
constexpr uint8_t kGreen = 0x1C;
constexpr uint8_t kBlue = 0x03;
constexpr uint8_t kYellow = 0xFC;

void drawPatternRect(uint8_t* buffer, int width, int height,
                     int x, int y, int w, int h, uint8_t color) {
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > width) {
        w = width - x;
    }
    if (y + h > height) {
        h = height - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int row = y; row < y + h; ++row) {
        std::memset(buffer + static_cast<size_t>(row) * static_cast<size_t>(width) + x,
                    color,
                    static_cast<size_t>(w));
    }
}

} // namespace

void runPresentProbe(pixelroot32::graphics::DrawSurface& surface,
                     const char* probeId,
                     bool reportBlockSize,
                     int frames) {
    uint8_t* buffer = surface.getSpriteBuffer();
    if (buffer == nullptr) {
        csvPrefix(probeId);
        Serial.printf(",status=no_sprite_buffer\n");
        return;
    }

    const int count = frames > 0 ? frames : 1;
    const int logicalWidth = LOGICAL_WIDTH;
    const int logicalHeight = LOGICAL_HEIGHT;
    const size_t pixelCount =
        static_cast<size_t>(logicalWidth) * static_cast<size_t>(logicalHeight);

    fillPresentPattern(buffer, logicalWidth, logicalHeight);
    const uint32_t expectedChecksum = crc32(buffer, pixelCount);

    // Warm-up: one full frame plus its tail, so the first timed frame does not
    // pay for an empty pipeline or a cold cache.
    surface.sendBuffer();
    surface.flushPendingTransfers();

    uint32_t minUs = UINT32_MAX;
    uint32_t maxUs = 0;
    uint64_t totalUs = 0;
    uint64_t callUs = 0;
    uint64_t drainUs = 0;
    uint32_t badFrames = 0;
    uint32_t lastChecksum = expectedChecksum;

    for (int frame = 0; frame < count; ++frame) {
        const uint32_t start = micros();
        surface.sendBuffer();
        const uint32_t afterCall = micros();
        surface.flushPendingTransfers();
        const uint32_t end = micros();

        const uint32_t frameUs = end - start;
        totalUs += frameUs;
        callUs += afterCall - start;
        drainUs += end - afterCall;

        if (frameUs < minUs) {
            minUs = frameUs;
        }
        if (frameUs > maxUs) {
            maxUs = frameUs;
        }

        lastChecksum = crc32(buffer, pixelCount);
        if (lastChecksum != expectedChecksum) {
            ++badFrames;
        }
    }

    const uint32_t wireBytes =
        static_cast<uint32_t>(PHYSICAL_DISPLAY_WIDTH) *
        static_cast<uint32_t>(PHYSICAL_DISPLAY_HEIGHT) * 2u;
#if defined(SPI_FREQUENCY) && (SPI_FREQUENCY > 0)
    const unsigned long spiHz = static_cast<unsigned long>(SPI_FREQUENCY);
    const uint32_t wireUs = static_cast<uint32_t>(
        (static_cast<uint64_t>(wireBytes) * 8u * 1000000u) /
        static_cast<uint64_t>(SPI_FREQUENCY));
#else
    const unsigned long spiHz = 0;
    const uint32_t wireUs = 0;
#endif

    const uint32_t divisor = static_cast<uint32_t>(count);

    csvPrefix(probeId);
    Serial.printf(",frames=%d,physical=%dx%d,logical=%dx%d,spi_hz=%lu,wire_bytes=%lu,wire_us=%lu",
                  count,
                  PHYSICAL_DISPLAY_WIDTH,
                  PHYSICAL_DISPLAY_HEIGHT,
                  logicalWidth,
                  logicalHeight,
                  static_cast<unsigned long>(spiHz),
                  static_cast<unsigned long>(wireBytes),
                  static_cast<unsigned long>(wireUs));

    if (reportBlockSize) {
        const uint32_t dmaBytesPerBuffer =
            static_cast<uint32_t>(PHYSICAL_DISPLAY_WIDTH) * 2u *
            static_cast<uint32_t>(PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK);
        Serial.printf(",lines_per_block=%d,dma_bytes_per_buffer=%lu,free_internal_at_init=%lu",
                      PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK,
                      static_cast<unsigned long>(dmaBytesPerBuffer),
                      static_cast<unsigned long>(gFreeInternalAfterInit));
    }

    Serial.printf(",present_us_min=%lu,present_us_mean=%lu,present_us_max=%lu,call_us_mean=%lu,drain_us_mean=%lu,bad_frames=%lu,checksum=0x%08lX,last_checksum=0x%08lX\n",
                  static_cast<unsigned long>(minUs),
                  static_cast<unsigned long>(totalUs / divisor),
                  static_cast<unsigned long>(maxUs),
                  static_cast<unsigned long>(callUs / divisor),
                  static_cast<unsigned long>(drainUs / divisor),
                  static_cast<unsigned long>(badFrames),
                  static_cast<unsigned long>(expectedChecksum),
                  static_cast<unsigned long>(lastChecksum));
}

// D0.6 - letterbox validation.
//
// Everything is drawn in logical coordinates; the drawer's xLUT/yLUT is what
// squashes it into the physical window, so what the photo shows is the path
// the port actually uses. The four markers survive the 0.75x namesake because
// they are blocks, not single pixels.
void runLetterboxProbe(pixelroot32::graphics::DrawSurface& surface, uint32_t holdMs) {
    uint8_t* buffer = surface.getSpriteBuffer();
    if (buffer == nullptr) {
        csvPrefix("D0.6");
        Serial.printf(",status=no_sprite_buffer\n");
        return;
    }

    const int width = LOGICAL_WIDTH;
    const int height = LOGICAL_HEIGHT;
    const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height);

    std::memset(buffer, kBlack, bytes);

    constexpr int kFrame = 4;
    constexpr int kMarker = 28;
    constexpr int kInset = kFrame + 2;

    drawPatternRect(buffer, width, height, 0, 0, width, kFrame, kWhite);
    drawPatternRect(buffer, width, height, 0, height - kFrame, width, kFrame, kWhite);
    drawPatternRect(buffer, width, height, 0, 0, kFrame, height, kWhite);
    drawPatternRect(buffer, width, height, width - kFrame, 0, kFrame, height, kWhite);

    drawPatternRect(buffer, width, height, kInset, kInset, kMarker, kMarker, kRed);
    drawPatternRect(buffer, width, height, width - kInset - kMarker, kInset, kMarker, kMarker, kGreen);
    drawPatternRect(buffer, width, height, kInset, height - kInset - kMarker, kMarker, kMarker, kBlue);
    drawPatternRect(buffer, width, height,
                    width - kInset - kMarker, height - kInset - kMarker,
                    kMarker, kMarker, kYellow);

    surface.sendBuffer();
    surface.flushPendingTransfers();

    csvPrefix("D0.6");
    Serial.printf(",physical=%dx%d,logical=%dx%d,x_offset=%d,y_offset=%d,rotation=%d,hold_ms=%lu,frame_px=%d,marker_px=%d,corner_tl=red,corner_tr=green,corner_bl=blue,corner_br=yellow\n",
                  PHYSICAL_DISPLAY_WIDTH,
                  PHYSICAL_DISPLAY_HEIGHT,
                  width,
                  height,
                  X_OFF_SET,
                  Y_OFF_SET,
                  DISPLAY_ROTATION,
                  static_cast<unsigned long>(holdMs),
                  kFrame,
                  kMarker);

    delay(holdMs);
}

} // namespace bench
