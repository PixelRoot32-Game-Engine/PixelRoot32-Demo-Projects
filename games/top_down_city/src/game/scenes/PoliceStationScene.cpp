#include "game/scenes/PoliceStationScene.h"

#include "audio/AudioDirector.h"
#include "game/systems/CityCollision.h"
#include "generated/tilemaps/police_station.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

PoliceStationScene::PoliceStationScene()
    : staff_() {
}

void PoliceStationScene::onEnterSpace(bool firstEntry) {
    (void)firstEntry;   // the run always starts outdoors; this is never it

    CityWorld& world = CityWorld::instance();

    collision::setSpace(collision::Space::PoliceStation);

    // The cycle is a sky effect and there is no sky in here. The clock keeps
    // running -- the HUD readout still moves -- but the room is lit by its own
    // ceiling, at whatever hour it is outside.
    world.dayNight.setSuspended(true);

    // A round still in the air holds city pixels, and the room's start again
    // at zero: left alone it would arrive somewhere arbitrary in the lobby.
    world.weapons.dropProjectiles();
    world.returnFire.dropProjectiles();

    // Same reason: the city's blocker would report a car in the lobby wherever
    // the two coordinate spaces happen to overlap -- an invisible wall whose
    // position depends on the map seed.
    collision::setDynamicBlocker(nullptr, nullptr);

    applySpaceBounds();

    world.player.placeAt(station::SPAWN_TILE_X * kTilePx,
                         station::SPAWN_TILE_Y * kTilePx,
                         Facing::Up);
    world.player.setVisible(true);

    // Once per RUN and not once per visit -- and the flag that says so is
    // the pool's, because chapter 2 deploys from outside this file. See
    // StationStaff::deployOnce.
    staff_.deployOnce();
}

bool PoliceStationScene::atStationExit() const {
    const PlayerActor& player = CityWorld::instance().player;
    return player.tileX() == station::EXIT_TILE_X
        && player.tileY() == station::EXIT_TILE_Y;
}

bool PoliceStationScene::onActionPressed() {
    if (!atStationExit()) {
        return false;
    }
    CityWorld& world = CityWorld::instance();
    AudioDirector::instance().playCue(audio_cues::Cue::Doorway);
    world.requestScene(world.cityScene());
    return true;
}

const char* PoliceStationScene::hintLabel() {
    // The button first, always: this strip is the only thing that says what RUN
    // is about, so a line naming something else while a door was underfoot
    // would be worse than no line. The same order CityScene::hintLabel keeps.
    if (atStationExit()) {
        return "RUN: STEP OUT";
    }
    // Otherwise, and only in the beat that has nothing else on screen: no
    // clock, no marker, and a star row that says how much heat but not what
    // to do with it. There is one door and it is where the player came in.
    if (CityWorld::instance().contract.phase == contract::Phase::Struck) {
        return "LOSE THE STARS";
    }
    return nullptr;
}

PersonHit PoliceStationScene::hitPersonBox(int left, int top, int width,
                                           int height, std::uint8_t damage) {
    return staff_.hitBox(left, top, width, height, damage);
}

bool PoliceStationScene::crowdSees(int x, int y, int rangePx) {
    // A wanted level you can wait out in the lobby of a police station is the
    // one hiding place that has to be closed.
    return staff_.anyoneSees(x, y, rangePx);
}

void PoliceStationScene::alarmCrowd(int x, int y, int rangePx) {
    staff_.alarmNear(x, y, rangePx);
}

std::uint32_t PoliceStationScene::crowdVisualKey() const {
    return staff_.visualKey();
}

bool PoliceStationScene::stepSpace(const StepInput& in) {
    CityWorld& world = CityWorld::instance();

    stepWantedClock();
    // Neither clock stops at the door: the drop is outside and so is the car,
    // so pausing them would only be a rule to explain -- and a job you could
    // park indefinitely by standing at a till is not a job. Ticked here and
    // ANSWERED at the bottom of the step, the one order in this function that
    // is not a matter of taste -- see the objectiveExpired() call for why.
    stepObjectiveClock();

    movePlayerOnFoot(in);

    // Before the step, not after: hunt() sets the direction that step() then
    // walks. The other way round is a manhunt one step behind the player.
    staff_.hunt(focusCentreX(), focusCentreY(),
                wanted::pursuitRangePx(world.wanted.stars),
                wanted::chaseSpeedSub(world.wanted.stars));
    staff_.step();

    stepGuns();
    if (staff_.returnFire(world.player.centreX(), world.player.centreY(),
                          world.returnFire)) {
        AudioDirector::instance().playCue(audio_cues::Cue::PoliceGunshot);
    }

    // The mark, POLLED rather than caught on the hit, and after the guns so a
    // round that landed this step counts this step. `hitBox` reports that
    // somebody was hit and not which slot -- and the edge is not the player's
    // to own anyway: a round from the officer standing next to him puts him
    // down just as dead, and a chapter that refused to notice would be one the
    // player has to redo for the crime of being lucky.
    //
    // Asking every step is the same as asking once, because `struck` is a no-op
    // from every phase but `ToTarget`: outside the Hit nobody is marked and
    // `markIsDown` is false, during it the answer stays true for the rest of
    // the run. The read-back is only so the banner fires on the transition
    // rather than on all of them.
    const contract::Phase before = world.contract.phase;
    if (staff_.markIsDown()) {
        contract::struck(world.contract);
    }
    if (world.contract.phase != before) {
        // Worth a word, and it has to be: this is the one transition in the
        // demo where the HUD says LESS afterwards. The clock goes off the
        // plate and the marker off the radar -- see game/rules/Contract.h --
        // so without this the player is told what to do next by two things
        // disappearing.
        showBanner("GET CLEAN");
    }

    // Expiry LAST, after the shot has been resolved and the mark has been
    // asked: see the @warning over BaseCityScene::objectiveExpired. Here the
    // failure is concrete -- the player walks into a police station, shoots the
    // right man on the last step of the clock, and is told JOB LOST.
    //
    // Nothing above this line reads the contract phase, so moving the call
    // down costs nothing: the staff hunt on the wanted level, return fire on
    // the player's pixels, and `markIsDown` is a question about a body. The
    // one thing that does change is the courier arm -- `nextLeg` measures the
    // new leg from where the player is NOW, and now they have taken this
    // step's stride. One tile, in the direction of honesty: outdoors that arm
    // has always run after the player moved.
    //
    // The two banners cannot collide, which is worth knowing rather than
    // hoping: on the step the mark goes down the phase is Struck, and Struck
    // is `underway` with no clock -- so `contract::expired` is false and this
    // call returns without a word over GET CLEAN.
    objectiveExpired();
    return !checkPlayerDown();
}

void PoliceStationScene::drawSpace(gfx::Renderer& renderer) {
    // Two layers, not three: a room has no third storey of decoration. The
    // camera is pinned at the origin, so this is the whole frame.
    renderer.drawTileMap(station::background, 0, 0, gfx::LayerType::Static);
    renderer.drawTileMap(station::items, 0, 0, gfx::LayerType::Static);

    staff_.draw(renderer);
    Scene::draw(renderer);   // the player, drawn last so nothing hides them
}

}  // namespace top_down_city
