/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "FirstSpriteScene.h"

#include <core/Engine.h>
#include <core/Log.h>

#include "assets/PlayerSprite.h"

namespace pr32 = pixelroot32;

// The Engine object itself is defined in the platform header that main.cpp
// selected (src/platforms/native.h or src/platforms/esp32_dev.h), because only
// that file knows the display and button wiring. Scenes reach it through this
// extern declaration. It is the established PixelRoot32 pattern, not a
// workaround: one engine instance, declared where the hardware is known, used
// everywhere else by name.
extern pr32::core::Engine engine;

namespace first_sprite {

namespace gfx = pr32::graphics;
namespace logging = pr32::core::logging;

using gfx::Color;

// One short line naming what is on screen. Two literals instead of a formatted
// buffer: no snprintf, no char array, nothing to overflow.
static constexpr const char* LABEL_WALKING = "1bpp 16x16  WALK";
static constexpr const char* LABEL_IDLE    = "1bpp 16x16  IDLE";

/// Font5x7 at size 1 is 7 pixels tall; keep the text clear of the sprite.
static constexpr int LABEL_Y = 4;
static constexpr int LABEL_X = 8;
static constexpr uint8_t LABEL_SIZE = 1;

void FirstSpriteScene::init() {
    // Scene::init() clears entity state and (when physics is compiled in)
    // re-initialises the scheduler. Call it FIRST, before touching anything
    // this class owns, or it will wipe the values you just set.
    Scene::init();

    logging::log("FirstSpriteScene: initializing...");

    // Colours in this engine are palette *indices*, not RGB values: Color::Navy
    // is "slot 2", and which RGB565 that becomes depends on the active palette.
    // PR32 is the default palette and the one the Color names are chosen for,
    // so setting it explicitly makes the demo look the same under any engine
    // default that might change later.
    gfx::setPalette(gfx::PaletteType::PR32);

    // Start centred. The renderer knows the logical resolution, so this works
    // unchanged if someone rebuilds the demo at a different display size.
    const int screenWidth  = engine.getRenderer().getLogicalWidth();
    const int screenHeight = engine.getRenderer().getLogicalHeight();
    spriteX = (screenWidth  - assets::kPlayerWidth)  / 2;
    spriteY = (screenHeight - assets::kPlayerHeight) / 2;

    logging::log(logging::LogLevel::Info, "FirstSpriteScene: initialized");
}

void FirstSpriteScene::update(unsigned long deltaTime) {
    // Base class first again: Scene::update() advances every entity the scene
    // holds. This demo adds none, but calling it keeps the contract, and the
    // day you add an entity it will already work.
    Scene::update(deltaTime);

    handleMovement();
    handleAnimation(deltaTime);
}

void FirstSpriteScene::handleMovement() {
    auto& input = engine.getInputManager();

    // isButtonDown() is the *held* state, which is what continuous movement
    // needs. isButtonPressed() is the rising edge only — it would move the
    // sprite one pixel per press and then stop.
    if (input.isButtonDown(BTN_LEFT)) {
        spriteX -= MOVE_STEP_PX;
        // Facing is only changed by horizontal input. Walking straight up
        // should not reset which way the character looks.
        facingLeft = true;
    }
    if (input.isButtonDown(BTN_RIGHT)) {
        spriteX += MOVE_STEP_PX;
        facingLeft = false;
    }
    if (input.isButtonDown(BTN_UP)) {
        spriteY -= MOVE_STEP_PX;
    }
    if (input.isButtonDown(BTN_DOWN)) {
        spriteY += MOVE_STEP_PX;
    }

    // Clamp so the sprite cannot leave the screen. drawSprite() clips safely on
    // its own, but a sprite you cannot see is a sprite a beginner assumes is
    // broken, so keep it reachable.
    const int maxX = engine.getRenderer().getLogicalWidth()  - assets::kPlayerWidth;
    const int maxY = engine.getRenderer().getLogicalHeight() - assets::kPlayerHeight;
    if (spriteX < 0)    spriteX = 0;
    if (spriteX > maxX) spriteX = maxX;
    if (spriteY < 0)    spriteY = 0;
    if (spriteY > maxY) spriteY = maxY;
}

void FirstSpriteScene::handleAnimation(unsigned long deltaTime) {
    auto& input = engine.getInputManager();

    // A toggles the walk cycle. Here the *edge* is what we want: with
    // isButtonDown() the flag would flip on every frame the button is held and
    // the toggle would be unusable.
    if (input.isButtonPressed(BTN_A)) {
        animationRunning = !animationRunning;
    }

    if (!animationRunning) {
        // Park on frame 0 (legs apart) so stopping always looks the same, and
        // reset the clock so restarting gives a full frame rather than a
        // leftover sliver.
        frameIndex = 0;
        animationElapsedMs = 0;
        return;
    }

    // Time-based stepping, not frame-count-based: the walk runs at the same
    // speed whether the native build hits 60 fps or the ESP32 hits 25.
    animationElapsedMs += deltaTime;
    while (animationElapsedMs >= FRAME_DURATION_MS) {
        animationElapsedMs -= FRAME_DURATION_MS;
        // Two frames, so advancing is "the other one".
        frameIndex = static_cast<uint8_t>((frameIndex + 1) % assets::kPlayerFrameCount);
    }
}

void FirstSpriteScene::draw(pr32::graphics::Renderer& renderer) {
    // ORDER MATTERS, and it is the opposite of init()/update().
    //
    // Painter's algorithm: background first, then everything that sits on top
    // of it. Scene::draw() is what paints the scene's entities, so it goes
    // LAST — a full-screen fill placed after it would paint straight over them.
    // Do not "fix" this by calling Scene::draw() first out of symmetry with
    // update(); you would get a blank screen and no error message.

    // 1. Background. There is no separate clear step; this fill is the clear.
    renderer.drawFilledRectangle(
        0, 0,
        renderer.getLogicalWidth(),
        renderer.getLogicalHeight(),
        BACKGROUND_COLOR
    );

    // 2. The sprite. Four things are happening in this one call:
    //
    //    - kPlayerWalkFrames[frameIndex] picks the walk frame. The Sprite is
    //      passed by const reference and the renderer reads its rows through
    //      the stored pointer; nothing is copied, which is why the pixel data
    //      must be constexpr and outlive the draw.
    //    - spriteX / spriteY are the TOP-LEFT corner, not the centre.
    //    - SPRITE_COLOR tints every 1 bit. A 1bpp sprite has no palette of its
    //      own: the whole shape is one colour, chosen here at draw time. Change
    //      this argument and the same bits come out a different colour.
    //    - facingLeft is drawSprite's flipX parameter. The renderer mirrors the
    //      row bits as it blits, so the artwork only ever has to be drawn
    //      facing one way.
    renderer.drawSprite(
        assets::kPlayerWalkFrames[frameIndex],
        spriteX,
        spriteY,
        SPRITE_COLOR,
        facingLeft
    );

    // 3. The label. drawText goes through the renderer directly, so this demo
    //    needs no UI system (PIXELROOT32_ENABLE_UI_SYSTEM is 0 in
    //    lib/platformio.ini).
    renderer.drawText(
        animationRunning ? LABEL_WALKING : LABEL_IDLE,
        LABEL_X,
        LABEL_Y,
        TEXT_COLOR,
        LABEL_SIZE
    );

    // 4. Base class last. See the note at the top of this function.
    Scene::draw(renderer);
}

} // namespace first_sprite
