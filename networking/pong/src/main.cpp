/*
 * Networking Pong - target dispatch.
 *
 * Exactly one platform header is included, and that header is the only place in
 * the build that defines setup()/loop() (or main() on native). Two headers in
 * one translation unit would define setup() twice.
 */
#if defined(PLATFORM_ESP32CYD)
#include "platforms/esp32_cyd.h"
#elif defined(NET_PONG_ROLE_CONTROLLER)
#include "platforms/esp32_c3_controller.h"
#elif defined(PLATFORM_NATIVE)
#include "platforms/native.h"
#else
#error "networking/pong: pick a target — PLATFORM_ESP32CYD (env:esp32cyd_host), NET_PONG_ROLE_CONTROLLER (env:esp32c3_p1/p2) or PLATFORM_NATIVE (env:native)"
#endif
