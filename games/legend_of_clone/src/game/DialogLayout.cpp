#include "game/DialogLayout.h"

#include "graphics/Font5x7.h"

#include "GameConstants.h"
#include "game/DialogScripts.h"

namespace legend_of_clone {

namespace gfx = pixelroot32::graphics;

DialogBoxPlacement placeDialogBox(int16_t contentHeightPx) {
    if (contentHeightPx <= kStatusBarHeight) {
        return DialogBoxPlacement{static_cast<int16_t>(kStatusBarY),
                                  static_cast<int16_t>(kStatusBarHeight)};
    }
    return DialogBoxPlacement{static_cast<int16_t>(kDisplayHeight - contentHeightPx),
                              contentHeightPx};
}

gfx::DialogBoxStyle dialogBoxBaseStyle() {
    gfx::DialogBoxStyle style;
    // Named explicitly rather than left to FontManager's default, so the
    // measurement does not depend on whether the engine has set one yet.
    style.font = &gfx::FONT_5X7;
    style.w = 200;
    style.x = static_cast<int16_t>((kDisplayWidth - style.w) / 2);
    style.borderWidth = 1;
    style.padding = 1;
    style.textSize = 1;
    style.lineSpacing = 1;
    // The box is screen furniture; the camera must not move it.
    style.fixedPosition = true;
    return style;
}

int16_t tallestDialogHeightPx() {
    const gfx::DialogBoxStyle style = dialogBoxBaseStyle();
    const pixelroot32::gameplay::DialogScript* const scripts[] = {
        &kSignScript, &kOldManScript, &kChestScript, &kShopScript};

    int16_t tallest = 0;
    for (const auto* script : scripts) {
        const int16_t height = gfx::DialogBox::measureHeightPx(*script, style);
        if (height > tallest) tallest = height;
    }
    return tallest;
}

gfx::DialogBoxStyle dialogBoxStyle() {
    gfx::DialogBoxStyle style = dialogBoxBaseStyle();
    const DialogBoxPlacement placement = placeDialogBox(tallestDialogHeightPx());
    style.y = placement.y;
    style.h = placement.h;
    return style;
}

} // namespace legend_of_clone
