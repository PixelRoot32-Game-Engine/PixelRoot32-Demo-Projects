#include "game/scenes/CityScene.h"

#include <graphics/Color.h>

#include "audio/AudioDirector.h"
#include "game/rules/Hideout.h"
#include "game/rules/MinimapRuns.h"
#include "game/systems/CityCollision.h"
#include "game/systems/StationStaff.h"
#include "generated/tilemaps/city_scene.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

namespace {

/// Where the driver is put down, in the order the doors are tried: the two
/// kerb sides first, then in front and behind. Offsets run from the car's
/// sprite cell to the player's and overlap it by two pixels, so the driver
/// never appears to teleport a full tile away.
constexpr int kExitOffsets[4][2] = {
    { -kPlayerSpriteW + 2, 0 },
    { kVehicleSpriteW - 2, 0 },
    { 0, -kPlayerSpriteH + 2 },
    { 0, kVehicleSpriteH - 2 },
};

constexpr Facing kExitFacing[4] = {
    Facing::Left, Facing::Right, Facing::Up, Facing::Down,
};

/**
 * @brief An objective ring on the ground, in world coordinates.
 *
 * A ring rather than a filled pad: a solid block hides the tile it is marking,
 * and the tile is what the player has to recognise from across the street. Two
 * nested rectangles, because the renderer has no stroke width.
 *
 * Shared by the courier drop and the chapter marker rather than written twice:
 * the colour is the ONLY thing separating the two markers, and two copies of
 * this that drifted by a pixel would make that false without making it look
 * false.
 */
void drawObjectiveRing(gfx::Renderer& renderer, int tileX, int tileY,
                       int cameraX, int cameraY, gfx::Color colour) {
    const int ringX = tileX * kTilePx + (kTilePx - kMarkerRingPx) / 2;
    const int ringY = tileY * kTilePx + (kTilePx - kMarkerRingPx) / 2;
    if (ringX + kMarkerRingPx <= cameraX
        || ringX >= cameraX + DISPLAY_WIDTH
        || ringY + kMarkerRingPx <= cameraY
        || ringY >= cameraY + DISPLAY_HEIGHT) {
        return;
    }
    renderer.drawRectangle(ringX, ringY, kMarkerRingPx, kMarkerRingPx, colour);
    renderer.drawRectangle(ringX + 1, ringY + 1,
                           kMarkerRingPx - 2, kMarkerRingPx - 2, colour);
}

/**
 * @brief The same objective as one dot on the radar.
 *
 * CLAMPED to the window rather than culled by it: three blocks is a radar, not
 * an atlas, the target is almost never inside it, and a marker that appears
 * only once you have nearly arrived never helped.
 *
 * The one-pixel dark border is not decoration. The overlay has three grounds --
 * the island's yellow, the carriageway's grey and the near-black plate the sea
 * shows through -- and a bare dot is loud against two of them and merely
 * present against the third. That matters more for the chapter's cyan than for
 * the drop's green: see hud::kContract, whose luminance lands within a couple
 * of points of the carriageway's.
 */
void drawRadarDot(gfx::Renderer& renderer, int tileX, int tileY,
                  int firstTileX, int firstTileY, gfx::Color colour) {
    const int col = clampInt(tileX - firstTileX, 0, kMinimapTiles - 1);
    const int row = clampInt(tileY - firstTileY, 0, kMinimapTiles - 1);
    const int dotX = kMinimapOriginX + col * kMinimapScale - kMinimapDropPx / 2;
    const int dotY = kMinimapOriginY + row * kMinimapScale - kMinimapDropPx / 2;
    renderer.drawFilledRectangle(dotX - 1, dotY - 1,
                                 kMinimapDropPx + 2, kMinimapDropPx + 2,
                                 hud::kEdge);
    renderer.drawFilledRectangle(dotX, dotY, kMinimapDropPx, kMinimapDropPx,
                                 colour);
}

}  // namespace

CityScene::CityScene()
    : vehicles_(),
      pedestrians_(),
      traffic_(),
      pickups_(),
      driving_(nullptr),
      prevThrottle_(0),
      drawnTrafficKey_(0),
      drawnPickupKey_(0) {
}

void CityScene::onEnterSpace(bool firstEntry) {
    CityWorld& world = CityWorld::instance();

    // The collision space is global state and this scene may be entered more
    // than once, so it is set rather than assumed.
    collision::setSpace(collision::Space::City);
    // Back under the sky, at the hour the clock reached while the player was
    // inside: one palette rebuild here rather than one per step indoors.
    world.dayNight.setSuspended(false);
    // A round still in the air was fired in the other space's coordinates.
    world.weapons.dropProjectiles();
    world.returnFire.dropProjectiles();

    // You cannot drive into a building -- doorwayUnderfoot() answers "none"
    // while driving -- so this states the fact rather than releasing anything.
    driving_ = nullptr;
    prevThrottle_ = 0;

    if (firstEntry) {
        AudioDirector::instance().stopMusic();
        AudioDirector::instance().resetCooldowns();

        world.player.spawnAt(scene::SPAWN_TILE_X, scene::SPAWN_TILE_Y);
        world.player.setVisible(true);

        for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
            vehicles_[i].spawn(city_scene::VEHICLE_SPAWNS[i]);
        }
    } else {
        world.player.placeAt(world.streetX, world.streetY, world.streetFacing);
        world.player.setVisible(true);
    }

    // Cars are not in the tile data, so this is how the player and the
    // pedestrians find out they are there. After the cars are parked: a
    // blocker reporting positions nobody has set yet would wall off the spawn.
    collision::setDynamicBlocker(&CityScene::vehicleBlocks, this);

    // Bounds before any move: Camera2D's defaults are (0, 0), so a camera
    // moved before this would be pinned to the world origin.
    applySpaceBounds();

    if (firstEntry) {
        pedestrians_.reset(0xC1735EEDu);
        pedestrians_.prime(cameraOriginX(), cameraOriginY());
        // After the parked half: a traffic spawn refuses a lane that already
        // has a car in it, and asking before vehicles_ was filled would put
        // traffic on top of it.
        traffic_.reset(0x7A44F1C5u);
        traffic_.prime(cameraOriginX(), cameraOriginY());

        world.weapons.reset();
        // The police are equipped rather than left unarmed: an officer does
        // not walk to a pickup, and the second row of the weapon table is
        // what makes theirs weaker and slower than the player's.
        world.returnFire.reset();
        world.returnFire.equip(weapons::WeaponId::PolicePistol);

        world.player.heal();
        world.wanted = wanted::clear();
        onWantedChanged(world.wanted.stars);
        // Deterministic like everything else the generator lays out: the demo
        // opens the same way twice.
        world.mission = mission::begin(0, 0);
        nextLeg(false);
        world.mission.streak = 0;
        world.mission.best = 0;
        // Chapter 1, phone ringing. Stated rather than assumed: CityWorld is a
        // process-lifetime singleton, so a second run inherits this object --
        // and one that inherited a Complete contract would open with a story it
        // could not start and no sign of why.
        world.contract = contract::clear();
        pickups_.reset();
        world.player.setArmed(false);
        world.zone = scene::zoneAt(focusTileX(), focusTileY());
    } else {
        // The crowd streams against the viewport and was frozen the whole time
        // the player was inside. Priming here rather than letting it refill is
        // the difference between stepping out into a street and into a set.
        pedestrians_.prime(cameraOriginX(), cameraOriginY());
    }

    // An arrest that landed indoors could not be served there: the respawn
    // tile is a city coordinate.
    if (world.pendingRespawn) {
        world.pendingRespawn = false;
        respawn();
    }
}

std::uint8_t CityScene::zoneUnderFocus() const {
    return scene::zoneAt(focusTileX(), focusTileY());
}

int CityScene::focusCentreX() const {
    return driving_ != nullptr ? driving_->centreX()
                               : BaseCityScene::focusCentreX();
}

int CityScene::focusCentreY() const {
    return driving_ != nullptr ? driving_->centreY()
                               : BaseCityScene::focusCentreY();
}

int CityScene::focusTileX() const {
    return driving_ != nullptr ? driving_->tileX()
                               : BaseCityScene::focusTileX();
}

int CityScene::focusTileY() const {
    return driving_ != nullptr ? driving_->tileY()
                               : BaseCityScene::focusTileY();
}

std::uint32_t CityScene::focusVisualKey() const {
    return driving_ != nullptr ? driving_->visualKey()
                               : BaseCityScene::focusVisualKey();
}

void CityScene::focusHitBox(int& left, int& top,
                            int& width, int& height) const {
    if (driving_ == nullptr) {
        BaseCityScene::focusHitBox(left, top, width, height);
        return;
    }
    left   = driving_->boxLeft();
    top    = driving_->boxTop();
    width  = driving_->boxWidth();
    height = driving_->boxHeight();
}

std::uint8_t CityScene::incomingDamage(std::uint8_t damage) const {
    // The car is cover, not immunity: a car nobody can shoot through is a car
    // you sit in until the level decays, which clears every chase by parking.
    return driving_ != nullptr
        ? static_cast<std::uint8_t>(damage / kDrivingCoverDivisor)
        : damage;
}

std::uint8_t CityScene::drivingColor() const {
    return driving_ != nullptr ? driving_->color() : 0;
}

PersonHit CityScene::hitPersonBox(int left, int top, int width, int height,
                                  std::uint8_t damage) {
    return pedestrians_.hitBox(left, top, width, height, damage);
}

bool CityScene::crowdSees(int x, int y, int rangePx) {
    return pedestrians_.anyoneSees(x, y, rangePx)
        || traffic_.anyoneSees(x, y, rangePx);
}

void CityScene::alarmCrowd(int x, int y, int rangePx) {
    pedestrians_.alarmNear(x, y, rangePx);
}

std::uint32_t CityScene::crowdVisualKey() const {
    return pedestrians_.visualKey();
}

bool CityScene::spaceNeedsRedraw() const {
    return traffic_.visualKey() != drawnTrafficKey_
        || pickups_.visualKey() != drawnPickupKey_;
}

void CityScene::recordSpaceDrawn() {
    drawnTrafficKey_ = traffic_.visualKey();
    drawnPickupKey_  = pickups_.visualKey();
}

const char* CityScene::spaceLabel() const {
    return scene::ZONE_LABELS[CityWorld::instance().zone];
}

const char* CityScene::hintLabel() {
    // Same order as onActionPressed, and it has to be: this strip is the only
    // thing that tells the player which of the three the button is about, so a
    // line that named one and pressed another would be worse than no line.
    if (doorwayUnderfoot() < kNumDoorways) {
        return "RUN: GO IN";
    }
    if (atContractPhone()) {
        return "RUN: ANSWER";
    }
    if (driving_ != nullptr) {
        return "RUN: GET OUT";
    }
    if (vehicleInReach() != nullptr) {
        return "RUN: DRIVE";
    }
    // Last, under every button prompt, and only in the beat that has nothing
    // else to read: after the kill there is no clock on the plate and no ring
    // on the ground, and the star row says how much heat without saying what to
    // do about it. "Be somewhere nobody is looking" is not a place a marker can
    // point at, so it is a word instead. See `contract::struck`.
    return CityWorld::instance().contract.phase == contract::Phase::Struck
               ? "LOSE THE STARS"
               : nullptr;
}

void CityScene::onWantedChanged(std::uint8_t stars) {
    pedestrians_.setOfficerChanceIn(wanted::officerChanceIn(stars));
}

void CityScene::onRespawn() {
    if (driving_ != nullptr) {
        // Hand the car back before letting go. A vehicle left flagged as
        // driven is one nobody is driving and nothing may drive: harmless for
        // a parked car, but a traffic slot would be stuck at the scene of a
        // fight the player has already left.
        driving_->setDriven(false);
        driving_ = nullptr;
    }
    prevThrottle_ = 0;
    pedestrians_.reset(0xC1735EEDu);
    pedestrians_.prime(cameraOriginX(), cameraOriginY());
    traffic_.reset(0x7A44F1C5u);
    traffic_.prime(cameraOriginX(), cameraOriginY());
}

std::uint8_t CityScene::doorwayUnderfoot() const {
    if (driving_ != nullptr) {
        return kNumDoorways;
    }
    const PlayerActor& player = CityWorld::instance().player;
    for (std::uint8_t i = 0; i < kNumDoorways; ++i) {
        if (player.tileX() == kDoorways[i].tileX
                && player.tileY() == kDoorways[i].tileY) {
            return i;
        }
    }
    return kNumDoorways;
}

bool CityScene::onActionPressed() {
    CityWorld& world = CityWorld::instance();

    // Doors before cars: an entrance is on the pavement, where a car may well
    // be parked within reach, and somebody standing in a doorway pressing the
    // button is going through the door.
    const std::uint8_t door = doorwayUnderfoot();
    pr32::core::Scene* room =
        door < kNumDoorways
            ? world.interiorScene(kDoorways[door].interior)
            : nullptr;
    if (room != nullptr) {
        // Recorded here rather than on the way back, and in pixels: the door
        // cell is 16 px wide and coming back out re-centred would shove the
        // player sideways every single time.
        world.streetX      = world.player.spriteX();
        world.streetY      = world.player.spriteY();
        world.streetFacing = world.player.facing();
        // Armed on the way in, and it has to be here: policeCanSee() is a
        // question about the street, and one frame from now the street is a
        // scene nobody is stepping. See game/rules/Hideout.h.
        world.hideBurn = hideout::onEntry(world.wanted.stars, policeCanSee());
        AudioDirector::instance().playCue(audio_cues::Cue::Doorway);
        world.requestScene(room);
        return true;
    }
    // Then the phone, before the car and for the doors' reason: a car may well
    // be parked within reach of the ring. It is also the only one of the three
    // that can be missed -- the car is still there afterwards, the job is not.
    if (atContractPhone()) {
        takeContract();
        return true;
    }
    if (driving_ != nullptr) {
        exitVehicle();
        return true;
    }
    VehicleActor* target = vehicleInReach();
    if (target != nullptr) {
        enterVehicle(*target);
        return true;
    }
    return false;
}

bool CityScene::vehicleBlocks(const void* context, int left, int top,
                              int width, int height, const void* ignore) {
    const CityScene* scene = static_cast<const CityScene*>(context);
    for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
        const VehicleActor& vehicle = scene->vehicles_[i];
        if (&vehicle == ignore) {
            continue;
        }
        if (vehicle.overlapsBox(left, top, width, height)) {
            return true;
        }
    }
    // Parked cars first: two dozen against six, and the streamed pool is the
    // one that has to walk a live-slot bitmap.
    return scene->traffic_.blocks(left, top, width, height, ignore);
}

std::uint8_t CityScene::parkedCarOn(const VehicleBox& spot,
                                    std::uint8_t except,
                                    std::uint8_t alsoExcept) const {
    for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
        if (i == except || i == alsoExcept) {
            continue;
        }
        if (vehicles_[i].overlapsBox(spot.left, spot.top,
                                     spot.width, spot.height)) {
            return i;
        }
    }
    return city_scene::NUM_VEHICLES;
}

void CityScene::collectWeapon() {
    CityWorld& world = CityWorld::instance();
    // Not while driving: running a pistol over at 156 px/s and having it
    // appear in the driver's hand is a magnet, not a pickup.
    if (driving_ != nullptr) {
        return;
    }
    weapons::WeaponId taken = weapons::WeaponId::Pistol;
    const int left = world.player.spriteX() + kPlayerBoxOffsetX;
    const int top  = world.player.spriteY() + kPlayerBoxOffsetY;
    if (!pickups_.collect(left, top, kPlayerBoxWidth, kPlayerBoxHeight,
                          taken)) {
        return;
    }
    world.weapons.equip(taken);
    world.player.setArmed(true);
    AudioDirector::instance().playCue(audio_cues::Cue::WeaponPickup);
    showBanner(world.weapons.spec().name);
}

int CityScene::missionTileX() const {
    return scene::MISSION_TARGETS[CityWorld::instance().mission.target].tileX;
}

int CityScene::missionTileY() const {
    return scene::MISSION_TARGETS[CityWorld::instance().mission.target].tileY;
}

bool CityScene::atMissionDrop() const {
    const int dropX = missionTileX() * kTilePx + kTilePx / 2;
    const int dropY = missionTileY() * kTilePx + kTilePx / 2;
    // The focus, not the player: arriving in a car is the point of there
    // being cars.
    return threat::within(focusCentreX() - dropX, focusCentreY() - dropY,
                          kMarkerRadiusPx);
}

bool CityScene::contractOffered() const {
    // The count is the map's, the rule is not. See CityScene.h.
    return contract::offered(CityWorld::instance().contract,
                             scene::NUM_CONTRACT_PHONES);
}

bool CityScene::atContractPhone() const {
    if (!contractOffered() || driving_ != nullptr) {
        return false;
    }
    const CityWorld& world = CityWorld::instance();
    const scene::ContractPhone& phone = scene::CONTRACT_PHONES[
        static_cast<std::uint8_t>(world.contract.chapter)];
    // The whole cell, like a doorway and for the same reason: a phone is not
    // in the tile data, it is a ring drawn on open pavement, and a radius test
    // would make the one tile the player can SEE the marker on not quite the
    // one that answers.
    return world.player.tileX() == phone.tileX
        && world.player.tileY() == phone.tileY;
}

StationStaff* CityScene::staffBehindStationDoor() {
    // No cast, and that is the whole of it. `interiorScene` hands back a
    // BaseCityScene* -- the platform header's array is typed as one, so a room
    // that is not a room is a build error -- and every room answers this
    // question for itself: the station with its duty staff, everybody else
    // with nullptr. What that buys over a static_cast, and what a mis-ordered
    // door table would otherwise cost, is in BaseCityScene::stationStaff.
    BaseCityScene* room =
        CityWorld::instance().interiorScene(kDoorways[kPoliceDoorway].interior);
    return room != nullptr ? room->stationStaff() : nullptr;
}

void CityScene::takeContract() {
    CityWorld& world = CityWorld::instance();

    // Three chapters and no two share a leg: one names a car and a drop, one a
    // man in a room, one nothing at all. The split is here rather than inside
    // `contract::` because everything each arm needs is a scene-side table --
    // and both `beginHit` and `beginFrenzy` refuse a chapter that is not
    // theirs, so a branch that fell through would be a no-op, not a mix-up.
    //
    // The boost is the fall-through on purpose: `contract::begin` is the one
    // entry point with no chapter guard of its own, so a chapter added later
    // without an arm lands on the call that cannot refuse it -- loudly, on the
    // very first playthrough, instead of silently doing nothing.
    if (world.contract.chapter == contract::Chapter::Hit) {
        takeHit();
        return;
    }
    if (world.contract.chapter == contract::Chapter::Frenzy) {
        takeFrenzy();
        return;
    }
    takeBoost();
}

void CityScene::takeBoost() {
    CityWorld& world = CityWorld::instance();

    // Named rather than looked up. A per-chapter {vehicle, drop} table would
    // have a single entry, and chapter 2 did not grow one -- it wanted a
    // different pair of facts entirely, which is why it is an arm of its own.
    // The static_asserts in CityConstants.h are what fail if the tables move
    // without this call site following them.
    const std::uint8_t car  = scene::BOOST_VEHICLE_INDEX;
    const std::uint8_t drop = scene::BOOST_DROP_INDEX;

    // Put the car back before the clock is cut from where it stands: this is
    // the fix for a chapter that could otherwise become unwinnable in silence.
    // The clock is `mission::allowanceSteps` over two Manhattan legs, and that
    // allowance is proven generous against a tile COUNT, not against a walk.
    // Drive this car somewhere detour-heavy (behind the water, inside a
    // superblock: three tiles away, a block and a half on foot), lose the job
    // to the clock or to an arrest, and every retry recomputes the same
    // impossible allowance from the same stranded position, with nothing on
    // screen to say why and no other way to move the car back. The spawn
    // record is the one placement the generator VALIDATED -- a different
    // district from the phone, pavement beside it, not the nearest car to the
    // player spawn -- so resetting here is what makes those checks mean
    // something at runtime and not only at build time.
    //
    // On `begin` and deliberately not on `failed`: answering the phone means
    // standing on its cell on foot (`atContractPhone` refuses while driving),
    // so at this instant the car provably has no driver. Failure has no such
    // proof -- a job lost to the clock is most often lost in the car, and
    // teleporting a car out from under the player is a worse bug than the one
    // being fixed.
    //
    // `spawn()` is the same call `onEnterSpace` parks all two dozen cars with,
    // reused rather than reimplemented: it resets heading, colour, speed, the
    // turn cooldown, the crash accumulator, the throttle and `driven_`.
    //
    // But it SWAPS rather than stacks, because a respawn is the one placement
    // in this demo that happens mid-run with the city already arranged --
    // `TrafficPool::trySpawn` gates on a free spot and `onEnterSpace`'s parking
    // loop runs before anything has moved. Here the home tile is vacant
    // precisely BECAUSE the last attempt failed and left this car elsewhere, so
    // the player can legitimately drive another of the two dozen onto it and
    // walk away, and a blind `spawn()` would drop the boost car inside that
    // one's box. `VehicleActor::advance` stops dead on any overlap and has no
    // push-apart, so both cars would be stuck: the same silent unwinnable
    // chapter, through a different door. Refusing instead would be cheaper and
    // would give up exactly what the respawn exists for -- the spawn record is
    // the placement the generator PROVED fair, and the player having parked on
    // it is the case they are most likely to reach this code in.
    //
    // The displaced car goes where the boost car is standing, a destination
    // known good by construction rather than by a test: a parked car is
    // standing in it, so the box is already clear of scenery and of every other
    // car. The guard keeps that argument true -- when the boost car's own box
    // overlaps the home box its stand is not a destination, it IS part of the
    // spot, and nothing moves because nothing needs to: a car overlapping its
    // home box is within one car length of it, which is not stranded, and the
    // two Manhattan legs below are measured from where it stands.
    //
    // Streamed traffic is deliberately not consulted. A `TrafficPool` car on
    // the tile is driving through it and gone within a second or two, so the
    // overlap resolves itself; gating on it would make answering the phone fail
    // for a reason the player cannot see and cannot act on.
    VehicleActor& boost = vehicles_[car];
    const VehicleBox home =
        VehicleActor::boxFor(city_scene::VEHICLE_SPAWNS[car]);
    if (!boost.overlapsBox(home.left, home.top, home.width, home.height)) {
        // Counted before anything moves, because a half-done swap is worse than
        // no swap at all. Two cars parked nose to tail can both overlap the
        // home box while overlapping neither each other nor the boost car's
        // stand, and there is only one stand to give away: move the first and
        // the spot is STILL busy, so the respawn cannot happen and the car just
        // moved is sharing pixels with a boost car that never left -- a
        // rollback-less half-swap strands two cars where refusing outright
        // strands none.
        //
        // So the second occupant is looked for before the first is touched, and
        // finding one means doing nothing: no move, no respawn, the boost car
        // keeps its stand and the two legs below are measured from there. Worse
        // arithmetic, still a drivable city.
        const std::uint8_t sitting = parkedCarOn(home, car);
        const std::uint8_t crowded =
            (sitting == city_scene::NUM_VEHICLES)
                ? city_scene::NUM_VEHICLES
                : parkedCarOn(home, car, sitting);
        if (crowded == city_scene::NUM_VEHICLES) {
            if (sitting != city_scene::NUM_VEHICLES) {
                vehicles_[sitting].parkWhere(boost);
            }
            boost.spawn(city_scene::VEHICLE_SPAWNS[car]);
        }
    }
    const VehicleActor& target = vehicles_[car];

    // The walk is measured from where the player IS -- the phone's cell, which
    // is the one thing here that cannot be stale. The drive is measured from
    // the spawn the line above just restored, so both numbers describe the
    // same city the player is about to be handed.
    const int toCar =
        absInt(world.player.tileX() - target.tileX())
      + absInt(world.player.tileY() - target.tileY());
    const int carToDrop =
        absInt(target.tileX() - scene::MISSION_TARGETS[drop].tileX)
      + absInt(target.tileY() - scene::MISSION_TARGETS[drop].tileY);

    // No guard: begin() is a no-op from every phase but Idle, which is exactly
    // what a RUN edge that latches twice needs. See game/rules/Contract.h.
    contract::begin(world.contract, car, drop, toCar, carToDrop);
    showBanner("STEAL THE CAR");
    // And no cue: four SFX voices, rationed by audio_cues::admitCue, and
    // answering a phone is not an event the mix has a word for. A borrowed one
    // would say less than the banner and the marker jumping across the island.
}

void CityScene::takeHit() {
    CityWorld& world = CityWorld::instance();

    // Put the staff back on their posts before the clock is cut: chapter 1's
    // lesson with a corpse instead of a stranded car. Officers are placed once
    // per RUN, and nothing stops the player emptying a magazine into that lobby
    // on the way past, so the man this chapter is about can already be on the
    // floor when the phone is answered. `contract::struck` would then never
    // fire -- there is nobody left to shoot -- and the chapter would sit in
    // `ToTarget` behind a marker pointing at a door until the clock ran out,
    // hand itself back to the same phone, and do it again, with nothing on
    // screen to say why and no other way to put a body back on its feet. See
    // StationStaff::deployOnce.
    //
    // And the phone is not answered at all if the room is not there: a phone
    // that does not respond is at least a phone the player can see not
    // responding, where a contract started anyway would be the unwinnable
    // chapter this paragraph exists to refuse.
    //
    // Two calls, and their order is the argument: `deploy` clears the mark (the
    // fact belongs to the SLOT, see StationStaff.h) so marking has to follow
    // it, and `deploy` is also what tells the room it has been staffed this run
    // -- without which the player's first walk through that door would deploy
    // again and clear the mark they were just given.
    StationStaff* staff = staffBehindStationDoor();
    if (staff == nullptr) {
        return;
    }
    staff->deploy();
    staff->mark(scene::HIT_OFFICER_INDEX);

    // Both halves of the walk, and the second is the one easy to leave out.
    // `beginHit` prices the clock with `mission::allowanceSteps`, proven
    // generous against a tile COUNT -- and the count the generator validated is
    // to the DOOR. Behind it there is a lobby to cross to the far post, walked
    // at the same speed off the same clock, because PoliceStationScene ticks it
    // like every other space. Pay for the door only and the margin that makes
    // the leg walkable is spent indoors: the job would be finishable by car and
    // losable on foot, the silent-and-repeated failure Mission.h's allowance
    // exists to refuse.
    //
    // The two halves are measured differently and have to be. Outdoors is
    // Manhattan on purpose -- a grid, a player loose on it, no fixed starting
    // cell to have flood-filled from. Indoors there is one hand-laid room, one
    // mat, one post and a desk seven cells wide between them, so the indoor leg
    // is READ, not computed: `HIT_OFFICER_STEPS_IN` is the flood fill that
    // chose the mark in the first place -- 18 steps against 16 in a straight
    // line, and the gap is no accident of this room, because the mark is
    // SELECTED for being the longest walk in it. Recomputing here would stand a
    // second, cheaper answer next to the file that holds the right one, with
    // only allowanceSteps' margin hiding the difference.
    const int toDoor =
        absInt(world.player.tileX() - static_cast<int>(scene::POLICE_DOOR_TILE_X))
      + absInt(world.player.tileY() - static_cast<int>(scene::POLICE_DOOR_TILE_Y));
    const int doorToPost = static_cast<int>(scene::HIT_OFFICER_STEPS_IN);

    // No guard: `beginHit` is a no-op from every phase but Idle and from every
    // chapter but the Hit, which is exactly what a RUN edge that latches twice
    // needs. See game/rules/Contract.h.
    contract::beginHit(world.contract, scene::HIT_OFFICER_INDEX,
                       toDoor + doorToPost);
    showBanner("HIT THE STATION");
    // And no cue, on the same terms chapter 1 answers its phone: four SFX
    // voices, and a borrowed sound would say less than the banner and the
    // marker jumping to the far side of the island.
}

void CityScene::takeFrenzy() {
    CityWorld& world = CityWorld::instance();
    // Nothing to put back and nothing to measure, which makes this the short
    // arm. The two chapters before it both repair the world before starting --
    // a boost car that may have been driven off its stand, a marked officer who
    // may already be face down -- because both name a thing in the city the
    // player can have moved. The rampage names a corner and a number, and the
    // player is standing on the corner.
    //
    // No guard either: `beginFrenzy` is a no-op from every phase but Idle and
    // every chapter but the Frenzy, which is exactly what a RUN edge that
    // latches across two frames needs. See game/rules/Contract.h.
    contract::beginFrenzy(world.contract);
    showBanner("RAMPAGE");
    // And no cue, on the terms the other two phones answer: four SFX voices,
    // the banner says more than a borrowed sound would, and the chapter is
    // about to be extremely loud on its own.
}

bool CityScene::contractMarked() const {
    const CityWorld& world = CityWorld::instance();
    // `hasDestination` -- the narrowest of the three predicates, and the
    // opposite call to the sites that suppress the courier's ring: those ask
    // whether the main mission owns the player, this asks whether it is sending
    // them anywhere, and two phases answer no. During `Struck` the objective is
    // the star row, already on screen; during `Rampage` it is a count and the
    // corner is under the player's feet, where a cyan ring points at nothing
    // they have left to do, on the beat with the most to look at.
    //
    // One predicate rather than `active` minus two exceptions, because this is
    // also the guard on `contractTileX`/`contractTileY`: every phase that
    // answers true here has an arm in both of those switches, and the day one
    // does not, it is this line that has to change rather than a `default:`
    // quietly returning a payphone.
    return contract::hasDestination(world.contract) || contractOffered();
}

int CityScene::contractTileX() const {
    const CityWorld& world = CityWorld::instance();
    switch (world.contract.phase) {
        case contract::Phase::ToCar:
            // The car's live tile rather than its spawn record, even though
            // `takeContract` has just made the two the same: this leg is the
            // walk TO the car, so nothing is driving it, but the marker
            // should follow the object rather than the table if that ever
            // stops being true. One read, no second source of truth.
            return vehicles_[world.contract.vehicle].tileX();
        case contract::Phase::ToDrop:
            return scene::MISSION_TARGETS[world.contract.drop].tileX;
        case contract::Phase::ToTarget:
            // The DOOR, not the officer. The mark stands in a room with its own
            // coordinate space -- post (3,2) of a 15x15 lobby, which out on the
            // island is a stretch of water in the north-west -- so a marker
            // following him would point somewhere the player can neither see
            // nor walk to, and would draw as an ordinary ring on an ordinary
            // tile. `contract::State` keeps `mark` and `drop` in separate
            // fields precisely so this arm cannot subscript the wrong table;
            // pointing at the wrong SPACE is that mistake one level up, and
            // this is the only place it can be made. The station door is a city
            // fact and lives in the city's own table, beside the shop's: once
            // through it there is one room, one route, and one screen.
            return static_cast<int>(scene::POLICE_DOOR_TILE_X);
        default:
            return scene::CONTRACT_PHONES[
                static_cast<std::uint8_t>(world.contract.chapter)].tileX;
    }
}

int CityScene::contractTileY() const {
    const CityWorld& world = CityWorld::instance();
    switch (world.contract.phase) {
        case contract::Phase::ToCar:
            return vehicles_[world.contract.vehicle].tileY();
        case contract::Phase::ToDrop:
            return scene::MISSION_TARGETS[world.contract.drop].tileY;
        case contract::Phase::ToTarget:
            return static_cast<int>(scene::POLICE_DOOR_TILE_Y);
        default:
            return scene::CONTRACT_PHONES[
                static_cast<std::uint8_t>(world.contract.chapter)].tileY;
    }
}

bool CityScene::atContractDrop() const {
    const CityWorld& world = CityWorld::instance();
    if (world.contract.phase != contract::Phase::ToDrop) {
        return false;
    }
    // In THAT car, not merely in a car. Finishing the job in a taxi picked up
    // on the way is a different job -- and the identity test is also what
    // makes losing the car survivable instead of fatal: the phase stays
    // ToDrop, so the player can walk back to it and carry on with whatever the
    // clock has left. Nothing else fails them for the abandonment.
    if (driving_ == nullptr
            || driving_ != &vehicles_[world.contract.vehicle]) {
        return false;
    }
    const scene::MissionTarget& drop =
        scene::MISSION_TARGETS[world.contract.drop];
    const int dropX = drop.tileX * kTilePx + kTilePx / 2;
    const int dropY = drop.tileY * kTilePx + kTilePx / 2;
    // The same radius the courier's drop uses, off the same focus, so
    // "arrived" means one thing in this city. Here the focus is necessarily
    // the car, which is the point of the chapter.
    return threat::within(focusCentreX() - dropX, focusCentreY() - dropY,
                          kMarkerRadiusPx);
}

VehicleActor* CityScene::vehicleInReach() {
    if (driving_ != nullptr) {
        return nullptr;
    }
    const PlayerActor& player = CityWorld::instance().player;
    const int left = player.spriteX() + kPlayerBoxOffsetX;
    const int top  = player.spriteY() + kPlayerBoxOffsetY;

    VehicleActor* best = nullptr;
    long bestDistance = 0;
    for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
        VehicleActor& vehicle = vehicles_[i];
        if (!vehicle.isWithinRangeOf(left, top, kPlayerBoxWidth,
                                     kPlayerBoxHeight, kVehicleEnterRangePx)) {
            continue;
        }
        // Nearest by squared centre distance: standing between two cars is
        // rare, but picking one at random when it happens is the kind of thing
        // a player notices and cannot explain.
        const long dx = vehicle.centreX() - player.centreX();
        const long dy = vehicle.centreY() - player.centreY();
        const long distance = dx * dx + dy * dy;
        if (best == nullptr || distance < bestDistance) {
            best = &vehicle;
            bestDistance = distance;
        }
    }
    if (best != nullptr) {
        return best;
    }
    // Only if nothing parked was within reach -- see the class comment.
    return traffic_.inReachOf(left, top, kPlayerBoxWidth, kPlayerBoxHeight,
                              kVehicleEnterRangePx);
}

void CityScene::enterVehicle(VehicleActor& vehicle) {
    CityWorld& world = CityWorld::instance();
    driving_ = &vehicle;
    vehicle.setDriven(true);
    // The player is not moved: they are inside the car, and where they get out
    // is decided when they get out.
    world.player.setVisible(false);
    AudioDirector::instance().playCue(audio_cues::Cue::VehicleEnter);

    // The boarding test, an ADDRESS comparison rather than an index one:
    // `vehicle` may be a traffic slot rather than a parked car, and traffic has
    // no index into VEHICLE_SPAWNS to compare against. Comparing the object is
    // the only form of the question true for exactly one car in the city.
    //
    // The phase is not checked here: boarded() is a no-op from anything but
    // ToCar, which is what makes it safe to call from a place that also fires
    // for every ordinary car theft in the run. The read-back is only so the
    // banner fires on the transition rather than on every one of them.
    const contract::Phase before = world.contract.phase;
    if (&vehicle == &vehicles_[world.contract.vehicle]) {
        contract::boarded(world.contract);
    }
    if (world.contract.phase != before) {
        // Worth a word: the player has just taken a car out of two dozen and
        // the only other confirmation is a marker jumping to the far side of
        // the island, which is off screen by definition.
        showBanner("DRIVE IT");
    }
}

void CityScene::exitVehicle() {
    if (driving_ == nullptr) {
        return;
    }
    PlayerActor& player = CityWorld::instance().player;
    const int carX = driving_->spriteX();
    const int carY = driving_->spriteY();

    for (int i = 0; i < 4; ++i) {
        const int px = carX + kExitOffsets[i][0];
        const int py = carY + kExitOffsets[i][1];
        // driving_ is excluded from the obstacle test: the car being climbed
        // out of covers every spot beside it.
        if (!PlayerActor::canOccupy(px, py, driving_)) {
            continue;
        }
        player.placeAt(px, py, kExitFacing[i]);
        player.setVisible(true);
        driving_->setDriven(false);
        driving_ = nullptr;
        AudioDirector::instance().playCue(audio_cues::Cue::VehicleExit);
        return;
    }
    // All four doors are against something solid. Staying in the car is the
    // only answer that cannot put the player inside a wall.
}

bool CityScene::stepSpace(const StepInput& in) {
    CityWorld& world = CityWorld::instance();

    stepWantedClock();
    stepObjectiveClock();

    // ADR-18: startleNear returns a LEVEL, not an edge, so this is OR'd across
    // every car involved and Cue::CrowdPanic fires at most once per step. The
    // cue's own cooldown row is the edge detector.
    bool anyStartled = false;
    if (driving_ != nullptr) {
        driving_->step(in.up, in.down, in.left, in.right);
        // ADR-19: the pedal EDGE, from the car's own throttle history, never a
        // speed delta sampled across the director's per-frame clock -- see
        // pedalEventFor for why that would miss short taps and invent events.
        switch (audio_cues::pedalEventFor(driving_->throttleState(),
                                          prevThrottle_,
                                          driving_->speedSub())) {
            case audio_cues::PedalEvent::Accelerated:
                AudioDirector::instance().playCue(audio_cues::Cue::EngineRev);
                break;
            case audio_cues::PedalEvent::Braked:
                AudioDirector::instance().playCue(
                    audio_cues::Cue::BrakeScreech);
                break;
            case audio_cues::PedalEvent::None:
                break;
        }
        prevThrottle_ = driving_->throttleState();
        // Only the driven car is polled (R8.1): a crash heard three streets
        // away is not something a mono mix should ever claim.
        if (audio_cues::crashIsAudible(driving_->consumeCrash())) {
            AudioDirector::instance().playCue(audio_cues::Cue::VehicleCrash);
        }
        // Get out of the way first, then get run over: a pedestrian already
        // clear this step must not be squashed by the step that scared them.
        // Only the driven car, and only while lethal -- a crowd that fled from
        // every parked bonnet would never stand still anywhere.
        if (driving_->isLethal()) {
            anyStartled = pedestrians_.startleNear(
                driving_->centreX(), driving_->centreY(), kCarThreatPx);
        }
        // Every step, not every frame: at top speed the car covers three
        // pixels a step and a pedestrian is ten wide, so a per-frame test
        // would drive through people at low frame rates. Billed to the player,
        // unlike the traffic's below: this one had a driver.
        reportPersonHit(pedestrians_.runOverBy(*driving_));
    } else {
        prevThrottle_ = 0;
        movePlayerOnFoot(in);
    }
    pedestrians_.step(cameraOriginX(), cameraOriginY());

    // Traffic brakes for the player on foot rather than running them down: the
    // alternative needs damage, an invulnerability window and a reason the
    // player was in the road, and a car stopping for somebody crossing reads
    // as traffic either way.
    traffic::Yield yieldTo{0, 0, 0, 0};
    if (driving_ == nullptr) {
        yieldTo = traffic::Yield{
            world.player.spriteX() + kPlayerBoxOffsetX,
            world.player.spriteY() + kPlayerBoxOffsetY,
            kPlayerBoxWidth, kPlayerBoxHeight};
    }
    // Below two stars the count is zero, the chase is inactive and the pool
    // never spawns a patrol -- so the whole mechanism costs a table read and a
    // compare for most of a session.
    const std::uint8_t patrols = wanted::patrolCars(world.wanted.stars);
    const traffic::Chase chase{patrols > 0, focusCentreX(), focusCentreY()};
    // ADR-15/ADR-18: what the traffic SOUNDED like this step, not what it
    // played -- this pool must not learn what an AudioDirector is.
    const TrafficNoise trafficNoise = traffic_.step(
        cameraOriginX(), cameraOriginY(), driving_, yieldTo, chase, patrols);
    if (trafficNoise.passedBy) {
        AudioDirector::instance().playTrafficPass(trafficNoise.passHigh);
    }
    if (trafficNoise.honked) {
        AudioDirector::instance().playCue(audio_cues::Cue::CarHorn);
    }

    // The crowd does not get the same courtesy, and that is the point. The
    // scare radius is half the player car's: kTrafficThreatPx is the lane and
    // its kerb, while kCarThreatPx would have the whole pavement bolting every
    // time anything drove past.
    for (std::uint16_t slot = traffic_.firstLive();
         slot != TrafficPool::kEnd; slot = traffic_.nextLive(slot)) {
        VehicleActor* car = traffic_.at(slot);
        if (car == driving_ || !car->isLethal()) {
            continue;
        }
        anyStartled = pedestrians_.startleNear(
            car->centreX(), car->centreY(), kTrafficThreatPx)
            || anyStartled;
        // Not reported: billing the player for an accident they had no hand in
        // reads as the star counter being random.
        pedestrians_.runOverBy(*car);

        // The player is not in the crowd, so they need asking separately.
        // Traffic yields to somebody in the road ahead of it, so this is
        // walking into the SIDE of a moving car -- the only way to be hit.
        if (driving_ == nullptr) {
            const int boxLeft = world.player.spriteX() + kPlayerBoxOffsetX;
            const int boxTop  = world.player.spriteY() + kPlayerBoxOffsetY;
            if (car->overlapsBox(boxLeft, boxTop,
                                 kPlayerBoxWidth, kPlayerBoxHeight)
                    && world.player.hitByVehicle()) {
                showBanner("OUCH", kZoneBannerMs / 3);
                // Same gate as hitPlayer()'s: the killing blow gets
                // Cue::Busted from checkPlayerDown() below, never both.
                if (!world.player.isDown()) {
                    AudioDirector::instance().playCue(
                        audio_cues::Cue::PlayerHit);
                }
            }
        }
    }
    if (anyStartled) {
        AudioDirector::instance().playCue(audio_cues::Cue::CrowdPanic);
    }

    stepGuns();
    // The focus, not the player: officers chasing a driver's abandoned kerb
    // would walk to an empty spot and open fire on it. Aim, then walk -- the
    // other order is a manhunt permanently one step behind.
    pedestrians_.hunt(focusCentreX(), focusCentreY(),
                      wanted::pursuitRangePx(world.wanted.stars),
                      wanted::chaseSpeedSub(world.wanted.stars));
    if (pedestrians_.returnFire(focusCentreX(), focusCentreY(),
                                world.returnFire)) {
        AudioDirector::instance().playCue(audio_cues::Cue::PoliceGunshot);
    }
    if (checkPlayerDown()) {
        return false;
    }
    collectWeapon();

    // Arrival before expiry, so a drop made on the last step counts. The other
    // order costs a streak -- or a whole chapter -- for being exactly on time.
    //
    // And the chapter is asked INSTEAD of the courier leg. This is the third
    // and last place "one objective at a time" is spelled out: the leg's clock
    // was not ticked in stepObjectiveClock, its ring is not drawn in drawSpace,
    // and its arrival is not tested here. All three have to agree, or the
    // player is failed for a leg the HUD never showed them -- or paid for one
    // they never drove.
    //
    // `underway`, matching the freeze and the expiry test: during `Struck` the
    // chapter still owns the player, and falling through to the courier's arm
    // below would hand out a DELIVERY -- fee, banner, cue and a fresh leg --
    // for standing on a drop while walking a manhunt home. `atContractDrop` is
    // false outside `ToDrop`, so the else arm is what actually runs, and
    // `objectiveExpired` is a no-op during `Struck`.
    if (contract::underway(world.contract)) {
        if (atContractDrop()) {
            contract::delivered(world.contract);
            // Flat, so unlike the courier's fee it does not have to be read
            // after the state moves: kBoostFee is priced against the shop
            // counter rather than against a streak. See game/rules/Economy.h.
            economy::earn(world.purse, economy::kBoostFee);
            showBanner("BOOSTED");
            // The courier's delivery cue, reused: four SFX voices, and the
            // player already knows what this one means.
            AudioDirector::instance().playCue(
                audio_cues::Cue::MissionDelivered);
        } else {
            objectiveExpired();
        }
        // The courier leg is not tested on this step either way, including the
        // step the chapter ENDS on -- it comes back next step with the clock
        // it had, rather than being handed a delivery for standing where the
        // job happened to finish.
        return true;
    }

    if (atMissionDrop()) {
        nextLeg(true);
        showBanner("DELIVERED");
        AudioDirector::instance().playCue(audio_cues::Cue::MissionDelivered);
    } else {
        objectiveExpired();
    }
    return true;
}

void CityScene::drawSpace(gfx::Renderer& renderer) {
    const int cameraX = cameraOriginX();
    const int cameraY = cameraOriginY();

    // No screen clear: the camera is clamped inside the world, so Background
    // always covers the full viewport. The renderer culls each layer to the
    // viewport, so three 128x128 layers cost three times ~16x16 tiles.
    renderer.drawTileMap(scene::background, 0, 0, gfx::LayerType::Static);
    renderer.drawTileMap(scene::items, 0, 0, gfx::LayerType::Static);
    renderer.drawTileMap(scene::details, 0, 0, gfx::LayerType::Static);

    // Bottom to top: bodies lie in the road, cars drive over them, people walk
    // in front of the cars, the player is drawn last of all.
    pedestrians_.drawGround(renderer, cameraX, cameraY);

    for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
        VehicleActor& vehicle = vehicles_[i];
        // One AABB each: cheaper than asking the renderer to clip two dozen
        // sprites that are nowhere near the viewport.
        if (vehicle.spriteX() + kVehicleSpriteW <= cameraX ||
            vehicle.spriteX() >= cameraX + DISPLAY_WIDTH ||
            vehicle.spriteY() + kVehicleSpriteH <= cameraY ||
            vehicle.spriteY() >= cameraY + DISPLAY_HEIGHT) {
            continue;
        }
        vehicle.draw(renderer);
    }

    // Same layer as the parked cars and no depth sort: two cars cannot occupy
    // the same tile, so nothing here can overlap.
    traffic_.draw(renderer, cameraX, cameraY);

    // Before anything that walks: somebody standing on a pistol covers it,
    // which is the only depth cue a top-down view has.
    pickups_.draw(renderer, cameraX, cameraY);

    // The objective rings. The courier's is drawn only while it is the thing
    // being asked for -- `underway`, not `active`, so the leg is frozen for the
    // whole of the Hit including the untimed retreat. A green ring over a
    // frozen clock is a destination that pays nothing and costs nothing, which
    // the player has no way of knowing by looking, and during `Struck` it would
    // also be the ONLY ring on screen -- the chapter deliberately has none --
    // so the player would follow it, the worst version of the mistake.
    //
    // The two ARE drawn together while the phone is merely ringing, and that is
    // not a contradiction. An offer is not an objective: the courier run is
    // still live, and a phone nobody can find is a story nobody starts.
    if (!contract::underway(CityWorld::instance().contract)) {
        drawObjectiveRing(renderer, missionTileX(), missionTileY(),
                          cameraX, cameraY, hud::kObjective);
    }
    if (contractMarked()) {
        drawObjectiveRing(renderer, contractTileX(), contractTileY(),
                          cameraX, cameraY, hud::kContract);
    }

    pedestrians_.drawWalking(renderer, cameraX, cameraY);

    Scene::draw(renderer);   // the player, hidden while they are driving
}

void CityScene::drawSpaceOverlay(gfx::Renderer& renderer) {
    drawMinimap(renderer);
}

void CityScene::drawMinimap(gfx::Renderer& renderer) {
    // Three city blocks square around the focus, one pixel per tile. The
    // window is centred on the focus tile and NEVER clamped to the map: the
    // marker stays dead centre wherever the player is, and cells outside the
    // island are skipped rather than clamped, so the plate showing through is
    // how the overlay draws the coast.
    //
    // Two inks and an absence. Painting every terrain in its own colour made a
    // small picture of the island, and a picture is what a radar must not be:
    // every extra colour is one more thing the eye has to rule out first.
    //
    // The inks are ENGINE palette indices, because that is what a primitive
    // reads (see hud:: in CityConstants.h) -- which is also why the overlay
    // does not darken at night: the cycle tints the palettes the CITY is drawn
    // from, and the HUD is not drawn from any of them.
    const int halfTiles  = kMinimapTiles / 2;
    const int firstTileX = focusTileX() - halfTiles;
    const int firstTileY = focusTileY() - halfTiles;

    renderer.drawFilledRectangle(kMinimapOriginX - 2, kMinimapOriginY - 2,
                                 kMinimapSize + 4, kMinimapSize + 4,
                                 hud::kRadarPlate);

    // One row at a time, resolved into swatches first and drawn as runs of
    // equal colour second: that collapses about 1625 filled rectangles per
    // frame into about 162, each of which was costing a palette resolve, two
    // virtual dispatches and a dirty-region mark to put down a single pixel.
    // See MinimapRuns.h for the measurement and test_minimap for the proof that
    // the runs draw the same picture the cells did. Both buffers are stack,
    // ~620 bytes, and deliberately not members: working space for one call.
    std::uint8_t   rowSwatches[kMinimapTiles];
    minimap::Run   runs[kMinimapTiles];

    for (int row = 0; row < kMinimapTiles; ++row) {
        const int tileY = firstTileY + row;
        if (tileY < 0 || tileY >= kWorldTilesY) {
            continue;
        }
        const int screenY = kMinimapOriginY + row * kMinimapScale;
        const int mapRow  = tileY * kWorldTilesX;

        // The background layer and nothing else. Items could only ever answer
        // "there is a building here", which the ground under it already says.
        // Water carries kAbsent, which puts the sea and the world past the
        // coast down the same path: undrawn.
        for (int col = 0; col < kMinimapTiles; ++col) {
            const int tileX = firstTileX + col;
            rowSwatches[col] =
                (tileX < 0 || tileX >= kWorldTilesX)
                    ? minimap::kAbsent
                    : scene::MINIMAP_SWATCH_BACKGROUND[
                          scene::background.indices[mapRow + tileX]];
        }

        const int emitted = minimap::rowRuns(rowSwatches, kMinimapTiles, runs);
        for (int i = 0; i < emitted; ++i) {
            renderer.drawFilledRectangle(
                kMinimapOriginX + runs[i].start * kMinimapScale, screenY,
                runs[i].length * kMinimapScale, kMinimapScale,
                static_cast<gfx::Color>(runs[i].swatch));
        }
    }

    // Parked cars, so the overlay answers the question the player actually has
    // on foot: where is the nearest one. At three blocks the honest answer is
    // sometimes "none of them", which a whole-island map could never say.
    for (std::uint8_t i = 0; i < city_scene::NUM_VEHICLES; ++i) {
        const int col = vehicles_[i].tileX() - firstTileX;
        const int row = vehicles_[i].tileY() - firstTileY;
        if (col < 0 || col >= kMinimapTiles || row < 0 || row >= kMinimapTiles) {
            continue;
        }
        renderer.drawFilledRectangle(kMinimapOriginX + col * kMinimapScale,
                                     kMinimapOriginY + row * kMinimapScale,
                                     kMinimapScale, kMinimapScale,
                                     hud::kRadarCar);
    }

    // The objectives, on the same terms as their rings on the ground. The
    // chapter is drawn second so that on the one cell where the clamp could
    // stack them -- both pinned to the same edge of the window -- the live job
    // is the one on top. `underway` here too, and the three have to agree: the
    // ring on the ground, the dot on the radar and the number on the plate are
    // one objective seen three ways, and two of them saying "the courier" while
    // the third says nothing is not a HUD, it is a puzzle.
    if (!contract::underway(CityWorld::instance().contract)) {
        drawRadarDot(renderer, missionTileX(), missionTileY(),
                     firstTileX, firstTileY, hud::kObjective);
    }
    if (contractMarked()) {
        drawRadarDot(renderer, contractTileX(), contractTileY(),
                     firstTileX, firstTileY, hud::kContract);
    }

    // "You are here": a cross rather than a dot, so the marker stays visible
    // over any terrain colour. A constant position -- the window moves under
    // it instead.
    renderer.drawLine(kMinimapCentreX - 3, kMinimapCentreY,
                      kMinimapCentreX + 3, kMinimapCentreY, hud::kRadarYou);
    renderer.drawLine(kMinimapCentreX, kMinimapCentreY - 3,
                      kMinimapCentreX, kMinimapCentreY + 3, hud::kRadarYou);
    renderer.drawPixel(kMinimapCentreX, kMinimapCentreY, hud::kRadarYouCore);
}

}  // namespace top_down_city
