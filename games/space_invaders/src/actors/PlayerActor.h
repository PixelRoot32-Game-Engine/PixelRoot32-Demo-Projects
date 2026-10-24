#pragma once
#include "physics/KinematicActor.h"
#include "graphics/Renderer.h"

namespace spaceinvaders {

/**
 * @class PlayerActor
 * @brief Player-controlled cannon with continuous arcade movement (S7, D2).
 *
 * Coordinate Model:
 * The cannon stores screen coordinates like every other entity, because its
 * box, its shots and the enemy bullets all live in that space. Only the rule
 * is playfield-relative: the bound comes from rules::clampCannonX, so the
 * field edge is FIELD_X + clampCannonX(x - FIELD_X) and the ship can reach the
 * first and last column of the playfield but never the letterbox. One rules
 * tick is one pixel, which is the step that aiming at a two-pixel alien column
 * needs, and the tick itself is the clock (S20): there is no easing, no lane
 * target and no deltaTime arithmetic left to drift.
 *
 * Architecture Pattern:
 * KinematicActor is kept because the solver detects the cannon's box without
 * ever moving it; the scene applies damage. That separation between actor
 * behaviour and game rules is the same one the old grid version used.
 *
 * Collision Integration:
 * AABB box from GameConstants, so the drawing size and the collision size stay
 * the single pair of numbers.
 */
class PlayerActor : public pixelroot32::physics::KinematicActor {
public:
    /**
     * @brief Constructs the cannon at the given position, clamped into the field.
     *
     * The position is no longer quantised to a lane: a caller that asks for a
     * pixel between two former grid cells gets exactly that pixel.
     *
     * @param position Screen position; x is clamped to the playfield, so the
     *        actor can sit at FIELD_X but never left of it.
     */
    PlayerActor(pixelroot32::math::Vector2 position);

    /**
     * @brief Steps the cannon one pixel in the held direction for one 60 Hz tick.
     *
     * Both direction buttons or neither moves nothing, so a player cannot drift
     * diagonally into a wall. deltaTime is unused on purpose: the scene calls
     * this exactly once per rules tick, so the tick is the clock (S20) and no
     * frame rate can stretch a movement.
     *
     * @param deltaTime Ignored; present only to satisfy Entity::update().
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Renders the cannon at its screen position, coloured by its band (S3).
     *
     * The actor already stores the screen column, so the draw adds no offset:
     * the clamp is the only place that knows about the playfield origin.
     *
     * @param renderer Reference to the renderer.
     */
    void draw(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief Collision callback - Scene handles damage logic
     *
     * Design Note: PlayerActor only detects collisions. Damage application,
     * life reduction, and game state changes are managed by SpaceInvadersScene
     * to maintain separation of concerns between actor behavior and game rules.
     *
     * @param other The actor that was hit.
     */
    void onCollision(pixelroot32::core::Actor* other) override;

    /**
     * @brief Returns true if fire button is currently held
     * Used for continuous fire rate limiting
     *
     * @return true while the fire button is down.
     */
    bool isFireDown() const;

    /**
     * @brief Returns true if fire button was pressed this frame
     * Used for single-shot actions (Edge detection)
     *
     * @return true on the tick the fire button goes down.
     */
    bool wantsToShoot() const;

private:
    bool isAlive;   ///< False after a hit, so the death frame never draws the ship
};

}  // namespace spaceinvaders
