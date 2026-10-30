/*
 * The C++ side of the DOOM port's C <-> C++ seam.
 *
 * Everything here is a C symbol: each function is declared in
 * lib/doom/doom_link_probe.h inside the header's `extern "C"` guard and defined
 * here with `extern "C"`, so the C core links against plain names and not
 * against mangled ones. Including that header is what makes the compiler check
 * every definition against its declaration -- a definition the header forgot is
 * a stray symbol, and a declaration without one is a link error, never a stub.
 *
 * The chain these three entry points close (D1.5):
 *
 *     doom_link_probe_run()              C   lib/doom/doom_link_probe.c
 *       doom_bridge_mix()                C -> C++  this file
 *       doom_bridge_palette_byte()       C -> C++ -> doom::palette::indexTable()
 *       doom_bridge_emit_line()          C -> C++ -> doomc::writeLine()
 *                                                    -> logging::platformPrint()
 *                                                       (engine, C++)
 *     ... and the verified value returns to the C caller above.
 *
 * Two connections matter as much as the call itself:
 *
 *  - the engine is reached through the port's console symbol
 *    (src/doomc/DoomConsole.cpp), the same symbol `DoomScene` writes its own
 *    lines through, so the `LINK,...` record cannot take a different path from
 *    the rest of the serial contract;
 *  - the port is reached through `doom::palette::indexTable()`, an out-of-line
 *    symbol defined in another translation unit of this port, so the probe
 *    would not link if src/palette were dropped from the build.
 *
 * No allocation, no container, no floating point, no exceptions: the firmware
 * compiles this file with -fno-exceptions and -fno-rtti from lib/platformio.ini.
 * A fixed stack buffer is the only storage, so the probe is safe to run from
 * `DoomScene::init()` before anything else has been set up.
 */
#include "doom_link_probe.h"

#include "doomc/DoomConsole.h"
#include "palette/DoomPalette.h"

#include <cstddef>
#include <cstdio>

namespace {

/// Capacity of the line buffer: `LINK,status=fail,checksum=0xFFFFFFFF\n` is 39
/// bytes including the newline, so 48 leaves room for the terminator and for a
/// status word added later.
constexpr std::size_t kLineCapacity = 48;

/// The status words the record prints, indexed by the header's verdict.
constexpr const char* kOkWord = "ok";
constexpr const char* kFailWord = "fail";

}  // namespace

extern "C" uint32_t doom_bridge_mix(uint32_t value) {
    // The whole transform, and the whole point of the return path: the C side
    // computes `value ^ DOOM_LINK_SALT` on its own and compares. An XOR is
    // invertible, so a call that lost, truncated or byte-swapped a bit cannot
    // produce the expected answer by accident.
    return value ^ DOOM_LINK_SALT;
}

extern "C" uint32_t doom_bridge_palette_byte(uint32_t palette_index) {
    // A real port symbol, out of line in src/palette/DoomPalette.cpp: the probe
    // fails to link if that translation unit leaves the build, which is the
    // property a stub inside this file would not have.
    if (palette_index >= static_cast<uint32_t>(doom::palette::kEntryCount)) {
        // Outside the table there is no byte to report. The probe only asks for
        // in-range indices; this is the deterministic answer for anything else.
        return 0u;
    }

    // indexTable() builds the table on first use, so the probe does not depend
    // on DoomScene::init() having called palette::init() first.
    return static_cast<uint32_t>(doom::palette::indexTable()[palette_index]);
}

extern "C" void doom_bridge_emit_line(uint32_t status, uint32_t checksum) {
    char line[kLineCapacity];

    // Exactly one line per call, and the probe calls this once per run, so a
    // boot prints one LINK record. The checksum is the C side's own value: an
    // operator reading the serial log compares it with the checksum a host run
    // prints, without trusting anything the bridge computed.
    std::snprintf(line, sizeof line, "LINK,status=%s,checksum=0x%08X\n",
                  status == static_cast<uint32_t>(DOOM_LINK_STATUS_OK) ? kOkWord : kFailWord,
                  static_cast<unsigned>(checksum));

    // The port's console, which calls the engine's platformPrint() when the
    // engine is present and stdio when it is not. Same door as every line in
    // DoomScene.cpp.
    doom::doomc::writeLine(line);
}
