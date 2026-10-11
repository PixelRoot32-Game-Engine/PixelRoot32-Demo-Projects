#pragma once

#include <cstdint>

#include "core/Scene.h"
#include "platforms/EngineConfig.h"
#include "TetrisConstants.h"
#include "game/TetrisAi.h"
#include "game/TetrisSession.h"
#include "input/InputController.h"

namespace tetris {

/** Presentation states; the playthrough phase itself lives in TetrisSession. */
enum class SceneState : std::uint8_t {
    Title,
    Playing,
    Paused,
    GameOver,
    Demo
};

/**
 * @class TetrisScene
 * @brief PixelRoot32 integration: title, HUD, well drawing and sound cues.
 *
 * The scene owns no rules: gravity, clears and scoring sit in
 * TetrisSession, input shaping in InputController. Drawing never mutates
 * the session. The board is drawn with Renderer primitives only.
 */
class TetrisScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    TetrisSession session_;
    TetrisSession demoSession_;
    TetrisAi ai_;
    InputController input_;
    SceneState state_;
    int cellSize_;
    int boardX_;
    int boardY_;
    int panelX_;
    unsigned long gameOverMs_;
    unsigned long lastUpdateMs_;
    unsigned long titleEnteredMs_;
    unsigned long lastAiMs_;
    std::uint32_t seedCounter_;
    char line1_[32];
    char line2_[40];
    char line3_[40];

    void fitBoard();
    void startGame(unsigned long nowMs);
    void startDemo(unsigned long nowMs);
    void backToTitle(unsigned long nowMs);
    void pauseGame();
    void resumeGame(unsigned long nowMs);
    void handleTitle(std::uint16_t commands, unsigned long nowMs);
    void handlePlaying(std::uint16_t commands, std::uint16_t held, unsigned long nowMs);
    void handlePaused(std::uint16_t commands, unsigned long nowMs);
    void handleGameOver(std::uint16_t commands, unsigned long nowMs);
    void handleDemo(std::uint16_t commands, unsigned long nowMs);
    void stepDemoAi(unsigned long nowMs);
    [[nodiscard]] std::uint32_t nextSeed(unsigned long nowMs);
    void playTone(float frequency, float duration, float volume);
    void onLinesCleared(std::uint8_t count, bool leveledUp);

    void drawTitle(pixelroot32::graphics::Renderer& renderer);
    void drawHud(pixelroot32::graphics::Renderer& renderer);
    void drawBoard(pixelroot32::graphics::Renderer& renderer);
    void drawSidePanel(pixelroot32::graphics::Renderer& renderer);
    void drawPaused(pixelroot32::graphics::Renderer& renderer);
    void drawGameOver(pixelroot32::graphics::Renderer& renderer);
    void drawPiece(pixelroot32::graphics::Renderer& renderer, TetrominoType type,
                   std::uint8_t rotation, int boxX, int boxY,
                   pixelroot32::graphics::Color color, bool ghost);
};

}  // namespace tetris
