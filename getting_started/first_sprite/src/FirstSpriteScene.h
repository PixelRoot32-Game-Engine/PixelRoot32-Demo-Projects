/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

namespace first_sprite {

/**
 * @class FirstSpriteScene
 * @brief Draws one 1bpp sprite, moves it, mirrors it, and steps it through a
 *        two-frame walk cycle.
 *
 * The scene owns four pieces of state and nothing else. Every one of them is a
 * plain value member: there is no `new`, no `std::unique_ptr` and no container,
 * so `update()` and `draw()` never allocate. That is the rule for every
 * PixelRoot32 demo, and it is easiest to keep if you never break it once.
 *
 * The sprite pixels are NOT stored here — they live in `assets/PlayerSprite.h`
 * as `constexpr` flash data. This class only remembers *which* frame to draw
 * and *where*.
 */
class FirstSpriteScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Top-left corner of the sprite, in logical pixels. Set in init().
    int spriteX = 0;
    int spriteY = 0;

    /// true when the player last moved left. Passed straight to drawSprite's
    /// flipX parameter, so one right-facing artwork covers both directions.
    bool facingLeft = false;

    /// Index into assets::kPlayerWalkFrames. Only ever 0 or 1 here.
    uint8_t frameIndex = 0;

    /// Milliseconds accumulated toward the next frame swap.
    unsigned long animationElapsedMs = 0;

    /// Toggled by the A button so the frame stepping can be watched on its own.
    bool animationRunning = true;

    /// Pixels moved per frame while a D-pad direction is held.
    static constexpr int MOVE_STEP_PX = 1;

    /// Milliseconds each walk frame stays on screen. ~6 swaps per second.
    static constexpr unsigned long FRAME_DURATION_MS = 160;

    /// Button indices as wired in the platform headers' InputConfig, in the
    /// order the constructor takes them: Up, Down, Left, Right, A, B.
    static constexpr uint8_t BTN_UP    = 0;
    static constexpr uint8_t BTN_DOWN  = 1;
    static constexpr uint8_t BTN_LEFT  = 2;
    static constexpr uint8_t BTN_RIGHT = 3;
    static constexpr uint8_t BTN_A     = 4;

    static constexpr pixelroot32::graphics::Color BACKGROUND_COLOR =
        pixelroot32::graphics::Color::Navy;
    static constexpr pixelroot32::graphics::Color SPRITE_COLOR =
        pixelroot32::graphics::Color::LightGreen;
    static constexpr pixelroot32::graphics::Color TEXT_COLOR =
        pixelroot32::graphics::Color::White;

    void handleMovement();
    void handleAnimation(unsigned long deltaTime);
};

} // namespace first_sprite
