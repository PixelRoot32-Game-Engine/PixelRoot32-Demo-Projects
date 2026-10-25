#include "ui/MinesweeperScene.h"
#include "ui/ScenePalette.h"
#include "ui/TileCatalog.h"
#include "presentation/PresentationMap.h"
#include "core/Engine.h"
#include "audio/AudioTypes.h"
#include "audio/AudioMusicTypes.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace minesweeper {

using Color = pr32::graphics::Color;
using Sprite4bpp = pr32::graphics::Sprite4bpp;
using namespace pr32::audio;

namespace {

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

/** Cell width of the generated 8x8 letters and digits. */
constexpr int GLYPH_ADVANCE = 8;

/**
 * @brief Pixel width of a generated glyph run.
 *
 * The scene centres counters on the width of the label above them, so the
 * label strings themselves stay the single source of that number.
 *
 * @param text Glyph run to measure.
 * @return Width in pixels, one 8 px cell per character.
 */
constexpr int glyphRunWidth(const char* text) {
    int cells = 0;
    while (text[cells] != '\0') {
        ++cells;
    }
    return cells * GLYPH_ADVANCE;
}

/**
 * @brief Draws default-font text centred on the composition.
 *
 * Renderer::drawTextCentered() centres a run on the logical canvas and the
 * display offset is then added on top of that position, so on a canvas wider
 * than the composition (the 320 px CYD) the two shifts do not cancel and the
 * run lands one offset to the right of the panel it belongs to. Lifting the
 * offset for the call is what makes "the canvas centre" and "the composition
 * centre" the same point, because the viewport is centred in the canvas by
 * construction; y is therefore translated into canvas space by the offset that
 * was lifted. On the 240x240 targets the offset is (0, 0) and this is exactly
 * the plain call it replaces.
 *
 * @param renderer Target renderer.
 * @param text Text to draw with the engine's default font.
 * @param y Top edge of the run, in the composition's own coordinates.
 * @param color Text colour.
 * @param size Integer glyph scale.
 */
void drawCenteredText(pr32::graphics::Renderer& renderer, std::string_view text, int y, Color color,
                      std::uint8_t size) {
    const int offsetX = renderer.getXOffset();
    const int offsetY = renderer.getYOffset();
    renderer.setDisplayOffset(0, 0);
    renderer.drawTextCentered(text, y + offsetY, color, size);
    renderer.setDisplayOffset(offsetX, offsetY);
}

// --- Menu layout --------------------------------------------------------------
// Panel, title, row and hint geometry live in GameConstants.h: the scene header
// sizes its UI widget members from them.

// --- Result overlay layout ----------------------------------------------------
/** The smiley is drawn at its native size; the sheet has no other. */
constexpr int RESULT_SMILEY_SIZE = 16;
/** Offsets from the result box top; the interior starts at PANEL_BORDER. */
constexpr int RESULT_TITLE_Y = 26;
constexpr int RESULT_SCORE_Y = 44;
constexpr int RESULT_HINT_Y = 54;

}  // namespace

void MinesweeperScene::init() {
    applyScenePalette();
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
    hudLine2_[0] = '\0';
    input_.reset();

#if PIXELROOT32_ENABLE_UI_SYSTEM
    // The menu rows are engine widgets: a vertical layout places them, the
    // button style carries the ink reversal, and the scene only pushes the
    // selected index. Scrolling stays off -- three rows always fit.
    menuLayout_.setPadding(pr32::math::toScalar(0));
    menuLayout_.setSpacing(pr32::math::toScalar(MENU_ROW_GAP));
    for (std::uint8_t i = 0; i < 3; ++i) {
        menuLayout_.addElement(&menuRows_[i]);
    }
    menuLayout_.setButtonStyle(Color::White, Color::Black, Color::Navy, Color::Gray);
    menuLayout_.setScrollEnabled(false);
    menuLayout_.setSelectedIndex(static_cast<int>(difficulty_));
    menuHint1_.centerX(DESIGN_WIDTH);
    menuHint2_.centerX(DESIGN_WIDTH);
#endif
    refreshMenuLabels();
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
    cellSize_ = cellPixelsFor(difficulty_);
    const int boardW = cellSize_ * config.width;
    const int boardH = cellSize_ * config.height;
    boardX_ = (DESIGN_WIDTH - boardW) / 2;
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
#if PIXELROOT32_ENABLE_UI_SYSTEM
    // One selection owner: the pad moves difficulty_, the layout mirrors it.
    menuLayout_.setSelectedIndex(level);
#endif
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
        // The menu row shows this time; the label is rewritten in place.
        refreshMenuLabels();
    }
}

void MinesweeperScene::refreshMenuLabels() {
    static const Difficulty levels[3] = {
        Difficulty::Beginner, Difficulty::Intermediate, Difficulty::Expert};
    for (int i = 0; i < 3; ++i) {
        const BoardConfig config = boardConfigFor(levels[i]);
        const char* name = difficultyName(levels[i]);
        char next[40];
        const std::size_t written =
            formatMenuRow(next, sizeof(next), name, config.width, config.height, config.mineCount,
                          bestSeconds_[i]);
        // Only overwrite when the new row has exactly the width the buffer was
        // built with: the widget's view is bound to that length, so a shorter
        // row would leave it reading a stale tail and a longer one would be
        // truncated. Both lengths are pinned by the presentation tests.
        const std::size_t expected =
            menuRowWidth(name, config.width, config.height, config.mineCount);
        if (written != 0 && written == expected && written == std::strlen(menuRowText_[i])) {
            std::memcpy(menuRowText_[i], next, written + 1);
        }
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
    // The mid ink covers the whole canvas, not just the composition, so the
    // margin a wider canvas leaves around the design box is the same paper
    // colour instead of whatever the framebuffer still held.
    renderer.setDisplayOffset(0, 0);
    renderer.drawFilledRectangle(0, 0, CANVAS_WIDTH, CANVAS_HEIGHT, Color::Gray);

    // Everything below is authored in the fixed 240x240 box; one offset moves
    // the whole of it to the middle of the canvas. On the 240x240 targets
    // VIEW_X/VIEW_Y are (0, 0), so their output is unchanged.
    renderer.setDisplayOffset(VIEW_X, VIEW_Y);
    if (state_ == SceneState::Menu) {
        drawMenu(renderer);
    } else {
        drawHud(renderer, millis());
        drawBoard(renderer);
        if (state_ == SceneState::GameOver) {
            drawResult(renderer, millis());
        }
    }

    // Entities belong to the canvas the engine hands the scene, so the viewport
    // offset must be gone before the base class draws them: leaving it on would
    // shift them a second time.
    renderer.setDisplayOffset(0, 0);
    Scene::draw(renderer);
}

void MinesweeperScene::drawMenu(pr32::graphics::Renderer& renderer) {
    drawPanel(renderer, MENU_PANEL_X, MENU_PANEL_Y, MENU_PANEL_W, MENU_PANEL_H);

    // Title bar: black band inside the frame, neutral face on the left and the
    // name centred by the default font at double size.
    renderer.drawFilledRectangle(MENU_CONTENT_X, MENU_TITLE_Y, MENU_CONTENT_W, MENU_TITLE_H,
                                 Color::Black);
    const Sprite4bpp* face = smileySprite(SmileyId::Normal);
    if (face != nullptr) {
        renderer.drawSprite(*face, MENU_CONTENT_X + 4,
                            MENU_TITLE_Y + (MENU_TITLE_H - RESULT_SMILEY_SIZE) / 2);
    }
    drawCenteredText(renderer, "MINESWEEPER", MENU_TITLE_Y + (MENU_TITLE_H - 2 * 7) / 2,
                     Color::White, 2);

#if PIXELROOT32_ENABLE_UI_SYSTEM
    // Rows and hints are engine widgets. The layout already resolved their
    // positions against the panel, and the button style provides the selected
    // black bar / light text that the sheet's composition asks for.
    menuLayout_.draw(renderer);
    menuHint1_.draw(renderer);
    menuHint2_.draw(renderer);
#else
    static const Difficulty levels[3] = {
        Difficulty::Beginner, Difficulty::Intermediate, Difficulty::Expert};
    for (int i = 0; i < 3; ++i) {
        const int slot = static_cast<int>(levels[i]);
        const int rowY = MENU_ROW_FIRST_Y + i * MENU_ROW_STEP;
        const int textY = rowY + (MENU_ROW_H - 7) / 2;
        if (levels[i] == difficulty_) {
            renderer.drawFilledRectangle(MENU_CONTENT_X, rowY, MENU_CONTENT_W, MENU_ROW_H,
                                         Color::Black);
            drawCenteredText(renderer, menuRowText_[slot], textY, Color::White, 1);
        } else {
            drawCenteredText(renderer, menuRowText_[slot], textY, Color::Navy, 1);
        }
    }
    drawCenteredText(renderer, "UP/DN:LEVEL A:START", MENU_HINT_FIRST_Y, Color::Navy, 1);
    drawCenteredText(renderer, "OPEN:A FLAG:B MENU:A+B", MENU_HINT_FIRST_Y + MENU_HINT_STEP,
                     Color::Navy, 1);
#endif
}

void MinesweeperScene::drawHud(pr32::graphics::Renderer& renderer, unsigned long nowMs) {
    // The band goes down first: every glyph is transparent-backed light ink,
    // so the black strip is what makes it readable -- exactly the sheet layout.
    renderer.drawFilledRectangle(0, 0, DESIGN_WIDTH, HEADER_HEIGHT, Color::Black);
    renderer.drawLine(0, HEADER_RULE_Y, DESIGN_WIDTH - 1, HEADER_RULE_Y, Color::White);

    char counter[4];
    drawLetterRun(renderer, "MINES", HEADER_LEFT_X, HEADER_LABEL_Y);
    formatCounter(counter, session_.getRemainingMines());
    drawDigitRun(renderer, counter,
                 HEADER_LEFT_X + (glyphRunWidth("MINES") - glyphRunWidth("000")) / 2,
                 HEADER_COUNTER_Y);

    std::uint32_t seconds = session_.getElapsedSeconds(nowMs);
    if (seconds > 999) {
        seconds = 999;
    }
    drawLetterRun(renderer, "TIME", HEADER_RIGHT_X, HEADER_LABEL_Y);
    formatCounter(counter, static_cast<std::uint16_t>(seconds));
    drawDigitRun(renderer, counter,
                 HEADER_RIGHT_X + (glyphRunWidth("TIME") - glyphRunWidth("000")) / 2,
                 HEADER_COUNTER_Y);

    const Sprite4bpp* face = smileySprite(smileyFor(session_.getStatus()));
    if (face != nullptr) {
        renderer.drawSprite(*face, HEADER_SMILEY_X, HEADER_SMILEY_Y);
    }

    // Footer: the only thing drawn below the board. It is anchored at
    // FOOTER_Y, which sits under every difficulty's last row, so it can never
    // overlap the playfield. Navy is the dark ink, which reads on the mid
    // background the board leaves around itself.
    const BoardConfig& config = session_.getConfig();
    std::snprintf(hudLine2_, sizeof(hudLine2_), "%s %dX%d-%d X%02d Y%02d",
                  difficultyName(difficulty_), config.width, config.height, config.mineCount,
                  cursor_.x, cursor_.y);
    renderer.drawText(hudLine2_, 2, FOOTER_Y, Color::Navy, 1);
}

void MinesweeperScene::drawPanel(pr32::graphics::Renderer& renderer, int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    const int interiorW = w - 2 * PANEL_BORDER;
    const int interiorH = h - 2 * PANEL_BORDER;
    if (interiorW > 0 && interiorH > 0) {
        renderer.drawFilledRectangle(x + PANEL_BORDER, y + PANEL_BORDER, interiorW, interiorH,
                                     Color::Gray);
    }

    // Edge tiles only span what the corners leave free, so a panel narrower
    // than two pieces draws no edge instead of spilling outside its rect.
    const int edgeEndX = x + w - PANEL_BORDER;
    const int edgeEndY = y + h - PANEL_BORDER;

    const Sprite4bpp* top = panelPieceSprite(PanelPiece::Top);
    if (top != nullptr) {
        for (int px = x + PANEL_BORDER; px + PANEL_BORDER <= edgeEndX; px += PANEL_BORDER) {
            renderer.drawSprite(*top, px, y);
        }
    }
    const Sprite4bpp* bottom = panelPieceSprite(PanelPiece::Bottom);
    if (bottom != nullptr) {
        for (int px = x + PANEL_BORDER; px + PANEL_BORDER <= edgeEndX; px += PANEL_BORDER) {
            renderer.drawSprite(*bottom, px, y + h - PANEL_BORDER);
        }
    }
    const Sprite4bpp* left = panelPieceSprite(PanelPiece::Left);
    if (left != nullptr) {
        for (int py = y + PANEL_BORDER; py + PANEL_BORDER <= edgeEndY; py += PANEL_BORDER) {
            renderer.drawSprite(*left, x, py);
        }
    }
    const Sprite4bpp* right = panelPieceSprite(PanelPiece::Right);
    if (right != nullptr) {
        for (int py = y + PANEL_BORDER; py + PANEL_BORDER <= edgeEndY; py += PANEL_BORDER) {
            renderer.drawSprite(*right, x + w - PANEL_BORDER, py);
        }
    }

    // Corners last: below 8 px on either axis even one piece would not fit the
    // rect, so they are skipped rather than clipped.
    if (w < PANEL_BORDER || h < PANEL_BORDER) {
        return;
    }
    const int rightX = x + w - PANEL_BORDER;
    const int bottomY = y + h - PANEL_BORDER;
    const Sprite4bpp* topLeft = panelPieceSprite(PanelPiece::TopLeft);
    if (topLeft != nullptr) {
        renderer.drawSprite(*topLeft, x, y);
    }
    const Sprite4bpp* topRight = panelPieceSprite(PanelPiece::TopRight);
    if (topRight != nullptr) {
        renderer.drawSprite(*topRight, rightX, y);
    }
    const Sprite4bpp* bottomLeft = panelPieceSprite(PanelPiece::BottomLeft);
    if (bottomLeft != nullptr) {
        renderer.drawSprite(*bottomLeft, x, bottomY);
    }
    const Sprite4bpp* bottomRight = panelPieceSprite(PanelPiece::BottomRight);
    if (bottomRight != nullptr) {
        renderer.drawSprite(*bottomRight, rightX, bottomY);
    }
}

void MinesweeperScene::drawLetterRun(pr32::graphics::Renderer& renderer, const char* text, int x,
                                     int y) {
    int penX = x;
    for (const char* glyph = text; *glyph != '\0'; ++glyph) {
        const Sprite4bpp* sprite = letterSprite(*glyph);
        if (sprite != nullptr) {
            renderer.drawSprite(*sprite, penX, y);
        }
        penX += GLYPH_ADVANCE;
    }
}

void MinesweeperScene::drawDigitRun(pr32::graphics::Renderer& renderer, const char* text, int x,
                                    int y) {
    int penX = x;
    for (const char* glyph = text; *glyph != '\0'; ++glyph) {
        const Sprite4bpp* sprite = digitSprite(*glyph);
        if (sprite != nullptr) {
            renderer.drawSprite(*sprite, penX, y);
        }
        penX += GLYPH_ADVANCE;
    }
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

            // tileFor() owns the content precedence (explosion, revealed mine,
            // flag, open number, covered) so the scene only has to blit.
            const Sprite4bpp* sprite = tileSprite(tileFor(*cell, showMines, isBlast), cellSize_);
            if (sprite != nullptr) {
                renderer.drawSprite(*sprite, cellX, cellY);
            }

            // A wrong flag keeps the flag art -- the player's own mark -- and
            // is struck through instead of being replaced by a different tile,
            // which reads as "this flag was false" rather than "a mine".
            if (showMines && cell->state == CellState::Flagged && !cell->hasMine) {
                const int inset = std::max<int>(2, cellSize_ / 8);
                const int left = cellX + inset;
                const int top = cellY + inset;
                const int right = cellX + cellSize_ - 1 - inset;
                const int bottom = cellY + cellSize_ - 1 - inset;
                renderer.drawLine(left, top, right, bottom, Color::Navy);
                renderer.drawLine(right, top, left, bottom, Color::Navy);
            }
        }
    }

    // Cursor last, so the frame sits above the cell it marks. The frame is
    // transparent inside, so drawing it adds no fill; its two dash phases
    // animate it without any per-frame state. The dashes are generated inset
    // by one bevel (two pixels at 16 and 12, one at 8), so they land on the
    // tile body instead of merging with a covered cell's light top/left edge.
    if (state_ == SceneState::Playing) {
        const Sprite4bpp* frame = cursorSprite(cursorPhase(millis()), cellSize_);
        if (frame != nullptr) {
            renderer.drawSprite(*frame, boardX_ + cursor_.x * cellSize_,
                                boardY_ + cursor_.y * cellSize_);
        }
    }
}

void MinesweeperScene::drawResult(pr32::graphics::Renderer& renderer, unsigned long nowMs) {
    const bool won = (session_.getStatus() == GameStatus::Won);
    const int boxX = (DESIGN_WIDTH - RESULT_WIDTH) / 2;
    const int boxY = (DESIGN_HEIGHT - RESULT_HEIGHT) / 2;

    // Frame first, then repaint the interior black: the score and hint are
    // light ink authored for a dark panel, so the mid-ink default interior
    // would leave them washed out. This is the one overlay that opts out of
    // the mid fill.
    drawPanel(renderer, boxX, boxY, RESULT_WIDTH, RESULT_HEIGHT);
    renderer.drawFilledRectangle(boxX + PANEL_BORDER, boxY + PANEL_BORDER,
                                 RESULT_WIDTH - 2 * PANEL_BORDER, RESULT_HEIGHT - 2 * PANEL_BORDER,
                                 Color::Black);

    const Sprite4bpp* face = smileySprite(won ? SmileyId::Cool : SmileyId::Dead);
    if (face != nullptr) {
        renderer.drawSprite(*face, (DESIGN_WIDTH - RESULT_SMILEY_SIZE) / 2, boxY + PANEL_BORDER);
    }

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
        drawCenteredText(renderer, "YOU WIN!", boxY + RESULT_TITLE_Y, Color::White, 2);
    } else {
        drawCenteredText(renderer, "BOOM!", boxY + RESULT_TITLE_Y, Color::White, 2);
    }
    drawCenteredText(renderer, score, boxY + RESULT_SCORE_Y, Color::Gray, 1);
    drawCenteredText(renderer, "A:RETRY B:MENU", boxY + RESULT_HINT_Y, Color::Gray, 1);
}

}  // namespace minesweeper
