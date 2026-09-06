#pragma once

#include <cstdint>

#include "game/scenes/BaseCityScene.h"
#include "game/systems/StationStaff.h"

namespace top_down_city {

/**
 * @class PoliceStationScene
 * @brief The first place the city is not: one room behind one door.
 *
 * A separate 15x15 tilemap -- the GBA answer, an area of its own rather than a
 * continuation of the outdoors. The size of this file is the argument for
 * interiors being their own scene: a second one is a copy of it, not a third
 * arm of every branch in the city's.
 *
 * Nothing here owns any of the run: the player, the wanted level, the delivery
 * clock and both weapon systems belong to CityWorld and keep running while the
 * player is in the lobby, and all this scene owns is the duty staff. The room
 * is exactly one viewport, so the camera does not move in here.
 */
class PoliceStationScene : public BaseCityScene {
public:
    PoliceStationScene();

    /**
     * @brief The duty staff, as themselves.
     *
     * The one thing this room has that the base class does not, answered
     * through a virtual rather than recovered with a cast because chapter 2 is
     * taken outdoors and has to put the man it names back on his feet before
     * the clock starts. See BaseCityScene::stationStaff for what the cast cost
     * and CityScene::takeHit for what it is for.
     */
    StationStaff* stationStaff() override { return &staff_; }

protected:
    void onEnterSpace(bool firstEntry) override;
    bool onActionPressed() override;
    bool stepSpace(const StepInput& in) override;
    void drawSpace(pixelroot32::graphics::Renderer& renderer) override;

    PersonHit hitPersonBox(int left, int top, int width, int height,
                           std::uint8_t damage) override;
    bool crowdSees(int x, int y, int rangePx) override;
    void alarmCrowd(int x, int y, int rangePx) override;
    std::uint32_t crowdVisualKey() const override;

    const char* spaceLabel() const override { return "POLICE STATION"; }
    const char* hintLabel() override;

private:
    bool atStationExit() const;

    /// The once-per-RUN placement rule lives inside the pool rather than
    /// here, because the other caller is the CITY: see StationStaff::
    /// deployOnce.
    StationStaff staff_;
};

}  // namespace top_down_city
