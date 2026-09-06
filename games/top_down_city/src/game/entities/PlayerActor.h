#pragma once
#include <cstdint>

#include <core/Entity.h>
#include <graphics/Renderer.h>

#include "game/CityConstants.h"
#include "game/rules/Armor.h"

namespace top_down_city {

/**
 * @class PlayerActor
 * @brief The pedestrian: eight-way free movement with per-axis collision
 *        against the exported scene's behaviour layers.
 *
 * Not grid-locked. The authoritative position is a fixed-point pair with 8
 * fractional bits (`x_`, `y_`); `Entity::position` is refreshed from it only
 * so the scene and camera can read a world position through the normal entity
 * interface.
 *
 * `update()` is deliberately empty. The scene drives the actor through
 * `step()` on a fixed 16 ms tick, because movement integrated against raw
 * frame time would run at a different speed on the simulator than on hardware.
 */
class PlayerActor : public pixelroot32::core::Entity {
public:
    PlayerActor();

    /// Place the player on a tile. Only valid before the first step; there is
    /// no un-stick logic if the tile turns out to be solid.
    void spawnAt(int tileX, int tileY);

    /// Place the player at a world pixel (sprite top-left). Used when they
    /// climb out of a car, where the free spot has already been found and
    /// tested by the scene.
    void placeAt(int px, int py, Facing facing);

    /// Advance one fixed logic step from a direction pad state.
    void step(bool up, bool down, bool left, bool right, bool run);

    /// No-op: the scene owns the tick. See the class comment.
    void update(unsigned long deltaTime) override;

    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /// True if the collision box may sit with its sprite top-left at (x, y).
    /// Public because it is where the scene asks whether a spot beside a car
    /// is somewhere the driver could stand.
    ///
    /// `ignore` is a vehicle to leave out of the moving-obstacle test: the
    /// car being climbed out of overlaps every tile beside it, so without
    /// this the driver would be sealed inside their own vehicle.
    static bool canOccupy(int x, int y, const void* ignore = nullptr);

    /// Which way the figure is pointing: the direction a shot leaves in, and
    /// what the scene records at a doorway so the walk back out onto the
    /// street faces the same way it went in.
    Facing facing() const { return facing_; }

    /// Show the figure holding the pistol. The armed frames are the same nine
    /// walk frames with the gun stamped in (see tools/art_player.py), so this
    /// changes the sprite without changing the animation: the walk cycle,
    /// the facing and the phase all carry across untouched.
    void setArmed(bool armed);
    bool isArmed() const { return armed_; }

    /// Take `damage` hit points, through the vest if one is being worn.
    /// Every way of being hurt funnels through here -- rounds, pellets and
    /// bumpers alike -- which is the only reason the vest is one call rather
    /// than one per source.
    /// @return true when this hit was the one that put the player down.
    bool hit(std::uint8_t damage);

    /// Put a bought vest on. The counter's one non-weapon line; the arithmetic
    /// and the reason it SETS rather than stacks are in game/rules/Armor.h.
    void wearVest();

    /// Points of armour left. Drawn beside the hit points, because a number
    /// the player cannot see is a purchase they cannot spend.
    std::uint8_t armor() const { return vest_.points; }

    /**
     * @brief Run down by a car.
     *
     * Its own entry point and its own clock, because a bumper is not a bullet
     * -- see kCarImpactCooldownSteps in CityConstants.h for why the two
     * invulnerability windows cannot be one.
     *
     * @return true when the impact landed rather than being absorbed by the
     *         recovery window -- which is what decides whether it is worth
     *         telling the player about.
     */
    bool hitByVehicle();

    /// Steps of recovery left after being hit by a car. Ticked by step().
    std::uint8_t impactCooldown() const { return impactCooldown_; }

    /// Back to full, and out of the vest. Called on the respawn that follows
    /// going down -- there is no menu for this demo to go over to. The vest
    /// goes with the weapon deliberately: an arrest already costs the gun, the
    /// streak and the walk back, and armour that survived the cells would be
    /// the one purchase a lost fight cannot take. The cash still survives --
    /// see game/rules/Economy.h for why that one is different.
    void heal();

    std::uint8_t health() const { return health_; }
    bool isDown() const { return health_ == 0; }

    /// World pixel of the sprite's top-left -- what the scene needs to work
    /// out whether a car is close enough to get into.
    int spriteX() const { return x_ >> kSubPixelShift; }
    int spriteY() const { return y_ >> kSubPixelShift; }

    int centreX() const;
    int centreY() const;
    int tileX() const;
    int tileY() const;

    /// Fingerprint of everything draw() reads: whole-pixel position, facing and
    /// walk frame. Two steps with the same key produce an identical sprite at
    /// an identical place, which is what lets the scene skip a frame entirely.
    /// Sub-pixel motion is deliberately NOT in the key -- it changes nothing on
    /// screen until it crosses a whole pixel.
    std::uint32_t visualKey() const;

private:
    /// Move one axis and roll the move back if the destination is blocked.
    /// Splitting the axes is what produces sliding along a wall instead of
    /// stopping dead on a diagonal.
    void moveAxisX(std::int32_t delta);
    void moveAxisY(std::int32_t delta);

    void syncEntityPosition();

    std::int32_t  x_;          ///< Sprite top-left, fixed-point (8 fraction bits).
    std::int32_t  y_;
    Facing        facing_;
    std::uint32_t animTravelSub_;  ///< Distance walked since the last frame flip.
    std::uint8_t  animPhase_;      ///< Index into kWalkCycle.
    bool          moving_;
    bool          armed_;
    std::uint8_t  health_;
    armor::Vest   vest_;             ///< see hit()
    std::uint8_t  impactCooldown_;   ///< see hitByVehicle()
};

}  // namespace top_down_city
