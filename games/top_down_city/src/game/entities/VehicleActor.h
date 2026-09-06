#pragma once
#include <cstdint>

#include <core/Entity.h>
#include <graphics/Renderer.h>

#include "game/CityConstants.h"

namespace top_down_city {

/// A car's collision box in world pixels -- what `VEHICLE_BOXES` becomes once
/// a pose is applied to it. A struct rather than four out-parameters, because
/// every caller wants all four and none of them should have to remember the
/// order the generator emits them in.
struct VehicleBox {
    int left;
    int top;
    int width;
    int height;
};

/**
 * @class VehicleActor
 * @brief A car: parked scenery until the player gets in, then the thing the
 *        camera follows.
 *
 * Cars are not map data. A tile cannot move and the exported layers live in
 * flash, so the generator emits a spawn table (`city_scene::VEHICLE_SPAWNS`)
 * and every record in it becomes one of these.
 *
 * Four headings, not free rotation: the art is one 16x16 drawing rotated by
 * the generator, so a heading change is a turn and it costs speed. It also
 * keeps the collision box to two axis-aligned rectangles instead of a polygon.
 *
 * Like PlayerActor, `update()` is empty: the scene drives every car through
 * `step()` on the same fixed 16 ms tick, so a car covers the same ground per
 * second on a 200 fps simulator and a 25 fps panel.
 */
class VehicleActor : public pixelroot32::core::Entity {
public:
    VehicleActor();

    /// Park this car where the generator put it.
    void spawn(const city_scene::VehicleSpawn& record);

    /// Park this car on a tile, facing a way, in a colour. What `spawn` does
    /// from a generated record, for the traffic pool, which chooses its own.
    void spawnAt(int tileX, int tileY, std::uint8_t heading,
                 std::uint8_t color);

    /**
     * @brief Park this car exactly where another one stands -- same pixels,
     *        same heading -- keeping its own paint.
     *
     * The heading travels with the position because the box swaps its axes
     * with it: a car that inherited the pixels alone would sit in a box the
     * other car never proved was clear. The colour stays -- it is this car's
     * radio station (AudioCues.h) and the only thing distinguishing it from
     * its neighbours. The one caller is CityScene::takeContract, swapping a
     * parked car off the boost car's home tile; read its argument for why
     * that destination is known good.
     */
    void parkWhere(const VehicleActor& other);

    /**
     * @brief The box a car WOULD occupy standing at this pose.
     *
     * Static, and the only place the pose-to-box arithmetic is written: the
     * offsets are per-heading -- the art is not centred in its cell -- so a
     * hand-spelled lookup is a chance to read the wrong row. Callers are the
     * ones asking about a car that is not there yet (a turn not taken, a step
     * not moved into, a spawn not happened), which is exactly when boxLeft()
     * and friends cannot answer.
     */
    static VehicleBox boxFor(int spriteX, int spriteY, std::uint8_t heading);

    /// The box a spawn record puts a car in. Records are in tiles and a car's
    /// sprite is exactly one tile, so this is the tile-to-pixel step and
    /// nothing more.
    static VehicleBox boxFor(const city_scene::VehicleSpawn& record);

    /// Advance one fixed logic step from a direction pad state. A parked car
    /// returns immediately -- which is what makes simulating all of them at
    /// once affordable.
    void step(bool up, bool down, bool left, bool right);

    /**
     * @brief Advance one step under someone else's steering.
     *
     * A separate entry point rather than a synthesised d-pad, because the two
     * want opposite things from the physics: the player's car integrates a
     * throttle and stops wherever the arithmetic leaves it, while a traffic
     * car must be able to turn at a junction -- and a 90-degree turn is only
     * safe on a whole tile. So this runs at a constant speed that divides a
     * tile exactly (asserted in CityConstants.h).
     *
     * @param desiredHeading  Where to point. Adopted only on a tile boundary,
     *                        and only if the box fits that way round.
     * @param go              False to hold position -- something ahead.
     */
    void driveAutonomous(std::uint8_t desiredHeading, bool go);

    /// True when the sprite sits exactly on its tile, which is the only place
    /// a self-driving car may turn.
    bool isTileAligned() const;

    /// No-op: the scene owns the tick. See the class comment.
    void update(unsigned long deltaTime) override;

    void draw(pixelroot32::graphics::Renderer& renderer) override;

    bool isDriven() const { return driven_; }

    /// Hand the car over to the player, or take it back. Releasing it also
    /// stops it dead: an abandoned car does not coast away down the street.
    void setDriven(bool driven);

    /// Collision box in world pixels. Swaps its axes with the heading,
    /// because the art does.
    int boxLeft() const;
    int boxTop() const;
    int boxWidth() const;
    int boxHeight() const;

    bool overlapsBox(int left, int top, int width, int height) const;

    /// True when the box is within `range` pixels of the given box -- the
    /// test for "close enough to open the door".
    bool isWithinRangeOf(int left, int top, int width, int height,
                         int range) const;

    int spriteX() const { return x_ >> kSubPixelShift; }
    int spriteY() const { return y_ >> kSubPixelShift; }
    int centreX() const { return spriteX() + kVehicleSpriteW / 2; }
    int centreY() const { return spriteY() + kVehicleSpriteH / 2; }
    int tileX() const { return centreX() / kTilePx; }
    int tileY() const { return centreY() / kTilePx; }

    /// True when the car is moving fast enough to run somebody over rather
    /// than nudge them.
    bool isLethal() const;

    /**
     * @brief The speed this car lost the last time it stopped dead against
     *        something, in sub-pixels -- read-and-cleared.
     *
     * Zero when it has not hit anything since the last read: an ordinary stop
     * never touches it, only `advance()`'s collision branch does. Polled by
     * the scene rather than pushed as a callback, because an actor that knows
     * what an AudioDirector is has learned the wrong thing.
     */
    std::int32_t consumeCrash();

    /// This car's throttle as of its last `step()` -- positive is forward,
    /// negative brakes then reverses. Set only by the player-driven path;
    /// autonomous traffic has no pedal to sound (ADR-19). The scene diffs it
    /// through `audio_cues::pedalEventFor` to find the EDGE, never in here.
    std::int8_t throttleState() const { return throttle_; }

    /// Sub-pixels per fixed logic step, signed. The second half of
    /// `pedalEventFor`'s input, and a read-only peek unlike `consumeCrash`:
    /// speed is a value to gate an event against, not an event.
    std::int32_t speedSub() const { return speedSub_; }

    std::uint8_t heading() const { return heading_; }

    /// Row of kVehicleSprites this car is painted -- also `radioFor`'s only
    /// input (AudioCues.h): the station a car plays is a deterministic
    /// function of its paint, so the scene needs to read it back.
    std::uint8_t color() const { return color_; }

    /// True when the car's box would be clear if it faced this way from where
    /// it stands. A turn swaps the box's axes -- 10x13 becomes 13x10 -- so a
    /// car in a narrow gap can ask for a heading that does not fit, and the
    /// answer has to be no rather than a car overlapping a wall. Public
    /// because the traffic driver asks before it commits: a turn it cannot
    /// take is a junction it should drive straight through, not a stall in the
    /// middle of the crossroads.
    bool canFace(std::uint8_t heading) const;

    /// Fingerprint of everything draw() reads, for the scene's frame skip.
    std::uint32_t visualKey() const;

private:
    /// Turn to a new heading, paying the cornering speed penalty.
    void turnTo(std::uint8_t heading);

    /// Move along the current heading and stop dead against whatever is in
    /// the way. Only one axis is ever involved, because there are only four
    /// headings.
    void advance(std::int32_t delta);

    void syncEntityPosition();

    std::int32_t  x_;        ///< Sprite top-left, fixed-point (8 fraction bits).
    std::int32_t  y_;
    std::int32_t  speedSub_; ///< Signed: negative is reverse.
    std::uint8_t  heading_;  ///< city_scene::VehicleHeading
    std::uint8_t  color_;    ///< Row of kVehicleSprites.
    std::uint8_t  turnCooldown_;
    bool          driven_;
    std::int32_t  crashSpeedSub_;   ///< see consumeCrash()
    std::int8_t   throttle_;        ///< see throttleState()
};

}  // namespace top_down_city
