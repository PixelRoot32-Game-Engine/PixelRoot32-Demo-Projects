/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "HudWidgetsScene.h"

#include <core/Engine.h>
#include <math/Vector2.h>

#include <cstdio>

#include "assets/HudSprites.h"

namespace pr32 = pixelroot32;

/// The engine instance lives in the platform header that owns main()/setup().
/// Reaching it from the scene through an extern declaration is the established
/// pattern in this repository, not a workaround.
extern pr32::core::Engine engine;

namespace hud_widgets {

namespace gfx = pr32::graphics;
namespace ui = pr32::graphics::ui;
namespace math = pr32::math;

using gfx::Color;

namespace {

/// Sky above the horizon and ground below it.
constexpr Color kSkyColor = Color::Navy;
constexpr Color kGroundColor = Color::DarkGreen;

/// Posts cycle through these so two neighbours never look alike while scrolling.
constexpr Color kPostColors[] = {
    Color::Cyan,
    Color::Yellow,
    Color::Magenta,
    Color::LightGreen
};
constexpr int kPostColorCount = sizeof(kPostColors) / sizeof(kPostColors[0]);

/// HUD colours.
constexpr Color kFullHeartColor = Color::Red;
constexpr Color kEmptyHeartColor = Color::Gray;
constexpr Color kScoreColor = Color::Yellow;
constexpr Color kStatusColor = Color::White;
constexpr Color kPanelFill = Color::Black;
constexpr Color kPanelBorder = Color::White;

int clampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

}  // namespace

HudWidgetsScene::HudWidgetsScene()
    // Every widget needs its constructor arguments here: none of them is
    // default-constructible, and the whole tree is a plain member of the scene
    // so its cost lands in this object's sizeof rather than on a heap.
    : camera_(kScreenWidth, kScreenHeight),
      hud_(math::Vector2(math::toScalar(0), math::toScalar(0)), kScreenWidth, kScreenHeight),
      healthRow_(math::Vector2(math::toScalar(0), math::toScalar(0))),
      scoreLabel_("SC:0", math::Vector2(math::toScalar(0), math::toScalar(0)), kScoreColor, 1),
      statusPanel_(math::Vector2(math::toScalar(0), math::toScalar(0)), kScreenWidth, kPanelHeight),
      statusPad_(math::Vector2(math::toScalar(0), math::toScalar(0)), kScreenWidth, kPanelHeight),
      statusLabel_("", math::Vector2(math::toScalar(0), math::toScalar(0)), kStatusColor, 1) {
}

void HudWidgetsScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    cameraXq8_ = 0;
    furthestXq8_ = 0;
    health_ = kMaxHealth;
    score_ = 0;
    shownScore_ = -1;
    shownHealth_ = -1;
    shownCameraX_ = -1;

    // Bounds first. Camera2D::setPosition() clamps against them, and a camera
    // left unbounded would happily scroll past the end of the world.
    camera_.setBounds(math::toScalar(0), math::toScalar(kMaxCameraX));
    camera_.setPosition(math::Vector2(math::toScalar(0), math::toScalar(0)));

    // --- Health row -------------------------------------------------------
    // setUnitsPerIcon() decides which state indices are meaningful: with 2
    // units per icon, states 0..2 are empty/half/full and states 3..4 are
    // never asked for.
    healthRow_.setUnitsPerIcon(kUnitsPerHeart);
    healthRow_.setStateSprite(0, assets::kHeartEmpty, kEmptyHeartColor);
    healthRow_.setStateSprite(1, assets::kHeartHalf, kFullHeartColor);
    healthRow_.setStateSprite(2, assets::kHeartFull, kFullHeartColor);
    healthRow_.setSpacing(kHeartSpacing);
    // 0 means "one unwrapped row", not "no icons". Five hearts at 8 px plus
    // four 1 px gaps is 44 px, which fits 128 without wrapping.
    healthRow_.setIconsPerRow(0);
    healthRow_.setCapacity(kHeartCapacity);
    healthRow_.setValue(health_);

    // --- Status panel -----------------------------------------------------
    statusPanel_.setBackgroundColor(kPanelFill);
    statusPanel_.setBorderColor(kPanelBorder);
    statusPanel_.setBorderWidth(1);
    // UIPanel::setChild() plants the child at the panel's own top-left corner,
    // straight on top of the border. UIPaddingContainer is the piece that
    // moves the text inside the frame.
    statusPad_.setPadding(math::toScalar(kPanelPadding));
    statusPad_.setChild(&statusLabel_);
    statusPanel_.setChild(&statusPad_);

    // --- The flag this demo is about --------------------------------------
    // The layout's own flag would be enough: UIAnchorLayout::draw() turns the
    // offset bypass on before drawing its children and off afterwards, so the
    // whole subtree inherits it. Each widget is marked anyway, because the
    // flag belongs to the widget rather than to its container: any one of
    // these is camera-immune on its own, drawn through the layout or not.
    hud_.setFixedPosition(true);
    healthRow_.setFixedPosition(true);
    scoreLabel_.setFixedPosition(true);
    statusPanel_.setFixedPosition(true);

    // --- Anchoring --------------------------------------------------------
    // The only allocating calls in the demo. addElement() pushes onto two
    // std::vectors; it is idempotent (a second add of the same pointer is
    // dropped), which is what keeps a re-entered scene from doubling the HUD.
    hud_.setScreenSize(kScreenWidth, kScreenHeight);
    hud_.addElement(&healthRow_, ui::Anchor::TOP_LEFT);
    hud_.addElement(&scoreLabel_, ui::Anchor::TOP_RIGHT);
    hud_.addElement(&statusPanel_, ui::Anchor::BOTTOM_CENTER);

    refreshLabels();
}

void HudWidgetsScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    readInput(deltaTime);

    camera_.setPosition(math::Vector2(math::toScalar(cameraX()), math::toScalar(0)));

    // UISpriteRow has no clock of its own; it draws whatever setValue() last
    // said. health_ is already clamped in readInput().
    healthRow_.setValue(health_);

    refreshLabels();

    // The widgets are updated by the scene, not by UIManager, whose update()
    // is a deprecated no-op in 1.9.0. UIAnchorLayout::update() forwards to
    // every child; UILabel and UISpriteRow both no-op, so this costs a loop
    // over three pointers and keeps the wiring honest.
    hud_.update(deltaTime);
}

void HudWidgetsScene::readInput(unsigned long deltaTime) {
    const pr32::input::InputManager& input = engine.getInputManager();

    const unsigned long stepMs = (deltaTime > kMaxStepMs) ? kMaxStepMs : deltaTime;
    const int32_t step = static_cast<int32_t>(
        (static_cast<int32_t>(kScrollSpeed) * static_cast<int32_t>(stepMs) * kFixedOne) / 1000);

    if (input.isButtonDown(kButtonRight)) {
        cameraXq8_ += step;
    }
    if (input.isButtonDown(kButtonLeft)) {
        cameraXq8_ -= step;
    }

    const int32_t maxq8 = static_cast<int32_t>(kMaxCameraX) * kFixedOne;
    if (cameraXq8_ < 0) cameraXq8_ = 0;
    if (cameraXq8_ > maxq8) cameraXq8_ = maxq8;

    if (cameraXq8_ > furthestXq8_) {
        furthestXq8_ = cameraXq8_;
        score_ = (furthestXq8_ >> kFixedShift) / 4;
    }

    // A damages, B heals, one unit each, so a half heart is a reachable state.
    // Both clamps are this scene's job: setValue() is documented as unclamped
    // in both directions, and a value of -3 or 99 is not an error to the row,
    // it simply renders as fully empty or fully full.
    if (input.isButtonPressed(kButtonA)) {
        health_ = clampInt(health_ - 1, 0, kMaxHealth);
    }
    if (input.isButtonPressed(kButtonB)) {
        health_ = clampInt(health_ + 1, 0, kMaxHealth);
    }
}

void HudWidgetsScene::refreshLabels() {
    // UILabel stores its text in a std::string. Rebuilding it every frame
    // would be pointless work, and a string longer than the small-string
    // buffer would allocate inside update(), which is why both formats below
    // stay under 15 characters and why the text is rebuilt only on change.
    const int camX = cameraX();

    if (score_ != shownScore_) {
        shownScore_ = score_;
        std::snprintf(scoreText_, sizeof(scoreText_), "SC:%d", score_);
        scoreLabel_.setText(scoreText_);
        // TOP_RIGHT is computed from the element's width at layout time, and
        // "SC:9" is narrower than "SC:96". Without this the label creeps away
        // from the right edge as the score grows. updateLayout() walks the
        // anchored vector and assigns positions; it allocates nothing.
        hud_.updateLayout();
    }

    if (health_ != shownHealth_ || camX != shownCameraX_) {
        shownHealth_ = health_;
        shownCameraX_ = camX;
        std::snprintf(statusText_, sizeof(statusText_), "X:%d HP:%d/%d", camX, health_, kMaxHealth);
        statusLabel_.setText(statusText_);
        // No relayout: the status label is left-aligned inside the padding
        // container, so its position does not depend on its width.
    }
}

void HudWidgetsScene::draw(gfx::Renderer& renderer) {
    // Camera first: this writes the renderer's display offset, and every draw
    // call after it is in world space unless something bypasses the offset.
    camera_.apply(renderer);

    drawSky(renderer);
    drawWorld(renderer);

    // Scene::draw() paints the scene's entities, so it comes after the
    // background and not before it. There are no world entities yet; the call
    // stays because it is where one would go, and because Scene::draw() also
    // resets the renderer's palette context on the way out.
    Scene::draw(renderer);

    // The HUD is NOT a scene entity, and that is deliberate. Scene::draw()
    // culls each entity against the world-space viewport rectangle it derives
    // from the renderer offset (isVisibleInViewport() in core/Scene.cpp). A
    // fixed element's position is in screen coordinates and never moves, so
    // once the camera scrolls past 128 that cull would decide the whole HUD is
    // off-screen and stop drawing it. Drawing it here sidesteps the cull.
    hud_.draw(renderer);
}

void HudWidgetsScene::drawSky(gfx::Renderer& renderer) const {
    // A flat wash. It is screen-space, not world content, so it bypasses the
    // camera offset for the same reason the HUD does, by the same mechanism,
    // spelled out here rather than hidden behind a flag.
    const bool wasBypassed = renderer.isOffsetBypassEnabled();
    renderer.setOffsetBypass(true);

    renderer.drawFilledRectangle(0, 0, kScreenWidth, kHorizonY, kSkyColor);
    renderer.drawFilledRectangle(0, kHorizonY, kScreenWidth, kScreenHeight - kHorizonY, kGroundColor);

    renderer.setOffsetBypass(wasBypassed);
}

void HudWidgetsScene::drawWorld(gfx::Renderer& renderer) const {
    const int camX = cameraX();
    const int viewRight = camX + kScreenWidth;

    // Posts, culled to the visible span. Sixteen posts is cheap enough to draw
    // blind, but a demo about a scrolling world should show the arithmetic.
    const int firstPost = (camX - kPostWidth) / kPostSpacing;
    const int lastPost = viewRight / kPostSpacing;

    for (int i = (firstPost < 0 ? 0 : firstPost); i <= lastPost; ++i) {
        const int postX = i * kPostSpacing;
        if (postX >= kWorldWidth) break;

        renderer.drawFilledRectangle(postX,
                                     kHorizonY - kPostHeight,
                                     kPostWidth,
                                     kPostHeight,
                                     kPostColors[i % kPostColorCount]);

        // The world's X written into the world, so "the scenery moved and the
        // HUD did not" is readable off a single screenshot.
        char marker[8];
        std::snprintf(marker, sizeof(marker), "%d", postX);
        renderer.drawText(marker, static_cast<int16_t>(postX),
                          static_cast<int16_t>(kHorizonY + 4), Color::White, 1);
    }

    // Ground ticks: the fine-grained motion cue between posts.
    const int firstTick = camX / kTickSpacing;
    const int lastTick = viewRight / kTickSpacing;
    for (int i = (firstTick < 0 ? 0 : firstTick); i <= lastTick; ++i) {
        const int tickX = i * kTickSpacing;
        if (tickX >= kWorldWidth) break;
        renderer.drawFilledRectangle(tickX, kHorizonY, 1, 3, Color::LightGreen);
    }
}

}  // namespace hud_widgets
