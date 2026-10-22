#pragma once

#include <memory>

#include <core/Scene.h>
#include <graphics/Renderer.h>

#include "rules/PacmanDirection.h"
#include "rules/PacmanSession.h"

namespace pacman {

namespace audio {
class PacmanAudio;
}

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
 * itself. It is also the only place that talks to the audio: the rules
 * report what happened, and the scene asks for the matching sound.
 */
class PacmanScene : public pixelroot32::core::Scene {
public:
    PacmanScene();

    /// @brief Declared out of line: the audio facade is only forward declared
    ///        here, and the unique_ptr needs the complete type to destroy it.
    ~PacmanScene() override;

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

    /// Whether the board was frozen for a respawn the last time it was
    /// checked; the background music follows that beat.
    bool wasRespawning_ = false;

    /// Whether the title screen was on the last time it was checked; the
    /// attract-mode jingle follows that one.
    bool wasOnTitleScreen_ = false;

    /// The audio facade, created once the engine is up.
    std::unique_ptr<audio::PacmanAudio> audio_;

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
     * @brief Plays the sounds for the events the rules reported.
     */
    void playReportedEvents();

    /**
     * @brief Keeps the theme in step with the "ready" beat.
     *
     * The theme plays loud while the board is frozen for a respawn and
     * drops to the quiet loop once the player is allowed to move.
     */
    void updateTheme();

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

    /**
     * @brief The row the messages are drawn on.
     *
     * It is the row the player starts on, the way the arcade puts "READY!"
     * under the ghost house.
     *
     * @return The band's top edge, in pixels.
     */
    [[nodiscard]] int messageBandY() const;
};

}  // namespace pacman
