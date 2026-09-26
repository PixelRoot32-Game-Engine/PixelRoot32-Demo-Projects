#include "PoolMenuScene.h"

#include <core/Engine.h>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace pool {

namespace {

// Button ids follow the InputConfig order in platforms/native.h and
// platforms/esp32_dev.h: Up, Down, Left, Right, A, B.
constexpr uint8_t kBtnUp = 0;
constexpr uint8_t kBtnDown = 1;
constexpr uint8_t kBtnA = 4;

}  // namespace

void PoolMenuScene::setNextScene(PoolScene* next) {
    nextScene_ = next;
}

#if PIXELROOT32_ENABLE_UI_SYSTEM
namespace ui = pr32::graphics::ui;
#endif

PoolMenuScene::PoolMenuScene()
#if PIXELROOT32_ENABLE_UI_SYSTEM
    : titleLabel_("POOL", pr32::math::Vector2(0, 36), pr32::graphics::Color::Magenta, 4),
      startBtn_("START GAME", kBtnA, pr32::math::Vector2(30, 104), pr32::math::Vector2(180, 24)),
      musicBox_("MUSIC", kBtnA, pr32::math::Vector2(79, 136), pr32::math::Vector2(100, 24), true),
      sfxBox_("SFX", kBtnA, pr32::math::Vector2(91, 168), pr32::math::Vector2(76, 24), true),
      hintLabel_("UP DOWN SELECT A OK", pr32::math::Vector2(0, 214), pr32::graphics::Color::Gray, 1)
#endif
{
#if PIXELROOT32_ENABLE_UI_SYSTEM
    // Transparent rows over black: selection reads as ">" + yellow.
    startBtn_.setStyle(pr32::graphics::Color::White, pr32::graphics::Color::Black, false);
    musicBox_.setStyle(pr32::graphics::Color::White, pr32::graphics::Color::Black, false);
    sfxBox_.setStyle(pr32::graphics::Color::White, pr32::graphics::Color::Black, false);
#endif
}

void PoolMenuScene::init() {
    Scene::init();
    menuSel_ = 0;
    elapsedMs_ = 0;
#if PIXELROOT32_ENABLE_UI_SYSTEM
    titleLabel_.centerX(240);  // Fixed 240x240 logical display, see platformio.ini.
    hintLabel_.centerX(240);
    applyMenuSelection();
#endif
}

void PoolMenuScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);
    elapsedMs_ += deltaTime;
    if (nextScene_ == nullptr) {
        return;
    }
    auto& input = engine.getInputManager();
#if PIXELROOT32_ENABLE_UI_SYSTEM
    // D-pad menu: Up/Down moves the cursor with wrap, A activates the row.
    if (input.isButtonPressed(kBtnUp)) {
        menuSel_ = static_cast<uint8_t>((menuSel_ + 2) % 3);
        applyMenuSelection();
        nextScene_->playMenuTick();
    }
    if (input.isButtonPressed(kBtnDown)) {
        menuSel_ = static_cast<uint8_t>((menuSel_ + 1) % 3);
        applyMenuSelection();
        nextScene_->playMenuTick();
    }
    if (input.isButtonPressed(kBtnA)) {
        if (menuSel_ == 0) {
            // setScene re-inits the game scene (fresh run from stage 1),
            // then the run leaves its Menu state immediately.
            engine.setScene(nextScene_);
            nextScene_->startGameFromMenu();
        } else if (menuSel_ == 1) {
            musicBox_.toggle();
            nextScene_->setMusicEnabled(musicBox_.isChecked());
        } else {
            sfxBox_.toggle();
            nextScene_->setSfxEnabled(sfxBox_.isChecked());
        }
    }
#else
    if (input.isButtonPressed(kBtnA)) {
        engine.setScene(nextScene_);
        nextScene_->startGameFromMenu();
    }
#endif
}

void PoolMenuScene::draw(pr32::graphics::Renderer& renderer) {
    using Color = pr32::graphics::Color;
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);
#if PIXELROOT32_ENABLE_UI_SYSTEM
    titleLabel_.draw(renderer);
    startBtn_.draw(renderer);
    musicBox_.draw(renderer);
    sfxBox_.draw(renderer);
    // Blinking nav hint, bomberbot title-screen style.
    if (((elapsedMs_ / 500U) % 2U) == 0U) {
        hintLabel_.draw(renderer);
    }
#else
    renderer.drawTextCentered("POOL", 60, Color::Magenta, 4);
    renderer.drawTextCentered("PRESS A TO START", 140, Color::Gray, 1);
#endif
    Scene::draw(renderer);
}

#if PIXELROOT32_ENABLE_UI_SYSTEM
void PoolMenuScene::applyMenuSelection() {
    startBtn_.setSelected(menuSel_ == 0);
    musicBox_.setSelected(menuSel_ == 1);
    sfxBox_.setSelected(menuSel_ == 2);
}
#endif

}  // namespace pool
