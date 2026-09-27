#pragma once

/**
 * @brief The one Arduino symbol the engine's EngineConfig.h reads, for the
 *        dialog_test environment only.
 *
 * EngineConfig.h includes <Arduino.h> on every target that is not
 * PLATFORM_NATIVE, and the native branch pulls SDL2 in through MockArduino.h.
 * dialog_test defines neither, so this header is what it finds instead. The
 * adapter under test never reads the clock, so a clock that never moves is
 * honest here.
 */
inline unsigned long micros() { return 0; }
