#include "LunarPoolScene.h"

#include "pool/Fixed.h"
#include "pool/Geometry.h"
#include "pool/Tables.h"

namespace pr32 = pixelroot32;

namespace lunar_pool {

namespace {

/// Row-scan span buffer for one felt-fill row. 16 covers table 1's simplest
/// shape (2 spans/row almost everywhere) with headroom for a row that
/// crosses a pocket notch and picks up extra crossings.
constexpr uint8_t kMaxRowSpans = 16;

}  // namespace

void LunarPoolScene::init() {
    Scene::init();  // resetState() + physicsScheduler.init() — always call base first
    // No entities yet: the demo has no actors, only a hand-drawn table. Only
    // stage 1 exists so far (see pool::kStageCount), so it is loaded once
    // here rather than re-loaded every frame.
    tableError_ = pool::loadTable(pool::tableForStage(1), table_);
}

void LunarPoolScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);
    // Input mapping, pacing, and pool::Game::step() are wired in a later
    // slice, once src/pool/Game exists.
}

void LunarPoolScene::draw(pr32::graphics::Renderer& renderer) {
    if (tableError_ != pool::TableError::None) {
        // No text overlay exists yet (added once the HUD/overlay slice
        // lands), so a solid color is the only way to surface a bad table
        // instead of drawing from a partially-written Table or crashing.
        renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                      pr32::graphics::Color::Red);
        Scene::draw(renderer);
        return;
    }

    // HUD band placeholder (content added once the HUD exists) plus the
    // rail color across the whole table area; the felt fill below overpaints
    // every row inside the border, leaving the rail visible only at the
    // pocket notches and the outer margin.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), 40, pr32::graphics::Color::Black);
    renderer.drawFilledRectangle(0, 40, renderer.getLogicalWidth(), renderer.getLogicalHeight() - 40,
                                  pr32::graphics::Color::DarkRed);

    const pool::Polyline& border = pool::tableForStage(1).border;
    int16_t xs[kMaxRowSpans];
    for (int16_t y = 40; y < renderer.getLogicalHeight(); ++y) {
        const uint8_t count = pool::rowSpans(border, y, xs, kMaxRowSpans);
        for (uint8_t i = 0; static_cast<uint8_t>(i + 1) < count; i = static_cast<uint8_t>(i + 2)) {
            renderer.drawFilledRectangle(xs[i], y, xs[i + 1] - xs[i], 1, pr32::graphics::Color::Navy);
        }
    }

    for (uint8_t s = 0; s < table_.segmentCount; ++s) {
        const pool::Segment& seg = table_.segments[s];
        const int x1 = static_cast<int>(pool::toPixel(seg.ax));
        const int y1 = static_cast<int>(pool::toPixel(seg.ay));
        const int x2 = static_cast<int>(pool::toPixel(seg.ax + static_cast<int32_t>(seg.ex) * pool::kPxScale));
        const int y2 = static_cast<int>(pool::toPixel(seg.ay + static_cast<int32_t>(seg.ey) * pool::kPxScale));
        renderer.drawLine(x1, y1, x2, y2, pr32::graphics::Color::Blue);
    }

    for (uint8_t p = 0; p < table_.pocketCount; ++p) {
        const pool::Pocket& pocket = table_.pockets[p];
        renderer.drawFilledCircle(static_cast<int>(pool::toPixel(pocket.x)),
                                   static_cast<int>(pool::toPixel(pocket.y)), 5, pr32::graphics::Color::Black);
    }

    Scene::draw(renderer);
}

}  // namespace lunar_pool
