#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>

#if PIXELROOT32_ENABLE_UI_SYSTEM
#include <graphics/ui/UIButton.h>
#include <graphics/ui/UICheckbox.h>
#include <graphics/ui/UILabel.h>
#endif

#include "PoolScene.h"

namespace pool {

/**
 * @class PoolMenuScene
 * @brief Independent title screen (bomberbot TitleScreenScene pattern).
 *
 * Black background, pink POOL title, a START GAME row and two separate
 * MUSIC / SFX checks (engine UIButton/UICheckBox/UILabel, D-pad driven),
 * plus a nav hint. Up/Down moves the cursor with wrap, A activates:
 * START deals a fresh run in the game scene (engine.setScene() re-inits
 * it from stage 1), the checks mute music and effects independently.
 * One-way trip: there is no path back from the game scene.
 */
class PoolMenuScene : public pixelroot32::core::Scene {
public:
    PoolMenuScene();

    /// Pointer to the game scene. Set by the platform entry point before
    /// engine.run() begins (mirrors bomberbot's setNextScene).
    static void setNextScene(PoolScene* next);

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
#if PIXELROOT32_ENABLE_UI_SYSTEM
    /// Pushes the menu cursor onto the START/MUSIC/SFX widgets.
    void applyMenuSelection();

    pixelroot32::graphics::ui::UILabel titleLabel_;
    pixelroot32::graphics::ui::UIButton startBtn_;
    pixelroot32::graphics::ui::UICheckBox musicBox_;
    pixelroot32::graphics::ui::UICheckBox sfxBox_;
    pixelroot32::graphics::ui::UILabel hintLabel_;
#endif
    /// Menu cursor: 0 = START GAME, 1 = MUSIC, 2 = SFX.
    uint8_t menuSel_ = 0;
    /// Blink clock for the nav hint (~0.5 s on / 0.5 s off, square wave).
    unsigned long elapsedMs_ = 0;

    static inline PoolScene* nextScene_ = nullptr;
};

}  // namespace pool
