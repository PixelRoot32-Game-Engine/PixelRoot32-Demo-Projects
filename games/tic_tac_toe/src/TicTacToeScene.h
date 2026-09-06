#pragma once
#include "core/Scene.h"
#include "platforms/EngineConfig.h"
#include "GameConstants.h"

namespace tictactoe {

/** Cell ownership */
enum class Player {
    None,
    X,
    O
};

enum class GameState {
    Playing,
    WinX,
    WinO,
    Draw
};

/**
 * @class TicTacToeScene
 * @brief Tic-tac-toe with custom palette and AI opponent.
 *
 * Uses cursor-based input and minimax-style AI with configurable error chance.
 */
class TicTacToeScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    Player board[BOARD_SIZE][BOARD_SIZE];
    Player currentPlayer;
    Player humanPlayer;
    Player aiPlayer;
    float aiErrorChance;
    bool inputReady;
    GameState gameState;
    int cursorIndex;  ///< 0-8, maps to board[row][col]
    bool gameOver;
    unsigned long gameEndTime;

    pixelroot32::math::Vector2 boardPosition;
    char statusText[32];
    char instructionsText[64];
    bool instructionsVisible;

    void resetGame();
    void handleInput();
    void performAIMove();
    bool computeAIMove(int& outRow, int& outCol);
    bool wouldWin(Player player) const;
    void checkWinCondition();
    bool isBoardFull();
    void nextTurn();

    void drawGrid(pixelroot32::graphics::Renderer& renderer);
    void drawMarks(pixelroot32::graphics::Renderer& renderer);
    void drawCursor(pixelroot32::graphics::Renderer& renderer);
    void drawX(pixelroot32::graphics::Renderer& renderer, int x, int y);
    void drawO(pixelroot32::graphics::Renderer& renderer, int x, int y);
};

}
