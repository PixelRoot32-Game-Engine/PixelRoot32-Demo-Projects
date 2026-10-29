#if defined(PLATFORM_ESP32DEV_ST7789)
#include "platforms/esp32_dev_ST7789.h"
#elif defined(PLATFORM_ESP32DEV_ILI9341)
#include "platforms/esp32_dev_ILI9341.h"
#elif defined(PLATFORM_NATIVE)
#include "platforms/native.h"
#else
#error "doom: build one of the environments native, esp32dev_ST7789 or esp32dev_ILI9341"
#endif
