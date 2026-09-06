/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "DepthSortScene.h"

#include <core/Engine.h>
#include <core/Log.h>
#include <gameplay/DepthCompare.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace depth_sort {

namespace gfx = pr32::graphics;
using gfx::Color;
using pr32::math::Vector2;

namespace {

/// Button indices, mirrored from InputConfig in the platform headers.
constexpr std::uint8_t kButtonUp = 0;
constexpr std::uint8_t kButtonDown = 1;
constexpr std::uint8_t kButtonLeft = 2;
constexpr std::uint8_t kButtonRight = 3;

/// Height of the bright bar Walker paints across its own bottom edge.
constexpr int kFootBarHeight = 3;

/// Floor stripes, drawn behind the actors purely so the vertical axis reads as
/// depth rather than as "up and down on a flat rectangle".
constexpr int kFloorStripeCount = 5;
constexpr int kFloorStripeSpacing = 16;

}  // namespace

// ---------------------------------------------------------------------------
// Walker
// ---------------------------------------------------------------------------

Walker::Walker(int x, int y, int w, int h, Color body, Color trim, char label)
    : Entity(Vector2(x, y), w, h, pr32::core::EntityType::GENERIC),
      xQ8_(static_cast<std::int32_t>(x) << kShift),
      yQ8_(static_cast<std::int32_t>(y) << kShift),
      body_(body),
      trim_(trim),
      label_{label, '\0'} {}

std::int32_t Walker::stepQ8(int pixelsPerSecond, unsigned long deltaTime) {
    std::int32_t ms = static_cast<std::int32_t>(deltaTime);
    if (ms <= 0) {
        return 0;
    }
    if (ms > kMaxStepMs) {
        ms = kMaxStepMs;
    }
    return (static_cast<std::int32_t>(pixelsPerSecond) * kOne * ms) / 1000;
}

void Walker::moveBy(std::int32_t dxQ8, std::int32_t dyQ8) {
    xQ8_ += dxQ8;
    yQ8_ += dyQ8;

    const std::int32_t minX = static_cast<std::int32_t>(layout::kFieldLeft) << kShift;
    const std::int32_t maxX = static_cast<std::int32_t>(layout::kFieldRight - width + 1) << kShift;
    const std::int32_t minY = static_cast<std::int32_t>(layout::kFieldTop) << kShift;
    const std::int32_t maxY = static_cast<std::int32_t>(layout::kFieldBottom - height + 1) << kShift;

    if (xQ8_ < minX) xQ8_ = minX;
    if (xQ8_ > maxX) xQ8_ = maxX;
    if (yQ8_ < minY) yQ8_ = minY;
    if (yQ8_ > maxY) yQ8_ = maxY;

    commit();
}

void Walker::teleport(int x, int y) {
    xQ8_ = static_cast<std::int32_t>(x) << kShift;
    yQ8_ = static_cast<std::int32_t>(y) << kShift;
    commit();
}

void Walker::commit() {
    // The comparator reads Entity::position and Entity::height, so this line is
    // what makes the Q8 bookkeeping above visible to the sort.
    position = Vector2(pixelX(), pixelY());
}

void Walker::draw(gfx::Renderer& renderer) {
    const int x = pixelX();
    const int y = pixelY();

    renderer.drawFilledRectangle(x, y, width, height, body_);
    renderer.drawRectangle(x, y, width, height, trim_);

    // The feet. This bar sits at exactly `bottomY() - kFootBarHeight`, so when
    // two actors overlap you can see which set of feet is lower before you look
    // at the numbers under the field.
    renderer.drawFilledRectangle(x, y + height - kFootBarHeight, width, kFootBarHeight, trim_);

    // 5x7 font: a glyph is 5 px wide, so this centres one character.
    renderer.drawText(label_, static_cast<int16_t>(x + (width - 5) / 2), static_cast<int16_t>(y + 3),
                      Color::Black, 1);
}

// ---------------------------------------------------------------------------
// PatrolWalker
// ---------------------------------------------------------------------------

PatrolWalker::PatrolWalker(int x,
                           int y,
                           int w,
                           int h,
                           Color body,
                           Color trim,
                           char label,
                           int topY,
                           int bottomLimitY,
                           int pixelsPerSecond,
                           bool startsDownward)
    : Walker(x, y, w, h, body, trim, label),
      startY_(y),
      topY_(topY),
      bottomLimitY_(bottomLimitY),
      speed_(pixelsPerSecond),
      startsDownward_(startsDownward),
      movingDown_(startsDownward) {}

void PatrolWalker::update(unsigned long deltaTime) {
    const std::int32_t step = stepQ8(speed_, deltaTime);
    moveBy(0, movingDown_ ? step : -step);

    // Turn on the way out rather than clamping to the exact row: the field
    // clamp in moveBy() is the safety net, this is the patrol.
    if (movingDown_ && pixelY() >= bottomLimitY_) {
        movingDown_ = false;
    } else if (!movingDown_ && pixelY() <= topY_) {
        movingDown_ = true;
    }
}

void PatrolWalker::resetPatrol() {
    movingDown_ = startsDownward_;
    teleport(pixelX(), startY_);
}

// ---------------------------------------------------------------------------
// PlayerWalker
// ---------------------------------------------------------------------------

PlayerWalker::PlayerWalker(int x,
                           int y,
                           int w,
                           int h,
                           Color body,
                           Color trim,
                           char label,
                           int pixelsPerSecond)
    : Walker(x, y, w, h, body, trim, label),
      startX_(x),
      startY_(y),
      speed_(pixelsPerSecond) {}

void PlayerWalker::update(unsigned long deltaTime) {
    auto& input = engine.getInputManager();

    // isButtonDown() is the held state, which is what continuous movement
    // wants; isButtonPressed() is the edge, and that is what the scene uses
    // for the A and B one-shots.
    const std::int32_t step = stepQ8(speed_, deltaTime);
    std::int32_t moveX = 0;
    std::int32_t moveY = 0;

    if (input.isButtonDown(kButtonLeft)) moveX -= step;
    if (input.isButtonDown(kButtonRight)) moveX += step;
    if (input.isButtonDown(kButtonUp)) moveY -= step;
    if (input.isButtonDown(kButtonDown)) moveY += step;

    if (moveX != 0 || moveY != 0) {
        moveBy(moveX, moveY);
    }
}

void PlayerWalker::resetToStart() {
    teleport(startX_, startY_);
}

// ---------------------------------------------------------------------------
// DepthSortScene
// ---------------------------------------------------------------------------

DepthSortScene::DepthSortScene()
    // Four heights on purpose: 26, 30, 22 and 14 pixels. If every actor were the
    // same height, sorting by top-left Y would give the same answer as sorting
    // by the bottom edge and the demo would prove nothing.
    : player_(68, 62, 12, 26, Color::Cyan, Color::White, 'P', 46),
      tall_(30, 21, 12, 30, Color::Green, Color::White, 'A', 21, 75, 16, true),
      medium_(40, 83, 14, 22, Color::Orange, Color::White, 'B', 27, 83, 23, false),
      squat_(52, 48, 16, 14, Color::Magenta, Color::White, 'C', 24, 91, 31, true),
      order_{&player_, &tall_, &medium_, &squat_} {}

void DepthSortScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    sorted_ = pr32::platforms::config::EnableDepthSort;
    resetPositions();
    applyMode();
    refreshHud();

    pr32::core::logging::log("DepthSortScene: four actors, one comparator");
}

void DepthSortScene::update(unsigned long deltaTime) {
    // Base first: Scene::update() is what runs each actor's update(), and the
    // player reads the D-pad from inside its own update().
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    if (input.isButtonPressed(kButtonA)) {
        // A build without the flag has no working toggle, and says so on the
        // banner instead of moving a switch that is wired to nothing.
        if constexpr (pr32::platforms::config::EnableDepthSort) {
            sorted_ = !sorted_;
            applyMode();
        }
    }

    if (input.isButtonPressed(kButtonB)) {
        resetPositions();
    }

    refreshHud();
}

void DepthSortScene::draw(gfx::Renderer& renderer) {
    // Background first, THEN the base call. Scene::draw() paints the entities,
    // so a fill placed after it would erase them.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                 Color::Black);

    renderer.drawRectangle(layout::kFieldX, layout::kFieldY, layout::kFieldW, layout::kFieldH,
                           Color::Navy);

    for (int i = 1; i <= kFloorStripeCount; ++i) {
        const int y = layout::kFieldTop + i * kFloorStripeSpacing;
        renderer.drawLine(layout::kFieldLeft, y, layout::kFieldRight, y, Color::Navy);
    }

    Scene::draw(renderer);

    if constexpr (pr32::platforms::config::EnableDepthSort) {
        if (sorted_) {
            renderer.drawText("SORTED: BY BOTTOM Y", 2, layout::kModeTextY, Color::Green, 1);
        } else {
            renderer.drawText("UNSORTED: ADD ORDER", 2, layout::kModeTextY, Color::Red, 1);
        }
    } else {
        renderer.drawText("DEPTH_SORT FLAG OFF", 2, layout::kModeTextY, Color::Red, 1);
    }

    renderer.drawText("A TOGGLE   B RESET", 2, layout::kHintTextY, Color::Gray, 1);
    renderer.drawText("BOTTOM Y: HIGH=FRONT", 2, layout::kLegendTextY, Color::Gray, 1);

    // Four readouts across the bottom, each in its actor's own colour, so the
    // ordering rule can be checked against the picture above it.
    for (int i = 0; i < kWalkerCount; ++i) {
        renderer.drawText(valueText_[i], static_cast<int16_t>(2 + i * 32),
                          layout::kValueTextY, order_[i]->bodyColor(), 1);
    }
}

void DepthSortScene::applyMode() {
    // Re-add in the canonical order first. Scene::sortEntities() permutes the
    // array in place, so without this the UNSORTED reading would be "whatever
    // the last sorted frame left behind" instead of the add order.
    clearEntities();
    for (int i = 0; i < kWalkerCount; ++i) {
        addEntity(order_[i]);
    }

    if constexpr (pr32::platforms::config::EnableDepthSort) {
        // The engine ships this comparator; it is a plain function pointer, so
        // it drops straight into Scene::DepthComparator.
        //
        // Contract, from Scene::shouldPrecede(): the comparator answers "must
        // `a` be placed BEFORE `b` in the entity array?", and the array is draw
        // order, so "before" means "painted first", which means BEHIND.
        // compareByBottomY returns true when a's bottom edge is higher up the
        // screen than b's — so the actor standing further back is drawn first
        // and the one standing lower covers it. Invert this and you get a demo
        // that still looks like it is doing something, and is wrong.
        depthComparator = sorted_ ? &pr32::gameplay::compareByBottomY : nullptr;

        // Everyone moves every frame, so the order has to be recomputed every
        // frame. Without this, sortEntities() only runs when needsSorting is
        // set — that is, when an entity is added or removed.
        depthSortEnabled = sorted_;
    }
}

void DepthSortScene::resetPositions() {
    player_.resetToStart();
    tall_.resetPatrol();
    medium_.resetPatrol();
    squat_.resetPatrol();
}

void DepthSortScene::refreshHud() {
    // snprintf into fixed char members: no std::string, no heap, nothing that
    // allocates once the scene is running.
    for (int i = 0; i < kWalkerCount; ++i) {
        snprintf(valueText_[i], sizeof(valueText_[i]), "%c%3d", order_[i]->label(),
                 order_[i]->bottomY());
    }
}

}  // namespace depth_sort
