#include "PacmanScene.h"

#include <cstdio>
#include <cstring>

#include <core/Engine.h>
#include <graphics/FontManager.h>
#include <platforms/EngineConfig.h>

#include "PacmanPalette.h"
#include "assets/PacmanSprites.h"
#include "audio/PacmanSounds.h"
#include "rules/PacmanConfig.h"

namespace pr32 = pixelroot32;

/// The Engine lives in the platform file; the scene reaches it from there.
extern pr32::core::Engine engine;

namespace pacman {

namespace {

/**
 * @brief Width of the playfield, in pixels.
 */
constexpr int kBoardWidth = kMazeCols * kCellSize;

/**
 * @brief Width of one text cell of the engine's default font.
 *
 * The default font is 5 pixels wide with 1 pixel of spacing, so a character
 * advances 6 pixels. Used to right-align the status band.
 */
constexpr int kTextAdvance = 6;

/**
 * @brief Interpolates a grid actor's position between two cells.
 *
 * @param cell The cell the actor came from.
 * @param target The cell the actor is travelling towards.
 * @param progress How far the actor has travelled into the cell.
 * @param stepsPerCell How many steps the actor takes to cross a cell.
 * @return The position in sub-cell units.
 */
constexpr int interpolatedSubCell(int cell, int target, int progress, int stepsPerCell) {
    return cell * stepsPerCell + (target - cell) * progress;
}

}  // namespace

PacmanScene::PacmanScene() = default;

void PacmanScene::init() {
    Scene::init();

    accumulator_ = 0;
    frameCounter_ = 0;
    requestedDirection_ = Direction::None;
}

void PacmanScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);
    ++frameCounter_;

    auto& input = engine.getInputManager();

    // A direction press is latched here, at frame scope, and only read by
    // the logic step. The step below may run zero times in this frame, and a
    // press that lands on such a frame must not be lost.
    if (input.isButtonPressed(kBtnUp)) {
        requestedDirection_ = Direction::Up;
    } else if (input.isButtonPressed(kBtnDown)) {
        requestedDirection_ = Direction::Down;
    } else if (input.isButtonPressed(kBtnLeft)) {
        requestedDirection_ = Direction::Left;
    } else if (input.isButtonPressed(kBtnRight)) {
        requestedDirection_ = Direction::Right;
    }

    // Button A starts the game, pauses it and resumes it. Button B always
    // starts a new game.
    if (input.isButtonPressed(kBtnA)) {
        switch (session_.state()) {
            case GameState::Title:
            case GameState::Victory:
            case GameState::Defeat:
                session_.startNewGame();
                requestedDirection_ = Direction::None;
                accumulator_ = 0;
                break;
            case GameState::Playing:
            case GameState::Paused:
                session_.togglePause();
                break;
        }
    }
    if (input.isButtonPressed(kBtnB)) {
        session_.startNewGame();
        requestedDirection_ = Direction::None;
        accumulator_ = 0;
    }

    accumulator_ += deltaTime;

    int steps = 0;
    while (accumulator_ >= static_cast<unsigned long>(kLogicStepMs) &&
           steps < kMaxLogicStepsPerFrame) {
        accumulator_ -= static_cast<unsigned long>(kLogicStepMs);
        session_.step(requestedDirection_);
        ++steps;
    }

    if (steps == kMaxLogicStepsPerFrame) {
        // The frame arrived with a backlog. Dropping it keeps the game from
        // fast-forwarding after a stall.
        accumulator_ = 0;
    }

    playReportedEvents();
}

void PacmanScene::playReportedEvents() {
    const std::uint8_t events = session_.pendingEvents();
    if (events == kEventNone) {
        return;
    }
    session_.clearEvents();

#if PIXELROOT32_ENABLE_AUDIO
    auto& audioEngine = engine.getAudioEngine();

    if ((events & kEventDot) != 0) {
        audioEngine.playEvent(pacman::audio::dotSound());
    }
    if ((events & kEventPowerPellet) != 0) {
        audioEngine.playEvent(pacman::audio::powerPelletSound());
    }
    if ((events & kEventEnemyDefeated) != 0) {
        audioEngine.playEvent(pacman::audio::enemyDefeatedSound());
    }
    if ((events & kEventLifeLost) != 0) {
        audioEngine.playEvent(pacman::audio::lifeLostSound());
    }
    if ((events & kEventLevelCompleted) != 0) {
        audioEngine.playEvent(pacman::audio::levelCompletedSound());
    }
    if ((events & kEventExtraLife) != 0) {
        audioEngine.playEvent(pacman::audio::extraLifeSound());
    }
#else
    // Without the audio subsystem the events are simply dropped: the rules
    // never depend on a sound being played.
    (void)events;
#endif
}

int PacmanScene::boardOriginX() const {
    return (LOGICAL_WIDTH - kBoardWidth) / 2;
}

int PacmanScene::boardOriginY() const {
    return kStatusBandHeight;
}

bool PacmanScene::levelIsValid() const {
    return session_.maze().validate() == PacmanMaze::ValidationError::None;
}

void PacmanScene::draw(pixelroot32::graphics::Renderer& renderer) {
    // The background is painted first and the base Scene::draw() is called
    // afterwards, so the scene's entities stay visible on top of it.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(),
                                 kBackgroundColor);

    Scene::draw(renderer);

    drawMazeWalls(renderer);
    drawCollectibles(renderer);
    drawPlayer(renderer);
    drawEnemies(renderer);
    drawStatusBand(renderer);
    drawStateOverlay(renderer);
}

void PacmanScene::drawStateOverlay(pr32::graphics::Renderer& renderer) const {
    switch (session_.state()) {
        case GameState::Title: {
            // The title sits on a band that hides the maze behind it, so the
            // text reads cleanly over the walls.
            const int bandY = boardOriginY() + 60;
            renderer.drawFilledRectangle(0, bandY, LOGICAL_WIDTH, 48, kBackgroundColor);
            renderer.drawTextCentered("PAC-MAN", bandY + 8, kPelletColor, 2);
            renderer.drawTextCentered("PRESS SPACE TO START", bandY + 30, kTextColor, 1);
            break;
        }
        case GameState::Paused:
            drawMessage(renderer, "PAUSED", kTextColor);
            break;
        case GameState::Victory:
            drawMessage(renderer, "LEVEL COMPLETE", kPelletColor, "PRESS SPACE TO PLAY AGAIN");
            break;
        case GameState::Defeat:
            drawMessage(renderer, "GAME OVER", kAlertColor, "PRESS SPACE TO PLAY AGAIN");
            break;
        case GameState::Playing:
            if (session_.isRespawning()) {
                drawMessage(renderer, "READY!", kPelletColor);
            }
            break;
    }
}

void PacmanScene::drawMessage(pr32::graphics::Renderer& renderer, const char* title,
                               pr32::graphics::Color titleColor, const char* subtitle) const {
    const int bandHeight = subtitle != nullptr ? 36 : 20;
    const int bandY = boardOriginY() + 72;
    renderer.drawFilledRectangle(0, bandY, LOGICAL_WIDTH, bandHeight, kBackgroundColor);
    renderer.drawTextCentered(title, bandY + 6, titleColor, 1);
    if (subtitle != nullptr) {
        renderer.drawTextCentered(subtitle, bandY + 20, kTextColor, 1);
    }
}

void PacmanScene::drawMazeWalls(pr32::graphics::Renderer& renderer) const {
    const PacmanMaze& maze = session_.maze();
    const int originX = boardOriginX();
    const int originY = boardOriginY();

    // The maze is drawn as the outline of its wall regions: a wall cell
    // paints the side that faces a non-wall cell and stays silent on the
    // sides that face another wall. A region two cells thick therefore shows
    // two parallel lines with the corridor between them, which is the shape
    // the arcade original uses.
    for (int row = 0; row < maze.rowCount(); ++row) {
        for (int col = 0; col < maze.colCount(); ++col) {
            if (!maze.isWall(col, row)) {
                continue;
            }

            const int x = originX + col * kCellSize;
            const int y = originY + row * kCellSize;
            const int right = x + kCellSize - 1;
            const int bottom = y + kCellSize - 1;

            if (!maze.isWall(col, row - 1)) {
                renderer.drawLine(x, y, right, y, kWallColor);
            }
            if (!maze.isWall(col, row + 1)) {
                renderer.drawLine(x, bottom, right, bottom, kWallColor);
            }
            if (!maze.isWall(col - 1, row)) {
                renderer.drawLine(x, y, x, bottom, kWallColor);
            }
            if (!maze.isWall(col + 1, row)) {
                renderer.drawLine(right, y, right, bottom, kWallColor);
            }
        }
    }

    // The ghost house doorway: a bar across the opening at the top of the
    // house, so the room is closed to the player and open to the enemies.
    for (int col = 0; col < maze.colCount(); ++col) {
        if (maze.cellType(col, 12) != CellType::HouseDoor) {
            continue;
        }
        const int x = originX + col * kCellSize;
        const int y = originY + 13 * kCellSize;
        renderer.drawLine(x, y, x + kCellSize - 1, y, kHouseColor);
    }
}

void PacmanScene::drawCollectibles(pr32::graphics::Renderer& renderer) const {
    const PacmanMaze& maze = session_.maze();
    const int originX = boardOriginX();
    const int originY = boardOriginY();

    for (int row = 0; row < maze.rowCount(); ++row) {
        for (int col = 0; col < maze.colCount(); ++col) {
            const Collectible collectible = session_.collectibleAt(col, row);
            if (collectible == Collectible::None) {
                continue;
            }

            const int centerX = originX + col * kCellSize + kCellSize / 2;
            const int centerY = originY + row * kCellSize + kCellSize / 2;

            if (collectible == Collectible::Dot) {
                renderer.drawFilledRectangle(centerX - 1, centerY - 1, 2, 2, kDotColor);
            } else {
                // The pellets pulse, so the player can tell at a glance that
                // they are worth more than a dot.
                const int radius = ((frameCounter_ / 8) % 2 == 0) ? 3 : 2;
                renderer.drawFilledCircle(centerX, centerY, radius, kPelletColor);
            }
        }
    }
}

void PacmanScene::drawPlayer(pr32::graphics::Renderer& renderer) const {
    const PacmanPlayer& player = session_.player();

    // A player that is still protected from a lost life blinks, so the
    // protection is visible instead of being a rule the player cannot see.
    if (session_.playerIsInvulnerable() && ((frameCounter_ / 4) % 2 == 0)) {
        return;
    }

    // The player's position is interpolated between the cell it came from
    // and the cell it is travelling towards, so the sprite slides smoothly
    // from one cell to the next.
    const int subCol = interpolatedSubCell(player.cellCol(), player.targetCol(), player.progress(),
                                            kPlayerStepsPerCell);
    const int subRow = interpolatedSubCell(player.cellRow(), player.targetRow(), player.progress(),
                                            kPlayerStepsPerCell);

    const int x = boardOriginX() + subCol * kCellSize / kPlayerStepsPerCell;
    const int y = boardOriginY() + subRow * kCellSize / kPlayerStepsPerCell;

    const bool mouthOpen = player.mouthIsOpen();
    const pr32::graphics::Sprite* sprite = nullptr;
    bool flipX = false;

    switch (player.direction()) {
        case Direction::Up:
            sprite = mouthOpen ? &sprites::PLAYER_UP_OPEN_SPRITE : &sprites::PLAYER_UP_CLOSED_SPRITE;
            break;
        case Direction::Down:
            sprite = mouthOpen ? &sprites::PLAYER_DOWN_OPEN_SPRITE : &sprites::PLAYER_DOWN_CLOSED_SPRITE;
            break;
        case Direction::Left:
            sprite = mouthOpen ? &sprites::PLAYER_RIGHT_OPEN_SPRITE : &sprites::PLAYER_RIGHT_CLOSED_SPRITE;
            flipX = true;
            break;
        case Direction::Right:
        case Direction::None:
            sprite = mouthOpen ? &sprites::PLAYER_RIGHT_OPEN_SPRITE : &sprites::PLAYER_RIGHT_CLOSED_SPRITE;
            break;
    }

    if (sprite != nullptr) {
        renderer.drawSprite(*sprite, x, y, kPlayerColor, flipX);
    }
}

void PacmanScene::drawEnemies(pr32::graphics::Renderer& renderer) const {
    for (int index = 0; index < session_.enemyCount(); ++index) {
        const PacmanEnemy& enemy = session_.enemy(index);

        const int subCol = interpolatedSubCell(enemy.cellCol(), enemy.targetCol(), enemy.progress(),
                                                kEnemyStepsPerCell);
        const int subRow = interpolatedSubCell(enemy.cellRow(), enemy.targetRow(), enemy.progress(),
                                                kEnemyStepsPerCell);
        const int x = boardOriginX() + subCol * kCellSize / kEnemyStepsPerCell;
        const int y = boardOriginY() + subRow * kCellSize / kEnemyStepsPerCell;

        // A hunter that is fleeing shows the frightened face; a hunter that
        // has been defeated shows nothing but its eyes, the way the arcade
        // original draws the pair that floats home.
        pr32::graphics::Color bodyColor = kEnemyColors[index];
        const pr32::graphics::Sprite* body = &sprites::GHOST_BODY_SPRITE;
        const pr32::graphics::Sprite* eyes = &sprites::GHOST_EYES_SPRITE;

        if (enemy.isVulnerable()) {
            bodyColor = kVulnerableColor;
            body = &sprites::GHOST_SCARED_BODY_SPRITE;
            eyes = &sprites::GHOST_SCARED_EYES_SPRITE;
            // The frightened hunters flash as the effect is about to run out,
            // which is the warning that it is nearly over.
            if (session_.powerStepsLeft() < kPowerFlashSteps && ((frameCounter_ / 4) % 2 == 0)) {
                bodyColor = kTextColor;
            }
        }

        if (!enemy.isDefeated()) {
            renderer.drawSprite(*body, x, y, bodyColor, false);
        }
        renderer.drawSprite(*eyes, x, y, kTextColor, false);
    }
}

void PacmanScene::drawStatusBand(pr32::graphics::Renderer& renderer) const {
    char buffer[24];

    std::snprintf(buffer, sizeof(buffer), "SCORE %06d", session_.score());
    renderer.drawText(buffer, 4, 2, kTextColor, 1);

    std::snprintf(buffer, sizeof(buffer), "LIVES %d", session_.lives());
    const int width = static_cast<int>(std::strlen(buffer)) * kTextAdvance;
    renderer.drawText(buffer, LOGICAL_WIDTH - 4 - width, 2, kTextColor, 1);

    std::snprintf(buffer, sizeof(buffer), "LEFT %d", session_.remainingCollectibles());
    renderer.drawText(buffer, 4, 12, kTextColor, 1);

    if (session_.powerIsActive()) {
        renderer.drawFilledCircle(LOGICAL_WIDTH - 12, 16, 3, kPelletColor);
    }
}

}  // namespace pacman
