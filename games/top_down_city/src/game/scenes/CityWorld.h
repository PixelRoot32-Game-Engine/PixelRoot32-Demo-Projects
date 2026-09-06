#pragma once

#include <cstdint>

#include <math/MathUtil.h>

#include "game/CityConstants.h"
#include "game/entities/PlayerActor.h"
#include "game/rules/Contract.h"
#include "game/rules/Economy.h"
#include "game/rules/Facing.h"
#include "game/rules/Hideout.h"
#include "game/rules/Mission.h"
#include "game/rules/Wanted.h"
#include "game/systems/CityDayNight.h"
#include "game/systems/WeaponSystem.h"

namespace pixelroot32 {
namespace core {
class Engine;
class Scene;
}  // namespace core
}  // namespace pixelroot32

namespace top_down_city {

class BaseCityScene;

/**
 * @class CityWorld
 * @brief Everything that must outlive a doorway, plus the router between the
 *        two spaces that share it.
 *
 * The city and the police station are two core::Scene objects, and a scene swap
 * re-runs init() on the arriving one. Anything a scene owns therefore restarts
 * at the door, so what belongs to the RUN lives here and the scenes hold only
 * their own space.
 *
 * A singleton, like AudioDirector: there is one run and the only place that
 * could own it is a platform header, of which there are two.
 */
class CityWorld {
public:
    static CityWorld& instance();

    /**
     * @brief Called once from the platform entry point, BEFORE the first
     *        setScene.
     *
     * @param interiors  The rooms, in the order kDoorways names them. Passed
     *        as an array rather than as an argument each, because the door
     *        table indexes it: adding an interior would otherwise change this
     *        signature, both platform headers and every call.
     *
     *        `BaseCityScene*` and not `core::Scene*`, which is the difference
     *        between a room and a scene that happens to be behind a door: a
     *        caller that needs something only a ROOM has -- chapter 2 needs the
     *        duty staff -- would otherwise recover it with a downcast over a
     *        build with RTTI off. Here the platform header's array is what the
     *        compiler checks, and the room answers for itself. See
     *        BaseCityScene::stationStaff.
     */
    void bind(pixelroot32::core::Engine* engine,
              pixelroot32::core::Scene* city,
              BaseCityScene* const* interiors);

    pixelroot32::core::Scene* cityScene() const { return city_; }

    /// The room behind door `index`, or nullptr if nothing was bound there.
    /// Out of range is nullptr rather than undefined: a door pointing at a
    /// room that does not exist is a demo that hard-faults on a keypress.
    BaseCityScene* interiorScene(std::uint8_t index) const;

    /**
     * @brief Run the one-time world setup. Returns true on the call that did
     *        it, which is also how a scene tells a first entry from a return.
     *
     * Once and not per-init: CityDayNight::init() adopts whatever is bound to
     * the palette slots as the city's daylight, so a second call after dusk
     * would freeze the tinted palettes in as noon.
     */
    bool bootOnce();

    /// Ask for the other space. Not performed here -- see commit().
    void requestScene(pixelroot32::core::Scene* next) { pending_ = next; }

    /**
     * @brief Perform a requested swap, if any.
     *
     * Engine::setScene re-enters init() on the arriving scene, so this may
     * only be called where update() returns immediately afterwards: the
     * departing scene would otherwise keep stepping on a stack it has left.
     *
     * @return true when a swap happened.
     */
    bool commit();

    // -- the run ----------------------------------------------------------

    PlayerActor               player;
    /// Ticked in both spaces: the police do not forget while you are in the
    /// lobby, and the drop outside does not wait.
    wanted::State             wanted;
    mission::State            mission;
    /// The main mission, beside the courier run rather than inside it. Here for
    /// the same reason `wanted` and `hideBurn` are: it must outlive a doorway.
    /// The job clock does not stop at the corner shop's till and the chapter is
    /// not re-offered for stepping through a door -- a contract that reset at
    /// the doorway would be a mission you finish by going shopping. See
    /// game/rules/Contract.h.
    contract::State           contract;
    /// What the courier has been paid, and what the shop will take. Survives
    /// an arrest deliberately -- see game/rules/Economy.h.
    economy::Purse            purse;
    pixelroot32::math::Random missionRng;
    WeaponSystem              weapons;
    /// The police's, shared by every officer in the demo. One system means one
    /// set of projectile slots and one cooldown for the whole force, which is
    /// also the rate limit on it.
    WeaponSystem              returnFire;
    CityDayNight              dayNight;

    /// How long the police keep watching the door the player last used. Lives
    /// here rather than in the room it applies to for the usual reason: a
    /// scene swap re-runs init(), and a burn that reset at the doorway would
    /// be a rule that never fires. See game/rules/Hideout.h.
    hideout::Burn             hideBurn;

    /// Where the player stood when they went inside, in world pixels. Kept
    /// rather than derived from the door tile: the cell is 16 px wide and
    /// coming back out re-centred would shove them sideways every time.
    int                       streetX;
    int                       streetY;
    Facing                    streetFacing;

    /// Milliseconds left on the caught notice, or 0. The one piece of state
    /// that FREEZES the fixed logic step rather than feeding it.
    int                       bustedMs;

    /// Set when the notice runs out on a player who went down INDOORS. The
    /// respawn tile is a city coordinate, so CityScene::onEnterSpace serves
    /// the arrest on arrival.
    bool                      pendingRespawn;

    std::uint8_t              zone;

    /// Edge detection for RUN and FIRE. Shared rather than per-scene: the
    /// button is still held on the frame the other space arrives, and a latch
    /// that reset at the doorway would read one press twice.
    bool                      actionLatched;
    bool                      fireLatched;

    /// Is the top star running down? Cached from the logic step for the HUD.
    bool                      cooling;

private:
    CityWorld();

    CityWorld(const CityWorld&) = delete;
    CityWorld& operator=(const CityWorld&) = delete;

    pixelroot32::core::Engine* engine_;
    pixelroot32::core::Scene*  city_;
    BaseCityScene*             interiors_[kNumDoorways];
    pixelroot32::core::Scene*  pending_;
    bool                       booted_;
};

}  // namespace top_down_city
