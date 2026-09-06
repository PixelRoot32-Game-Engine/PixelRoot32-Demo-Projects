/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <gameplay/ObjectPool.h>

#include <cstddef>
#include <cstdint>

#if !PIXELROOT32_ENABLE_GAMEPLAY_OBJECT_POOL
#error "memory_budget needs -D PIXELROOT32_ENABLE_GAMEPLAY_OBJECT_POOL=1 (see lib/platformio.ini)"
#endif

namespace memory_budget {

/**
 * @struct BoxStyle
 * @brief Per-slot appearance record, allocated from the SceneArena in init().
 *
 * Trivially destructible on purpose: SceneArena::reset() only rewinds a bump
 * offset and never runs a destructor, so anything placed in arena memory must
 * be safe to abandon.
 */
struct BoxStyle {
    /// Explicit constructor: arenaNew() forwards to a real constructor call,
    /// and C++17 does not allow parenthesised aggregate initialisation.
    constexpr BoxStyle(int16_t half,
                       int16_t pixelsPerSecond,
                       pixelroot32::graphics::Color fillColor,
                       pixelroot32::graphics::Color outlineColor)
        : halfSize(half), speed(pixelsPerSecond), fill(fillColor), outline(outlineColor) {}

    int16_t halfSize;                     ///< Half the box edge, in pixels.
    int16_t speed;                        ///< Travel speed, in pixels per second.
    pixelroot32::graphics::Color fill;    ///< Interior colour.
    pixelroot32::graphics::Color outline; ///< Border colour.
};

/**
 * @struct Box
 * @brief One pooled object: a box bouncing inside the playfield.
 *
 * Position and velocity are Q8 fixed point (256 units = 1 pixel) so motion
 * stays smooth without touching a float on the ESP32 path.
 */
struct Box {
    Box(int32_t centerX, int32_t centerY, int32_t velocityX, int32_t velocityY, const BoxStyle* boxStyle)
        : x(centerX), y(centerY), vx(velocityX), vy(velocityY), style(boxStyle) {}

    int32_t x;              ///< Centre X, Q8 pixels.
    int32_t y;              ///< Centre Y, Q8 pixels.
    int32_t vx;             ///< Velocity X, Q8 pixels per second.
    int32_t vy;             ///< Velocity Y, Q8 pixels per second.
    const BoxStyle* style;  ///< Arena-owned style, never null (see kFallbackStyle).
};

/**
 * @class MemoryBudgetScene
 * @brief Draws the live occupancy of a SceneArena and an ObjectPool.
 *
 * Both ceilings are compile-time constants (kArenaBytes, kPoolCapacity) and
 * both stores are plain members of this scene, so the whole budget is sized
 * before main() runs. init() is the only place that allocates; update() and
 * draw() do arithmetic and nothing else.
 */
class MemoryBudgetScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Pool slot count. The bar on screen is drawn against exactly this number.
    static constexpr uint16_t kPoolCapacity = 12;

    /// Arena size in bytes. 12 BoxStyle records fit with room to spare, which
    /// is what the arena bar is meant to show: headroom you can see.
    static constexpr std::size_t kArenaBytes = 128;

    using BoxPool = pixelroot32::gameplay::ObjectPool<Box, kPoolCapacity>;

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) -----
    static constexpr int kBarX = 4;
    static constexpr int kBarWidth = 120;
    static constexpr int kBarHeight = 7;
    static constexpr int kArenaTextY = 13;
    static constexpr int kArenaBarY = 22;
    static constexpr int kPoolTextY = 33;
    static constexpr int kPoolBarY = 42;
    static constexpr int kFieldX = 4;
    static constexpr int kFieldY = 52;
    static constexpr int kFieldWidth = 120;
    static constexpr int kFieldHeight = 56;
    static constexpr int kStatusTextY = 110;
    static constexpr int kControlsTextY = 119;

    /// Button indices as wired by InputConfig in the platform headers.
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    void acquireBox();
    void releaseNewestBox();
    void stepBoxes(unsigned long deltaTime);
    void clampIntoField(Box& box) const;
    void refreshHud();
    uint16_t nextRandom();

    void drawBudgetBar(pixelroot32::graphics::Renderer& renderer,
                       int y,
                       std::size_t used,
                       std::size_t capacity,
                       bool atCeiling) const;

    /// Backing store for the SceneArena. A plain member, so its cost is in the
    /// scene's own sizeof rather than on some heap nobody is measuring.
    alignas(8) unsigned char arenaBuffer_[kArenaBytes];

    /// One arena-allocated style per slot. Entries stay null when the build
    /// has no PIXELROOT32_ENABLE_SCENE_ARENA, and the fallback style is used.
    const BoxStyle* styles_[kPoolCapacity] = {nullptr};

    BoxPool pool_;

    /// Acquire order, so B releases the most recent object (LIFO).
    Box* acquireOrder_[kPoolCapacity] = {nullptr};
    uint16_t acquireCount_ = 0;

    /// True while the last acquire() returned nullptr. Shown, never hidden.
    bool lastAcquireFailed_ = false;

    uint16_t randomState_ = 0x1234;

    char arenaText_[24] = {0};
    char poolText_[24] = {0};
    char statusText_[24] = {0};
    pixelroot32::graphics::Color statusColor_ = pixelroot32::graphics::Color::Green;
};

} // namespace memory_budget
