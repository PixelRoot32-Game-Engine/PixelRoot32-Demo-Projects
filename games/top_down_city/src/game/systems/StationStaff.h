#pragma once
#include <cstdint>

#include <gameplay/ObjectPool.h>
#include <graphics/Renderer.h>

#include "game/CityConstants.h"
#include "game/entities/PedestrianActor.h"
#include "game/systems/WeaponSystem.h"

namespace top_down_city {

/**
 * @brief The officers on duty inside the police station.
 *
 * The same figure as everybody else in this demo: an officer is a
 * PedestrianActor drawn through one more sprite palette slot -- the police
 * uniform, `kPoliceTint` -- so the whole staff costs 32 bytes of colour and not
 * one pixel of new sprite data. Nine frames of walk cycle serve the player, the
 * crowd and the force.
 *
 * A pool rather than a plain array because PedestrianActor has no default
 * constructor -- a pedestrian is built where it will be seen.
 * `ObjectPool::acquire` forwards its arguments to that constructor over storage
 * this object already owns, so the officers are placement-new'd once at scene
 * init and nothing allocates.
 *
 * Unlike the street crowd, nobody is ever streamed in or out: the room is one
 * screen, the officers are always on it, and the count is whatever the map
 * declared. They are stepped only while the player is inside -- outside, the
 * whole room is off in a coordinate space nothing is looking at.
 */
class StationStaff {
public:
    StationStaff();

    /// Place everybody at the posts the map declared, whoever is already
    /// standing there. Call from the scene's init(), after Scene::init() -- the
    /// same contract PedestrianPool has, and for the same reason: resetState()
    /// rewinds the arena without running destructors. Clears the mark, which is
    /// the point rather than a side effect; `markMask_` says why.
    void deploy();

    /**
     * @brief Deploy, but only if nobody has yet this RUN.
     *
     * The station's entry point. Once per run and not once per visit, which is
     * what the room did as a mode of the city scene: deploying on every entry
     * is the one-line version, and it makes the lobby a respawning shooting
     * gallery.
     *
     * The flag lives here rather than in the scene because both callers are
     * about the STAFF and the pair only works if they share it. Chapter 2 calls
     * `deploy()` from the CITY, on the step the phone is answered, and the
     * player may never have been inside -- so with the flag on the scene their
     * first entry would deploy again and clear the mark just handed out: a
     * chapter with nobody marked, parked in `ToTarget` until the clock runs
     * out, indistinguishable while it happens from the bug it was written to
     * prevent.
     */
    void deployOnce();

    /// Name one officer by his row in `police_station::OFFICERS`: the man
    /// chapter 2 was sent here for. Out of range marks nobody, which the
    /// static_assert in CityConstants.h is what keeps unreachable. Call it
    /// after `deploy()`, never before -- deploying clears the mark, and
    /// `markMask_` says why that is the right way round.
    void mark(std::uint8_t officer);

    /// Is the marked officer down? False when nobody is marked, which is every
    /// moment of the run outside the Hit. Not const, like `anyoneSees` and for
    /// the same reason: the pool's `at()` is not. Cheap enough to poll -- three
    /// slots and a mask test.
    bool markIsDown();

    /// One fixed logic step of wander, for everybody. Only ever called while
    /// the player is inside: the officers stand in room coordinates, and
    /// stepping them against the city's collision map would walk them into
    /// whatever happens to be at the same numbers out on the island.
    void step();

    void draw(pixelroot32::graphics::Renderer& renderer);

    /// Take a hit anywhere inside this box, on the first person standing in it:
    /// one person per shot, because a bullet is a point, not a blast. A blast
    /// weapon would want its own function rather than a changed one.
    /// @return who was there. `any` is what stops the shot; the rest is what
    ///         the city bills the player for -- and shooting an officer inside
    ///         a police station is the loudest thing they can do.
    PersonHit hitBox(int left, int top, int width, int height,
                     std::uint8_t damage);

    /// Frighten everybody within `radiusPx` of a world pixel -- a gunshot, or
    /// a car bearing down; each runs away from it (game/rules/Threat.h has the
    /// direction and the circle).
    void startleNear(int worldX, int worldY, int radiusPx);

    /// Answer a gunshot at a world pixel. Civilians within `radiusPx` run;
    /// officers within it turn and stay hostile. One call, because the crowd
    /// does not sort itself into two groups before reacting.
    void alarmNear(int worldX, int worldY, int radiusPx);

    /// Let every hostile officer take a shot at a world pixel through `gun`,
    /// passed in rather than owned: every officer in the demo fires from ONE
    /// shared WeaponSystem, so a street full of police costs the same eight
    /// projectile slots as one. Returns whether a round actually left a barrel
    /// -- ADR-18: this pool learns nothing about an AudioDirector, it only
    /// reports what it heard.
    bool returnFire(int targetX, int targetY, WeaponSystem& gun);

    /// Set every officer within `rangePx` on the player. The room is one
    /// screen across, so a manhunt in here has nowhere to develop -- which is
    /// the point of it. There is one door.
    void hunt(int targetX, int targetY, int rangePx,
              std::int32_t chaseSpeedSub);

    /// Does anybody on duty have the player in reach and in view? The room
    /// is one screen across with very little in it, so the answer is nearly
    /// always yes -- which is the point. Walking into the police station to
    /// wait out a wanted level is the one hiding place that must not work.
    bool anyoneSees(int targetX, int targetY, int rangePx);

    /// Fingerprint of what draw() would put on screen, for the frame skip.
    /// Cached like the crowd's, because the scene asks for it from a const
    /// method and the pool's iteration is not const.
    std::uint32_t visualKey() const { return staffKey_; }

private:
    using Pool = pixelroot32::gameplay::ObjectPool<PedestrianActor,
                                                   station::NUM_OFFICERS>;

    void refreshKey();

    Pool          pool_;
    std::uint32_t staffKey_;
    /// One bit per slot: set means chapter 2 was sent for the man in it. A mask
    /// rather than an index, and `TrafficPool::policeMask_`'s shape on purpose
    /// -- being the mark is a property of the SLOT, so `deploy` clears it the
    /// way a released traffic slot loses its police bit. An index
    /// would survive a redeploy and point at whoever the pool put in that
    /// storage next: a marked officer nobody named. Room for one bit each and
    /// no more, covered by the static_assert in StationStaff.cpp -- the same
    /// guard `kTrafficPoolSize <= 8` gives the masks over there.
    std::uint8_t  markMask_;
    /// Has anybody been placed yet this RUN? See `deployOnce`.
    bool          deployed_;
};

}  // namespace top_down_city
