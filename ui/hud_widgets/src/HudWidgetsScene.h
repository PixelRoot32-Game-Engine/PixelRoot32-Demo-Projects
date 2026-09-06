/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Camera2D.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

#if !PIXELROOT32_ENABLE_UI_SYSTEM
#error "hud_widgets needs -D PIXELROOT32_ENABLE_UI_SYSTEM=1 (see lib/platformio.ini)"
#endif

#include <graphics/ui/UIAnchorLayout.h>
#include <graphics/ui/UILabel.h>
#include <graphics/ui/UIPaddingContainer.h>
#include <graphics/ui/UIPanel.h>
#include <graphics/ui/UISpriteRow.h>

namespace hud_widgets {

/**
 * @class HudWidgetsScene
 * @brief A HUD that stays put while the world scrolls.
 *
 * The single idea: `UIElement::setFixedPosition(true)` is not a HUD feature and
 * does not create a second render pass. It sets one bool that makes the
 * element's own `draw()` call `Renderer::setOffsetBypass(true)` for the length
 * of that call, so the renderer ignores the display offset `Camera2D::apply()`
 * just wrote. The widget then paints at literal logical screen coordinates
 * while everything else paints in world space. Take the flag away and the same
 * four widgets scroll off the left edge with the scenery.
 *
 * Three widgets, all anchored by one `UIAnchorLayout` and all fixed:
 *
 * - `UISpriteRow` (TOP_LEFT) — five hearts at half-heart granularity.
 * - `UILabel` (TOP_RIGHT) — the score.
 * - `UIPanel` (BOTTOM_CENTER) — a bar holding a `UIPaddingContainer` holding
 *   the status `UILabel`. The padding container exists because `UIPanel`
 *   plants its child at the panel's own top-left corner, on top of the border.
 *
 * `UIManager` is deliberately absent. Its `update()` and `draw()` are
 * deprecated no-ops in engine 1.9.0 — their bodies do nothing but `(void)` the
 * argument — and the rest of it routes touch events to `UITouchWidget`s. None
 * of the four widgets here is touch-interactive, so the scene drives them
 * directly: `hud_.update()` from `update()`, `hud_.draw()` from `draw()`.
 */
class HudWidgetsScene : public pixelroot32::core::Scene {
public:
    HudWidgetsScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    // --- Screen and world -------------------------------------------------
    /// Logical render size. `LOGICAL_*` defaults to `PHYSICAL_DISPLAY_*`, which
    /// both environments set to 128.
    static constexpr int kScreenWidth = pixelroot32::platforms::config::LogicalWidth;
    static constexpr int kScreenHeight = pixelroot32::platforms::config::LogicalHeight;

    /// The scrollable strip, four screens wide. Nothing here is a tilemap: the
    /// world is a handful of rectangles, because the lesson is the camera, not
    /// the scenery.
    static constexpr int kWorldWidth = kScreenWidth * 4;
    static constexpr int kMaxCameraX = kWorldWidth - kScreenWidth;

    /// Skyline horizon, in world/screen rows (the camera never scrolls in Y).
    static constexpr int kHorizonY = 96;

    /// World X between two posts, and between two ground ticks.
    static constexpr int kPostSpacing = 32;
    static constexpr int kTickSpacing = 16;
    static constexpr int kPostWidth = 10;
    static constexpr int kPostHeight = 30;

    /// Scroll rate while Left or Right is held, in pixels per second.
    static constexpr int kScrollSpeed = 72;

    /// Q8 fixed point for the camera: 256 sub-units = 1 pixel, so a 60 Hz frame
    /// at 72 px/s advances a fraction instead of rounding to zero every time.
    static constexpr int kFixedShift = 8;
    static constexpr int32_t kFixedOne = 1 << kFixedShift;

    /// Upper bound on one integration step. A stall (a breakpoint, the first
    /// frame after init) must not teleport the camera across the world.
    static constexpr unsigned long kMaxStepMs = 100;

    // --- Health -----------------------------------------------------------
    /// Icons in the row, and units that fill one icon. `UISpriteRow` counts in
    /// UNITS, not icons: with 5 hearts at 2 units each, full health is 10.
    static constexpr uint8_t kHeartCapacity = 5;
    static constexpr uint8_t kUnitsPerHeart = 2;
    static constexpr int kMaxHealth = kHeartCapacity * kUnitsPerHeart;

    /// Pixels between hearts, so five 8 px icons span 44 px, not 40.
    static constexpr int kHeartSpacing = 1;

    // --- HUD geometry -----------------------------------------------------
    static constexpr int kPanelHeight = 13;
    static constexpr int kPanelPadding = 3;

    // --- Button indices, in the order InputConfig lists them ---------------
    // The platform headers pass (Up, Down, Left, Right, A, B), and
    // InputManager addresses buttons by that position.
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    /// Camera X in Q8 pixels. Clamped here as well as by `Camera2D`'s bounds,
    /// so holding Left at the wall does not build up a debt you have to
    /// scroll back out of before the view moves.
    int32_t cameraXq8_ = 0;

    /// Health in units, 0..kMaxHealth. Clamped by this scene, because
    /// `UISpriteRow::setValue()` clamps in neither direction.
    int health_ = kMaxHealth;

    /// Furthest camera X reached, divided down. Gives the score label something
    /// that changes while the world scrolls, which is half the demonstration:
    /// the HUD's *content* is live, only its *position* is frozen.
    int score_ = 0;
    int32_t furthestXq8_ = 0;

    /// Last values rendered into the label buffers. Text is rebuilt only when
    /// one of them actually changed.
    int shownScore_ = -1;
    int shownHealth_ = -1;
    int shownCameraX_ = -1;

    /// Label text, kept short on purpose — see refreshLabels().
    char scoreText_[12] = {0};
    char statusText_[16] = {0};

    pixelroot32::graphics::Camera2D camera_;

    // --- The HUD tree, built once in init() -------------------------------
    // UILayout holds its children in a std::vector, and UIAnchorLayout keeps a
    // second vector of (element, anchor) pairs. Both grow on addElement(), so
    // every addElement()/setChild() call in this demo happens in init() and
    // none in update() or draw().
    pixelroot32::graphics::ui::UIAnchorLayout hud_;
    pixelroot32::graphics::ui::UISpriteRow healthRow_;
    pixelroot32::graphics::ui::UILabel scoreLabel_;
    pixelroot32::graphics::ui::UIPanel statusPanel_;
    pixelroot32::graphics::ui::UIPaddingContainer statusPad_;
    pixelroot32::graphics::ui::UILabel statusLabel_;

    /// Camera X in whole pixels.
    int cameraX() const { return cameraXq8_ >> kFixedShift; }

    void readInput(unsigned long deltaTime);
    void refreshLabels();

    void drawSky(pixelroot32::graphics::Renderer& renderer) const;
    void drawWorld(pixelroot32::graphics::Renderer& renderer) const;
};

}  // namespace hud_widgets
