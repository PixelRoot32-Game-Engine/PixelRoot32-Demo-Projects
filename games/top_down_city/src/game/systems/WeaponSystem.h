#pragma once
#include <cstdint>

#include <graphics/Renderer.h>

#include "game/CityConstants.h"
#include "game/rules/Weapon.h"

namespace top_down_city {

/**
 * @brief Holds the equipped weapon, its cooldown and every bullet in the air.
 *
 * The engine-facing half of the gun. Weapon.h owns the numbers and the rules
 * that can be checked on a host; this owns the things that need a position, a
 * collision map and a Renderer.
 *
 * It knows what a wall and a car are, both asked through `collision::`, which
 * every mover in the demo already shares. It deliberately does NOT know what a
 * pedestrian is: a bullet that has reached a person is reported through a
 * function pointer the scene installs -- the same shape as
 * `collision::setDynamicBlocker`, and for the same reason, that the crowd
 * outdoors and the officers indoors are two different objects and neither is
 * the weapon's business.
 *
 * Nothing here allocates. The projectiles are a plain array of
 * `kMaxProjectiles`, swept by an `alive` flag: a projectile is trivially
 * constructible, so an ObjectPool would buy indirection rather than memory.
 *
 * ONE SLOT, and what a second one would cost
 *
 * This class holds ONE `spec_` and ONE `state_`: a single weapon and a single
 * magazine, both overwritten by `equip()`. The consequences mostly show up in
 * the shop. A weapon and its ammunition are the SAME purchase -- there is
 * nowhere to store rounds for a gun that is not in your hands, so
 * `shop::Line::Pistol` is a loaded pistol, priced as the reload. Buying the
 * shotgun DISCARDS whatever was in the pistol, and vice versa. And `equip()` on
 * the weapon already carried is a reload, which is what makes the counter's
 * "already loaded" refusal necessary.
 *
 * **An inventory of several weapons with ammunition counted per weapon** is the
 * shape of the change, and none of it is free:
 *
 *  1. `weapons::State` moves OUT of this class and into the run -- a
 *     `weapons::State ammo[WeaponId::Count]` on CityWorld, 12 bytes of RAM --
 *     three ids, and four bytes each once the `std::uint16_t` pads the
 *     `std::uint8_t` behind it -- so a magazine survives being put down. This
 *     class then holds an index and a pointer rather than owning the state,
 *     which touches every call site of `stateOf()`, `ammo()` and `onFired`.
 *  2. Something has to SWITCH weapons, and the pad's six buttons are every one
 *     spoken for. The counter's own menu is the precedent: a button that is
 *     contextually free somewhere, not a seventh. There is no such press out in
 *     the street, which is the real blocker -- so a switch would most likely
 *     live at the till too, and a weapon you can only change indoors is a
 *     different game from one you cycle mid-chase.
 *  3. The shop's catalogue splits: `shop::Offer` grows an "ammunition for X"
 *     row per weapon alongside the weapon itself, and `Economy.h` a price for
 *     each. Both are table rows rather than code.
 *  4. The HUD's weapon plate shows one gun and its count. Several carried
 *     weapons want either a cycling readout or a wider row, and the column is
 *     already asserted flush against the prompt strip.
 *
 * Written down rather than built because none of it makes the demo teach
 * anything it does not already teach: the economy is legible with one slot, and
 * the second slot's real cost is the input problem in (2), not the bytes.
 */
class WeaponSystem {
public:
    /**
     * Signature of the damage test the scene installs: the projectile's box in
     * world pixels, `damage` to take off whatever is there, and `context`
     * whatever was handed to setTargetSink. True when something was hit, which
     * also stops the projectile.
     */
    using HitFn = bool (*)(void* context, int left, int top, int width,
                           int height, std::uint8_t damage);

    WeaponSystem();

    /// Drop every bullet and reload. Call from the scene's init().
    void reset();

    /// Drop every bullet in flight, keeping the magazine and the cooldown.
    /// For crossing between the city and an interior: a bullet holds world
    /// pixels, and the room's pixels start again at zero, so a round still in
    /// the air would arrive indoors somewhere arbitrary. Separate from
    /// reset() because walking through a door must not refill a magazine.
    void dropProjectiles();

    /// Install (or, with a null function, remove) the target test. Removing
    /// it does not disable the gun: bullets still fly and still stop at
    /// walls, they simply have nobody to hit.
    void setTargetSink(HitFn fn, void* context);

    /// Take a weapon. Until this is called the player has nothing: the demo
    /// opens unarmed and the pistol is walked to, so `fire()` returning false
    /// is the normal state at the start of a run, not an error.
    void equip(weapons::WeaponId id);

    /// Put the weapon down and drop what is in flight.
    void disarm();

    bool armed() const { return spec_ != nullptr; }

    /**
     * Fire, if the weapon is ready. `centreX`/`centreY` are the shooter's
     * centre in world pixels. True when a round actually left the barrel --
     * false on cooldown, on an empty magazine, or when every projectile slot is
     * already busy.
     */
    bool fire(int centreX, int centreY, Facing facing);

    /// One fixed logic step: cool the weapon, fade the flash, and advance
    /// every live bullet through the world.
    void step();

    /// Bullets and muzzle flash, in world coordinates. Drawn by the scene
    /// after the actors, so a tracer passes in front of whoever it is about
    /// to hit rather than behind them.
    void draw(pixelroot32::graphics::Renderer& renderer);

    /// Fingerprint of everything draw() reads. Bullets move every step and
    /// are the only thing on screen that does while the player stands still,
    /// so without this in the frame skip a shot fired from a standstill would
    /// not be drawn at all.
    std::uint32_t visualKey() const { return visualKey_; }

    /// Only valid while armed(). Nothing asks otherwise.
    const weapons::WeaponSpec& spec() const { return *spec_; }
    std::uint16_t ammo() const { return state_.ammo; }

    /// The whole mutable half, for callers that need more than the count --
    /// the scene asks `weapons::isEmpty` on the step a magazine runs out, and
    /// "ammo() == 0" is a different question from "this weapon is empty" for
    /// a magazine that is infinite.
    const weapons::State& stateOf() const { return state_; }
    bool ready() const { return armed() && weapons::canFire(state_); }

private:
    /// One bullet in flight. Position is fixed-point in the same 8-bit scale
    /// as every other mover in the demo, so a projectile and a car agree on
    /// where a pixel is.
    struct Projectile {
        std::int32_t  x;
        std::int32_t  y;
        std::int32_t  stepX;      ///< Sub-pixels per logic step, per axis.
        std::int32_t  stepY;
        std::uint16_t stepsLeft;
        std::uint8_t  damage;
        std::uint8_t  tracerPx;
        bool          alive;
    };

    /// Advance one projectile and decide whether it survives the move.
    bool advance(Projectile& p);

    void refreshKey();

    const weapons::WeaponSpec* spec_;
    weapons::State             state_;
    Projectile                 shots_[kMaxProjectiles];

    HitFn        hitFn_;
    void*        hitContext_;

    /// Where the last shot left the barrel, and how long the flash has left.
    int          flashX_;
    int          flashY_;
    int          flashSteps_;

    std::uint32_t visualKey_;
};

}  // namespace top_down_city
