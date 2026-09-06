#pragma once
#include <cstdint>

#include <gameplay/ObjectPool.h>
#include <graphics/Renderer.h>
#include <math/MathUtil.h>

#include "game/CityConstants.h"
#include "game/entities/PedestrianActor.h"
#include "game/systems/WeaponSystem.h"
#include "game/entities/VehicleActor.h"

namespace top_down_city {

/**
 * @brief The crowd: a fixed slab of pedestrian slots, streamed around the
 *        camera.
 *
 * The city has 16,384 tiles and no memory for a person on any of them, so the
 * crowd is `kPedestrianPoolSize` slots filled from the ring just outside the
 * viewport and emptied when somebody is left far enough behind. Twelve is what
 * a 240x240 window can show at once; the rest of the city is empty and the
 * player cannot tell.
 *
 * The storage is the engine's `ObjectPool`: the slab is a member, the slots are
 * placement-new'd in it, and acquire/release never touch the heap -- which is
 * also why this must be reset AFTER `Scene::init()`, since `resetState()`
 * rewinds the scene arena without running destructors.
 */
class PedestrianPool {
public:
    PedestrianPool();

    /// Empty every slot and reseed the wander noise. Call from the scene's
    /// init(), after Scene::init().
    void reset(std::uint32_t seed);

    /// Fill the pool for the first frame, allowing spawns inside the
    /// viewport: at scene start there is no "just off screen" the player has
    /// not already been looking at.
    void prime(int cameraX, int cameraY);

    /// One fixed logic step: retire whoever is out of range, admit somebody
    /// new if there is room, and walk everybody who is left.
    void step(int cameraX, int cameraY);

    /// Run over whoever this car is driving through. A no-op below the car's
    /// lethal speed, so parking against somebody does not kill them.
    /// @return who went under it, for the star counter.
    PersonHit runOverBy(const VehicleActor& car);

    /// Take a hit anywhere inside this box, on the first person standing in it:
    /// one person per shot, because a bullet is a point, not a blast. A blast
    /// weapon would want its own function rather than a changed one.
    /// @return who was there. `any` is what stops the shot; the rest is what
    ///         the city holds against the player for it.
    PersonHit hitBox(int left, int top, int width, int height,
                     std::uint8_t damage);

    /// Frighten everybody within `radiusPx` of a world pixel -- a gunshot, or
    /// a car bearing down; each runs away from it (game/rules/Threat.h has the
    /// direction and the circle). Returns whether anybody was startled: ADR-18,
    /// and a LEVEL, not an edge -- true on every step somebody is inside the
    /// radius, because `startle()` re-arms rather than accumulates. The edge
    /// detector is `Cue::CrowdPanic`'s own cooldown row.
    bool startleNear(int worldX, int worldY, int radiusPx);

    /// Answer a gunshot at a world pixel. Civilians within `radiusPx` run;
    /// officers within it turn and stay hostile. One call, because the crowd
    /// does not sort itself into two groups before reacting.
    void alarmNear(int worldX, int worldY, int radiusPx);

    /// Let every hostile officer take a shot at a world pixel through `gun`,
    /// passed in rather than owned: every officer in the demo fires from ONE
    /// shared WeaponSystem, so a street full of police costs the same eight
    /// projectile slots as one. Returns whether a round actually left a barrel
    /// -- ADR-18: this pool learns nothing about an AudioDirector, it only
    /// reports what it heard, same shape as runOverBy's PersonHit.
    bool returnFire(int targetX, int targetY, WeaponSystem& gun);

    /// Set every officer within `rangePx` of a world pixel on the player:
    /// alerted, and walking at `chaseSpeedSub`. Called every step while the
    /// player is wanted, so an officer who spawns into a manhunt joins it
    /// rather than waiting to be shot at first. A range of 0 -- nobody
    /// wanted -- does nothing.
    void hunt(int targetX, int targetY, int rangePx,
              std::int32_t chaseSpeedSub);

    /// Does any officer on this street have the player in reach AND a clear
    /// line to them? This is what holds the wanted level up, and it is neither
    /// of the cheaper questions: not "is anybody chasing", because a chase
    /// carries on for kOfficerAlertSteps after they lose you, and not "is
    /// anybody near", because a building between you is the whole mechanic.
    ///
    /// Stops at the first one. Not const because ObjectPool's iteration is not
    /// -- the same reason visualKey() caches instead of walking the slots.
    bool anyoneSees(int targetX, int targetY, int rangePx);

    /// The 1-in-N roll that decides whether the next spawn wears a uniform.
    /// Driven by the star counter; see wanted::officerChanceIn.
    void setOfficerChanceIn(std::uint8_t chanceIn);

    /// Bodies, drawn before the traffic so a car covers what it flattened.
    void drawGround(pixelroot32::graphics::Renderer& renderer,
                    int cameraX, int cameraY);

    /// People still on their feet, drawn after it.
    void drawWalking(pixelroot32::graphics::Renderer& renderer,
                     int cameraX, int cameraY);

    /// Fingerprint of the whole crowd, for the scene's frame skip. Cached
    /// rather than walked on demand: the scene asks for it once per frame
    /// from a const method, and the pool's iteration is non-const.
    std::uint32_t visualKey() const { return crowdKey_; }

    std::uint16_t liveCount() const { return pool_.size(); }

private:
    using Pool = pixelroot32::gameplay::ObjectPool<PedestrianActor,
                                                   kPedestrianPoolSize>;

    bool trySpawn(int cameraX, int cameraY, bool allowOnScreen);
    void refreshKey();
    static bool isOnScreen(int spriteX, int spriteY, int cameraX, int cameraY,
                           int margin);

    Pool                      pool_;
    pixelroot32::math::Random rng_;   ///< Spawn placement, not wander noise.
    std::uint32_t             crowdKey_;
    std::uint8_t              officerChanceIn_;
};

}  // namespace top_down_city
