#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>

namespace lunar_pool {

/**
 * @class LunarPoolScene
 * @brief Engine-side presentation layer for the Lunar Pool demo.
 *
 * Maps the 6 engine buttons to the engine-free `pool::Game` core, paces
 * simulation steps against wall-clock time, and draws from the core's const
 * accessors. This scaffold stub only fills the screen; input mapping,
 * pacing, and rendering are wired in later slices once `pool::Game` exists.
 */
class LunarPoolScene : public pixelroot32::core::Scene {
public:
    /**
     * @brief Initializes the scene. Always calls `Scene::init()` first.
     */
    void init() override;

    /**
     * @brief Advances the scene by one engine update tick.
     * @param deltaTime Elapsed time in milliseconds since the last update.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the scene.
     * @param renderer The renderer to draw into.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;
};

}  // namespace lunar_pool
