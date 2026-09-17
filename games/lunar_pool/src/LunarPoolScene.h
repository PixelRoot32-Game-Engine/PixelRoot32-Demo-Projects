#pragma once
#include <core/Scene.h>
#include <graphics/Renderer.h>

#include "pool/Table.h"

namespace lunar_pool {

/**
 * @class LunarPoolScene
 * @brief Engine-side presentation layer for the Lunar Pool demo.
 *
 * Maps the 6 engine buttons to the engine-free `pool::Game` core, paces
 * simulation steps against wall-clock time, and draws from the core's const
 * accessors. Input mapping, pacing, and `pool::Game` itself are wired in a
 * later slice; this one loads and draws the table only.
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

private:
    /// Loaded once in init() from tableForStage(1). draw() reads this every
    /// frame; a failed load leaves it only partially written (loadTable()'s
    /// own contract), which is why draw() always checks tableError_ first.
    pool::Table table_;
    /// Result of loading table_ in init(). The engine always calls init()
    /// before draw(), so a fresh Scene never draws with a stale None here.
    pool::TableError tableError_ = pool::TableError::None;
};

}  // namespace lunar_pool
