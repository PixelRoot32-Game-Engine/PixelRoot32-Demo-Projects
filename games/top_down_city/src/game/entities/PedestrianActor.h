#pragma once
#include <cstdint>

#include <core/Entity.h>
#include <graphics/Renderer.h>
#include <math/MathUtil.h>

#include "game/CityConstants.h"

namespace top_down_city {

/**
 * @brief What a round or a bonnet did to whoever was standing there.
 *
 * Returned by the crowd's hit tests rather than a bare bool: a grazed civilian
 * and a dead officer are two very different amounts of trouble (see
 * game/rules/Wanted.h). `officer` is true when at least one of the people
 * involved wore a uniform, so a car that takes out a bystander and a policeman
 * in the same step reports the worse of the two.
 */
struct PersonHit {
    bool any;       ///< somebody was there
    bool killed;    ///< and they went down
    bool officer;   ///< at least one of them was police
};

/**
 * @class PedestrianActor
 * @brief One of the people in the street: wanders, gets in the way, and can
 *        be run over.
 *
 * Pooled, never owned outright -- only the dozen near the camera exist at any
 * moment (see PedestrianPool), constructed where they will be seen and
 * destroyed once left behind, which is why a pedestrian carries no identity
 * beyond a tint and a position.
 *
 * It costs no sprite data at all: the frames ARE the player's, drawn through a
 * different SPRITE PALETTE SLOT -- one 32-byte palette per tint instead of a
 * nine-frame sheet per person. The squashed pose is the only new artwork.
 *
 * Collision is the cheap whole-tile test rather than the player's per-pixel
 * loop: a dozen of these running it would not fit the ESP32's frame budget,
 * and nobody watching a pedestrian cares that they gave a lamp post a wider
 * berth than the player does.
 */
class PedestrianActor : public pixelroot32::core::Entity {
public:
    /// @param px,py  World pixel of the sprite's top-left.
    /// @param tint   Index into PEDESTRIAN_PALETTES; picks the palette slot.
    /// @param seed   Wander noise. Distinct per pedestrian, or a crowd walks
    ///               in formation.
    /// @param officer  An officer answers gunfire by standing and shooting
    ///                 where a civilian answers it by running. The uniform
    ///                 palette is the caller's business.
    PedestrianActor(int px, int py, std::uint8_t tint, std::uint32_t seed,
                    bool officer = false);

    /// Advance one fixed logic step: wander, or count down the body's stay.
    void step();

    /// No-op: the pool owns the tick, on the scene's fixed step.
    void update(unsigned long deltaTime) override;

    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /// Run this one over. Idempotent, so a car passing over a body changes
    /// nothing -- which is also what keeps the lingering timer honest.
    ///
    /// Deliberately not expressed as a large amount of damage: being hit by a
    /// car is not a quantity, and giving it one would mean deciding how much
    /// of a person a bumper at 31 px/s is worth.
    void squash();

    /// Take `damage` hit points. Kills -- and squashes, because the flattened
    /// pose is the only art for somebody who has stopped walking.
    /// @return true when this hit was the one that put them down.
    bool hit(std::uint8_t damage);

    /// Take fright at something `offset` pixels away -- FROM the threat TO
    /// this pedestrian, which is what threat::awayFrom expects. Re-armed
    /// rather than accumulated if it happens again. A no-op on a body.
    void startle(int offsetX, int offsetY);

    /// Answer gunfire. A civilian runs; an officer turns to face the shot and
    /// stays hostile for kOfficerAlertSteps. One call site for both, because
    /// the crowd does not sort itself before reacting.
    void alarm(int offsetX, int offsetY);

    /// Turn to face a target and report whether a shot is worth taking from
    /// here -- in range, and lined up on the axis being fired along.
    /// @param offsetX,offsetY  FROM this officer TO the target.
    /// @param rangePx  The equipped weapon's own range, passed in rather than
    ///                 copied here: the number already exists in the weapon
    ///                 table, and a second copy is a second thing to forget.
    bool takeAim(int offsetX, int offsetY, int rangePx);

    /**
     * @brief Close on a target until the shot is on, then hold.
     * @param offsetX,offsetY  FROM this officer TO the target, world pixels.
     * @param rangePx          How far they are willing to come. See
     *                         wanted::pursuitRangePx.
     * @param chaseSpeedSub    The gait for this chase, from
     *                         wanted::chaseSpeedSub. Handed over because the
     *                         star level is the caller's business, and an
     *                         actor that read it would have to include the
     *                         counter.
     * @return true when the target is inside that range, chased or not.
     *
     * Three states, and the middle one is the whole feature. Out of the
     * weapon's line, they step ACROSS it (threat::intoLine) -- closing the
     * distance instead walks the entire way to the player without ever lining
     * up. In the line and in range, they STAND: a four-way aim fired while
     * walking is an aim walking out of its own line. Beyond the weapon but
     * lined up, they come down the street at you. Being close enough to be
     * chased is being alerted by it, so an officer who spawns into a manhunt
     * joins it rather than waiting to be shot at.
     */
    bool pursue(int offsetX, int offsetY, int rangePx, int weaponRangePx,
                std::int32_t chaseSpeedSub);

    /// An officer who is currently hostile. Two exemptions hang off this: a
    /// policeman on a chase moves at his own speed, and does not wait at the
    /// kerb for a crossing.
    bool isChasing() const { return officer_ && alertSteps_ > 0 && !squashed_; }

    bool isPanicking() const { return panicSteps_ > 0; }
    bool isOfficer() const { return officer_; }
    /// Which way they are pointing. The pool needs it to fire from an
    /// officer: the shot leaves along the frames they are drawn in.
    Facing facing() const { return facing_; }
    bool isAlert() const { return alertSteps_ > 0; }

    bool isSquashed() const { return squashed_; }

    /// True once the body has lain in the road long enough for the slot to be
    /// worth more to somebody still walking.
    bool hasFaded() const { return squashed_ && lingerSteps_ <= 0; }

    /// Collision box, the player's -- a pedestrian is the same figure.
    int boxLeft() const { return (x_ >> kSubPixelShift) + kPlayerBoxOffsetX; }
    int boxTop() const { return (y_ >> kSubPixelShift) + kPlayerBoxOffsetY; }
    static constexpr int boxWidth() { return kPlayerBoxWidth; }
    static constexpr int boxHeight() { return kPlayerBoxHeight; }

    int spriteX() const { return x_ >> kSubPixelShift; }
    int spriteY() const { return y_ >> kSubPixelShift; }
    int centreX() const { return spriteX() + kPlayerSpriteW / 2; }
    int centreY() const { return spriteY() + kPlayerSpriteH / 2; }

    std::uint32_t visualKey() const;

private:
    /// @param allowIdle  false while panicking: one roll in four is normally
    ///                   a pause, and somebody who stops dead in the middle
    ///                   of running away is worse than somebody who never ran.
    void chooseDirection(bool allowIdle = true);
    void moveAxisX(std::int32_t delta);
    void moveAxisY(std::int32_t delta);
    bool canOccupy(int px, int py) const;

public:
    /**
     * @brief May somebody stand here without standing in traffic?
     *
     * True off the carriageway, and true on a marked crossing. Sprite
     * top-left coordinates, resolved through the tile the person's MIDDLE is
     * on: a 10x6 box straddles two tiles most of the time, and "half on the
     * kerb" is not a decision this rule needs an opinion about.
     *
     * Always true indoors: the room is a 15x15 space with its own origin, so
     * its coordinates land on the north-west corner of the city and the lane
     * table would answer about a street a mile away.
     */
    static bool isPavement(int spriteX, int spriteY);

private:
    /**
     * @brief Is this step allowed by the kerb rule?
     *
     * Stepping ONTO the carriageway needs a crossing. Stepping off it, or
     * along it, never does -- a rule that also held somebody in the road
     * would strand anybody a car had shoved into a lane, and would freeze a
     * pedestrian the moment their panic ran out halfway across one.
     *
     * Panic ignores this entirely; see step().
     */
    static bool respectsKerb(int fromX, int fromY, int toX, int toY);
    void syncEntityPosition();

    std::int32_t  x_;
    std::int32_t  y_;
    /// Per-pedestrian rather than the global PRNG: two people spawned in the
    /// same step must not share a stream, or they wander in lockstep.
    pixelroot32::math::Random rng_;
    std::uint32_t animTravelSub_;
    int           holdSteps_;      ///< Steps left on the current direction.
    int           lingerSteps_;    ///< Steps left lying in the road.
    int           panicSteps_;     ///< Steps left running from a fright.
    int           alertSteps_;     ///< Steps left hostile, officers only.
    /// The gait of the chase currently on. Set by pursue() and read by
    /// step() a step later, so an officer does not slow down mid-stride
    /// because a star fell off somewhere across the city.
    std::int32_t  chaseSpeedSub_;
    int           stuckSteps_;     ///< Steps spent going nowhere; see
                                   ///< kPedKerbGiveUpSteps.
    std::int8_t   dirX_;
    std::int8_t   dirY_;
    Facing        facing_;
    std::uint8_t  tint_;
    std::uint8_t  health_;
    std::uint8_t  animPhase_;
    bool          squashed_;
    bool          officer_;
};

}  // namespace top_down_city
