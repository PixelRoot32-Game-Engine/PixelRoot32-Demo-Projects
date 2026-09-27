#include "game/dialog/CityBanner.h"

#include <cstdint>

namespace top_down_city {

namespace dlg = pixelroot32::gameplay;

namespace {

/// DialogLine::autoAdvanceMs is 16 bits.
constexpr int kMaxBannerMs = 0xFFFF;

}  // namespace

CityBanner::CityBanner()
    : line_{nullptr, nullptr, dlg::kNoLine, 0, 0, 0, 0, dlg::LineKind::Text, 0},
      script_{&line_, nullptr, 1, 0},
      runner_() {
}

void CityBanner::show(const char* label, int ms) {
    if (ms <= 0) {
        hide();
        return;
    }
    line_.text          = label;
    line_.autoAdvanceMs = static_cast<std::uint16_t>(ms < kMaxBannerMs ? ms : kMaxBannerMs);
    // Always a fresh start, even over the same words: entering the line is
    // what restarts the timer and bumps the revision.
    runner_.start(script_);
}

void CityBanner::hide() {
    // Only a notice that is up. stop() on a runner that merely FINISHED
    // still counts as detaching a session and bumps the revision, which
    // would read as a change to a strip that was already empty.
    if (runner_.isActive()) {
        runner_.stop();
    }
}

void CityBanner::update(unsigned long deltaMs) {
    runner_.update(deltaMs);
}

bool CityBanner::visible() const {
    return runner_.isActive();
}

const char* CityBanner::label() const {
    const dlg::DialogLine* line = runner_.currentLine();
    return line != nullptr ? line->text : nullptr;
}

std::uint16_t CityBanner::revision() const {
    return runner_.revision();
}

}  // namespace top_down_city
