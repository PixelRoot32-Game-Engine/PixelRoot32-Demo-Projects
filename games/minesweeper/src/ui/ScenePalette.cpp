#include "ui/ScenePalette.h"

#include <graphics/Color.h>

#include "assets/GraphicsPalette.h"

namespace minesweeper {

void applyScenePalette() {
    // setCustomPalette() assigns both the background and sprite palette
    // pointers without enabling dual-palette mode. Leaving the engine in
    // single-palette mode is deliberate: primitives (panel fills, the wrong
    // flag cross) and generated sprites must resolve through the same table,
    // otherwise a Color::Navy cross would land on a different ink than the
    // sprite pixels drawn beside it.
    pixelroot32::graphics::setCustomPalette(kMinesweeperPalette);
}

}  // namespace minesweeper
