#if defined(PLATFORM_ESP32DEV_ST7789)
#include "platforms/esp32_dev_ST7789.h"
#elif defined(PLATFORM_NATIVE)
#include "platforms/native.h"
#else
#error "doom: build one of the environments native or esp32dev_ST7789"
#endif
