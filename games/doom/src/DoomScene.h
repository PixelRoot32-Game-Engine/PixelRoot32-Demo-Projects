#pragma once

#include <core/Scene.h>

namespace doom {

/**
 * @brief Composition root of the DOOM port.
 *
 * This is the Phase 0 scaffold: it exists so the project links against the
 * engine, owns the 320x200 logical surface and proves that the whole surface
 * reaches the panel through the engine's present path. The rasteriser that
 * replaces the placeholder frame is Phase 3 work; the palette contract it will
 * need is Phase 1 (docs/plan-doom-esp32.md, Phase 1).
 *
 * The logical surface is a constant here on purpose. DOOM's renderer is
 * column-indexed: `distscale[SCREENWIDTH]`, `visplane_t::top[SCREENWIDTH]`,
 * `floorclip` / `ceilingclip` and the status bar layout all assume 320x200.
 * Rendering into anything narrower is a renderer rewrite plus a HUD relayout,
 * not a build flag, so the panel only ever changes the physical window.
 */
class DoomScene : public pixelroot32::core::Scene {
public:
    /// DOOM's own surface width. See the class comment.
    static constexpr int kLogicalWidth = 320;
    /// DOOM's own surface height, status bar included.
    static constexpr int kLogicalHeight = 200;

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    unsigned long frames_ = 0;
};

}  // namespace doom
