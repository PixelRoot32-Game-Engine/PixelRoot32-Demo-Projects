#include "ui/TetrisScene.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include "audio/TetrisMusic.h"
#include "game/Tetromino.h"
#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace tetris {

using Color = pr32::graphics::Color;

namespace {

/** @brief Fill color per tetromino (classic Guideline palette, mapped to engine colors). */
Color colorForType(TetrominoType type) {
    switch (type) {
        case TetrominoType::I:
            return Color::Cyan;
        case TetrominoType::O:
            return Color::Yellow;
        case TetrominoType::T:
            return Color::Purple;
        case TetrominoType::S:
            return Color::Green;
        case TetrominoType::Z:
            return Color::Red;
        case TetrominoType::J:
            return Color::Blue;
        case TetrominoType::L:
            return Color::Orange;
        default:
            return Color::White;
    }
}

/** @brief Fill color per locked cell value. */
Color colorForCell(Cell cell) {
    switch (cell) {
        case Cell::I:
            return Color::Cyan;
        case Cell::O:
            return Color::Yellow;
        case Cell::T:
            return Color::Purple;
        case Cell::S:
            return Color::Green;
        case Cell::Z:
            return Color::Red;
        case Cell::J:
            return Color::Blue;
        case Cell::L:
            return Color::Orange;
        default:
            return Color::Black;
    }
}

}  // namespace

void TetrisScene::init() {
    Scene::init();
    state_ = SceneState::Title;
    cellSize_ = 8;
    boardX_ = 0;
    boardY_ = 0;
    panelX_ = 0;
    gameOverMs_ = 0;
    lastUpdateMs_ = 0;
    titleEnteredMs_ = millis();
    lastAiMs_ = 0;
    seedCounter_ = 0x8BADF00DU;
    line1_[0] = '\0';
    line2_[0] = '\0';
    line3_[0] = '\0';
    session_.start(1);
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    // Fondo discreto: tempo lento y master bajo para no tapar los SFX.
    engine.getMusicPlayer().setBPM(music::BGM_BPM);
    engine.getMusicPlayer().setMasterVolume(music::BGM_MASTER_VOLUME);
    engine.getMusicPlayer().setTempoFactor(1.0f);
#endif
}

void TetrisScene::fitBoard() {
    // Fit the well plus a side panel for next/score on any logical size.
    const int panelNeed = 84;
    int availForBoard = LOGICAL_WIDTH - panelNeed - 8;
    if (availForBoard < 40) {
        availForBoard = LOGICAL_WIDTH - 8;
    }
    cellSize_ = cellSizeFor(BOARD_WIDTH, BOARD_HEIGHT, availForBoard, BOARD_AREA_HEIGHT);
    if (cellSize_ > 10) {
        cellSize_ = 10;
    }
    const int boardW = cellSize_ * BOARD_WIDTH;
    const int boardH = cellSize_ * BOARD_HEIGHT;
    const int totalW = boardW + 8 + 72;
    if (totalW <= LOGICAL_WIDTH - 4) {
        boardX_ = (LOGICAL_WIDTH - totalW) / 2;
    } else {
        boardX_ = (LOGICAL_WIDTH - boardW) / 2;
    }
    boardY_ = BOARD_TOP + (BOARD_AREA_HEIGHT - boardH) / 2;
    panelX_ = boardX_ + boardW + 8;
}

void TetrisScene::startGame(unsigned long nowMs) {
    session_.start(nextSeed(nowMs));
    fitBoard();
    state_ = SceneState::Playing;
    lastUpdateMs_ = nowMs;
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    engine.getMusicPlayer().setBPM(music::BGM_BPM);
    engine.getMusicPlayer().setTempoFactor(1.0f);
    engine.getMusicPlayer().play(music::BGM_TRACK);
#endif
    playTone(700.0f, 0.06f, 0.5f);
}

void TetrisScene::backToTitle(unsigned long nowMs) {
    state_ = SceneState::Title;
    titleEnteredMs_ = nowMs;
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    engine.getMusicPlayer().stop();
#endif
    playTone(330.0f, 0.08f, 0.5f);
}

void TetrisScene::startDemo(unsigned long nowMs) {
    // Attract mode: the AI plays on its own session so the player's
    // best score and run are never polluted by the machine.
    fitBoard();
    demoSession_.start(nextSeed(nowMs));
    state_ = SceneState::Demo;
    lastUpdateMs_ = nowMs;
    lastAiMs_ = nowMs;
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    engine.getMusicPlayer().setBPM(music::BGM_BPM);
    engine.getMusicPlayer().setTempoFactor(1.0f);
    engine.getMusicPlayer().play(music::BGM_TRACK);
#endif
    playTone(700.0f, 0.06f, 0.5f);
}

void TetrisScene::pauseGame() {
    if (state_ != SceneState::Playing) {
        return;
    }
    state_ = SceneState::Paused;
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    engine.getMusicPlayer().pause();
#endif
    playTone(440.0f, 0.07f, 0.4f);
}

void TetrisScene::resumeGame(unsigned long nowMs) {
    if (state_ != SceneState::Paused) {
        return;
    }
    state_ = SceneState::Playing;
    // Re-anchor the clock so the paused time never becomes a fall step.
    lastUpdateMs_ = nowMs;
    input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
    engine.getMusicPlayer().resume();
#endif
    playTone(660.0f, 0.07f, 0.4f);
}

void TetrisScene::update(unsigned long deltaTime) {
    const unsigned long nowMs = millis();
    std::uint16_t held = 0;
    for (std::uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        if (engine.getInputManager().isButtonDown(i)) {
            held |= static_cast<std::uint16_t>(1U << i);
        }
    }
    const std::uint16_t commands = input_.poll(held, nowMs);

    switch (state_) {
        case SceneState::Title:
            handleTitle(commands, nowMs);
            break;
        case SceneState::Playing:
            handlePlaying(commands, held, nowMs);
            break;
        case SceneState::Paused:
            handlePaused(commands, nowMs);
            break;
        case SceneState::GameOver:
            handleGameOver(commands, nowMs);
            break;
        case SceneState::Demo:
            handleDemo(commands, nowMs);
            break;
    }
    Scene::update(deltaTime);
}

void TetrisScene::handleTitle(std::uint16_t commands, unsigned long nowMs) {
    if (hasCommand(commands, GameCommand::Confirm) ||
        hasCommand(commands, GameCommand::Pause)) {
        startGame(nowMs);
        return;
    }
    if (commands != 0) {
        // Any button activity proves a player is present: restart the idle clock.
        titleEnteredMs_ = nowMs;
        return;
    }
    if (nowMs - titleEnteredMs_ >= TITLE_ATTRACT_DELAY_MS) {
        startDemo(nowMs);
    }
}

void TetrisScene::handlePlaying(std::uint16_t commands, std::uint16_t held,
                                unsigned long nowMs) {
    if (hasCommand(commands, GameCommand::Pause)) {
        pauseGame();
        return;
    }
    if (hasCommand(commands, GameCommand::Left)) {
        session_.tryMove(-1);
    }
    if (hasCommand(commands, GameCommand::Right)) {
        session_.tryMove(1);
    }
    if (hasCommand(commands, GameCommand::RotateCw)) {
        if (session_.tryRotateCw()) {
            playTone(520.0f, 0.04f, 0.35f);
        }
    } else if (hasCommand(commands, GameCommand::RotateCcw)) {
        if (session_.tryRotateCcw()) {
            playTone(460.0f, 0.04f, 0.35f);
        }
    }
    if (hasCommand(commands, GameCommand::HardDrop)) {
        const std::uint16_t linesBefore = session_.lines();
        const std::uint8_t levelBefore = session_.level();
        const std::uint8_t cleared = session_.hardDrop();
        playTone(180.0f, 0.08f, 0.5f);
        if (cleared > 0) {
            onLinesCleared(cleared, session_.level() != levelBefore);
        }
        (void)linesBefore;
        lastUpdateMs_ = nowMs;
        if (session_.status() == GameStatus::GameOver) {
            state_ = SceneState::GameOver;
            gameOverMs_ = nowMs;
            input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
            engine.getMusicPlayer().stop();
#endif
            playTone(140.0f, 0.4f, 0.6f);
        }
        return;
    }

    // Gravity: delta from the monotonic clock, soft drop while Down is held.
    unsigned long delta = (nowMs >= lastUpdateMs_) ? (nowMs - lastUpdateMs_) : 0;
    lastUpdateMs_ = nowMs;
    const std::uint16_t linesBefore = session_.lines();
    const std::uint8_t levelBefore = session_.level();
    const StepResult step =
        session_.update(static_cast<std::uint32_t>(delta), softDropHeld(held));
    if (step.locked) {
        const unsigned gained = session_.lines() - linesBefore;
        if (gained > 0) {
            onLinesCleared(static_cast<std::uint8_t>(gained),
                           session_.level() != levelBefore);
        } else {
            playTone(240.0f, 0.05f, 0.4f);
        }
    }
    if (session_.status() == GameStatus::GameOver) {
        state_ = SceneState::GameOver;
        gameOverMs_ = nowMs;
        input_.reset();
#if PIXELROOT32_ENABLE_AUDIO
        engine.getMusicPlayer().stop();
#endif
        playTone(140.0f, 0.4f, 0.6f);
    }
}

void TetrisScene::handlePaused(std::uint16_t commands, unsigned long nowMs) {
    if (hasCommand(commands, GameCommand::Pause) ||
        hasCommand(commands, GameCommand::Confirm)) {
        resumeGame(nowMs);
    }
}

void TetrisScene::handleGameOver(std::uint16_t commands, unsigned long nowMs) {
    if (nowMs - gameOverMs_ < GAME_OVER_LOCK_MS) {
        return;
    }
    if (hasCommand(commands, GameCommand::Confirm)) {
        startGame(nowMs);
    } else if (hasCommand(commands, GameCommand::Pause)) {
        backToTitle(nowMs);
    }
}

void TetrisScene::handleDemo(std::uint16_t commands, unsigned long nowMs) {
    // Any A press (Confirm) hands the machine back: title screen first,
    // so the player always starts their own run explicitly.
    if (hasCommand(commands, GameCommand::Confirm) ||
        hasCommand(commands, GameCommand::Pause)) {
        backToTitle(nowMs);
        return;
    }
    if (nowMs - lastAiMs_ >= AI_STEP_MS) {
        lastAiMs_ = nowMs;
        stepDemoAi(nowMs);
        if (state_ != SceneState::Demo) {
            return;
        }
    }

    // Gravity runs exactly like a player run, without soft drop.
    unsigned long delta = (nowMs >= lastUpdateMs_) ? (nowMs - lastUpdateMs_) : 0;
    lastUpdateMs_ = nowMs;
    const std::uint16_t linesBefore = demoSession_.lines();
    const std::uint8_t levelBefore = demoSession_.level();
    const StepResult step = demoSession_.update(static_cast<std::uint32_t>(delta), false);
    if (step.locked) {
        const unsigned gained = demoSession_.lines() - linesBefore;
        if (gained > 0) {
            onLinesCleared(static_cast<std::uint8_t>(gained),
                           demoSession_.level() != levelBefore);
        } else {
            playTone(240.0f, 0.05f, 0.4f);
        }
    }
    if (demoSession_.status() == GameStatus::GameOver) {
        // Attract mode never shows game over: restart silently and keep looping.
        demoSession_.start(nextSeed(nowMs));
        lastUpdateMs_ = nowMs;
        lastAiMs_ = nowMs;
        input_.reset();
    }
}

void TetrisScene::stepDemoAi(unsigned long nowMs) {
    if (demoSession_.status() != GameStatus::Playing) {
        return;
    }
    // Fresh plan every tick from the live board: kick nudges and blocked
    // paths self-correct instead of accumulating steering error.
    const ActivePiece& active = demoSession_.active();
    const AiPlacement target =
        ai_.findBestPlacement(demoSession_.board(), active.type);
    if (active.rotation != target.rotation) {
        demoSession_.tryRotateCw();
        return;
    }
    if (active.x < target.x) {
        demoSession_.tryMove(1);
        return;
    }
    if (active.x > target.x) {
        demoSession_.tryMove(-1);
        return;
    }
    // Aligned with the plan: lock it now so the demo keeps a lively pace.
    const std::uint8_t levelBefore = demoSession_.level();
    const std::uint8_t cleared = demoSession_.hardDrop();
    playTone(180.0f, 0.08f, 0.5f);
    if (cleared > 0) {
        onLinesCleared(cleared, demoSession_.level() != levelBefore);
    }
    lastUpdateMs_ = nowMs;
    if (demoSession_.status() == GameStatus::GameOver) {
        demoSession_.start(nextSeed(nowMs));
        lastUpdateMs_ = nowMs;
        lastAiMs_ = nowMs;
        input_.reset();
    }
}

std::uint32_t TetrisScene::nextSeed(unsigned long nowMs) {
    seedCounter_ += 0x9E3779B9U;
    std::uint32_t seed =
        static_cast<std::uint32_t>(nowMs) * 1664525U + 1013904223U + seedCounter_;
    return (seed != 0) ? seed : 1U;
}

void TetrisScene::playTone(float frequency, float duration, float volume) {
    pr32::audio::AudioEvent tone{};
    tone.type = pr32::audio::WaveType::PULSE;
    tone.frequency = frequency;
    tone.duration = duration;
    tone.volume = volume;
    tone.duty = 0.5f;
    engine.getAudioEngine().playEvent(tone);
}

void TetrisScene::onLinesCleared(std::uint8_t count, bool leveledUp) {
    if (count >= 4) {
        playTone(880.0f, 0.18f, 0.6f);
    } else if (count == 3) {
        playTone(760.0f, 0.14f, 0.55f);
    } else if (count == 2) {
        playTone(660.0f, 0.12f, 0.55f);
    } else {
        playTone(580.0f, 0.1f, 0.5f);
    }
    if (leveledUp) {
        playTone(990.0f, 0.12f, 0.5f);
    }
}

void TetrisScene::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, Color::Black);
    if (state_ == SceneState::Title) {
        drawTitle(renderer);
    } else {
        drawHud(renderer);
        drawBoard(renderer);
        drawSidePanel(renderer);
        if (state_ == SceneState::Paused) {
            drawPaused(renderer);
        } else if (state_ == SceneState::GameOver) {
            drawGameOver(renderer);
        }
    }
    Scene::draw(renderer);
}

void TetrisScene::drawTitle(pr32::graphics::Renderer& renderer) {
    renderer.drawTextCentered("TETRIS", 40, Color::Cyan, 3);
    renderer.drawTextCentered("PIXELROOT32 DEMO", 70, Color::Gray, 1);
    renderer.drawTextCentered("10X20 7-BAG NEXT LVL", 100, Color::White, 1);
    renderer.drawTextCentered("LF/RT:MOVE DN:SOFT", 130, Color::Gray, 1);
    renderer.drawTextCentered("UP:DROP A/B:ROTATE", 144, Color::Gray, 1);
    renderer.drawTextCentered("A+B:PAUSE", 158, Color::Gray, 1);
    renderer.drawTextCentered("IDLE 15S:AUTO DEMO", 172, Color::Gray, 1);
    if ((millis() / 500) % 2 == 0) {
        renderer.drawTextCentered("PRESS A TO START", 190, Color::Yellow, 1);
    }
}

void TetrisScene::drawHud(pr32::graphics::Renderer& renderer) {
    const TetrisSession& shown = (state_ == SceneState::Demo) ? demoSession_ : session_;
    std::uint32_t score = shown.score();
    if (score > 999999) {
        score = 999999;
    }
    std::snprintf(line1_, sizeof(line1_), "SC %06d BEST %06d",
                  static_cast<int>(score),
                  static_cast<int>(shown.bestScore() > 999999 ? 999999
                                                                 : shown.bestScore()));
    renderer.drawText(line1_, 2, HUD_LINE_1_Y, Color::White, 1);

    std::snprintf(line2_, sizeof(line2_), "LV %02d LINES %03d", shown.level(),
                  shown.lines());
    renderer.drawText(line2_, 2, HUD_LINE_2_Y, Color::Yellow, 1);

    if (state_ == SceneState::Playing) {
        std::snprintf(line3_, sizeof(line3_), "A+B:PAUSE UP:DROP");
    } else if (state_ == SceneState::Paused) {
        std::snprintf(line3_, sizeof(line3_), "PAUSED");
    } else if (state_ == SceneState::Demo) {
        std::snprintf(line3_, sizeof(line3_), "DEMO");
    } else {
        std::snprintf(line3_, sizeof(line3_), "A:RETRY B:MENU");
    }
    renderer.drawText(line3_, 2, HUD_LINE_3_Y, Color::Gray, 1);
    renderer.drawLine(0, HUD_DIVIDER_Y, DISPLAY_WIDTH - 1, HUD_DIVIDER_Y, Color::Gray);
}

void TetrisScene::drawBoard(pr32::graphics::Renderer& renderer) {
    const TetrisSession& shown = (state_ == SceneState::Demo) ? demoSession_ : session_;
    const Board& board = shown.board();
    const int boardW = cellSize_ * BOARD_WIDTH;
    const int boardH = cellSize_ * BOARD_HEIGHT;
    // Well background plus frame.
    renderer.drawFilledRectangle(boardX_, boardY_, boardW, boardH, Color::Navy);
    renderer.drawRectangle(boardX_ - 1, boardY_ - 1, boardW + 2, boardH + 2, Color::Gray);

    // Ghost piece: outline only, never mutates the session.
    if (state_ == SceneState::Playing || state_ == SceneState::Demo) {
        const ActivePiece& active = shown.active();
        const int ghostBoxY = boardY_ + shown.ghostY() * cellSize_;
        const int ghostBoxX = boardX_ + active.x * cellSize_;
        for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
            const BlockOffset block =
                Tetromino::blockAt(active.type, active.rotation, i);
            const int gx = ghostBoxX + block.x * cellSize_;
            const int gy = ghostBoxY + block.y * cellSize_;
            if (gy < boardY_) {
                continue;
            }
            renderer.drawRectangle(gx, gy, cellSize_, cellSize_, Color::Gray);
        }
    }

    // Locked cells.
    for (int y = 0; y < BOARD_HEIGHT; ++y) {
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const Cell cell = board.get(x, y);
            if (cell == Cell::Empty) {
                continue;
            }
            const int px = boardX_ + x * cellSize_;
            const int py = boardY_ + y * cellSize_;
            renderer.drawFilledRectangle(px, py, cellSize_, cellSize_,
                                         colorForCell(cell));
        }
    }

    // Active piece with a bright top lip for the retro bevel look.
    if (state_ == SceneState::Playing || state_ == SceneState::Paused ||
        state_ == SceneState::Demo) {
        const ActivePiece& active = shown.active();
        const Color color = colorForType(active.type);
        for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
            const BlockOffset block =
                Tetromino::blockAt(active.type, active.rotation, i);
            const int px = boardX_ + (active.x + block.x) * cellSize_;
            const int py = boardY_ + (active.y + block.y) * cellSize_;
            if (py + cellSize_ <= boardY_) {
                continue;
            }
            renderer.drawFilledRectangle(px, py, cellSize_, cellSize_, color);
            renderer.drawLine(px, py, px + cellSize_ - 1, py, Color::White);
            renderer.drawLine(px, py, px, py + cellSize_ - 1, Color::White);
        }
    }
}

void TetrisScene::drawSidePanel(pixelroot32::graphics::Renderer& renderer) {
    const int panelW = LOGICAL_WIDTH - panelX_ - 2;
    if (panelW < 40) {
        return;
    }
    renderer.drawText("NEXT", static_cast<std::int16_t>(panelX_),
                      static_cast<std::int16_t>(boardY_), Color::Gray, 1);
    // 4x4 preview box with compact cells.
    const int previewCell = (cellSize_ >= 8) ? 4 : 3;
    const int previewBox = previewCell * 4;
    const int px0 = panelX_;
    const int py0 = boardY_ + 14;
    renderer.drawRectangle(px0 - 1, py0 - 1, previewBox + 2, previewBox + 2,
                           Color::Gray);
    renderer.drawFilledRectangle(px0, py0, previewBox, previewBox, Color::Navy);
    const TetrisSession& shown = (state_ == SceneState::Demo) ? demoSession_ : session_;
    const TetrominoType next = shown.nextType();
    const Color color = colorForType(next);
    for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
        const BlockOffset block = Tetromino::blockAt(next, 0, i);
        renderer.drawFilledRectangle(px0 + block.x * previewCell,
                                     py0 + block.y * previewCell, previewCell,
                                     previewCell, color);
    }

    char buf[24];
    std::snprintf(buf, sizeof(buf), "LV %d", shown.level());
    renderer.drawText(buf, static_cast<std::int16_t>(panelX_),
                      static_cast<std::int16_t>(py0 + previewBox + 10), Color::Yellow, 1);
    std::snprintf(buf, sizeof(buf), "LN %d", shown.lines());
    renderer.drawText(buf, static_cast<std::int16_t>(panelX_),
                      static_cast<std::int16_t>(py0 + previewBox + 24), Color::White, 1);
    std::snprintf(buf, sizeof(buf), "SC %d", static_cast<int>(shown.score() > 99999
                                                                  ? 99999
                                                                  : shown.score()));
    renderer.drawText(buf, static_cast<std::int16_t>(panelX_),
                      static_cast<std::int16_t>(py0 + previewBox + 38), Color::White, 1);
}

void TetrisScene::drawPaused(pr32::graphics::Renderer& renderer) {
    const int boxX = (DISPLAY_WIDTH - BANNER_WIDTH) / 2;
    const int boxY = (DISPLAY_HEIGHT - BANNER_HEIGHT) / 2;
    renderer.drawFilledRectangle(boxX, boxY, BANNER_WIDTH, BANNER_HEIGHT, Color::Navy);
    renderer.drawRectangle(boxX, boxY, BANNER_WIDTH, BANNER_HEIGHT, Color::White);
    renderer.drawTextCentered("PAUSED", boxY + 10, Color::Yellow, 2);
    renderer.drawTextCentered("A+B:RESUME", boxY + 44, Color::White, 1);
    renderer.drawTextCentered("FALL FROZEN", boxY + 60, Color::Gray, 1);
}

void TetrisScene::drawGameOver(pr32::graphics::Renderer& renderer) {
    const int boxX = (DISPLAY_WIDTH - BANNER_WIDTH) / 2;
    const int boxY = (DISPLAY_HEIGHT - BANNER_HEIGHT) / 2;
    renderer.drawFilledRectangle(boxX, boxY, BANNER_WIDTH, BANNER_HEIGHT, Color::Navy);
    renderer.drawRectangle(boxX, boxY, BANNER_WIDTH, BANNER_HEIGHT, Color::White);
    renderer.drawTextCentered("GAME OVER", boxY + 8, Color::Red, 2);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "SC %d BEST %d",
                  static_cast<int>(session_.score() > 99999 ? 99999 : session_.score()),
                  static_cast<int>(session_.bestScore() > 99999 ? 99999
                                                                 : session_.bestScore()));
    renderer.drawTextCentered(buf, boxY + 36, Color::White, 1);
    renderer.drawTextCentered("A:RETRY B:TITLE", boxY + 56, Color::Gray, 1);
    (void)millis;
}

void TetrisScene::drawPiece(pixelroot32::graphics::Renderer& renderer, TetrominoType type,
                            std::uint8_t rotation, int boxX, int boxY, Color color,
                            bool ghost) {
    for (std::uint8_t i = 0; i < BLOCKS_PER_PIECE; ++i) {
        const BlockOffset block = Tetromino::blockAt(type, rotation, i);
        const int px = boxX + block.x * cellSize_;
        const int py = boxY + block.y * cellSize_;
        if (ghost) {
            renderer.drawRectangle(px, py, cellSize_, cellSize_, color);
        } else {
            renderer.drawFilledRectangle(px, py, cellSize_, cellSize_, color);
        }
    }
}

}  // namespace tetris
