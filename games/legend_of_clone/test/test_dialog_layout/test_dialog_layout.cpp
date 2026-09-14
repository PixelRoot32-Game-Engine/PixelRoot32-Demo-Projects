/**
 * @brief Where the dialog box goes and how the slice's text fits in it.
 *
 * Measured with the engine's own DialogBox::measureHeightPx and
 * DialogBox::pageCountFor over the real scripts and the real 5x7 font, so a
 * wording change that stops the old man's speech from paging (R3), or that
 * pushes the shop past the 64 px status bar (R12), fails here first.
 */
#include <unity.h>

#include "GameConstants.h"
#include "assets/PlayerPalette.h"
#include "game/DialogLayout.h"
#include "game/DialogScripts.h"

#include <cstdio>

using namespace legend_of_clone;
namespace gfx = pixelroot32::graphics;

void setUp() {}
void tearDown() {}

namespace {

void reportHeight(const char* name, const pixelroot32::gameplay::DialogScript& script) {
    char message[64];
    std::snprintf(message, sizeof(message), "%s measureHeightPx = %d", name,
                  static_cast<int>(gfx::DialogBox::measureHeightPx(script, dialogBoxBaseStyle())));
    TEST_MESSAGE(message);
}

}  // namespace

// --- Paging (R3) ---------------------------------------------------------

void test_the_old_man_speech_needs_more_than_one_page() {
    const uint8_t pages =
        gfx::DialogBox::pageCountFor(kOldManScript.lines[kOldManIntro], dialogBoxBaseStyle());
    TEST_ASSERT_TRUE(pages >= 2);
}

void test_no_other_line_pages() {
    const gfx::DialogBoxStyle style = dialogBoxBaseStyle();
    const pixelroot32::gameplay::DialogScript* const scripts[] = {
        &kSignScript, &kOldManScript, &kChestScript, &kShopScript};
    for (const auto* script : scripts) {
        for (uint16_t i = 0; i < script->lineCount; ++i) {
            if (script == &kOldManScript && i == kOldManIntro) continue;
            TEST_ASSERT_EQUAL_UINT8(1, gfx::DialogBox::pageCountFor(script->lines[i], style));
        }
    }
}

// --- Placement (R12) -----------------------------------------------------

void test_a_box_that_fits_takes_the_whole_status_bar() {
    DialogBoxPlacement placement = placeDialogBox(kStatusBarHeight);
    TEST_ASSERT_EQUAL_INT16(kStatusBarY, placement.y);
    TEST_ASSERT_EQUAL_INT16(kStatusBarHeight, placement.h);

    placement = placeDialogBox(22);
    TEST_ASSERT_EQUAL_INT16(kStatusBarY, placement.y);
    TEST_ASSERT_EQUAL_INT16(kStatusBarHeight, placement.h);
}

void test_a_box_too_tall_for_the_bar_overlays_the_playfield() {
    const DialogBoxPlacement placement = placeDialogBox(kStatusBarHeight + 1);
    TEST_ASSERT_EQUAL_INT16(kDisplayHeight - (kStatusBarHeight + 1), placement.y);
    TEST_ASSERT_EQUAL_INT16(kStatusBarHeight + 1, placement.h);
    TEST_ASSERT_TRUE(placement.y < kStatusBarY);
}

void test_every_script_fits_the_status_bar() {
    reportHeight("sign", kSignScript);
    reportHeight("old man", kOldManScript);
    reportHeight("chest", kChestScript);
    reportHeight("shop", kShopScript);
    TEST_ASSERT_TRUE(tallestDialogHeightPx() > 0);
    TEST_ASSERT_TRUE(tallestDialogHeightPx() <= kStatusBarHeight);
}

void test_the_tallest_script_is_the_shop_menu() {
    TEST_ASSERT_EQUAL_INT16(gfx::DialogBox::measureHeightPx(kShopScript, dialogBoxBaseStyle()),
                            tallestDialogHeightPx());
}

void test_the_game_style_is_placed_from_the_tallest_script() {
    const gfx::DialogBoxStyle style = dialogBoxStyle();
    const DialogBoxPlacement placement = placeDialogBox(tallestDialogHeightPx());
    TEST_ASSERT_EQUAL_INT16(placement.y, style.y);
    TEST_ASSERT_EQUAL_INT16(placement.h, style.h);
    TEST_ASSERT_TRUE(style.fixedPosition);
}

void test_the_box_stays_on_screen() {
    const gfx::DialogBoxStyle style = dialogBoxStyle();
    TEST_ASSERT_TRUE(style.x >= 0);
    TEST_ASSERT_TRUE(style.x + style.w <= kDisplayWidth);
    TEST_ASSERT_TRUE(style.y >= 0);
    TEST_ASSERT_TRUE(style.y + style.h <= kDisplayHeight);
}

// --- Colors ---------------------------------------------------------------

// Dialog text is drawn through the sprite palette, where a slot no art uses
// holds 0x0000 unless the exporter reserves it. A color that resolves to black
// on the black panel draws nothing at all - the shop's selected choice did.
void test_every_dialog_text_color_is_visible_in_the_sprite_palette() {
    const gfx::DialogBoxStyle style = dialogBoxStyle();
    const struct {
        const char* name;
        gfx::Color color;
    } inks[] = {
        {"border", style.border},
        {"ink", style.ink},
        {"inkDim", style.inkDim},
        {"inkSelected", style.inkSelected},
    };
    for (const auto& ink : inks) {
        const uint16_t rgb = PLAYER_SPRITE_PALETTE_RGB565[static_cast<uint8_t>(ink.color)];
        TEST_ASSERT_NOT_EQUAL_UINT16_MESSAGE(0x0000, rgb, ink.name);
    }
    TEST_ASSERT_NOT_EQUAL_UINT16(
        PLAYER_SPRITE_PALETTE_RGB565[static_cast<uint8_t>(style.ink)],
        PLAYER_SPRITE_PALETTE_RGB565[static_cast<uint8_t>(style.inkSelected)]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_the_old_man_speech_needs_more_than_one_page);
    RUN_TEST(test_no_other_line_pages);
    RUN_TEST(test_a_box_that_fits_takes_the_whole_status_bar);
    RUN_TEST(test_a_box_too_tall_for_the_bar_overlays_the_playfield);
    RUN_TEST(test_every_script_fits_the_status_bar);
    RUN_TEST(test_the_tallest_script_is_the_shop_menu);
    RUN_TEST(test_the_game_style_is_placed_from_the_tallest_script);
    RUN_TEST(test_the_box_stays_on_screen);
    RUN_TEST(test_every_dialog_text_color_is_visible_in_the_sprite_palette);
    return UNITY_END();
}
