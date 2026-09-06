/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "MenuNavigationScene.h"

#include <core/Engine.h>
#include <math/MathUtil.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace menu_navigation {

namespace gfx = pr32::graphics;
namespace ui = pr32::graphics::ui;
using gfx::Color;
using pr32::math::Vector2;
using pr32::math::toScalar;

namespace {

/// The scene that owns the widgets.
///
/// UIElement's callback types are raw function pointers — `void(*)()` for a
/// button and `void(*)(bool)` for a checkbox — chosen over std::function to
/// keep the widgets allocation-free. A lambda with a capture list therefore
/// does not convert, and the callbacks below reach the scene through this
/// file-scope pointer instead. init() sets it; there is exactly one scene.
MenuNavigationScene* g_scene = nullptr;

const char* const kDifficultyNames[] = {"EASY", "NORMAL", "HARD"};
const char* const kSpeedNames[] = {"1X", "2X", "3X"};

void onDifficultyPressed() {
    if (g_scene) g_scene->cycleDifficulty();
}

void onSpeedPressed() {
    if (g_scene) g_scene->cycleSpeed();
}

void onDefaultsPressed() {
    if (g_scene) g_scene->restoreDefaults();
}

void onMusicChanged(bool checked) {
    if (g_scene) g_scene->setMusicEnabled(checked);
}

void onHudChanged(bool checked) {
    if (g_scene) g_scene->setHudEnabled(checked);
}

} // namespace

MenuNavigationScene::MenuNavigationScene()
    // The layout owns the geometry; every row is constructed at the origin
    // and moved by UIVerticalLayout::updateLayout() when it is added.
    : menu_(Vector2(kLayoutX, kLayoutY), kLayoutWidth, kLayoutHeight),
      // Second argument is kButtonA on every row: it is the physical button
      // that activates the widget, never the row's position in the menu.
      difficultyRow_("DIFFICULTY", kButtonA, Vector2(0, 0), Vector2(kRowWidth, kRowHeight),
                     onDifficultyPressed, ui::TextAlignment::CENTER, 1),
      speedRow_("SPEED", kButtonA, Vector2(0, 0), Vector2(kRowWidth, kRowHeight),
                onSpeedPressed, ui::TextAlignment::CENTER, 1),
      musicRow_("MUSIC", kButtonA, Vector2(0, 0), Vector2(kRowWidth, kRowHeight),
                kDefaultMusic, onMusicChanged, 1),
      hudRow_("SHOW HUD", kButtonA, Vector2(0, 0), Vector2(kRowWidth, kRowHeight),
              kDefaultHud, onHudChanged, 1),
      defaultsRow_("DEFAULTS", kButtonA, Vector2(0, 0), Vector2(kRowWidth, kRowHeight),
                   onDefaultsPressed, ui::TextAlignment::CENTER, 1) {}

void MenuNavigationScene::init() {
    Scene::init();

    g_scene = this;

    gfx::setPalette(gfx::PaletteType::PR32);

    difficulty_ = kDefaultDifficulty;
    speed_ = kDefaultSpeed;
    music_ = kDefaultMusic;
    hud_ = kDefaultHud;
    lastAction_ = "";

    musicRow_.setChecked(kDefaultMusic);
    hudRow_.setChecked(kDefaultHud);

    menu_.setPadding(toScalar(1));
    menu_.setSpacing(toScalar(1));

    // UIVerticalLayout drives the selection itself. Its defaults are already
    // 0 and 1, but naming them keeps the menu honest if InputConfig's order
    // ever changes in the platform headers.
    menu_.setNavigationButtons(kButtonUp, kButtonDown);

    // setSelectedIndex() re-applies these four colours to every row, so the
    // selected row is filled and the others are drawn with a ">" marker.
    menu_.setButtonStyle(Color::Black, Color::Cyan, Color::White, Color::Black);

    // The only allocation in this demo: UILayout keeps its children in a
    // std::vector<UIElement*>, and addElement() push_backs into it. It runs
    // here and never again — update() and draw() add nothing. The widgets
    // are scene members, so the non-owning pointers stay valid for as long
    // as the layout does.
    menu_.addElement(&difficultyRow_);
    menu_.addElement(&speedRow_);
    menu_.addElement(&musicRow_);
    menu_.addElement(&hudRow_);
    menu_.addElement(&defaultsRow_);

    menu_.setSelectedIndex(0);

    refreshStatus();
}

void MenuNavigationScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    const pr32::input::InputManager& input = engine.getInputManager();

    // One call does the whole menu: UIVerticalLayout::handleInput() moves the
    // selection on Up/Down and then forwards the same InputManager to the
    // selected row, whose own handleInput() fires on kButtonA. No UIManager,
    // no touch, no hit test.
    menu_.handleInput(input);

    if (input.isButtonPressed(kButtonB)) {
        restoreDefaults();
    }

    menu_.update(deltaTime);
}

void MenuNavigationScene::draw(gfx::Renderer& renderer) {
    // Background first, base class second: Scene::draw() paints the scene's
    // entities, so a fill placed after it would overpaint them.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("SETTINGS", kTitleY, Color::White, 1);

    // The layout draws its children. UIManager::draw() is a deprecated no-op
    // and would draw nothing even if these widgets were registered with it.
    menu_.draw(renderer);

    renderer.drawFilledRectangle(0, kSeparatorY, renderer.getLogicalWidth(), 1, Color::Gray);

    renderer.drawText(statusLine1_, kStatusX, kStatusLine1Y, Color::LightGreen, 1);
    renderer.drawText(statusLine2_, kStatusX, kStatusLine2Y, Color::LightGreen, 1);
    renderer.drawText(lastAction_, kStatusX, kActionY, Color::Yellow, 1);

    renderer.drawText("UP/DN  A:OK  B:RESET", kStatusX, kHintY, Color::Gray, 1);
}

void MenuNavigationScene::cycleDifficulty() {
    difficulty_ = static_cast<uint8_t>((difficulty_ + 1) % kDifficultyCount);
    lastAction_ = "LAST: DIFFICULTY";
    refreshStatus();
}

void MenuNavigationScene::cycleSpeed() {
    speed_ = static_cast<uint8_t>((speed_ + 1) % kSpeedCount);
    lastAction_ = "LAST: SPEED";
    refreshStatus();
}

void MenuNavigationScene::setMusicEnabled(bool enabled) {
    music_ = enabled;
    lastAction_ = "LAST: MUSIC";
    refreshStatus();
}

void MenuNavigationScene::setHudEnabled(bool enabled) {
    hud_ = enabled;
    lastAction_ = "LAST: SHOW HUD";
    refreshStatus();
}

void MenuNavigationScene::restoreDefaults() {
    difficulty_ = kDefaultDifficulty;
    speed_ = kDefaultSpeed;

    // setChecked() only fires onCheckChanged when the value actually moves,
    // so the two calls below may or may not run the callbacks. The two
    // assignments after them make the settings block correct either way.
    musicRow_.setChecked(kDefaultMusic);
    hudRow_.setChecked(kDefaultHud);
    music_ = kDefaultMusic;
    hud_ = kDefaultHud;

    lastAction_ = "LAST: DEFAULTS";
    refreshStatus();
}

void MenuNavigationScene::refreshStatus() {
    // snprintf into fixed members: no std::string, no heap, and only when a
    // value changes rather than once per frame.
    snprintf(statusLine1_, sizeof(statusLine1_), "DIFF %s  SPD %s",
             kDifficultyNames[difficulty_], kSpeedNames[speed_]);
    snprintf(statusLine2_, sizeof(statusLine2_), "MUSIC %s  HUD %s",
             music_ ? "ON" : "OFF", hud_ ? "ON" : "OFF");
}

} // namespace menu_navigation
