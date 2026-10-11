#include "ui/MinesweeperScene.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include "audio/AudioMusicTypes.h"
#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace minesweeper {

using Color = pr32::graphics::Color;
using namespace pr32::audio;

namespace {

// Classic number colors on the dark opened cell.
const Color NUMBER_COLORS[9] = {
    Color::Black,  // unused placeholder, never drawn
    Color::Blue,
    Color::Green,
    Color::Red,
    Color::Purple,
    Color::DarkRed,
    Color::Cyan,
    Color::White,
    Color::Gray
};

const MusicNote WIN_MELODY[] = {
    makeNote(INSTR_PULSE_LEAD, Note::C, 0.15f),
    makeNote(INSTR_PULSE_LEAD, Note::E, 0.15f),
    makeNote(INSTR_PULSE_LEAD, Note::G, 0.15f),
    makeNote(INSTR_PULSE_LEAD, Note::C, 6, 0.35f)
};

const MusicTrack WIN_MUSIC = {
    WIN_MELODY,
    sizeof(WIN_MELODY) / sizeof(MusicNote),
    false,
    WaveType::PULSE,
    0.5f
};

int textPixelWidth(const char* text, int size) {
    int chars = 0;
    while (text[chars] != '\0') {
        ++chars;
    }
    // Default 5x7 font advances 6 px per glyph at size 1.
    return chars * 6 * size - size;
}

}  // namespace

void MinesweeperScene::init() {
    state_ = SceneState::Menu;
    difficulty_ = Difficulty::Beginner;
    cursor_ = Position{0, 0};
    cellSize_ = 0;
    boardX_ = 0;
    boardY_ = 0;
    gameOverMs_ = 0;
    seedCounter_ = 0x8BADF00DU;
    bestSeconds_[0] = 0;
    bestSeconds_[1] = 0;
    bestSeconds_[2] = 0;
    hudLine1_[0] = '\0';
    hudLine2_[0] = '\0';
    hudHint_[0] = '\0';
    input_.reset();
}

void MinesweeperScene::update(unsigned long deltaTime) {
    const unsigned long nowMs = millis();
    std::uint16_t held = 0;
    for (std::uint8_t i = 0; i < BUTTON_COUNT; ++i) {
        if (engine.getInputManager().isButtonDown(i)) {
            held |= static_cast<std::uint16_t>(1U << i);
        }
    }
    const std::uint16_t commands = input_.poll(held, nowMs);

    switch (state_) {
        case SceneState::Menu:
            handleMenu(commands);
            break;
        case SceneState::Playing:
            handlePlaying(commands, nowMs);
            break;
        case SceneState::GameOver:
            handleGameOver(commands, nowMs);
            break;
    }
    Scene::update(deltaTime);
}

void MinesweeperScene::startGame() {
    const BoardConfig config = boardConfigFor(difficulty_);
    if (!session_.start(config, nextSeed(millis()))) {
        return;
    }
    // Center the cursor so the opening move starts mid-board.
    cursor_.x = static_cast<std::uint8_t>(config.width / 2);
    cursor_.y = static_cast<std::uint8_t>(config.height / 2);
    cellSize_ = cellSizeFor(config.width, config.height);
    const int boardW = cellSize_ * config.width;
    const int boardH = cellSize_ * config.height;
    boardX_ = (LOGICAL_WIDTH - boardW) / 2;
    boardY_ = BOARD_TOP + (BOARD_AREA_HEIGHT - boardH) / 2;
    state_ = SceneState::Playing;
    input_.reset();
    engine.getMusicPlayer().stop();
    playTone(700.0f, 0.06f, 0.5f);
}

void MinesweeperScene::backToMenu() {
    state_ = SceneState::Menu;
    input_.reset();
    engine.getMusicPlayer().stop();
    playTone(330.0f, 0.08f, 0.5f);
}

void MinesweeperScene::cycleDifficulty(int direction) {
    int level = static_cast<int>(difficulty_) + direction;
    if (level < 0) {
        level = 2;
    } else if (level > 2) {
        level = 0;
    }
    difficulty_ = static_cast<Difficulty>(level);
    playTone(520.0f, 0.05f, 0.4f);
}

void MinesweeperScene::handleMenu(std::uint16_t commands) {
    if (hasCommand(commands, GameCommand::Up) || hasCommand(commands, GameCommand::Left)) {
        cycleDifficulty(-1);
    } else if (hasCommand(commands, GameCommand::Down) ||
               hasCommand(commands, GameCommand::Right)) {
        cycleDifficulty(1);
    }
    if (hasCommand(commands, GameCommand::Reveal)) {
        startGame();
    }
}

void MinesweeperScene::handlePlaying(std::uint16_t commands, unsigned long nowMs) {
    if (hasCommand(commands, GameCommand::Cancel)) {
        backToMenu();
        return;
    }
    int dx = 0;
    int dy = 0;
    if (hasCommand(commands, GameCommand::Left)) {
        dx -= 1;
    }
    if (hasCommand(commands, GameCommand::Right)) {
        dx += 1;
    }
    if (hasCommand(commands, GameCommand::Up)) {
        dy -= 1;
    }
    if (hasCommand(commands, GameCommand::Down)) {
        dy += 1;
    }
    if (dx != 0 || dy != 0) {
        moveCursor(dx, dy);
    }

    if (hasCommand(commands, GameCommand::Reveal)) {
        if (session_.reveal(cursor_, nowMs)) {
            if (session_.getStatus() == GameStatus::Lost) {
                engine.getMusicPlayer().stop();
                AudioEvent boom{};
                boom.type = WaveType::NOISE;
                boom.frequency = 220.0f;
                boom.duration = 0.5f;
                boom.volume = 0.8f;
                boom.duty = 0.5f;
                engine.getAudioEngine().playEvent(boom);
                state_ = SceneState::GameOver;
                gameOverMs_ = nowMs;
                input_.reset();
            } else if (session_.getStatus() == GameStatus::Won) {
                recordBest(session_.getElapsedSeconds(nowMs));
                engine.getMusicPlayer().stop();
                engine.getMusicPlayer().play(WIN_MUSIC);
                state_ = SceneState::GameOver;
                gameOverMs_ = nowMs;
                input_.reset();
            } else {
                playTone(880.0f, 0.06f, 0.5f);
            }
        } else {
            // Flagged or already open: dull feedback, no state change.
            playTone(200.0f, 0.05f, 0.35f);
        }
        return;
    }

    if (hasCommand(commands, GameCommand::ToggleFlag)) {
        const Minefield& field = session_.getMinefield();
        const Cell* before = field.getCell(cursor_);
        const bool wasFlagged =
            (before != nullptr) && (before->state == CellState::Flagged);
        if (session_.toggleFlag(cursor_, nowMs)) {
            playTone(wasFlagged ? 440.0f : 660.0f, 0.07f, 0.5f);
        }
    }
}

void MinesweeperScene::handleGameOver(std::uint16_t commands, unsigned long nowMs) {
    if (nowMs - gameOverMs_ < GAME_OVER_LOCK_MS) {
        return;
    }
    if (hasCommand(commands, GameCommand::Reveal)) {
        startGame();
    } else if (hasCommand(commands, GameCommand::ToggleFlag) ||
               hasCommand(commands, GameCommand::Cancel)) {
        backToMenu();
    }
}

void MinesweeperScene::moveCursor(int dx, int dy) {
    const BoardConfig& config = session_.getConfig();
    if (config.width == 0 || config.height == 0) {
        return;
    }
    int x = static_cast<int>(cursor_.x) + dx;
    int y = static_cast<int>(cursor_.y) + dy;
    // Clamp: the cursor never leaves the board (spec section 8).
    if (x < 0) {
        x = 0;
    } else if (x >= config.width) {
        x = config.width - 1;
    }
    if (y < 0) {
        y = 0;
    } else if (y >= config.height) {
        y = config.height - 1;
    }
    cursor_.x = static_cast<std::uint8_t>(x);
    cursor_.y = static_cast<std::uint8_t>(y);
}

void MinesweeperScene::recordBest(std::uint32_t seconds) {
    const int slot = static_cast<int>(difficulty_);
    if (seconds > 0xFFFFU) {
        seconds = 0xFFFFU;
    }
    const std::uint16_t value = static_cast<std::uint16_t>(seconds);
    if (bestSeconds_[slot] == 0 || value < bestSeconds_[slot]) {
        bestSeconds_[slot] = value;
    }
}

std::uint32_t MinesweeperScene::nextSeed(unsigned long nowMs) {
    seedCounter_ += 0x9E3779B9U;
    std::uint32_t seed =
        static_cast<std::uint32_t>(nowMs) * 1664525U + 1013904223U + seedCounter_;
    return (seed != 0) ? seed : 1U;
}

void MinesweeperScene::playTone(float frequency, float duration, float volume) {
    AudioEvent tone{};
    tone.type = WaveType::PULSE;
    tone.frequency = frequency;
    tone.duration = duration;
    tone.volume = volume;
    tone.duty = 0.5f;
    engine.getAudioEngine().playEvent(tone);
}

void MinesweeperScene::draw(pr32::graphics::Renderer& renderer) {
    renderer.drawFilledRectangle(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, Color::Black);
    if (state_ == SceneState::Menu) {
        drawMenu(renderer);
    } else {
        drawHud(renderer, millis());
        drawBoard(renderer);
        if (state_ == SceneState::GameOver) {
            drawResult(renderer, millis());
        }
    }
    Scene::draw(renderer);
}

void MinesweeperScene::drawMenu(pr32::graphics::Renderer& renderer) {
    renderer.drawTextCentered("MINESWEEPER", 30, Color::Yellow, 2);
    renderer.drawTextCentered("PIXELROOT32 DEMO", 52, Color::Gray, 1);

    static const Difficulty levels[3] = {
        Difficulty::Beginner, Difficulty::Intermediate, Difficulty::Expert};
    for (int i = 0; i < 3; ++i) {
        const BoardConfig config = boardConfigFor(levels[i]);
        char row[40];
        const int slot = static_cast<int>(levels[i]);
        if (bestSeconds_[slot] > 0) {
            std::snprintf(row, sizeof(row), "%s %dX%d-%d BEST %d", difficultyName(levels[i]),
                          config.width, config.height, config.mineCount, bestSeconds_[slot]);
        } else {
            std::snprintf(row, sizeof(row), "%s %dX%d-%d", difficultyName(levels[i]),
                          config.width, config.height, config.mineCount);
        }
        char marked[44];
        std::snprintf(marked, sizeof(marked), "%c %s", (levels[i] == difficulty_) ? '>' : ' ',
                      row);
        const int y = 96 + i * 26;
        if (levels[i] == difficulty_) {
            renderer.drawTextCentered(marked, y, Color::Yellow, 1);
        } else {
            renderer.drawTextCentered(marked, y, Color::White, 1);
        }
    }
    renderer.drawTextCentered("UP/DN:LEVEL A:START", 196, Color::Gray, 1);
    renderer.drawTextCentered("OPEN:A FLAG:B MENU:A+B", 210, Color::Gray, 1);
}

void MinesweeperScene::drawHud(pr32::graphics::Renderer& renderer, unsigned long nowMs) {
    std::uint32_t seconds = session_.getElapsedSeconds(nowMs);
    if (seconds > 999) {
        seconds = 999;
    }
    std::snprintf(hudLine1_, sizeof(hudLine1_), "MIN %03d T %03d",
                  session_.getRemainingMines(), static_cast<int>(seconds));
    renderer.drawText(hudLine1_, 2, HUD_LINE_1_Y, Color::White, 1);

    const char* level = difficultyName(difficulty_);
    renderer.drawText(level, DISPLAY_WIDTH - textPixelWidth(level, 1) - 2, HUD_LINE_1_Y,
                      Color::Yellow, 1);

    const BoardConfig& config = session_.getConfig();
    std::snprintf(hudLine2_, sizeof(hudLine2_), "%dX%d-%d X%02d Y%02d", config.width,
                  config.height, config.mineCount, cursor_.x, cursor_.y);
    renderer.drawText(hudLine2_, 2, HUD_LINE_2_Y, Color::White, 1);

    if (state_ == SceneState::Playing) {
        std::snprintf(hudHint_, sizeof(hudHint_), "A:OPEN B:FLAG A+B:MENU");
    } else {
        std::snprintf(hudHint_, sizeof(hudHint_), "A:RETRY B:MENU");
    }
    renderer.drawText(hudHint_, 2, HUD_HINT_Y, Color::Gray, 1);
    renderer.drawLine(0, HUD_DIVIDER_Y, DISPLAY_WIDTH - 1, HUD_DIVIDER_Y, Color::Gray);
}

void MinesweeperScene::drawBoard(pr32::graphics::Renderer& renderer) {
    const Minefield& field = session_.getMinefield();
    const BoardConfig& config = field.getConfig();
    if (config.width == 0 || cellSize_ == 0) {
        return;
    }
    const bool showMines = (session_.getStatus() == GameStatus::Lost);
    Position detonated{0, 0};
    const bool hasBlast = field.detonatedPosition(detonated);

    for (std::uint8_t y = 0; y < config.height; ++y) {
        for (std::uint8_t x = 0; x < config.width; ++x) {
            const Position pos{x, y};
            const Cell* cell = field.getCell(pos);
            if (cell == nullptr) {
                continue;
            }
            const int cellX = boardX_ + x * cellSize_;
            const int cellY = boardY_ + y * cellSize_;
            const bool isBlast =
                hasBlast && (detonated.x == x) && (detonated.y == y);

            if (cell->state == CellState::Revealed && !cell->hasMine) {
                // Opened cell: dark tile plus a colored digit when needed.
                renderer.drawFilledRectangle(cellX, cellY, cellSize_, cellSize_, Color::Black);
                if (cell->adjacentMines > 0 && cell->adjacentMines <= 8) {
                    const char glyph = static_cast<char>('0' + cell->adjacentMines);
                    drawCellText(renderer, glyph, cellX, cellY,
                                 NUMBER_COLORS[cell->adjacentMines]);
                }
                continue;
            }

            if (cell->state == CellState::Revealed && cell->hasMine) {
                // Only reachable after a loss: the exploded mine burns red.
                renderer.drawFilledRectangle(cellX, cellY, cellSize_, cellSize_, Color::Red);
                const int radius = (cellSize_ / 4 >= 2) ? cellSize_ / 4 : 2;
                renderer.drawFilledCircle(cellX + cellSize_ / 2, cellY + cellSize_ / 2,
                                          radius, Color::White);
                continue;
            }

            // Covered cell (hidden or flagged): gray tile, white top/left lip.
            renderer.drawFilledRectangle(cellX, cellY, cellSize_, cellSize_, Color::Gray);
            renderer.drawLine(cellX, cellY, cellX + cellSize_ - 1, cellY, Color::White);
            renderer.drawLine(cellX, cellY, cellX, cellY + cellSize_ - 1, Color::White);

            if (cell->state == CellState::Flagged) {
                if (showMines && !cell->hasMine) {
                    // Wrong flag exposed by the end-of-game reveal.
                    drawCellText(renderer, 'X', cellX, cellY, Color::Yellow);
                } else {
                    drawCellText(renderer, 'F', cellX, cellY, Color::Red);
                }
            } else if (showMines && cell->hasMine && !isBlast) {
                const int radius = (cellSize_ / 4 >= 2) ? cellSize_ / 4 : 2;
                renderer.drawFilledCircle(cellX + cellSize_ / 2, cellY + cellSize_ / 2,
                                          radius, Color::White);
            }

            if (isBlast) {
                renderer.drawRectangle(cellX, cellY, cellSize_, cellSize_, Color::Yellow);
            }
        }
    }

    // Cursor: blinking yellow frame, board play only.
    if (state_ == SceneState::Playing && (millis() / 300) % 2 == 0) {
        const int cursorX = boardX_ + cursor_.x * cellSize_;
        const int cursorY = boardY_ + cursor_.y * cellSize_;
        renderer.drawRectangle(cursorX, cursorY, cellSize_, cellSize_, Color::Yellow);
        if (cellSize_ >= 10) {
            renderer.drawRectangle(cursorX + 1, cursorY + 1, cellSize_ - 2, cellSize_ - 2,
                                   Color::Yellow);
        }
    }
}

void MinesweeperScene::drawResult(pr32::graphics::Renderer& renderer, unsigned long nowMs) {
    const bool won = (session_.getStatus() == GameStatus::Won);
    const int boxX = (DISPLAY_WIDTH - RESULT_WIDTH) / 2;
    const int boxY = (DISPLAY_HEIGHT - RESULT_HEIGHT) / 2;
    renderer.drawFilledRectangle(boxX, boxY, RESULT_WIDTH, RESULT_HEIGHT, Color::Navy);
    renderer.drawRectangle(boxX, boxY, RESULT_WIDTH, RESULT_HEIGHT, Color::White);

    std::uint32_t seconds = session_.getElapsedSeconds(nowMs);
    if (seconds > 999) {
        seconds = 999;
    }
    const int slot = static_cast<int>(difficulty_);
    char score[32];
    if (bestSeconds_[slot] > 0) {
        std::snprintf(score, sizeof(score), "TIME %d BEST %d", static_cast<int>(seconds),
                      bestSeconds_[slot]);
    } else {
        std::snprintf(score, sizeof(score), "TIME %d", static_cast<int>(seconds));
    }

    if (won) {
        renderer.drawTextCentered("YOU WIN!", boxY + 8, Color::Yellow, 2);
    } else {
        renderer.drawTextCentered("BOOM!", boxY + 8, Color::Red, 2);
    }
    renderer.drawTextCentered(score, boxY + 34, Color::White, 1);
    renderer.drawTextCentered("A:RETRY B:MENU", boxY + 50, Color::Gray, 1);
}

void MinesweeperScene::drawCellText(pr32::graphics::Renderer& renderer, char glyph, int cellX,
                                    int cellY, Color color) {
    const std::uint8_t size = cellTextSize(cellSize_);
    char text[2] = {glyph, '\0'};
    // The default 5x7 glyphs leave the text slightly left-heavy on purpose:
    // the 1 px white lip on covered cells eats the left edge.
    const int glyphX = cellX + (cellSize_ - 5 * size) / 2;
    const int glyphY = cellY + (cellSize_ - 7 * size) / 2;
    renderer.drawText(text, static_cast<std::int16_t>(glyphX),
                      static_cast<std::int16_t>(glyphY), color, size);
}

}  // namespace minesweeper
