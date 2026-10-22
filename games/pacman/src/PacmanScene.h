#pragma once

#include <core/Scene.h>
#include <graphics/Renderer.h>

#include "rules/PacmanDirection.h"
#include "rules/PacmanSession.h"

namespace pacman {

/**
 * @class PacmanScene
 * @brief The demo's single scene: it drives the rules and draws them.
 *
 * The scene is the presentation layer only. Every rule about movement,
 * scoring and enemies lives in the engine-independent layer under
 * `src/rules`, which is what makes the game testable without a graphical
 * session.
 *
 * The scene reads the input, hands the rules a fixed logic step at a time,
 * and draws whatever the rules report. It never decides a rule of the game
 * itself.
 */
class PacmanScene : public pixelroot32::core::Scene {
public:
    PacmanScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Column of the maze's top-left corner, in pixels.
     * @return The horizontal origin of the playfield.
     */
    [[nodiscard]] int boardOriginX() const;

    /**
     * @brief Row of the maze's top-left corner, in pixels.
     * @return The vertical origin of the playfield.
     */
    [[nodiscard]] int boardOriginY() const;

    /**
     * @brief Tells whether the level passed validation.
     * @return true when the level can be played.
     */
    [[nodiscard]] bool levelIsValid() const;

private:
    PacmanSession session_;

    /// Milliseconds of real time waiting to be turned into logic steps.
    unsigned long accumulator_ = 0;

    /// Frames drawn since the scene started, used for the blinking player.
    unsigned long frameCounter_ = 0;

    /// The direction the player last asked for, latched at frame scope.
    Direction requestedDirection_ = Direction::None;

    void drawMazeWalls(pixelroot32::graphics::Renderer& renderer) const;
    void drawCollectibles(pixelroot32::graphics::Renderer& renderer) const;
    void drawPlayer(pixelroot32::graphics::Renderer& renderer) const;
    void drawEnemies(pixelroot32::graphics::Renderer& renderer) const;
    void drawStatusBand(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draws the screen that belongs to the game's current state.
     *
     * @param renderer The renderer to draw with.
     */
    void drawStateOverlay(pixelroot32::graphics::Renderer& renderer) const;

    /**
     * @brief Draws a band across the maze with a message on it.
     *
     * @param renderer The renderer to draw with.
     * @param title The headline.
     * @param titleColor Colour of the headline.
     * @param subtitle The second line, or nullptr for none.
     */
    void drawMessage(pixelroot32::graphics::Renderer& renderer, const char* title,
                     pixelroot32::graphics::Color titleColor, const char* subtitle = nullptr) const;
};

}  // namespace pacman
