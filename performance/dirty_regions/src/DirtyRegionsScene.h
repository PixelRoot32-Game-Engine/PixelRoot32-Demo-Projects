#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

namespace dirty_regions {

/**
 * @brief One screen that makes the cost of a redraw visible.
 *
 * The scene is deliberately sparse: a lattice of small static blocks and four
 * moving boxes, drawn with `Renderer` primitives only. Every primitive marks
 * the 8x8 cells it touched, and `Renderer::beginFrame()` clears only the cells
 * marked by the previous frame, so a scene that leaves most of the screen
 * untouched pays for a fraction of a full framebuffer clear.
 *
 * A toggles `Renderer::forceFullRedraw()` on every frame, which is the closest
 * honest stand-in for building without `PIXELROOT32_ENABLE_DIRTY_REGIONS`.
 * B toggles the engine's debug dirty-cell overlay.
 */
class DirtyRegionsScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /** @brief Positions are integers in 1/16 of a pixel, so no float enters the loop. */
    static constexpr int SUBPIXEL_SHIFT = 4;

    static constexpr int BOX_COUNT = 4;
    static constexpr int BOX_SIZE = 14;

    /** @brief Static blocks: 6x6 pixels every 24, small enough to leave most cells clean. */
    static constexpr int LATTICE_STEP = 24;
    static constexpr int LATTICE_BLOCK = 6;

    /** @brief Rows reserved at the top and bottom of the screen for the three text lines. */
    static constexpr int HUD_TOP_HEIGHT = 22;
    static constexpr int HUD_BOTTOM_HEIGHT = 12;

    /** @brief Frame times averaged over this many frames. */
    static constexpr int FRAME_SAMPLES = 32;

    /** @brief A single frame longer than this is a stall, not a frame; it is clamped out. */
    static constexpr unsigned long MAX_FRAME_MS = 50;

    struct Box {
        int32_t x;   ///< Left edge, in 1/16 px.
        int32_t y;   ///< Top edge, in 1/16 px.
        int32_t vx;  ///< Horizontal speed, in 1/16 px per millisecond.
        int32_t vy;  ///< Vertical speed, in 1/16 px per millisecond.
        pixelroot32::graphics::Color color;
    };

    Box boxes[BOX_COUNT] = {};

    unsigned long frameSamples[FRAME_SAMPLES] = {};
    unsigned long frameSampleSum = 0;
    uint8_t frameSampleIndex = 0;
    uint8_t frameSampleCount = 0;

    bool fullRedrawMode = false;

    /**
     * @brief Whether the engine's dirty-region path is actually running.
     *
     * `Renderer::beginFrame()` skips the dirty grid entirely when the draw
     * surface exposes no 8bpp framebuffer, which is the case for the SDL2
     * driver. Enabling PIXELROOT32_ENABLE_DIRTY_REGIONS is therefore necessary
     * but not sufficient, and the demo reads the same precondition the engine
     * reads rather than assuming the flag is the whole story.
     */
    bool dirtyPathLive = false;

    int screenWidth = 0;
    int screenHeight = 0;
    int fieldTop = 0;
    int fieldBottom = 0;

    char modeText[24] = {};
    char pathText[24] = {};

    void readButtons();
    void moveBoxes(unsigned long deltaTime);
    void recordFrameTime(unsigned long deltaTime);
    void readDirtyPathState();
    void buildModeText();
    void drawStaticLattice(pixelroot32::graphics::Renderer& renderer) const;
};

} // namespace dirty_regions
