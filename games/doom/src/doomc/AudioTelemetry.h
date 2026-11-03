#pragma once

/*
 * The read side of the sound seam's telemetry, and the line that prints it.
 *
 * WHY THIS IS ITS OWN TRANSLATION UNIT
 *
 * `DoomAudio.cpp` carries the APU and the engine in every build that has sound
 * (`DOOM_AUDIO`). The two numbers this telemetry exists for -- the DMA underrun count and
 * the worst synthesis time -- are read through a source the *platform* installs
 * (src/platforms/esp32_dev_ILI9341.h owns the backend), and the line that prints
 * them is built
 * from plain integers. Keeping both here, free of the engine and the APU, is
 * what lets `env:sound_test` prove the seam on a host: no engine, no APU, no
 * board. It is also why the store exists in a build without `DOOM_AUDIO`: the
 * functions are defined once instead of being stubbed in the silent half.
 *
 * THE COST WHEN IDLE
 *
 * One function pointer and one user pointer in `.bss`, and the source is only
 * called when a caller asks (the scene's one-second report). Nothing polls it,
 * so an audio subsystem with nobody listening costs the frame path nothing.
 */

#include <cstddef>
#include <cstdint>

#include "doomc/DoomAudio.h"

namespace doom {
namespace doomc {

/// Room for the longest `SHOW,audio,...` line this file writes, terminator
/// included. The counters are ten digits each at most, so 192 B has slack.
constexpr std::size_t kAudioReportLineBytes = 192;

/**
 * @brief Formats the periodic `SHOW,audio,...` line.
 *
 * `plays=..,steals=..,refused=..,unknown=..,voices=..,active=..` always carry
 * numbers; `underruns=`/`worst_us=` carry numbers only when @p haveTelemetry is
 * true and `-` otherwise. `-` is the honest state for a build whose backend
 * exposes no counters (the native SDL2 one does not): a zero there would claim
 * a measurement nobody took.
 *
 * @return The character count written, or 0 when the buffer is unusable or the
 *         line did not fit.
 */
std::size_t formatAudioReportLine(char* out, std::size_t size,
                                  const DoomAudioStats& stats, int voices,
                                  int active, bool haveTelemetry,
                                  const DoomAudioTelemetry& telemetry);

}  // namespace doomc
}  // namespace doom
