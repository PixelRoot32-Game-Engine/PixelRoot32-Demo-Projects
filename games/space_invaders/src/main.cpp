#ifdef PLATFORM_NATIVE
#include "platforms/native.h"
#elif defined(PLATFORM_ESP32DEV_ILI9341)
#include "platforms/esp32_dev_ILI9341.h"
#else
#include "platforms/esp32_dev_ST7789.h"
#endif
