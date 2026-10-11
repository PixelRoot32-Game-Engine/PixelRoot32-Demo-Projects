#pragma once

#include <cstdint>

#include "core/Scene.h"
#include "platforms/EngineConfig.h"
#include "GameConstants.h"
#include "game/GameSession.h"
#include "game/MinefieldConfig.h"
#include "input/InputController.h"

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
    char hudLine1_[32];
    char hudLine2_[40];
    char hudHint_[48];

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

    void drawMenu(pixelroot32::graphics::Renderer& renderer);
    void drawHud(pixelroot32::graphics::Renderer& renderer, unsigned long nowMs);
    void drawBoard(pixelroot32::graphics::Renderer& renderer);
    void drawResult(pixelroot32::graphics::Renderer& renderer, unsigned long nowMs);
    void drawCellText(pixelroot32::graphics::Renderer& renderer, char glyph,
                      int cellX, int cellY, pixelroot32::graphics::Color color);
};

}  // namespace minesweeper
