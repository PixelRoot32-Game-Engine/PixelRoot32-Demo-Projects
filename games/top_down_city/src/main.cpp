#if defined(PLATFORM_NATIVE)
#include "platforms/native.h"
#elif defined(PLATFORM_ESP32DEV_ILI9341)
#include "platforms/esp32_dev_ILI9341.h"
#elif defined(PLATFORM_ESP32DEV_ST7789)
#include "platforms/esp32_dev_ST7789.h"
#else
#error "Define a supported platform: PLATFORM_NATIVE, PLATFORM_ESP32DEV_ILI9341, or PLATFORM_ESP32DEV_ST7789"
#endif
