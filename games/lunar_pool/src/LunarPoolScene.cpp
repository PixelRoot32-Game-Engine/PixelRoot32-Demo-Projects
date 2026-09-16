#include "LunarPoolScene.h"

namespace pr32 = pixelroot32;

namespace lunar_pool {

void LunarPoolScene::init() {
    Scene::init();  // resetState() + physicsScheduler.init() — always call base first
    // No entities yet: the demo has no actors, only a hand-drawn table (added
    // once src/pool/ and the table format exist).
}

void LunarPoolScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);
    // Input mapping, pacing, and pool::Game::step() are wired in a later
    // slice, once src/pool/Game exists.
}

void LunarPoolScene::draw(pr32::graphics::Renderer& renderer) {
    // Black fill only: this scaffold stub proves the platform boots before
    // any table/ball rendering exists. draw() paints the background first,
    // then calls Scene::draw() last, since the base paints entities that a
    // later fill would overpaint.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                  pr32::graphics::Color::Black);
    Scene::draw(renderer);
}

}  // namespace lunar_pool
