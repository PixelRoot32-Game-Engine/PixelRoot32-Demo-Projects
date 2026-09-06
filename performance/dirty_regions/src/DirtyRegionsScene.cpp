#include "DirtyRegionsScene.h"

#include <core/Engine.h>
#include <core/Log.h>
#include <graphics/DrawSurface.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace dirty_regions {

namespace gfx = pr32::graphics;
namespace logging = pr32::core::logging;

using gfx::Color;
using logging::log;
using logging::LogLevel;

namespace {

/** @brief Button indices in the order the platform headers wire InputConfig. */
constexpr uint8_t BUTTON_A = 4;
constexpr uint8_t BUTTON_B = 5;

} // namespace

void DirtyRegionsScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    auto& renderer = engine.getRenderer();
    screenWidth = renderer.getLogicalWidth();
    screenHeight = renderer.getLogicalHeight();
    fieldTop = HUD_TOP_HEIGHT;
    fieldBottom = screenHeight - HUD_BOTTOM_HEIGHT;

    // Speeds are 1/16 px per millisecond: 2 is 125 px/s. Each box gets a
    // different pair so the four never settle into the same diagonal.
    const int32_t startX[BOX_COUNT] = {10, 60, 30, 90};
    const int32_t startY[BOX_COUNT] = {20, 40, 80, 60};
    const int32_t speedX[BOX_COUNT] = {2, -1, 1, -2};
    const int32_t speedY[BOX_COUNT] = {1, 2, -2, -1};
    const Color colors[BOX_COUNT] = {Color::Cyan, Color::Yellow, Color::Magenta, Color::LightGreen};

    for (int i = 0; i < BOX_COUNT; ++i) {
        boxes[i].x = startX[i] << SUBPIXEL_SHIFT;
        boxes[i].y = startY[i] << SUBPIXEL_SHIFT;
        boxes[i].vx = speedX[i];
        boxes[i].vy = speedY[i];
        boxes[i].color = colors[i];
    }

    buildModeText();

    log(LogLevel::Info, "DirtyRegionsScene: A toggles full redraw, B toggles the dirty-cell overlay");
}

void DirtyRegionsScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    readButtons();
    moveBoxes(deltaTime);
    recordFrameTime(deltaTime);
    readDirtyPathState();
    buildModeText();

    // Engine::run() calls update() and only then draw(), and draw() opens with
    // Renderer::beginFrame(). forceFullRedraw() sets the grid's full-dirty bit,
    // which beginFrame() consumes, so asking for it here lands on this frame's
    // clear. Calling it from draw() would arrive one frame late.
    if (fullRedrawMode) {
        engine.getRenderer().forceFullRedraw();
    }
}

void DirtyRegionsScene::draw(pr32::graphics::Renderer& renderer) {
    Scene::draw(renderer);

    // No full-screen background fill anywhere in this method. Filling the
    // screen would mark every cell, every cell would then be cleared next
    // frame, and the selective clear would cost the same as the full one.
    drawStaticLattice(renderer);

    for (int i = 0; i < BOX_COUNT; ++i) {
        const int x = static_cast<int>(boxes[i].x >> SUBPIXEL_SHIFT);
        const int y = static_cast<int>(boxes[i].y >> SUBPIXEL_SHIFT);
        renderer.drawFilledRectangle(x, y, BOX_SIZE, BOX_SIZE, boxes[i].color);
    }

    renderer.drawText(modeText, 2, 3, Color::White, 1);

    // When the path is off, A and B still toggle their flags and nothing on
    // screen changes. Saying why beats leaving the reader to conclude the
    // feature does nothing.
    renderer.drawText(pathText, 2, 12, dirtyPathLive ? Color::LightGreen : Color::Yellow, 1);

    renderer.drawText("A:REDRAW B:CELLS", 2, screenHeight - 9, Color::Gray, 1);
}

void DirtyRegionsScene::readButtons() {
    auto& input = engine.getInputManager();
    auto& renderer = engine.getRenderer();

    // isButtonPressed() is the rising edge, so a held button toggles once.
    if (input.isButtonPressed(BUTTON_A)) {
        fullRedrawMode = !fullRedrawMode;
        frameSampleSum = 0;
        frameSampleIndex = 0;
        frameSampleCount = 0;
    }

    if (input.isButtonPressed(BUTTON_B)) {
        renderer.setDebugDirtyCellOverlay(!renderer.isDebugDirtyCellOverlayEnabled());
    }
}

void DirtyRegionsScene::moveBoxes(unsigned long deltaTime) {
    const int32_t step = static_cast<int32_t>(deltaTime > MAX_FRAME_MS ? MAX_FRAME_MS : deltaTime);

    const int32_t minX = 0;
    const int32_t maxX = static_cast<int32_t>(screenWidth - BOX_SIZE) << SUBPIXEL_SHIFT;
    const int32_t minY = static_cast<int32_t>(fieldTop) << SUBPIXEL_SHIFT;
    const int32_t maxY = static_cast<int32_t>(fieldBottom - BOX_SIZE) << SUBPIXEL_SHIFT;

    for (int i = 0; i < BOX_COUNT; ++i) {
        boxes[i].x += boxes[i].vx * step;
        boxes[i].y += boxes[i].vy * step;

        if (boxes[i].x < minX) {
            boxes[i].x = minX;
            boxes[i].vx = -boxes[i].vx;
        } else if (boxes[i].x > maxX) {
            boxes[i].x = maxX;
            boxes[i].vx = -boxes[i].vx;
        }

        if (boxes[i].y < minY) {
            boxes[i].y = minY;
            boxes[i].vy = -boxes[i].vy;
        } else if (boxes[i].y > maxY) {
            boxes[i].y = maxY;
            boxes[i].vy = -boxes[i].vy;
        }
    }
}

void DirtyRegionsScene::recordFrameTime(unsigned long deltaTime) {
    const unsigned long sample = deltaTime > MAX_FRAME_MS ? MAX_FRAME_MS : deltaTime;

    frameSampleSum -= frameSamples[frameSampleIndex];
    frameSamples[frameSampleIndex] = sample;
    frameSampleSum += sample;

    frameSampleIndex = static_cast<uint8_t>((frameSampleIndex + 1) % FRAME_SAMPLES);
    if (frameSampleCount < FRAME_SAMPLES) {
        ++frameSampleCount;
    }
}

void DirtyRegionsScene::readDirtyPathState() {
    // The same test Renderer::beginFrame() runs before it touches the grid: no
    // 8bpp framebuffer, no dirty regions. Read every frame rather than cached
    // in init(), so the answer never depends on when a driver allocates its
    // buffer relative to scene construction.
    dirtyPathLive = engine.getRenderer().getDrawSurface().getSpriteBuffer() != nullptr;

    std::snprintf(pathText, sizeof(pathText), "%s",
                  dirtyPathLive ? "PATH ON" : "PATH OFF: NO 8BPP FB");
}

void DirtyRegionsScene::buildModeText() {
    // Tenths of a millisecond, so the average still moves when frames are short.
    const unsigned long tenths =
        (frameSampleCount == 0) ? 0UL : (frameSampleSum * 10UL) / frameSampleCount;

    std::snprintf(modeText, sizeof(modeText), "%s %lu.%lums",
                  fullRedrawMode ? "FULL" : "DIRTY",
                  tenths / 10UL,
                  tenths % 10UL);
}

void DirtyRegionsScene::drawStaticLattice(pr32::graphics::Renderer& renderer) const {
    // Small blocks on a black field. Nothing here ever moves, but it is still
    // redrawn every frame: beginFrame() cleared these cells because this same
    // pass marked them last frame. Dirty regions do not let a scene skip the
    // draw, they let the engine skip clearing the pixels nobody touched.
    for (int y = fieldTop + 4; y + LATTICE_BLOCK <= fieldBottom; y += LATTICE_STEP) {
        for (int x = 4; x + LATTICE_BLOCK <= screenWidth; x += LATTICE_STEP) {
            renderer.drawFilledRectangle(x, y, LATTICE_BLOCK, LATTICE_BLOCK, Color::DarkGreen);
        }
    }
}

} // namespace dirty_regions
