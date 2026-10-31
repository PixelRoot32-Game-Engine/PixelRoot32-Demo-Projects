#include "doomc/SceneRenderer.h"

#include <cstdio>

#include "doomc/DoomConsole.h"
#include "palette/DoomPalette.h"

namespace doom {
namespace doomc {

namespace {

/// Bytes one `SHOW,mem,...` line can need. The line is `FRAMEPROF,mem`'s field
/// list with a shorter tag, so the probe's 1,024-byte budget covers it twice.
constexpr std::size_t kSceneMemoryLineCapacity = 1024u;

/// Bytes the frame-00 verification line can need: a 64-character hash, four
/// viewpoint numbers, the hash time and the newline.
constexpr std::size_t kSpawnCheckLineCapacity = 256u;

}  // namespace

bool SceneRenderer::open() {
    if (open_) {
        return true;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    if (!frameLevelOpen("SHOW", marker_)) {
        return false;
    }

    // The rasteriser writes DOOM palette indices, so the colour bridge is the
    // WAD's own PLAYPAL. This builds both forms once: the RGB565 table the scene
    // installs on the 8bpp surface with setPalette8(), and the RGB332 byte table
    // for any pass that still stores colours. The panel shows 1993's palette,
    // not the shell's synthetic ramp.
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

bool SceneRenderer::runTics(std::uint32_t tics, std::uint8_t buttons) {
    if (!open_) {
        return false;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    if (tics == 0u) {
        // The frame covered no tic: nothing to run, and not a failure. A frame
        // faster than 28.57 ms is normal here -- the renderer is the slower half.
        return true;
    }
    return frameLevelRunTics(tics, buttons);
#else
    (void)tics;
    (void)buttons;
    return false;
#endif
}

bool SceneRenderer::render(std::uint8_t* indexPixels, int width, int height, bool wantHash,
                           SceneFrameMeasurements* out) {
    if (!open_ || indexPixels == nullptr) {
        return false;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    LevelPlayer state;
    if (!frameLevelPlayer(&state)) {
        // A closed world is not a capacity refusal and has no pass code. The
        // scene keeps drawing the last picture all the same: only a session that
        // never opened falls back to the shell.
        if (out != nullptr) {
            out->refusedStage = "player";
            out->refusedCode = -1;
        }
        return false;
    }

    FrameProfileSample sample;
    char hash[65] = {0};
    std::uint32_t hashMicros = 0u;
    if (!frameLevelRenderView(indexPixels, width, height, state.view.x, state.view.y,
                              state.view.z, state.view.angle, &sample, hash, wantHash,
                              &hashMicros)) {
        // The driver recorded which pass refused and with which code; a caller
        // that only saw "false" could not tell this from a closed session (D4.2g).
        if (out != nullptr) {
            frameLevelLastRefusal(&out->refusedStage, &out->refusedCode);
        }
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

ScenePlayerState SceneRenderer::player() const {
    ScenePlayerState state;

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    if (!open_) {
        return state;
    }
    LevelPlayer live;
    if (!frameLevelPlayer(&live)) {
        return state;
    }
    state.valid = true;
    state.x = live.view.x;
    state.y = live.view.y;
    state.viewz = live.view.z;
    state.angle = live.view.angle;
    state.health = live.health;
    state.tics = live.leveltime;
#endif
    return state;
}

bool SceneRenderer::verifySpawnFrame(std::uint8_t* indexPixels, int width, int height) {
    if (!open_ || indexPixels == nullptr) {
        return false;
    }

#if defined(PIXELROOT32_DOOM_SHOW_E1M1)
    LevelView spawn;
    if (!frameLevelSpawnView(&spawn)) {
        return false;
    }

    FrameProfileSample sample;
    char hash[65] = {0};
    std::uint32_t hashMicros = 0u;
    if (!frameLevelRenderView(indexPixels, width, height, spawn.x, spawn.y, spawn.z,
                              spawn.angle, &sample, hash, true, &hashMicros)) {
        return false;
    }

    char line[kSpawnCheckLineCapacity];
    std::snprintf(line, sizeof line,
                  "SHOW,frame00,x=%ld,y=%ld,z=%ld,angle=%lu,hash_us=%u,hash=%s\n",
                  static_cast<long>(spawn.x), static_cast<long>(spawn.y),
                  static_cast<long>(spawn.z), static_cast<unsigned long>(spawn.angle),
                  static_cast<unsigned>(hashMicros), hash);
    writeLine(line);
    return hash[0] != '\0';
#else
    (void)width;
    (void)height;
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
