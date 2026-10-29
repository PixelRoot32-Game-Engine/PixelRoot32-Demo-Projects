// Phase 0 bench entry point: boot banner, serial menu, dispatch.
//
// No engine loop and no DOOM code: each probe is a one-shot function the
// maintainer triggers from the serial console. The drawer is the engine's real
// TFT_eSPI_Drawer, so every present number includes the actual LUT conversion
// and DMA path the port will ship.

#include <Arduino.h>
#include <drivers/esp32/TFT_eSPI_Drawer.h>
#include <graphics/DrawSurface.h>
#include <platforms/EngineConfig.h>
#include <platforms/PlatformDefaults.h>

#include "bench_common.h"
#include "bench_flash.h"
#include "bench_memory.h"
#include "bench_present.h"

// S8: DOOM's renderer is 320 columns wide; the panel only changes the physical
// window. A bench build that moved LOGICAL would measure a different product.
static_assert(LOGICAL_WIDTH == 320 && LOGICAL_HEIGHT == 200,
              "the DOOM surface is 320x200 in every configuration (S8)");

namespace {

pixelroot32::drivers::esp32::TFT_eSPI_Drawer gDrawer;
bool gDisplayReady = false;

// The drawer is configured and initialised once, at boot, so D0.4's
// before/after census is captured without depending on which probe ran first.
// Rotation is applied after init(): the drawer's setRotation() talks to the
// panel, and by then TFT_eSPI has configured the bus.
void ensureDisplay() {
    if (gDisplayReady) {
        return;
    }

    bench::gFreeInternalBeforeInit = bench::freeInternal();
    bench::gLargestInternalBeforeInit = bench::largestInternal();

    gDrawer.setDisplaySize(LOGICAL_WIDTH, LOGICAL_HEIGHT);
    gDrawer.setPhysicalSize(PHYSICAL_DISPLAY_WIDTH, PHYSICAL_DISPLAY_HEIGHT);
    gDrawer.setOffset(X_OFF_SET, Y_OFF_SET);
    gDrawer.init();
    gDrawer.setRotation(DISPLAY_ROTATION);

    bench::gFreeInternalAfterInit = bench::freeInternal();
    bench::gLargestInternalAfterInit = bench::largestInternal();
    gDisplayReady = true;
}

unsigned long spiFrequency() {
#ifdef SPI_FREQUENCY
    return static_cast<unsigned long>(SPI_FREQUENCY);
#else
    return 0UL;
#endif
}

void printHelp() {
    Serial.println("help: 1=D0.1 clock 2=D0.2 block 3=D0.3 fill 4=D0.4 dram 5=D0.5 flash 6=D0.6 letterbox h=help");
}

void printBanner() {
    bench::csvPrefix("BOOT");
    Serial.printf(",physical=%dx%d,logical=%dx%d,x_offset=%d,y_offset=%d,rotation=%d,spi_hz=%lu,lines_per_block=%d,free_internal=%lu,largest_internal=%lu\n",
                  PHYSICAL_DISPLAY_WIDTH,
                  PHYSICAL_DISPLAY_HEIGHT,
                  LOGICAL_WIDTH,
                  LOGICAL_HEIGHT,
                  X_OFF_SET,
                  Y_OFF_SET,
                  DISPLAY_ROTATION,
                  spiFrequency(),
                  PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK,
                  static_cast<unsigned long>(bench::freeInternal()),
                  static_cast<unsigned long>(bench::largestInternal()));

    // Arithmetic the plan can use before the board is flashed: the surface the
    // drawer allocates, the DMA blocks each probe env requests, and the LUTs.
    const uint32_t surfaceBytes =
        static_cast<uint32_t>(LOGICAL_WIDTH) * static_cast<uint32_t>(LOGICAL_HEIGHT);
    const uint32_t dmaBytesPerBuffer =
        static_cast<uint32_t>(PHYSICAL_DISPLAY_WIDTH) * 2u *
        static_cast<uint32_t>(PIXELROOT32_TFT_ESPI_LINES_PER_BLOCK);
    const uint32_t lutBytes =
        256u * 2u +
        static_cast<uint32_t>(PHYSICAL_DISPLAY_WIDTH) * 2u +
        static_cast<uint32_t>(PHYSICAL_DISPLAY_HEIGHT) * 2u;

    bench::csvPrefix("STATIC");
    Serial.printf(",surface_bytes=%lu,dma_bytes_per_buffer=%lu,dma_bytes_total=%lu,lut_bytes=%lu,present_state_bytes=%lu,drawer_size=%u\n",
                  static_cast<unsigned long>(surfaceBytes),
                  static_cast<unsigned long>(dmaBytesPerBuffer),
                  static_cast<unsigned long>(dmaBytesPerBuffer * 2u),
                  static_cast<unsigned long>(lutBytes),
                  static_cast<unsigned long>(surfaceBytes + dmaBytesPerBuffer * 2u + lutBytes),
                  static_cast<unsigned>(sizeof(gDrawer)));

    printHelp();
}

void runProbe(int mode) {
    switch (mode) {
    case 1:
        bench::runPresentProbe(gDrawer, "D0.1", false);
        break;
    case 2:
        bench::runPresentProbe(gDrawer, "D0.2", true);
        break;
    case 3:
        bench::runFillProbe();
        break;
    case 4:
        bench::runDramCensus();
        break;
    case 5:
        bench::runFlashProbe();
        break;
    case 6:
        bench::runLetterboxProbe(gDrawer);
        break;
    default:
        Serial.printf("probe %d is not implemented in this build\n", mode);
        break;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(200); // let the USB-UART bridge settle before the first line

    printBanner();
    ensureDisplay();

    bench::csvPrefix("BOOT");
    Serial.printf(",display=ready,free_internal_after_init=%lu,largest_internal_after_init=%lu\n",
                  static_cast<unsigned long>(bench::gFreeInternalAfterInit),
                  static_cast<unsigned long>(bench::gLargestInternalAfterInit));

#ifdef BENCH_DEFAULT_MODE
    if (BENCH_DEFAULT_MODE >= 1 && BENCH_DEFAULT_MODE <= 6) {
        runProbe(BENCH_DEFAULT_MODE);
    }
#endif

    Serial.print("> ");
}

void loop() {
    while (Serial.available() > 0) {
        const int command = Serial.read();
        if (command == '\r' || command == '\n' || command == ' ') {
            continue;
        }
        if (command >= '1' && command <= '6') {
            runProbe(command - '0');
        } else if (command == 'h' || command == 'H' || command == '?') {
            printHelp();
        } else {
            Serial.printf("unknown command '%c'\n", command);
            printHelp();
        }
        Serial.print("> ");
    }
    delay(20);
}
