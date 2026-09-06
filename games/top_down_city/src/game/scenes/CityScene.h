#pragma once

#include <cstdint>

#include "game/CityConstants.h"
#include "game/entities/VehicleActor.h"
#include "game/scenes/BaseCityScene.h"
#include "game/systems/PedestrianPool.h"
#include "game/systems/TrafficPool.h"
#include "game/systems/WeaponPickups.h"

namespace top_down_city {

/// Forward-declared rather than included: the city needs the station only to
/// post its staff when chapter 2 is taken, and pulling the room in would make
/// every translation unit that draws the island depend on what is behind a
/// door.
class StationStaff;

/**
 * @class CityScene
 * @brief The island: walk it, drive its cars, and answer for what you hit.
 *
 * The outdoor space, and the one the run starts and ends in. Holds only what
 * belongs to the city -- BaseCityScene has the camera, the step and the HUD,
 * CityWorld has the run:
 *
 *  - Two dozen parked VehicleActors from city_scene::VEHICLE_SPAWNS. A drivable
 *    car has to be simulated, and the honest ESP32 budget is "all of them,
 *    cheaply" rather than "some of them, streamed".
 *  - A pooled crowd streamed around the camera (PedestrianPool), which can be
 *    run over -- the one place the driving model has consequences.
 *  - A second streamed population driving itself along the lane table
 *    (TrafficPool). Same VehicleActor, which is what lets the player steal one
 *    out of the flow: the difference is which container it lives in.
 *  - The delivery marker, the pickups and the radar, laid out in city tiles
 *    and meaningless in a room.
 *
 * Coming back from an interior is a fresh init(), which is why onEnterSpace()
 * distinguishes a first entry from a return: the cars are still parked where
 * they were left and the crowd only has to be re-primed, not re-seeded.
 */
class CityScene : public BaseCityScene {
public:
    CityScene();

protected:
    void onEnterSpace(bool firstEntry) override;
    bool onActionPressed() override;
    bool stepSpace(const StepInput& in) override;
    void drawSpace(pixelroot32::graphics::Renderer& renderer) override;
    void drawSpaceOverlay(pixelroot32::graphics::Renderer& renderer) override;

    PersonHit hitPersonBox(int left, int top, int width, int height,
                           std::uint8_t damage) override;
    bool crowdSees(int x, int y, int rangePx) override;
    void alarmCrowd(int x, int y, int rangePx) override;
    std::uint32_t crowdVisualKey() const override;
    std::uint32_t focusVisualKey() const override;

    bool spaceNeedsRedraw() const override;
    void recordSpaceDrawn() override;

    const char* spaceLabel() const override;
    const char* hintLabel() override;

    void onWantedChanged(std::uint8_t stars) override;
    bool respawnsHere() const override { return true; }
    void onRespawn() override;

    bool isDriving() const override { return driving_ != nullptr; }
    std::uint8_t drivingColor() const override;

    std::uint8_t zoneUnderFocus() const override;

    int focusCentreX() const override;
    int focusCentreY() const override;
    int focusTileX() const override;
    int focusTileY() const override;
    void focusHitBox(int& left, int& top,
                     int& width, int& height) const override;
    std::uint8_t incomingDamage(std::uint8_t damage) const override;

private:
    /// Which doorway in kDoorways the player is standing in, or kNumDoorways
    /// for none.
    ///
    /// A coordinate test, not a tile flag: an entrance is open ground drawn
    /// inside a building stamp, with nothing added to the map to make it a
    /// door. Answers "none" while driving, which keeps cars out of shopfronts.
    std::uint8_t doorwayUnderfoot() const;

    /// The nearest car within reach, parked or moving, or nullptr. Parked ones
    /// are searched first: standing between a parked car and passing traffic
    /// and being given the one about to drive off is not what the button
    /// looked like it would do.
    VehicleActor* vehicleInReach();

    void enterVehicle(VehicleActor& vehicle);

    /// Does nothing if there is nowhere free to stand, which is the correct
    /// answer when the car has been parked inside a hedge.
    void exitVehicle();

    /// The moving-obstacle test the movers ask through CityCollision. Static
    /// because it is a plain function pointer: no allocation, no virtual, and
    /// nothing in PlayerActor or PedestrianActor learns what a car is.
    static bool vehicleBlocks(const void* context, int left, int top,
                              int width, int height, const void* ignore);

    /// Index of the parked car whose box overlaps this one, skipping `except`
    /// and `alsoExcept`, or NUM_VEHICLES for none.
    ///
    /// The second exclusion lets takeContract() ask the follow-up -- is there
    /// ANOTHER car on this spot -- through the same overlap test rather than a
    /// second predicate that would have to agree about what "on the spot"
    /// means.
    ///
    /// Parked cars only, and a choice rather than an omission: the streamed
    /// pool is asked through vehicleBlocks() by everything that MOVES, whereas
    /// the one caller here is deciding where to put a car down and traffic
    /// drives itself out of the way. See takeContract().
    std::uint8_t parkedCarOn(const VehicleBox& spot, std::uint8_t except,
                             std::uint8_t alsoExcept =
                                 city_scene::NUM_VEHICLES) const;

    void collectWeapon();

    int missionTileX() const;
    int missionTileY() const;
    bool atMissionDrop() const;

    // -- the main mission ------------------------------------------------
    // The chapter is CityWorld's, the state machine is game/rules/Contract.h,
    // and everything below is the half of it that needs a map: which tile is
    // ringing, which car was named, and what counts as having arrived in it.

    /**
     * @brief Is there a job going spare on a phone that exists?
     *
     * `contract::phoneLive` alone cannot be enough: it reports that the phase
     * is `Idle`, and once chapter 1 is delivered the phase IS `Idle` again, on
     * a chapter with no row in `CONTRACT_PHONES`, no car and no drop, because
     * the rule module advances the chapter and only reaches `Phase::Complete`
     * after it has run out of ENUM. The table bound is what makes the phone go
     * quiet at the end of the story, and CityConstants.h asserts the table can
     * never outgrow the enum.
     *
     * The bound itself is `contract::offered` and NOT written here, even though
     * the table it counts is the scene's: it is all that stands between
     * `CONTRACT_PHONES[chapter]` and an out-of-range read that draws as a phone
     * ringing on a plausible tile rather than as a crash, and a rule with a
     * silent failure belongs where a host test can reach it. This method is the
     * map half -- it supplies the count and nothing else.
     */
    bool contractOffered() const;

    /// Standing on the ringing phone's cell, on foot, with a job to take. On
    /// foot is a condition rather than a consequence: the player actor is left
    /// parked at the kerb they got in at while driving, so its tile is a stale
    /// coordinate that could be any cell in the city -- including this one.
    bool atContractPhone() const;

    /// Answer it. Two chapters, two legs that share nothing but a phone cell,
    /// so this is the branch and each arm is its own method below.
    void takeContract();

    /// Chapter 1: put the car back where the generator validated it --
    /// swapping whatever is parked there onto the spot the car is vacating,
    /// never stacking one on the other -- then name the car, the drop and the
    /// clock off that.
    void takeBoost();

    /// Chapter 2: put the duty staff back on their posts and mark the one the
    /// contract names, then price the clock over the whole walk -- to the
    /// station door AND across the lobby behind it.
    void takeHit();

    /// Chapter 3's arm: start the clock and the body count where the player is
    /// standing. The shortest of the three, because the rampage is the one
    /// chapter with nothing to look up -- no car, no room, no destination. The
    /// corner IS the phone.
    void takeFrenzy();

    /**
     * @brief The duty staff behind the station door, asked for rather than
     *        cast out of a `core::Scene*`.
     *
     * nullptr if nothing was bound there -- or if the door table and the
     * platform header's room table ever disagree, the case this exists to make
     * survivable. See BaseCityScene::stationStaff.
     *
     * The name has to differ from that virtual, and not for taste: a member
     * here with the base's name and signature IS an override, `override`
     * written or not, so calling it `stationStaff` would put a PRIVATE method
     * in a PUBLIC vtable slot and make the city answer "is this room the
     * station" with the station's staff. Two questions wearing one name -- the
     * virtual asks a space what it IS, this reaches THROUGH a door at a space
     * that is not this one.
     */
    StationStaff* staffBehindStationDoor();

    /// Is the chapter's marker on screen at all -- as an offer or as a job?
    /// False once the story is out of chapters, which is what takes the ring
    /// off the pavement and the dot off the radar.
    bool contractMarked() const;

    /// Where that marker points: the phone, then the car, then the drop. Reads
    /// tables indexed by the contract state, so it is only meaningful -- and
    /// only called -- under contractMarked().
    int contractTileX() const;
    int contractTileY() const;

    /// Arrived at the drop IN the car the job named. Both halves matter; see
    /// the definition for why the car's identity is tested rather than assumed
    /// from the phase.
    bool atContractDrop() const;

    void drawMinimap(pixelroot32::graphics::Renderer& renderer);

    VehicleActor   vehicles_[city_scene::NUM_VEHICLES];
    PedestrianPool pedestrians_;
    TrafficPool    traffic_;
    WeaponPickups  pickups_;

    VehicleActor*  driving_;      ///< nullptr when on foot

    /// Last step's driven-car throttle, for audio_cues::pedalEventFor's edge
    /// test (ADR-19). Reset to 0 on foot, so boarding a car and holding the
    /// throttle at once still reads as an edge.
    std::int8_t    prevThrottle_;

    /// The two things on this screen that move without the player touching
    /// them. BaseCityScene owns the rest of the frame-skip comparison.
    std::uint32_t  drawnTrafficKey_;
    std::uint32_t  drawnPickupKey_;
};

}  // namespace top_down_city
