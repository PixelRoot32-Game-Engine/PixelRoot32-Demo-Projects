#include "doomc/DoomConsole.h"

// The engine header is probed rather than required. Every firmware environment
// links the engine, so it is on the include path there and the real primitive
// is used; `env:link_test` installs no library dependency, so the probe is
// false and this file makes the same call the engine itself makes on a host.
// That is what keeps the C <-> C++ seam free of an engine dependency, and the
// #error below keeps the board from silently taking the fallback.
#if defined(__has_include)
#  if __has_include(<platforms/PlatformLog.h>)
#    include <platforms/PlatformLog.h>
#    define DOOMC_HAS_ENGINE_CONSOLE 1
#  endif
#endif

// Every panel environment links the engine, so on the board a missing header is
// a broken dependency or a broken include path, never a supported build: the
// stdio fallback below would write into the Arduino core's stdout and no
// `LINK,...` line would ever reach the serial port. Fail the build instead of
// shipping a console that prints nowhere.
#if defined(ESP32) && !defined(DOOMC_HAS_ENGINE_CONSOLE)
#  error "DoomConsole: platforms/PlatformLog.h is not on the include path"
#endif

#if !defined(DOOMC_HAS_ENGINE_CONSOLE)
#include <cstdio>
#endif

namespace doom {
namespace doomc {

void writeLine(const char* line) {
#if defined(DOOMC_HAS_ENGINE_CONSOLE)
    // Serial on the board, stdout on the simulator; both flush per call, which
    // is why every line in this port is complete before it is written.
    pixelroot32::core::logging::platformPrint(line);
#else
    std::fputs(line, stdout);
    std::fflush(stdout);
#endif
}

}  // namespace doomc
}  // namespace doom
