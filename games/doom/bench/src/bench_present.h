// D0.1 (clock sweep), D0.2 (block size) and D0.6 (letterbox).
#pragma once

#include <cstdint>

namespace pixelroot32::graphics {
class DrawSurface;
}

namespace bench {

/// Presents `frames` fixed frames through the real drawer and reports the
/// per-frame cost. `present_us` is the full serialised frame (sendBuffer plus
/// the deferred DMA tail); `call_us` is sendBuffer alone, the cost the engine
/// loop pays when the tail overlaps the next frame's work.
///
/// `reportBlockSize` adds the D0.2 fields: the compile-time lines-per-block,
/// the DMA bytes that implies and the post-init free heap.
void runPresentProbe(pixelroot32::graphics::DrawSurface& surface,
                     const char* probeId,
                     bool reportBlockSize,
                     int frames = 100);

} // namespace bench
