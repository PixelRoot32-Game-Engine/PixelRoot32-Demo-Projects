#include "DoomScene.h"

#include <graphics/Renderer.h>

namespace doom {

void DoomScene::init() {}

void DoomScene::update(unsigned long deltaTime) {
    (void)deltaTime;
    ++frames_;
}

void DoomScene::draw(pixelroot32::graphics::Renderer& renderer) {
    using pixelroot32::graphics::Color;

    renderer.drawFilledRectangle(0, 0, kLogicalWidth, kLogicalHeight, Color::Navy);

    // The 1 px frame is inset by nothing: it is drawn on the outermost logical
    // pixels, so a present path that crops instead of scaling loses a side of
    // it immediately. On the 240-wide panel it also proves the downscale is a
    // skip (0.75x), not a nearest-neighbour upscale of a cropped surface.
    renderer.drawRectangle(0, 0, kLogicalWidth, kLogicalHeight, Color::White);

    // Orientation markers, in the order the rasteriser fills them. Red is the
    // first thing DOOM writes (column 0, row 0); if the panel shows red
    // anywhere else, the rotation or the LUT direction is wrong.
    renderer.drawFilledRectangle(0, 0, 16, 16, Color::Red);
    renderer.drawFilledRectangle(kLogicalWidth - 16, 0, 16, 16, Color::Green);
    renderer.drawFilledRectangle(0, kLogicalHeight - 16, 16, 16, Color::Blue);
    renderer.drawFilledRectangle(kLogicalWidth - 16, kLogicalHeight - 16, 16, 16, Color::Yellow);
}

}  // namespace doom
