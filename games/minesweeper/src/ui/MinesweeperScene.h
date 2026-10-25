#pragma once

#include <cstdint>

#include "core/Scene.h"
#include "platforms/EngineConfig.h"
#include "GameConstants.h"
#include "game/GameSession.h"
#include "game/MinefieldConfig.h"
#include "input/InputController.h"

#if PIXELROOT32_ENABLE_UI_SYSTEM
#include <graphics/ui/UIButton.h>
#include <graphics/ui/UILabel.h>
#include <graphics/ui/UIVerticalLayout.h>
#endif

namespace minesweeper {

/** Presentation states; the rules phase itself lives in GameSession. */
enum class SceneState : std::uint8_t {
    Menu,
    Playing,
    GameOver
};

/**
 * @class MinesweeperScene
 * @brief PixelRoot32 integration: menu, HUD, board drawing and sound cues.
 *
 * The scene owns no rules: cursor navigation, difficulty selection and
 * rendering sit here, while Minefield/GameSession decide mines, floods
 * and victory. Drawing never mutates the session.
 */
class MinesweeperScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    GameSession session_;
    InputController input_;
    SceneState state_;
    Difficulty difficulty_;
    Position cursor_;
    std::uint8_t cellSize_;
    int boardX_;
    int boardY_;
    unsigned long gameOverMs_;
    std::uint32_t seedCounter_;
    std::uint16_t bestSeconds_[3];
    char hudLine2_[48];

    /**
     * @brief Difficulty row labels, owned because `UIButton` does not own them.
     *
     * `UIButton` stores a non-owning `std::string_view` and ships no label
     * setter, so the text lives here and every rewrite must keep the same
     * character count. presentation::menuRowWidth() is that contract: the
     * best-time slot is always three characters ("042" or "---"), which the
     * initial strings below already respect.
     */
    char menuRowText_[3][40] = {
        "EASY 9X9-10 BEST ---",
        "MEDIUM 16X16-40 BEST ---",
        "EXPERT 30X16-99 BEST ---",
    };

#if PIXELROOT32_ENABLE_UI_SYSTEM
    /**
     * @brief The three difficulty rows, positioned by the engine layout.
     *
     * The layout owns their geometry and the button style owns the ink
     * reversal; the scene keeps the pad through InputController and only
     * pushes the selection onto the layout.
     */
    pixelroot32::graphics::ui::UIVerticalLayout menuLayout_{
        pixelroot32::math::Vector2(MENU_CONTENT_X, MENU_ROW_FIRST_Y), MENU_CONTENT_W,
        MENU_ROW_AREA_H};
    pixelroot32::graphics::ui::UIButton menuRows_[3] = {
        {menuRowText_[0], MENU_ACTIVATE_BUTTON, {0, 0}, {MENU_CONTENT_W, MENU_ROW_H}, nullptr,
         pixelroot32::graphics::ui::TextAlignment::CENTER, 1},
        {menuRowText_[1], MENU_ACTIVATE_BUTTON, {0, 0}, {MENU_CONTENT_W, MENU_ROW_H}, nullptr,
         pixelroot32::graphics::ui::TextAlignment::CENTER, 1},
        {menuRowText_[2], MENU_ACTIVATE_BUTTON, {0, 0}, {MENU_CONTENT_W, MENU_ROW_H}, nullptr,
         pixelroot32::graphics::ui::TextAlignment::CENTER, 1},
    };

    /** Hint lines under the rows; owned strings, so literals are safe. */
    pixelroot32::graphics::ui::UILabel menuHint1_{"UP/DN:LEVEL A:START",
                                                 {0, MENU_HINT_FIRST_Y},
                                                 pixelroot32::graphics::Color::Navy, 1};
    pixelroot32::graphics::ui::UILabel menuHint2_{"OPEN:A FLAG:B MENU:A+B",
                                                 {0, MENU_HINT_FIRST_Y + MENU_HINT_STEP},
                                                 pixelroot32::graphics::Color::Navy, 1};
#endif

    void startGame();
    void backToMenu();
    void cycleDifficulty(int direction);
    void handleMenu(std::uint16_t commands);
    void handlePlaying(std::uint16_t commands, unsigned long nowMs);
    void handleGameOver(std::uint16_t commands, unsigned long nowMs);
    void moveCursor(int dx, int dy);
    void recordBest(std::uint32_t seconds);
    [[nodiscard]] std::uint32_t nextSeed(unsigned long nowMs);
    void playTone(float frequency, float duration, float volume);

    /**
     * @brief Rewrites the three row labels, preserving their character count.
     *
     * Called when a best time changes; the engine's button keeps a view into
     * `menuRowText_`, so a length change would leave it pointing at a stale
     * tail. presentation::formatMenuRow() writes the fixed three-character
     * slot that makes a rewrite safe.
     */
    void refreshMenuLabels();

    void drawMenu(pixelroot32::graphics::Renderer& renderer);
    void drawHud(pixelroot32::graphics::Renderer& renderer, unsigned long nowMs);
    void drawBoard(pixelroot32::graphics::Renderer& renderer);
    void drawResult(pixelroot32::graphics::Renderer& renderer, unsigned long nowMs);

    /**
     * @brief Draws the 9-slice frame plus its mid-ink interior.
     *
     * The frame is cosmetic, so the helper never writes outside the requested
     * rect: pieces that would not fit an edge span are skipped, which keeps a
     * too-small panel from bleeding over whatever it covers.
     *
     * @param renderer Target renderer.
     * @param x Left edge of the panel.
     * @param y Top edge of the panel.
     * @param w Panel width in pixels.
     * @param h Panel height in pixels.
     */
    void drawPanel(pixelroot32::graphics::Renderer& renderer, int x, int y, int w, int h);

    /**
     * @brief Blits an uppercase letter run with the 8x8 sheet glyphs.
     *
     * Advances one 8 px cell per character whether or not a glyph exists, so a
     * missing letter leaves a gap instead of shifting the rest of the run.
     *
     * @param renderer Target renderer.
     * @param text Characters to draw; only 'M','I','N','E','S','T' render.
     * @param x Left edge of the first cell.
     * @param y Top edge of the glyph row.
     */
    void drawLetterRun(pixelroot32::graphics::Renderer& renderer, const char* text, int x, int y);

    /**
     * @brief Blits a digit run with the 8x8 sheet glyphs.
     *
     * Same advance rule as drawLetterRun(): fixed cells keep the counter
     * centred on its label no matter which digits appear.
     *
     * @param renderer Target renderer.
     * @param text Characters to draw; only '0'..'9' render.
     * @param x Left edge of the first cell.
     * @param y Top edge of the glyph row.
     */
    void drawDigitRun(pixelroot32::graphics::Renderer& renderer, const char* text, int x, int y);
};

}  // namespace minesweeper
