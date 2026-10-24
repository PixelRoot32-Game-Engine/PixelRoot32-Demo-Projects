#pragma once
#include <cstdint>

#include "graphics/Renderer.h"
#include "physics/StaticActor.h"

namespace spaceinvaders {

/**
 * @class SaucerActor
 * @brief The mystery ship: a 16x7 white mask crossing the saucer row (S11).
 *
 * Architecture Note:
 * The scene owns exactly one of these as a plain member and never registers it
 * with addEntity() nor allocates it from the arena. Those two hold the wave's
 * actors for a whole run, while this object is a single transient that respawns
 * at most every 1536 ticks; making it an entity would spend a permanent list
 * slot and an arena block on something that is absent most of the time. The
 * scene therefore steps and draws it explicitly.
 *
 * Motion:
 * The crossing is one fixed rules::SaucerStepX step per rules tick rather than
 * a velocity, so the saucer covers the field in the same number of ticks at any
 * frame rate (S20) and the same seed and input script reproduce every side.
 */
class SaucerActor : public pixelroot32::physics::StaticActor {
public:
    /**
     * @brief Builds the actor inactive, parked on the saucer row.
     *
     * It is a StaticActor because nothing integrates it: the rules tick moves
     * it, and it never takes part in a physics response, only in the scene's
     * swept hit test.
     */
    SaucerActor();

    /**
     * @brief Returns the saucer to its start-of-run state.
     *
     * A reset run starts with no saucer on screen and no side inherited from
     * the previous run, so the next spawn() decides both again.
     */
    void reset();

    /**
     * @brief Starts a crossing from the chosen side.
     *
     * Both entry positions put the whole mask outside the playfield, so the
     * first visible pixel appears on the following tick instead of popping in
     * mid-row.
     *
     * @param direction +1 to cross rightwards, -1 to cross leftwards. Any other
     *        value is treated as a rightward crossing.
     */
    void spawn(int8_t direction);

    /**
     * @brief Advances the crossing by one rules tick.
     *
     * Deactivates once the whole mask has crossed the opposite edge, which is
     * also the moment the scene stops the siren.
     *
     * @param deltaTime Frame delta in ms. Unused: the tick is the clock (S20).
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Draws the mask at its current position.
     *
     * S11 fixes the saucer as white even though it flies through the red band,
     * so no band tint is applied here, unlike the cannon and the shots (S3).
     *
     * @param renderer Renderer to draw into.
     */
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /**
     * @brief Takes the saucer off the panel after a player hit.
     *
     * The scene owns the score and the popup, so a hit only ends the crossing.
     */
    void collect();

    /**
     * @brief Reports whether a crossing is in progress.
     * @return true while the mask is on its way between the two edges.
     */
    bool isActive() const;

    /**
     * @brief Side the current crossing travels towards.
     * @return +1 rightwards, -1 leftwards.
     */
    int8_t direction() const;

private:
    bool active = false;         ///< True while the mask is crossing.
    int8_t travelDirection = 1;  ///< +1 rightwards, -1 leftwards.
};

}  // namespace spaceinvaders
