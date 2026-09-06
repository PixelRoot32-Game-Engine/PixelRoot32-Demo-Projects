/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>
#include <graphics/ui/UIButton.h>
#include <graphics/ui/UICheckbox.h>
#include <graphics/ui/UIVerticalLayout.h>

#include <cstdint>

#if !PIXELROOT32_ENABLE_UI_SYSTEM
#error "menu_navigation needs -D PIXELROOT32_ENABLE_UI_SYSTEM=1 (see lib/platformio.ini)"
#endif

namespace menu_navigation {

/**
 * @class MenuNavigationScene
 * @brief A settings menu built from physical-input UI widgets, no touch.
 *
 * Five rows live in a UIVerticalLayout: three UIButton rows and two
 * UICheckBox rows. Up and Down move the selection, A activates the selected
 * row, B restores every row to its default.
 *
 * Nothing here is registered with UIManager. UIManager is the touch router
 * and its update()/draw() are deprecated no-ops; the widgets in this scene
 * are updated and drawn by the scene that owns them.
 *
 * The widgets are plain members, so they outlive the layout that points at
 * them. UILayout keeps its children in a std::vector<UIElement*> — the one
 * allocation this demo makes, and it happens in init() only.
 */
class MenuNavigationScene : public pixelroot32::core::Scene {
public:
    MenuNavigationScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    // --- Widget actions --------------------------------------------------
    // UIElement callbacks are raw function pointers (void(*)() for buttons,
    // void(*)(bool) for checkboxes), so a capturing lambda will not compile.
    // The .cpp holds captureless free functions that reach this scene
    // through a file-scope pointer and call the methods below.

    /// Advances DIFFICULTY through EASY / NORMAL / HARD and wraps.
    void cycleDifficulty();

    /// Advances SPEED through 1X / 2X / 3X and wraps.
    void cycleSpeed();

    /// Mirrors the MUSIC checkbox into the settings block.
    void setMusicEnabled(bool enabled);

    /// Mirrors the SHOW HUD checkbox into the settings block.
    void setHudEnabled(bool enabled);

    /// Restores every setting and both checkbox widgets to their defaults.
    void restoreDefaults();

private:
    /// Button indices as wired by InputConfig in the platform headers:
    /// 0 Up, 1 Down, 2 Left, 3 Right, 4 A, 5 B.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;

    /// The index every row is constructed with.
    ///
    /// UIButton::handleInput and UICheckBox::handleInput both read
    /// `input.isButtonPressed(index)`, so `index` is the *physical button
    /// that activates the widget*, not the row's position in the menu.
    /// Passing a row number there would bind row 2 to Left and row 3 to
    /// Right, silently and without a warning.
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    static constexpr uint8_t kDifficultyCount = 3;
    static constexpr uint8_t kSpeedCount = 3;

    // Defaults, restored by B and by the DEFAULTS row.
    static constexpr uint8_t kDefaultDifficulty = 1;  ///< NORMAL
    static constexpr uint8_t kDefaultSpeed = 0;       ///< 1X
    static constexpr bool kDefaultMusic = true;
    static constexpr bool kDefaultHud = false;

    // --- Screen layout (128x128, 5x7 font: 6 px per character advance) ----
    static constexpr int kLayoutX = 0;
    static constexpr int kLayoutY = 14;
    static constexpr int kLayoutWidth = 128;

    /// padding*2 + 5 rows * 10 px + 4 gaps * 1 px = 56.
    /// UIVerticalLayout::updateLayout() hides any element that falls outside
    /// this viewport, so a smaller value would silently drop the last rows.
    static constexpr int kLayoutHeight = 56;
    static constexpr int kRowWidth = 96;
    static constexpr int kRowHeight = 10;

    static constexpr int kTitleY = 2;
    static constexpr int kSeparatorY = 72;
    static constexpr int kStatusX = 4;
    static constexpr int kStatusLine1Y = 78;
    static constexpr int kStatusLine2Y = 88;
    static constexpr int kActionY = 100;
    static constexpr int kHintY = 118;

    /// Rewrites the two status strings. Called only when a value changes, so
    /// update() and draw() stay free of formatting work.
    void refreshStatus();

    // --- Widgets ---------------------------------------------------------
    // Declaration order is construction order; the constructor's member
    // initialiser list follows it exactly.
    pixelroot32::graphics::ui::UIVerticalLayout menu_;
    pixelroot32::graphics::ui::UIButton difficultyRow_;
    pixelroot32::graphics::ui::UIButton speedRow_;
    pixelroot32::graphics::ui::UICheckBox musicRow_;
    pixelroot32::graphics::ui::UICheckBox hudRow_;
    pixelroot32::graphics::ui::UIButton defaultsRow_;

    // --- Settings, the thing the menu edits ------------------------------
    uint8_t difficulty_ = kDefaultDifficulty;
    uint8_t speed_ = kDefaultSpeed;
    bool music_ = kDefaultMusic;
    bool hud_ = kDefaultHud;

    /// Last row that fired, so activation is visible even when the new value
    /// happens to look like the old one.
    const char* lastAction_ = "";

    char statusLine1_[24] = {0};
    char statusLine2_[24] = {0};
};

} // namespace menu_navigation
