/*
 * The telemetry seam: who supplies the APU's counters, and the line that shows
 * them. See AudioTelemetry.h for why this is engine-free.
 */
#include "doomc/AudioTelemetry.h"

#include <cstdio>

namespace {

// Installed once by the platform header, after engine.init() has started the
// audio task, and never cleared in a running build. Not atomic: the writer runs
// before the scene does and the reader is the same loop; the counters it reads
// are the backend's own atomics.
doom::doomc::DoomAudioTelemetrySource gTelemetrySource = nullptr;
void* gTelemetryUser = nullptr;

}  // namespace

namespace doom {
namespace doomc {

std::size_t formatAudioReportLine(char* out, std::size_t size,
                                  const DoomAudioStats& stats, int voices,
                                  int active, bool haveTelemetry,
                                  const DoomAudioTelemetry& telemetry) {
    if (out == nullptr || size == 0u) {
        return 0u;
    }

    // One format string, with the two telemetry fields as a tail: the counters
    // are the same either way, and a single call site is what keeps this
    // affordable in the shipped image (the alternative is the same prefix
    // spelled twice, which the linker must keep twice).
    char tail[40];
    if (haveTelemetry) {
        std::snprintf(tail, sizeof tail, "underruns=%u,worst_us=%u",
                      static_cast<unsigned>(telemetry.underruns),
                      static_cast<unsigned>(telemetry.worstUs));
    } else {
        std::snprintf(tail, sizeof tail, "underruns=-,worst_us=-");
    }

    const int written = std::snprintf(
        out, size,
        "SHOW,audio,plays=%u,steals=%u,refused=%u,unknown=%u,"
        "voices=%d,active=%d,%s\n",
        static_cast<unsigned>(stats.plays), static_cast<unsigned>(stats.steals),
        static_cast<unsigned>(stats.refused), static_cast<unsigned>(stats.unknown),
        voices, active, tail);

    // snprintf's negative answer means an encoding error, and its truncation
    // answer is the length it *would* have written: both leave no usable line.
    if (written <= 0 || static_cast<std::size_t>(written) >= size) {
        out[0] = '\0';
        return 0u;
    }
    return static_cast<std::size_t>(written);
}

void doomAudioSetTelemetrySource(DoomAudioTelemetrySource source, void* user) {
    gTelemetrySource = source;
    gTelemetryUser = user;
}

bool doomAudioReadTelemetry(DoomAudioTelemetry* out) {
    if (gTelemetrySource == nullptr || out == nullptr) {
        return false;
    }
    return gTelemetrySource(gTelemetryUser, out);
}

}  // namespace doomc
}  // namespace doom
