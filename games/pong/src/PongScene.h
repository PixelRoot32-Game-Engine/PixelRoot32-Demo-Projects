/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <cstdint>

#include <core/Scene.h>
#include <graphics/Camera2D.h>

#include "game/PongRules.h"
#include "presentation/PongEffects.h"

namespace pong {

/**
 * @class PongScene
 * @brief The local arcade Pong match: one player against the CPU.
 *
 * This class is the composition root of the demo and nothing more. The match
 * itself lives in `game::Rules`, held by value as a member; the CPU opponent is
 * already inside those rules, so the scene never moves the right paddle and has
 * no second input path to keep in sync. Reading the D-pad, turning the rules'
 * events into sound and particles, and painting the court are the whole of its
 * job.
 *
 * The court advances on a fixed 60 Hz step. `update()` measures the wall clock,
 * adds the elapsed time to an accumulator and replays `kStepDtUs` slices of it,
 * so a slow frame cannot change how far the ball travelled or how far the
 * paddles moved: the same sequence of steps always replays the same match. The
 * frame's own delta never reaches the simulation, only the accumulator does.
 *
 * The court is drawn outside the entity pass. `PongScene` owns no entities: the
 * ball, the paddles, the HUD and the overlay are painted straight from the rules
 * by `presentation::CourtRenderer`, in the order the repo requires (background,
 * entities, actors, HUD, overlay). `Scene::draw()` is still called, because it is
 * the base contract, but it has nothing to iterate over -- and because the
 * renderer helpers are stateless, the court can be interleaved with it without
 * an offset or a palette leaking from one pass into the other.
 */
class PongScene : public pixelroot32::core::Scene {
public:
    PongScene();

    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /**
     * @brief Fires the feedback for a paddle contact.
     * @param player 0 for the player's paddle, 1 for the CPU's.
     */
    void playPaddleHit(std::uint8_t player);

    /// @brief Fires the feedback for a top or bottom wall contact.
    void playWallBounce();

    /**
     * @brief Fires the feedback for a point.
     * @param scorer 0 when the player scored, 1 when the CPU did.
     */
    void playGoal(std::uint8_t scorer);

    /// @brief Fires the heavier feedback that closes the match.
    void playMatchPoint();

    /**
     * @brief Reads which side was awarded the point the phase edge reports.
     * @return 0 for the player, 1 for the CPU.
     *
     * Prefers the ball's position, which is still the edge it left the court
     * through on the frame the point is awarded, and falls back to the score
     * when the ball has already been re-centred.
     */
    [[nodiscard]] std::uint8_t goalScorer() const;

    /**
     * @brief Restores the score band after it rode a shaken pass.
     * @param renderer Renderer to paint with.
     */
    void repaintHudBand(pixelroot32::graphics::Renderer& renderer);

    /// Authoritative match state; the CPU plays slot 1 from inside it.
    game::Rules rules;

    /// Bursts and shake, kept out of the class so the particle member stays in
    /// the effect module where its feature gate lives.
    presentation::PongEffects effects;

    /// Camera used only to push and restore the shake offset around the court
    /// pass; it never scrolls, because the court fits its canvas.
    pixelroot32::graphics::Camera2D shakeCamera;

    /// Wall clock of the previous `update()`, the accumulator's time base.
    std::uint32_t lastUpdateMs = 0;

    /// Microseconds owed to the simulation but not stepped yet.
    std::uint32_t accumulatorUs = 0;

    /// Phase seen on the previous frame, so the edges can be detected.
    game::Phase lastPhase = game::Phase::Countdown;
};

}  // namespace pong
