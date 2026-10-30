#include "perf/FrameLog.h"

#if defined(PIXELROOT32_DOOM_FRAME_LOG)

#include <cstddef>
#include <cstdio>

namespace doom {
namespace perf {

namespace {

/// Largest line the format below can produce, NUL included.
///
/// "FRAME," (6) + five "name=" pairs with 32-bit values (~90) + '\n' + NUL is
/// under 100 bytes; 112 keeps a margin without relying on the exact widths.
constexpr std::size_t kLineCapacity = 112;

}  // namespace

void FrameLog::beginFrame(std::uint32_t micros, std::uint32_t tic) {
    frameStartMicros_ = micros;
    renderMicros_ = 0;
    tic_ = tic;
    freeHeap_ = 0;
    open_ = true;
}

void FrameLog::endDraw(std::uint32_t micros, std::uint32_t freeHeap) {
    if (!open_) {
        return;
    }

    // Unsigned subtraction is the wrap-safe form: a sample taken after the
    // 32-bit micros() wrap still yields the forward distance.
    renderMicros_ = micros - frameStartMicros_;
    freeHeap_ = freeHeap;
}

void FrameLog::endFrame(std::uint32_t micros) {
    if (!open_) {
        return;
    }

    const std::uint32_t periodMicros = micros - frameStartMicros_;
    emit(periodMicros);

    ++frameIndex_;
    open_ = false;
}

void FrameLog::emit(std::uint32_t periodMicros) {
    // A residual cannot go negative. In the real loop render_us is bounded by
    // the period because both are forward samples, but a reset or an
    // out-of-order source must clamp instead of printing a huge unsigned value.
    const std::uint32_t presentMicros =
        periodMicros > renderMicros_ ? periodMicros - renderMicros_ : 0;

    char line[kLineCapacity];
    std::snprintf(line, sizeof line,
                  "FRAME,i=%u,tic=%u,render_us=%u,present_us=%u,free_heap=%u\n",
                  static_cast<unsigned>(frameIndex_), static_cast<unsigned>(tic_),
                  static_cast<unsigned>(renderMicros_), static_cast<unsigned>(presentMicros),
                  static_cast<unsigned>(freeHeap_));

    if (sink_ != nullptr) {
        sink_(line);
    }
}

}  // namespace perf
}  // namespace doom

#endif  // PIXELROOT32_DOOM_FRAME_LOG
