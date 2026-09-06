/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "MemoryBudgetScene.h"

#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace memory_budget {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// Q8 fixed point: 256 units = 1 pixel.
constexpr int kFixedShift = 8;
constexpr int32_t kFixedOne = 1 << kFixedShift;

/// Interior of the playfield border, in pixels.
constexpr int kFieldInnerX = 5;
constexpr int kFieldInnerY = 53;
constexpr int kFieldInnerRight = 122;   ///< Last drawable column.
constexpr int kFieldInnerBottom = 106;  ///< Last drawable row.

/// Upper bound on a single integration step, so a long stall (a breakpoint,
/// the first frame after init) cannot teleport a box through the border.
constexpr int32_t kMaxStepMs = 100;

constexpr Color kStylePalette[] = {
    Color::Cyan,
    Color::Green,
    Color::Yellow,
    Color::Orange,
    Color::Magenta,
    Color::LightGreen,
    Color::Blue,
    Color::LightRed
};
constexpr uint16_t kStylePaletteSize = sizeof(kStylePalette) / sizeof(kStylePalette[0]);

/// Used when the build has no PIXELROOT32_ENABLE_SCENE_ARENA: every
/// SceneArena::allocate() then returns nullptr, and arenaNew() with it.
/// The demo still runs; only the arena bar reads 0 / 0.
constexpr BoxStyle kFallbackStyle(3, 40, Color::Gray, Color::White);

} // namespace

void MemoryBudgetScene::init() {
    // Scene::init() runs resetState(), which clears entities and rewinds the
    // arena. Everything below therefore starts from offset 0 on every entry.
    Scene::init();

    // ObjectPool.h's contract: reset the pool *after* Scene::init(), so no
    // dangling pointer survives in the base entity list.
    pool_.reset();

    gfx::setPalette(gfx::PaletteType::PR32);

    acquireCount_ = 0;
    lastAcquireFailed_ = false;
    randomState_ = 0x1234;

    // The one and only allocation site in this demo. arenaBuffer_ is a member,
    // so the arena's ceiling was fixed at compile time; arena.init() just tells
    // the bump allocator where that fixed buffer lives.
    arena.init(arenaBuffer_, kArenaBytes);

    for (uint16_t i = 0; i < kPoolCapacity; ++i) {
        styles_[i] = pr32::core::arenaNew<BoxStyle>(
            arena,
            static_cast<int16_t>(2 + (i % 3)),
            static_cast<int16_t>(34 + (i % 5) * 9),
            kStylePalette[i % kStylePaletteSize],
            Color::White);
    }

    refreshHud();

    pr32::core::logging::log("MemoryBudgetScene: budget fixed at init");
}

void MemoryBudgetScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    // isButtonPressed() is edge triggered, so one press is one slot.
    if (input.isButtonPressed(kButtonA)) {
        acquireBox();
    } else if (input.isButtonPressed(kButtonB)) {
        releaseNewestBox();
    }

    stepBoxes(deltaTime);
}

void MemoryBudgetScene::draw(pr32::graphics::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, so filling after it would overpaint them. init() and update()
    // are the overrides that call their base first, not draw().
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("MEMORY BUDGET", 2, Color::White, 1);

    renderer.drawText(arenaText_, kBarX, kArenaTextY, Color::White, 1);
    drawBudgetBar(renderer, kArenaBarY, arena.offset, arena.capacity, arena.offset >= arena.capacity);

    renderer.drawText(poolText_, kBarX, kPoolTextY, Color::White, 1);
    drawBudgetBar(renderer, kPoolBarY, pool_.size(), pool_.capacity(), pool_.isFull());

    renderer.drawRectangle(kFieldX, kFieldY, kFieldWidth, kFieldHeight, Color::Gray);

    for (uint16_t i = pool_.nextLive(0); i != BoxPool::kEnd; i = pool_.nextLive(static_cast<uint16_t>(i + 1))) {
        const Box& box = *pool_.at(i);
        const int edge = box.style->halfSize * 2;
        const int left = static_cast<int>(box.x >> kFixedShift) - box.style->halfSize;
        const int top = static_cast<int>(box.y >> kFixedShift) - box.style->halfSize;
        renderer.drawFilledRectangle(left, top, edge, edge, box.style->fill);
        renderer.drawRectangle(left, top, edge, edge, box.style->outline);
    }

    renderer.drawText(statusText_, kBarX, kStatusTextY, statusColor_, 1);
    renderer.drawText("A:ACQUIRE  B:RELEASE", kBarX, kControlsTextY, Color::Gray, 1);
}

void MemoryBudgetScene::acquireBox() {
    // Spawn near the middle of the field with a pseudo-random nudge, so a full
    // pool does not look like one box.
    const int32_t spawnX = static_cast<int32_t>(kFieldInnerX + 10 + (nextRandom() % 96)) << kFixedShift;
    const int32_t spawnY = static_cast<int32_t>(kFieldInnerY + 8 + (nextRandom() % 36)) << kFixedShift;

    // acquire() forwards these to Box's constructor. It returns nullptr when
    // every slot is live — that is the ceiling, and the HUD says so.
    Box* box = pool_.acquire(spawnX, spawnY, kFixedOne, kFixedOne, &kFallbackStyle);
    if (box == nullptr) {
        lastAcquireFailed_ = true;
        refreshHud();
        return;
    }

    const uint16_t index = pool_.indexOf(box);
    if (index != BoxPool::kEnd && styles_[index] != nullptr) {
        box->style = styles_[index];
    }

    const int32_t speed = static_cast<int32_t>(box->style->speed) << kFixedShift;
    box->vx = ((nextRandom() & 1u) != 0u) ? speed : -speed;
    box->vy = ((nextRandom() & 1u) != 0u) ? (speed * 3) / 4 : -((speed * 3) / 4);
    clampIntoField(*box);

    acquireOrder_[acquireCount_] = box;
    ++acquireCount_;
    lastAcquireFailed_ = false;
    refreshHud();
}

void MemoryBudgetScene::releaseNewestBox() {
    if (acquireCount_ == 0) {
        return;
    }

    --acquireCount_;
    pool_.release(acquireOrder_[acquireCount_]);
    acquireOrder_[acquireCount_] = nullptr;
    lastAcquireFailed_ = false;
    refreshHud();
}

void MemoryBudgetScene::stepBoxes(unsigned long deltaTime) {
    int32_t step = static_cast<int32_t>(deltaTime);
    if (step <= 0) {
        return;
    }
    if (step > kMaxStepMs) {
        step = kMaxStepMs;
    }

    for (uint16_t i = pool_.nextLive(0); i != BoxPool::kEnd; i = pool_.nextLive(static_cast<uint16_t>(i + 1))) {
        Box& box = *pool_.at(i);
        box.x += (box.vx * step) / 1000;
        box.y += (box.vy * step) / 1000;
        clampIntoField(box);
    }
}

void MemoryBudgetScene::clampIntoField(Box& box) const {
    const int32_t half = static_cast<int32_t>(box.style->halfSize) << kFixedShift;
    const int32_t minX = (static_cast<int32_t>(kFieldInnerX) << kFixedShift) + half;
    const int32_t maxX = (static_cast<int32_t>(kFieldInnerRight) << kFixedShift) - half;
    const int32_t minY = (static_cast<int32_t>(kFieldInnerY) << kFixedShift) + half;
    const int32_t maxY = (static_cast<int32_t>(kFieldInnerBottom) << kFixedShift) - half;

    if (box.x < minX) {
        box.x = minX;
        box.vx = -box.vx;
    } else if (box.x > maxX) {
        box.x = maxX;
        box.vx = -box.vx;
    }

    if (box.y < minY) {
        box.y = minY;
        box.vy = -box.vy;
    } else if (box.y > maxY) {
        box.y = maxY;
        box.vy = -box.vy;
    }
}

void MemoryBudgetScene::refreshHud() {
    // snprintf into fixed members: no std::string, no heap, and only on the
    // frames where the occupancy actually changed.
    snprintf(arenaText_, sizeof(arenaText_), "ARENA %lu/%lu B",
             static_cast<unsigned long>(arena.offset),
             static_cast<unsigned long>(arena.capacity));

    snprintf(poolText_, sizeof(poolText_), "POOL %u/%u (%luB)",
             static_cast<unsigned>(pool_.size()),
             static_cast<unsigned>(pool_.capacity()),
             static_cast<unsigned long>(sizeof(BoxPool)));

    if (lastAcquireFailed_) {
        snprintf(statusText_, sizeof(statusText_), "FULL: GOT NULLPTR");
        statusColor_ = Color::Red;
    } else if (pool_.isFull()) {
        snprintf(statusText_, sizeof(statusText_), "POOL FULL");
        statusColor_ = Color::Orange;
    } else {
        snprintf(statusText_, sizeof(statusText_), "%u SLOTS FREE",
                 static_cast<unsigned>(pool_.capacity() - pool_.size()));
        statusColor_ = Color::Green;
    }
}

uint16_t MemoryBudgetScene::nextRandom() {
    // 16-bit xorshift. Deterministic, allocation-free, and small enough that
    // nobody mistakes it for a real PRNG.
    randomState_ ^= static_cast<uint16_t>(randomState_ << 7);
    randomState_ ^= static_cast<uint16_t>(randomState_ >> 9);
    randomState_ ^= static_cast<uint16_t>(randomState_ << 8);
    return randomState_;
}

void MemoryBudgetScene::drawBudgetBar(pr32::graphics::Renderer& renderer,
                                      int y,
                                      std::size_t used,
                                      std::size_t capacity,
                                      bool atCeiling) const {
    renderer.drawRectangle(kBarX, y, kBarWidth, kBarHeight, Color::Gray);

    if (capacity == 0) {
        return;
    }

    const int innerWidth = kBarWidth - 2;
    int filled = static_cast<int>((used * static_cast<std::size_t>(innerWidth)) / capacity);
    if (filled > innerWidth) {
        filled = innerWidth;
    }
    if (filled <= 0) {
        return;
    }

    renderer.drawFilledRectangle(kBarX + 1, y + 1, filled, kBarHeight - 2,
                                 atCeiling ? Color::Red : Color::Green);
}

} // namespace memory_budget
