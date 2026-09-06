#include "game/rules/MinimapRuns.h"

namespace top_down_city::minimap {

int rowRuns(const std::uint8_t* swatches, int count, Run* out) {
    if (swatches == nullptr || out == nullptr || count <= 0) {
        return 0;
    }

    int emitted   = 0;
    int runStart  = -1;                 // -1 means "no run open"
    std::uint8_t runSwatch = kAbsent;

    for (int i = 0; i < count; ++i) {
        const std::uint8_t swatch = swatches[i];

        if (swatch == kAbsent) {
            if (runStart >= 0) {
                out[emitted++] = Run{runStart, i - runStart, runSwatch};
                runStart = -1;
            }
            continue;
        }
        if (runStart >= 0 && swatch == runSwatch) {
            continue;                   // the run grows
        }
        if (runStart >= 0) {
            out[emitted++] = Run{runStart, i - runStart, runSwatch};
        }
        runStart  = i;
        runSwatch = swatch;
    }

    // The row ended with a run still open. Forgetting this is the whole bug:
    // the right-hand column of the radar goes missing, and on a plate that is
    // now always on screen that reads as the map being wrong.
    if (runStart >= 0) {
        out[emitted++] = Run{runStart, count - runStart, runSwatch};
    }
    return emitted;
}

}  // namespace top_down_city::minimap
