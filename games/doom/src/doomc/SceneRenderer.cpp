#include "doomc/SceneRenderer.h"

#include "doomc/DoomConsole.h"
#include "palette/DoomPalette.h"

namespace doom {
namespace doomc {

namespace {

/// Bytes one `SHOW,mem,...` line can need. The line is `FRAMEPROF,mem`'s field
/// list with a shorter tag, so the probe's 1,024-byte budget covers it twice.
constexpr std::size_t kSceneMemoryLineCapacity = 1024u;

}  // namespace

bool SceneRenderer::open() {
    if (open_) {
        return true;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    if (!frameLevelOpen("SHOW", marker_)) {
        return false;
    }

    // The rasteriser wrote DOOM palette indices; the surface's bytes are RGB332.
    // The table is the whole bridge, and it comes from the WAD's own PLAYPAL so
    // the panel shows 1993's colours instead of the shell's synthetic ramp.
    const std::uint8_t* rgb888 = frameLevelPlaypal();
    if (rgb888 == nullptr) {
        writeLine("SHOW,setup,status=fail,reason=playpal\n");
        frameLevelClose();
        marker_[0] = '\0';
        return false;
    }
    palette::initFromPlaypal(rgb888);

    open_ = true;

    // The RAM record, from `sizeof` inside the session: the static blocks, the
    // heap before the level was bound and the decoded level bytes (0 in the
    // flash path). One line, once, so a captured boot log states the cost.
    char line[kSceneMemoryLineCapacity];
    if (frameLevelFormatMemory(line, sizeof line) > 0u) {
        writeLine(line);
    }
    return true;
#else
    // No `wad` partition and no renderer in this build: the scene keeps its
    // shell. Nothing is printed, because a shipped build that never defines the
    // flag must not grow a boot line it did not have.
    return false;
#endif
}

bool SceneRenderer::render(std::uint8_t* indexPixels, int width, int height, bool wantHash,
                           SceneFrameMeasurements* out) {
    if (!open_ || indexPixels == nullptr) {
        return false;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    FrameProfileSample sample;
    char hash[65] = {0};
    std::uint32_t hashMicros = 0u;
    if (!frameLevelRenderSpawn(indexPixels, width, height, &sample, hash, wantHash,
                               &hashMicros)) {
        return false;
    }

    if (out != nullptr) {
        out->renderMicros = sample.frameMicros;
        out->hashMicros = hashMicros;
        out->drawsegs = sample.drawsegs;
        out->hashed = wantHash && hash[0] != '\0';
        for (int index = 0; index < 65; ++index) {
            out->hash[index] = hash[index];
        }
    }
    return true;
#else
    (void)width;
    (void)height;
    (void)wantHash;
    (void)out;
    return false;
#endif
}

void SceneRenderer::close() {
#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    if (open_) {
        frameLevelClose();
    }
#endif
    open_ = false;
    marker_[0] = '\0';
}

}  // namespace doomc
}  // namespace doom
