/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "PaletteSwapScene.h"

#include "assets/Palettes.h"

#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>
#include <string_view>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace palette_swap {

namespace gfx = pr32::graphics;
using gfx::Color;

namespace {

/// One character per palette slot, so the ruler under the swatch strip labels
/// slot 10 as 'A' rather than needing two columns for a two-digit number.
constexpr char kSlotDigits[] = "0123456789ABCDEF";

/**
 * @struct NamedColor
 * @brief A label drawn in the colour it names.
 *
 * The label is a literal and the colour is a slot number; neither changes when
 * the palette does. "ORANGE" reading as blue is not a bug — it is the demo.
 */
struct NamedColor {
    const char* label;
    Color color;
};

/// Two columns by four rows. Together with the shapes band and the HUD, these
/// cover all sixteen slots, so no swap can be invisible.
constexpr NamedColor kNamedColors[][2] = {
    {{"WHITE",      Color::White},      {"NAVY",    Color::Navy}},
    {{"BLUE",       Color::Blue},       {"DARKGREEN", Color::DarkGreen}},
    {{"LIGHTGREEN", Color::LightGreen}, {"ORANGE",  Color::Orange}},
    {{"LIGHTRED",   Color::LightRed},   {"DARKRED", Color::DarkRed}}
};

constexpr int kNamedColorRows =
    static_cast<int>(sizeof(kNamedColors) / sizeof(kNamedColors[0]));

} // namespace

void PaletteSwapScene::init() {
    Scene::init();

    // The first palette is applied here and nowhere else afterwards: update()
    // only swaps, draw() only draws.
    selectPalette(0);

    pr32::core::logging::log("PaletteSwapScene: 4 palettes, one draw path");
}

void PaletteSwapScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    // isButtonPressed() is edge triggered, so holding a direction does not
    // spin through the cycle.
    if (input.isButtonPressed(kButtonB)) {
        restoreEngineDefault();
    } else if (input.isButtonPressed(kButtonRight) || input.isButtonPressed(kButtonA)) {
        selectPalette(static_cast<uint8_t>((index_ + 1u) % kPaletteCount));
    } else if (input.isButtonPressed(kButtonLeft)) {
        selectPalette(static_cast<uint8_t>((index_ + kPaletteCount - 1u) % kPaletteCount));
    }
}

void PaletteSwapScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, so filling after it would overpaint them. init() and update()
    // are the overrides that call their base first, not draw().
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                 Color::Black);

    Scene::draw(renderer);

    // Nothing below this line is aware of which palette is active. The whole
    // swap happened in one setCustomPalette() call in selectPalette().
    renderer.drawTextCentered("PALETTE SWAP", kTitleY, Color::White, 1);
    renderer.drawText(statusText_, kNameColumn0X, kStatusY, Color::Yellow, 1);

    drawSwatchStrip(renderer);
    drawShapes(renderer);
    drawNamedColors(renderer);

    renderer.drawText("<>,A:CYCLE B:DEFAULT", kNameColumn0X, kControlsY, Color::Gray, 1);
}

void PaletteSwapScene::selectPalette(uint8_t index) {
    index_ = index;
    showingEngineDefault_ = false;

    // The one line the demo exists for. kPalettes[index].entries points into
    // rodata with static storage duration, which matters: the engine stores
    // this pointer and dereferences it on every resolveColor() from now on.
    gfx::setCustomPalette(kPalettes[index_].entries);

    refreshStatus();
}

void PaletteSwapScene::restoreEngineDefault() {
    showingEngineDefault_ = true;

    // setPalette() is the built-in counterpart of setCustomPalette(): it points
    // the same internal pointer at one of the engine's own tables instead.
    // index_ is left alone so the next cycle press resumes the walk.
    gfx::setPalette(gfx::PaletteType::PR32);

    refreshStatus();
}

void PaletteSwapScene::refreshStatus() {
    // snprintf into a fixed member, on swap frames only. draw() never formats.
    if (showingEngineDefault_) {
        snprintf(statusText_, sizeof(statusText_), "DEFAULT: PR32");
        return;
    }

    snprintf(statusText_, sizeof(statusText_), "%u/%u %s",
             static_cast<unsigned>(index_ + 1u),
             static_cast<unsigned>(kPaletteCount),
             kPalettes[index_].name);
}

void PaletteSwapScene::drawSwatchStrip(gfx::Renderer& renderer) const {
    // Sixteen slots, drawn by casting the loop counter straight to Color. That
    // cast is legal precisely because a Color enumerator IS a palette index.
    for (int slot = 0; slot < static_cast<int>(kPaletteEntries); ++slot) {
        const int x = slot * kSwatchWidth;
        renderer.drawFilledRectangle(x, kSwatchY, kSwatchWidth, kSwatchHeight,
                                     static_cast<Color>(slot));

        // The ruler digit sits under its swatch: 5 px glyph in an 8 px cell.
        renderer.drawText(std::string_view(&kSlotDigits[slot], 1), x + 1, kRulerY, Color::White, 1);
    }

    renderer.drawRectangle(0, kSwatchY, static_cast<int>(kPaletteEntries) * kSwatchWidth,
                           kSwatchHeight, Color::Gray);
}

void PaletteSwapScene::drawShapes(gfx::Renderer& renderer) const {
    const int top = kShapesY;
    const int bottom = kShapesY + kShapesHeight - 1;
    const int middle = kShapesY + kShapesHeight / 2;

    renderer.drawFilledCircle(16, middle, 10, Color::Red);

    renderer.drawFilledRectangle(34, top + 1, 24, kShapesHeight - 2, Color::Green);

    renderer.drawRectangle(64, top, 26, kShapesHeight, Color::Cyan);
    renderer.drawFilledRectangle(68, top + 4, 18, kShapesHeight - 8, Color::Yellow);

    renderer.drawLine(96, top + 1, 124, bottom, Color::Magenta);
    renderer.drawLine(96, bottom, 124, top + 1, Color::Purple);
    renderer.drawLine(96, middle, 124, middle, Color::LightGreen);
}

void PaletteSwapScene::drawNamedColors(gfx::Renderer& renderer) const {
    for (int row = 0; row < kNamedColorRows; ++row) {
        const int y = kNameRow0Y + row * kNameRowStep;
        renderer.drawText(kNamedColors[row][0].label, kNameColumn0X, y,
                          kNamedColors[row][0].color, 1);
        renderer.drawText(kNamedColors[row][1].label, kNameColumn1X, y,
                          kNamedColors[row][1].color, 1);
    }

    // Color::Silver is an alias of Color::Gray, so these two words always come
    // out of the same slot and always look identical. Change entry 15 and both
    // move together; there is no separate silver to tune.
    renderer.drawText("GRAY", kNameColumn0X, kAliasY, Color::Gray, 1);
    renderer.drawText("SILVER", 32, kAliasY, Color::Silver, 1);
    renderer.drawText("=ALIAS", 74, kAliasY, Color::White, 1);
}

} // namespace palette_swap
