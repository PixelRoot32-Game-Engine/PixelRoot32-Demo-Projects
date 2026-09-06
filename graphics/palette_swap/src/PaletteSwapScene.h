/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstdint>

namespace palette_swap {

/**
 * @class PaletteSwapScene
 * @brief One fixed scene, four palettes, and a single call between them.
 *
 * draw() is written once and never branches on the selected palette. Every
 * primitive names a `pixelroot32::graphics::Color`, which is a palette index
 * rather than a colour, so `setCustomPalette()` repaints the whole frame
 * without the drawing code knowing a swap happened.
 *
 * The scene owns no palette memory: `assets/Palettes.h` holds the four arrays
 * with static storage duration, because the engine keeps the pointer it is
 * given and never copies the entries.
 */
class PaletteSwapScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Button indices, in the order InputConfig lists them in the platform
    /// headers: Up, Down, Left, Right, A, B.
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    // --- Screen layout (128x128, 5x7 font at size 1: 6 px per character) ---
    static constexpr int kTitleY = 1;
    static constexpr int kStatusY = 10;

    /// 16 swatches at 8 px pitch fill the 128 px width exactly, which also
    /// leaves room for one 5 px hex digit under each of them.
    static constexpr int kSwatchWidth = 8;
    static constexpr int kSwatchY = 19;
    static constexpr int kSwatchHeight = 11;
    static constexpr int kRulerY = 31;

    static constexpr int kShapesY = 41;
    static constexpr int kShapesHeight = 22;

    static constexpr int kNameRow0Y = 66;
    static constexpr int kNameRowStep = 9;
    static constexpr int kNameColumn0X = 2;
    static constexpr int kNameColumn1X = 66;

    static constexpr int kAliasY = 102;
    static constexpr int kControlsY = 120;

    void selectPalette(uint8_t index);
    void restoreEngineDefault();
    void refreshStatus();

    void drawSwatchStrip(pixelroot32::graphics::Renderer& renderer) const;
    void drawShapes(pixelroot32::graphics::Renderer& renderer) const;
    void drawNamedColors(pixelroot32::graphics::Renderer& renderer) const;

    /// Index into `kPalettes`. Kept across a jump to the engine default, so
    /// the cycle resumes from where the viewer left it.
    uint8_t index_ = 0;

    /// True while the built-in PR32 palette is active because B was pressed.
    bool showingEngineDefault_ = false;

    /// "2/4 SIGNAL" or "-/- PR32 DEFAULT". Rewritten only on a swap, never
    /// per frame: draw() must not allocate or format.
    char statusText_[24] = {0};
};

} // namespace palette_swap
