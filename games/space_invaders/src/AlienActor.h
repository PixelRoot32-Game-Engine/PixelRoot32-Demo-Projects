#pragma once
#include "core/Actor.h"
#include "graphics/Renderer.h"
#include "rules/RulesConstants.h"

namespace spaceinvaders {

/**
 * @class AlienActor
 * @brief Enemy alien whose position and animation frame are owned by the rules rack.
 *
 * The formation is a rules concern: one live cell moves per 60 Hz tick, the
 * whole rack shares a two-frame animation that flips once per completed sweep,
 * and the rack freezes while an explosion plays. This actor therefore only
 * mirrors state that SpaceInvadersScene copies out of rules::Rack. It never
 * advances its own animation and never decides where the formation goes, so
 * there is exactly one march clock in the game.
 */
class AlienActor : public pixelroot32::core::Actor {
public:
    /**
     * @brief Constructs an alien at the given position.
     *
     * Dimensions come from the rules layer rather than a second copy of the
     * art numbers, so the hit box and the edge sweep can never disagree.
     *
     * @param position World position (top-left).
     * @param type Rack variant; selects the sprite pair, the collision size and the score.
     */
    AlienActor(pixelroot32::math::Vector2 position, rules::AlienType type);

    /**
     * @brief No-op; position and frame are written by SpaceInvadersScene.
     * @param deltaTime Unused.
     */
    void update(unsigned long deltaTime) override;

    /**
     * @brief Renders the alien using the type-specific sprite animation.
     * @param renderer Reference to the renderer.
     */
    void draw(pixelroot32::graphics::Renderer& renderer);

    /**
     * @brief Moves the alien by a delta without advancing the animation.
     *
     * The frame follows rules::Rack::animationFrame(), which flips once per
     * sweep; stepping it on every move would desynchronise the sprites from the
     * rules clock.
     *
     * @param dx Horizontal delta.
     * @param dy Vertical delta.
     */
    void move(pixelroot32::math::Scalar dx, pixelroot32::math::Scalar dy);

    /**
     * @brief Sets the sprite frame from the rack's animation index.
     * @param frame Frame index from rules::Rack::animationFrame(), normally 0 or 1.
     */
    void setAnimationFrame(int frame);

    pixelroot32::core::Rect getHitBox() override;
    void onCollision(pixelroot32::core::Actor* other) override;

    /**
     * @brief Returns true if the alien is alive and should be processed.
     * @return true while the mirrored rack cell is alive.
     */
    bool isActive() const { return active; }

    /**
     * @brief Mirrors the rack cell's alive flag onto the actor.
     *
     * Kept as an explicit setter because the rack owns liveness: the scene
     * copies the cell state into the actor instead of the actor deciding it,
     * and spawning a wave reuses the same call for the alive case.
     *
     * @param alive true while the alien occupies a live cell.
     */
    void setAlive(bool alive) { active = alive; }

    /**
     * @brief Convenience form of setAlive(false) for a resolved hit.
     */
    void kill() { setAlive(false); }

    /**
     * @brief Returns the score awarded when this alien is destroyed.
     * @return Points from the rules table for this variant.
     */
    int getScoreValue() const;

private:
    rules::AlienType type;                        ///< Alien variant, fixed for the run
    bool active;                                  ///< Mirrors the rack cell's alive flag
    pixelroot32::graphics::SpriteAnimation animation; ///< Two-frame sprite pair
};

}  // namespace spaceinvaders
