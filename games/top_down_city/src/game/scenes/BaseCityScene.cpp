#include "game/scenes/BaseCityScene.h"

#include <core/Engine.h>
#include <graphics/Color.h>

#include "audio/AudioDirector.h"
#include "game/systems/CityCollision.h"
#include "generated/tilemaps/city_scene.h"

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace top_down_city {

namespace gfx = pr32::graphics;
namespace math = pr32::math;

using math::toScalar;

namespace {

/// A five-pixel heart, as four rectangles.
void drawHeart(gfx::Renderer& renderer, int x, int y, gfx::Color colour) {
    renderer.drawFilledRectangle(x,     y,     2, 1, colour);
    renderer.drawFilledRectangle(x + 3, y,     2, 1, colour);
    renderer.drawFilledRectangle(x,     y + 1, 5, 2, colour);
    renderer.drawFilledRectangle(x + 1, y + 3, 3, 1, colour);
    renderer.drawFilledRectangle(x + 2, y + 4, 1, 1, colour);
}

/// A five-pixel shield: square shoulders and a point, which is the smallest
/// shape that is not read as another heart or another diamond.
void drawShield(gfx::Renderer& renderer, int x, int y, gfx::Color colour) {
    renderer.drawFilledRectangle(x,     y,     5, 3, colour);
    renderer.drawFilledRectangle(x + 1, y + 3, 3, 1, colour);
    renderer.drawFilledRectangle(x + 2, y + 4, 1, 1, colour);
}

/// A five-pixel diamond, as three rectangles -- one row is shared between the
/// top and bottom halves, which is why there are three and not five.
void drawDiamond(gfx::Renderer& renderer, int x, int y, gfx::Color colour) {
    renderer.drawFilledRectangle(x + 2, y,     1, 1, colour);
    renderer.drawFilledRectangle(x + 1, y + 1, 3, 1, colour);
    renderer.drawFilledRectangle(x,     y + 2, 5, 1, colour);
    renderer.drawFilledRectangle(x + 1, y + 3, 3, 1, colour);
    renderer.drawFilledRectangle(x + 2, y + 4, 1, 1, colour);
}

}  // namespace

BaseCityScene::BaseCityScene()
    : camera_(DISPLAY_WIDTH, DISPLAY_HEIGHT),
      accumulatorMs_(0),
      zoneBannerMs_(kZoneBannerMs),
      bannerOverride_(nullptr),
      frenzyLabel_{},
      bannerSerial_(0),
      hintVisible_(false),
      drawnFocusKey_(0),
      drawnCrowdKey_(0),
      drawnWeaponKey_(0),
      drawnReturnKey_(0),
      drawnObjectiveSeconds_(-1),
      drawnAmmo_(0),
      drawnCash_(0),
      drawnStars_(0),
      drawnHealth_(0),
      drawnArmor_(0),
      drawnMissionTarget_(0),
      drawnContractPhase_(0),
      drawnZone_(0),
      drawnTintStep_(0),
      drawnCooling_(false),
      drawnBusted_(false),
      drawnBannerVisible_(false),
      drawnBannerSerial_(0),
      drawnHintVisible_(false),
      drawnOnce_(false) {
}

void BaseCityScene::init() {
    Scene::init();

    CityWorld& world = CityWorld::instance();
    const bool firstEntry = world.bootOnce();

    // AFTER Scene::init(), never in a constructor: init() runs resetState(),
    // which calls clearEntities(), so an entity added at construction time is
    // silently dropped -- an invisible actor, not a crash. The player is the
    // ONLY registered entity in either space and belongs to CityWorld, so both
    // scenes register the same object; only one is ever on the engine's stack.
    addEntity(&world.player);

    // Before the space, so anything it puts up on arrival wins. A scene keeps
    // its banner state across a visit, so without this the city would step
    // back out of the station still announcing what it was on the way in.
    bannerOverride_ = nullptr;
    zoneBannerMs_   = kZoneBannerMs;

    onEnterSpace(firstEntry);

    // After the space exists, because both sinks reach into it. Re-installed
    // on every entry rather than swapped at the doorway.
    world.weapons.setTargetSink(&BaseCityScene::hitTarget, this);
    world.returnFire.setTargetSink(&BaseCityScene::hitPlayer, this);

    accumulatorMs_ = 0;
    hintVisible_   = false;
    drawnOnce_     = false;
}

std::uint8_t BaseCityScene::zoneUnderFocus() const {
    return CityWorld::instance().zone;
}

int BaseCityScene::focusCentreX() const {
    return CityWorld::instance().player.centreX();
}

int BaseCityScene::focusCentreY() const {
    return CityWorld::instance().player.centreY();
}

int BaseCityScene::focusTileX() const {
    return CityWorld::instance().player.tileX();
}

int BaseCityScene::focusTileY() const {
    return CityWorld::instance().player.tileY();
}

std::uint32_t BaseCityScene::focusVisualKey() const {
    return CityWorld::instance().player.visualKey();
}

void BaseCityScene::focusHitBox(int& left, int& top,
                                int& width, int& height) const {
    const PlayerActor& player = CityWorld::instance().player;
    left   = player.spriteX() + kPlayerBoxOffsetX;
    top    = player.spriteY() + kPlayerBoxOffsetY;
    width  = kPlayerBoxWidth;
    height = kPlayerBoxHeight;
}

int BaseCityScene::cameraOriginX() const {
    const int high = collision::spaceWidthPx() - DISPLAY_WIDTH;
    return clampInt(focusCentreX() - DISPLAY_WIDTH / 2, 0, high > 0 ? high : 0);
}

int BaseCityScene::cameraOriginY() const {
    const int high = collision::spaceHeightPx() - DISPLAY_HEIGHT;
    return clampInt(focusCentreY() - DISPLAY_HEIGHT / 2, 0, high > 0 ? high : 0);
}

void BaseCityScene::applySpaceBounds() {
    const int highX = collision::spaceWidthPx() - DISPLAY_WIDTH;
    const int highY = collision::spaceHeightPx() - DISPLAY_HEIGHT;
    camera_.setBounds(toScalar(0), toScalar(highX > 0 ? highX : 0));
    camera_.setVerticalBounds(toScalar(0), toScalar(highY > 0 ? highY : 0));
    camera_.setPosition(math::Vector2(cameraOriginX(), cameraOriginY()));
}

bool BaseCityScene::hitTarget(void* context, int left, int top, int width,
                              int height, std::uint8_t damage) {
    BaseCityScene* scene = static_cast<BaseCityScene*>(context);
    const PersonHit hit =
        scene->hitPersonBox(left, top, width, height, damage);
    scene->reportPersonHit(hit);
    return hit.any;
}

bool BaseCityScene::hitPlayer(void* context, int left, int top, int width,
                              int height, std::uint8_t damage) {
    BaseCityScene* scene = static_cast<BaseCityScene*>(context);
    CityWorld& world = CityWorld::instance();

    int boxLeft = 0;
    int boxTop  = 0;
    int boxW    = 0;
    int boxH    = 0;
    scene->focusHitBox(boxLeft, boxTop, boxW, boxH);
    if (left >= boxLeft + boxW || boxLeft >= left + width
        || top >= boxTop + boxH || boxTop >= top + height) {
        return false;
    }
    world.player.hit(scene->incomingDamage(damage));
    // hit()'s bool return is true ONLY on the blow that brings health to zero;
    // a non-fatal hit and a no-op against an already-downed player both return
    // false, so it cannot tell them apart. !isDown() right after the call can:
    // true exactly when a live player just took real damage.
    if (!world.player.isDown()) {
        AudioDirector::instance().playCue(audio_cues::Cue::PlayerHit);
    }
    return true;
}

bool BaseCityScene::contractIsLive() const {
    // `active` and NOT `underway`, unlike the freeze and the expiry test: this
    // is a question about the PLATE, whose number is on it. During `Struck` the
    // chapter owns the player but has no clock, so the honest answer is no --
    // and `objectiveIsTimed` below stops that being read as "the courier's".
    return contract::active(CityWorld::instance().contract);
}

bool BaseCityScene::objectiveIsTimed() const {
    const CityWorld& world = CityWorld::instance();
    // The chapter's clock while a chapter has one, the courier's while no
    // chapter owns the player -- and during `Struck`, neither. That last case
    // is the only reason this exists, and leaving it out is silent:
    // `objectiveSecondsLeft` would fall through to the courier's counter, which
    // stepObjectiveClock has frozen, and the plate would sit there in green
    // showing a number that does not move for the whole retreat. Getting clean
    // is untimed on purpose (see game/rules/Contract.h), and a stopped clock
    // reads as a bug long before it reads as a design.
    return contract::active(world.contract)
        || !contract::underway(world.contract);
}

int BaseCityScene::objectiveSecondsLeft() const {
    const CityWorld& world = CityWorld::instance();
    // One plate, one clock. The frozen one is not shown at all rather than
    // greyed: the column has no second row to put it in -- kWeaponOriginY is
    // asserted clear of the prompt strip by a handful of pixels -- and a timer
    // nobody can act on is a timer that only worries. `active`, matching
    // contractIsLive, so the two pick the same counter and no number is drawn
    // in the other one's ink. What happens when NEITHER is running is not
    // decided here -- see objectiveIsTimed.
    const std::uint16_t steps = contract::active(world.contract)
                                    ? world.contract.stepsLeft
                                    : world.mission.stepsLeft;
    return (static_cast<int>(steps) * kLogicStepMs + 999) / 1000;
}

void BaseCityScene::nextLeg(bool delivered) {
    CityWorld& world = CityWorld::instance();
    const std::uint8_t next = mission::nextTarget(
        world.mission.target, scene::NUM_MISSION_TARGETS,
        world.missionRng.next());
    // Manhattan from where the player is NOW, not from the drop they just
    // left: somebody chased across the island since the last delivery has
    // further to go than the two markers suggest.
    const int distance =
        absInt(focusTileX() - scene::MISSION_TARGETS[next].tileX)
      + absInt(focusTileY() - scene::MISSION_TARGETS[next].tileY);
    if (delivered) {
        mission::delivered(world.mission, next, distance);
        // After delivered(), not before: the fee reads the streak this drop
        // just earned, so the first delivery of a run pays for one rather
        // than for none. See game/rules/Economy.h.
        economy::earn(world.purse,
                      economy::deliveryFee(world.mission.streak));
    } else {
        mission::failed(world.mission, next, distance);
    }
}

bool BaseCityScene::policeCanSee() {
    CityWorld& world = CityWorld::instance();
    if (!wanted::isWanted(world.wanted)) {
        return false;
    }
    const int reach =
        static_cast<int>(wanted::pursuitRangePx(world.wanted.stars));
    // The focus, not the player: while driving, the player is still standing
    // on the pavement they got in at, and an officer looking at that empty
    // kerb is not looking at the player.
    return crowdSees(focusCentreX(), focusCentreY(), reach);
}

void BaseCityScene::reportCrime(wanted::Crime crime) {
    CityWorld& world = CityWorld::instance();
    const std::uint8_t before = world.wanted.stars;
    wanted::report(world.wanted, crime);
    if (world.wanted.stars == before) {
        return;
    }
    AudioDirector::instance().playWantedUp(world.wanted.stars);
    onWantedChanged(world.wanted.stars);
    if (before == 0) {
        // Only on the way UP from nothing. The star row appearing is the
        // feedback for every level after the first, and a banner on each of
        // them would spend the whole chase covering the radar.
        showBanner("WANTED");
    }
}

void BaseCityScene::reportPersonHit(const PersonHit& hit) {
    if (!hit.any) {
        return;
    }
    // ADR-16: one Roadkill cue, not two. The civilian/officer distinction is
    // already audible through playWantedUp's pitch, so this fires before the
    // crime routing rather than branching on hit.officer.
    AudioDirector::instance().playCue(audio_cues::Cue::Roadkill);
    if (hit.officer) {
        reportCrime(hit.killed ? wanted::Crime::OfficerKilled
                               : wanted::Crime::OfficerHurt);
    } else {
        reportCrime(hit.killed ? wanted::Crime::CivilianKilled
                               : wanted::Crime::CivilianHurt);
    }
    if (hit.killed) {
        // After the crime, so the chapter's banner is the one left standing:
        // `reportCrime` puts "WANTED" up on the way off zero, and a rampage's
        // first body is exactly that step -- the count is what has to be read.
        countRampageBody();
    }
}

void BaseCityScene::countRampageBody() {
    CityWorld& world = CityWorld::instance();
    // Read the phase, move it, then ask what moved -- the same shape
    // CityScene uses around `contract::boarded`. The alternative is to test
    // for `Rampage` here first, which is `contract::culled`'s own guard
    // written a second time on the far side of the call.
    const contract::Phase before = world.contract.phase;
    contract::culled(world.contract);
    if (before != contract::Phase::Rampage) {
        return;
    }
    if (world.contract.phase == contract::Phase::Rampage) {
        showBodiesLeft(world.contract.targetsLeft);
        return;
    }
    // The last body, which is also the last chapter. Flat and read after the
    // state moved, like the other two: kFrenzyFee is priced against the shop
    // counter rather than against anything the state holds.
    economy::earn(world.purse, economy::kFrenzyFee);
    showBanner("STORY OVER");
    // Chapter 1's delivery cue for the third time: four SFX voices, and a
    // sound the player already knows the meaning of beats a new one.
    AudioDirector::instance().playCue(audio_cues::Cue::MissionDelivered);
}

void BaseCityScene::showBodiesLeft(std::uint8_t left) {
    // Two digits and a word, which is the whole format. The assert is what
    // makes the buffer arithmetic below safe rather than merely correct
    // today: a three-digit rampage would run off the end of `frenzyLabel_`,
    // and it would do it by writing into whatever follows it.
    static_assert(contract::kFrenzyTargets < 100,
                  "showBodiesLeft writes at most two digits into an 8-byte "
                  "buffer -- a bigger rampage needs a bigger label");
    char* out = frenzyLabel_;
    if (left >= 10) {
        *out++ = static_cast<char>('0' + left / 10);
    }
    *out++ = static_cast<char>('0' + left % 10);
    *out++ = ' ';
    *out++ = 'L';
    *out++ = 'E';
    *out++ = 'F';
    *out++ = 'T';
    *out = '\0';
    // A third of a banner, like "OUCH": this fires up to twelve times inside
    // one chapter, and a full-length notice would cover the radar for most of
    // the rampage with a number that has already changed.
    showBanner(frenzyLabel_, kZoneBannerMs / 3);
}

void BaseCityScene::showBanner(const char* label, int ms) {
    bannerOverride_ = label;
    zoneBannerMs_   = ms;
    // Every call, whether or not anything else about the banner changed -- see
    // `bannerSerial_` for why the dirty check cannot compare anything else.
    // Wrapping is harmless: what the check asks is "different from last frame",
    // and 65536 banners between two frames is not a thing.
    ++bannerSerial_;
}

void BaseCityScene::stepWantedClock() {
    CityWorld& world = CityWorld::instance();
    const bool seen = policeCanSee();
    world.cooling = wanted::isWanted(world.wanted) && !seen;
    wanted::tick(world.wanted, seen);

    // Chapter 2 ends here, and nowhere else it could: every other objective in
    // this demo ends on a marker, this one on the star row going out, and this
    // is the one line in the run that moves it. Asked in every space for the
    // reason the tick above is -- the last star can burn off on a pavement, in
    // a shop or standing in the station lobby.
    //
    // Unconditional, and safe only because `cleaned` is a no-op from every
    // phase but `Struck` -- load-bearing twice: zero stars is a level the
    // player then STAYS at, so this test is true on every subsequent step of
    // the run; and a player who walked to the station with a clean record is at
    // zero for the whole of `ToTarget`, which would otherwise pay out a chapter
    // with no body in it. Both refusals live in game/rules/Contract.h, where a
    // host test can reach them.
    if (wanted::isWanted(world.wanted)
            || world.contract.phase != contract::Phase::Struck) {
        return;
    }
    contract::cleaned(world.contract);
    // Flat, like the boost's and read on the same terms: kHitFee is priced
    // against the shop counter rather than against a streak, so unlike the
    // courier's fee it does not have to be read after the state moves. See
    // game/rules/Economy.h.
    economy::earn(world.purse, economy::kHitFee);
    showBanner("CLEAN");
    // Chapter 1's delivery cue, reused rather than joined: four SFX voices,
    // rationed by audio_cues::admitCue, and a fifth would spend the budget on
    // a sound the player already knows the meaning of.
    AudioDirector::instance().playCue(audio_cues::Cue::MissionDelivered);
}

void BaseCityScene::stepObjectiveClock() {
    CityWorld& world = CityWorld::instance();
    // One objective at a time, and this is where the freeze is done: the
    // courier's counter is not decremented at all while a chapter runs, so it
    // still holds whatever it held when the phone was answered. A leg running
    // underneath would fail the player during a job the game asked them to take
    // instead.
    //
    // `underway`, not `active`, and the difference is the whole of chapter 2's
    // second half: `Struck` is a chapter with no clock, so `active` would thaw
    // the courier's leg while the player walks out of a police station with
    // five stars on them -- expiring under a HUD showing neither its ring nor
    // its number, failing a leg nobody asked for. `contract::tick` is itself
    // gated on `active`, so the wider question costs nothing during `Struck`:
    // neither clock moves, which is exactly the beat.
    if (contract::underway(world.contract)) {
        contract::tick(world.contract);
        return;
    }
    mission::tick(world.mission);
}

bool BaseCityScene::objectiveExpired() {
    CityWorld& world = CityWorld::instance();

    // The chapter first and, while it is running, INSTEAD: the courier's clock
    // has not moved since the phone was answered, so asking it here would
    // either say nothing or fail a leg for time it did not spend.
    //
    // `underway`, matching the freeze above -- the two have to agree or the leg
    // is answered on a step it was not ticked. During `Struck` the courier's
    // counter is frozen wherever it stood, and a phone answered late in a leg
    // freezes it at or near zero, which is exactly what `mission::expired`
    // tests: the arm below would fire on EVERY step of the retreat, re-rolling
    // the leg and shouting TOO SLOW over a manhunt for a clock the HUD is not
    // even showing. Inside the arm, `contract::expired` is deliberately still
    // the narrow question: `Struck` has no clock and cannot run out of one.
    if (contract::underway(world.contract)) {
        if (!contract::expired(world.contract)) {
            return false;
        }
        // Back to Idle on the SAME chapter: the phone is live again with the
        // same job. Failing costs the job and nothing else -- see
        // game/rules/Contract.h.
        contract::failed(world.contract);
        // Not "TOO SLOW": that word belongs to the courier leg and reappears
        // every couple of minutes. This one happens once and has to read as
        // the story being handed back, not as another missed drop.
        showBanner("JOB LOST");
        // The courier's failure cue, deliberately reused: four SFX voices,
        // rationed by audio_cues::admitCue, and a fifth would spend the budget
        // on a sound the player already knows the meaning of.
        AudioDirector::instance().playCue(audio_cues::Cue::MissionFailed);
        return true;
    }

    if (!mission::expired(world.mission)) {
        return false;
    }
    nextLeg(false);
    showBanner("TOO SLOW");
    AudioDirector::instance().playCue(audio_cues::Cue::MissionFailed);
    return true;
}

void BaseCityScene::movePlayerOnFoot(const StepInput& in) {
    PlayerActor& player = CityWorld::instance().player;
    const int beforeX = player.spriteX();
    const int beforeY = player.spriteY();
    player.step(in.up, in.down, in.left, in.right, in.run);
    if (player.spriteX() != beforeX || player.spriteY() != beforeY) {
        AudioDirector::instance().playFootstep(in.run);
    }
}

void BaseCityScene::stepGuns() {
    CityWorld& world = CityWorld::instance();
    world.weapons.step();
    world.returnFire.step();
}

bool BaseCityScene::checkPlayerDown() {
    CityWorld& world = CityWorld::instance();
    if (!world.player.isDown()) {
        return false;
    }
    world.bustedMs = kBustedHoldMs;
    AudioDirector::instance().playCue(audio_cues::Cue::Busted);
    return true;
}

void BaseCityScene::respawn() {
    CityWorld& world = CityWorld::instance();
    world.player.heal();
    // The station steps, not the map's start tile: being turned out of the
    // door you were brought through is a sentence the player can read off the
    // screen without being told it.
    world.player.spawnAt(kBustedSpawnTileX, kBustedSpawnTileY);
    onRespawn();
    world.player.setVisible(true);

    // The gun goes with them, or a gunfight with the police is a free retry.
    // So does the vest, which `heal()` above strips for the same reason. The
    // cost is a delivery and a walk to the counter, not a dead end -- and the
    // cash deliberately survives, because with no free guns left in the street
    // a broke and unarmed player has no route back. See game/rules/Economy.h.
    //
    // Before the drops below because disarm() drops the projectiles itself.
    world.weapons.disarm();
    world.player.setArmed(false);
    world.returnFire.dropProjectiles();
    world.weapons.dropProjectiles();

    // Respawning still wanted would put the player back in front of the force
    // that had just put them down, which is not a second chance.
    world.wanted = wanted::clear();
    onWantedChanged(world.wanted.stars);
    // The leg does not survive the arrest: the player has been moved across
    // the island, so letting its clock run out would look like the game
    // blaming them for the teleport.
    nextLeg(false);
    // Nor does the job, and for exactly the same reason -- with one more on
    // top of it: the car has been left wherever the fight ended, which may be
    // the far side of the island from the station steps. The chapter is kept,
    // so what an arrest costs here is the clock and the walk, not the story.
    // A no-op unless a job was actually running, which is most arrests.
    contract::failed(world.contract);

    // No banner: the held notice has just spent two seconds saying what
    // happened, and the strip has something better to say -- which district
    // they have been turned out into.
    bannerOverride_ = nullptr;
    zoneBannerMs_   = kZoneBannerMs;
}

void BaseCityScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    CityWorld& world = CityWorld::instance();

    // Real time, not the fixed tick: a clock driven off the accumulator would
    // run slow on exactly the frames the city is busiest.
    world.dayNight.update(deltaTime);

    // ADR-3: the whole music model, evaluated once here and nowhere else, and
    // before every early return below or the plan stops seeing the frames that
    // matter -- the frame a chase is lost, the frame an arrest lands. Also
    // AudioDirector::update's call site for aging cue cooldowns.
    //
    // ADR-7: isWanted is a BOOLEAN, never the star count. The raw count would
    // restart the radio on every escalation instead of exactly twice per
    // wanted episode.
    AudioDirector::instance().setMusicPlan(audio_cues::musicPlanFor(
        isDriving(),
        drivingColor(),
        wanted::isWanted(world.wanted),
        world.bustedMs > 0));
    AudioDirector::instance().update(deltaTime);

    // The single place the simulation STOPS. A respawn on the same frame as
    // the hit is a teleport the player never sees the cause of. Real
    // milliseconds rather than logic steps: the steps are what has been
    // switched off, so the hold cannot be counted in a clock that is stopped.
    if (world.bustedMs > 0) {
        world.bustedMs -= static_cast<int>(deltaTime);
        if (world.bustedMs <= 0) {
            world.bustedMs = 0;
            if (respawnsHere()) {
                respawn();
            } else {
                // The respawn tile is a CITY coordinate, so an arrest served
                // indoors has to walk the player out of the door first.
                world.pendingRespawn = true;
                world.requestScene(world.cityScene());
                world.commit();
                return;
            }
        }
        // The accumulator is not advanced below this point, so the frozen
        // seconds are not banked and do not come back as a burst of steps.
        return;
    }

    auto& input = engine.getInputManager();

    const StepInput in{
        input.isButtonDown(BTN_UP),
        input.isButtonDown(BTN_DOWN),
        input.isButtonDown(BTN_LEFT),
        input.isButtonDown(BTN_RIGHT),
        input.isButtonDown(BTN_RUN),
    };

    // RUN is the contextual button as well as the sprint: six buttons is the
    // whole pad, and a player next to a car who taps RUN wants the car.
    const bool actionPressed = in.run && !world.actionLatched;
    world.actionLatched = in.run;
    if (actionPressed) {
        onActionPressed();
    }
    // A door taken ends the frame: setScene has re-entered init() on the
    // arriving scene, so anything after this would be the departing scene
    // stepping a space nobody is looking at.
    if (world.commit()) {
        return;
    }

    // Edge-detected like the action button: a held trigger must be gated by
    // the weapon's fire rate, not the frame rate, or a fast simulator would
    // out-shoot the panel.
    const bool fireDown = input.isButtonDown(BTN_FIRE);
    const bool firePressed = fireDown && !world.fireLatched;
    world.fireLatched = fireDown;
    // Before the gun: a space that claims the trigger consumes it, and the
    // weapon does not also go off. See BaseCityScene::onFirePressed.
    const bool fireClaimed = firePressed && onFirePressed();
    // On foot only: the player sprite is not drawn while driving, so shots
    // would come out of a bonnet at whatever direction the walker last faced.
    if (firePressed && !fireClaimed && !isDriving()
        && world.weapons.fire(world.player.centreX(), world.player.centreY(),
                              world.player.facing())) {
        // Neither spec carries its WeaponId here, and a pellet count above one
        // is unique to the shotgun row (Weapon.cpp).
        AudioDirector::instance().playCue(
            world.weapons.spec().pellets > 1 ? audio_cues::Cue::ShotgunBlast
                                             : audio_cues::Cue::Gunshot);
        // Only on a round that actually left the barrel: a click on cooldown
        // is not a bang. alarmCrowd, not a startle -- civilians run from a
        // gunshot, officers turn and answer it, and the crowd does not sort
        // itself into two groups before reacting.
        alarmCrowd(world.player.centreX(), world.player.centreY(),
                   kGunshotHearingPx);
        // The entry-level offence, with no escalation of its own: a player who
        // learns the trigger itself brings the force down learns not to touch
        // it. See the ShotFired row in game/rules/Wanted.cpp.
        reportCrime(wanted::Crime::ShotFired);

        // Empty means unarmed for both player weapons now: no fallback gun to
        // hand down to, and none left in the street either, so running dry
        // costs a delivery and a walk to the counter. Done on the step it
        // empties rather than at the next trigger pull, because a press that
        // does nothing reads as the button being broken.
        if (weapons::isEmpty(world.weapons.stateOf())) {
            world.weapons.disarm();
            world.player.setArmed(false);
            // Not "SHELLS": the pistol is the row this fires on most of the
            // time now, and a shotgun word on a pistol is a banner the player
            // reads as belonging to something else.
            showBanner("OUT OF AMMO");
            AudioDirector::instance().playCue(audio_cues::Cue::DryFire);
        }
    }

    accumulatorMs_ += deltaTime;
    int steps = static_cast<int>(accumulatorMs_ / kLogicStepMs);
    if (steps > kMaxLogicStepsPerFrame) {
        // Drop the backlog instead of catching up. A long stall (a flash
        // write, a serial dump) would teleport the player across a block.
        steps = kMaxLogicStepsPerFrame;
        accumulatorMs_ = 0;
    } else {
        accumulatorMs_ -= static_cast<unsigned long>(steps) * kLogicStepMs;
    }

    for (int i = 0; i < steps; ++i) {
        // Out of the loop entirely: the frame may have several steps left and
        // every one would be the city carrying on over a body.
        if (!stepSpace(in)) {
            return;
        }
    }

    // Hard-centred, not followTarget. Camera2D's follow keeps a 30%-70% dead
    // zone, which suits a side-scroller; in an open city the player drifts
    // most of a screen off centre before the view reacts and walks into
    // scenery they cannot see yet.
    camera_.setPosition(math::Vector2(cameraOriginX(), cameraOriginY()));

    hintVisible_ = hintLabel() != nullptr;

    const std::uint8_t zone = zoneUnderFocus();
    if (zone != world.zone) {
        world.zone = zone;
        zoneBannerMs_ = kZoneBannerMs;
        AudioDirector::instance().playCue(audio_cues::Cue::DistrictChange);
        // A new district outranks the weapon notice: leaving it up would mean
        // the banner lies about where you are.
        bannerOverride_ = nullptr;
    } else if (zoneBannerMs_ > 0) {
        zoneBannerMs_ -= static_cast<int>(deltaTime);
        if (zoneBannerMs_ <= 0) {
            zoneBannerMs_ = 0;
            bannerOverride_ = nullptr;
        }
    }
}

// Every term here is something draw() puts on screen, and nothing else is.
// Sub-pixel movement is inside visualKey()'s definition, not missing from it;
// the camera is absent because it is a pure function of what it follows. zone
// is compared separately from the banner's visibility: crossing from one
// district straight into another keeps the banner up while changing its text.
bool BaseCityScene::shouldRedrawFramebuffer() const {
    if (!drawnOnce_) {
        return true;
    }
    const CityWorld& world = CityWorld::instance();
    return focusVisualKey()          != drawnFocusKey_
        || crowdVisualKey()          != drawnCrowdKey_
        || spaceNeedsRedraw()
        || world.weapons.visualKey() != drawnWeaponKey_
        || world.returnFire.visualKey() != drawnReturnKey_
        || world.player.health()     != drawnHealth_
        || world.player.armor()      != drawnArmor_
        || world.wanted.stars        != drawnStars_
        || world.cooling             != drawnCooling_
        || world.weapons.ammo()      != drawnAmmo_
        || world.purse.cash          != drawnCash_
        || world.mission.target      != drawnMissionTarget_
        // The phase, not the indices under it -- see drawnContractPhase_.
        || static_cast<std::uint8_t>(world.contract.phase)
                                     != drawnContractPhase_
        // Seconds, not steps: comparing the raw counter would force a full
        // ~36.5 ms redraw on every logic step of a delivery.
        || objectiveSecondsLeft()    != drawnObjectiveSeconds_
        || world.zone                != drawnZone_
        || world.dayNight.step()     != drawnTintStep_
        || (zoneBannerMs_ > 0)       != drawnBannerVisible_
        // And WHICH banner, not just whether there is one. See showBanner.
        || bannerSerial_             != drawnBannerSerial_
        || hintVisible_              != drawnHintVisible_
        // A bool, not the countdown: the notice does not animate.
        || (world.bustedMs > 0)      != drawnBusted_;
}

void BaseCityScene::draw(gfx::Renderer& renderer) {
    CityWorld& world = CityWorld::instance();

    camera_.apply(renderer);

    drawSpace(renderer);

    // After every actor and before the HUD: a tracer passes in FRONT of
    // whoever it is about to hit, which makes the shot and the hit read as one
    // event rather than two.
    world.weapons.draw(renderer);
    world.returnFire.draw(renderer);

    // HUD: absolute screen coordinates, so it must ignore the camera offset.
    renderer.setOffsetBypass(true);
    drawZoneBanner(renderer);
    // After the banner: the two share the top strip, the banner's label is
    // centred and the overlay is anchored left, so only the bar is covered.
    drawSpaceOverlay(renderer);
    drawClock(renderer);
    // Before health and after the clock: the cash plate overlaps the clock's
    // bottom edge by two pixels, so it has to be the one drawn second.
    drawCash(renderer);
    drawHealth(renderer);
    drawStars(renderer);
    drawTimer(renderer);
    drawWeapon(renderer);
    drawHint(renderer);
    // Over the HUD, because a modal the star row showed through would read as
    // a drawing bug rather than as a panel.
    drawModal(renderer);
    // Last, over the modal as well: while the notice is up nothing else on
    // screen is what the player is being asked to look at.
    drawBustedNotice(renderer);
    renderer.setOffsetBypass(false);

    // Only here: this is the record of what is now ON the panel, so it may
    // only be written on a frame that actually reached it.
    drawnFocusKey_       = focusVisualKey();
    drawnCrowdKey_       = crowdVisualKey();
    drawnStars_          = world.wanted.stars;
    drawnCooling_        = world.cooling;
    drawnAmmo_           = world.weapons.ammo();
    drawnCash_           = world.purse.cash;
    drawnMissionTarget_  = world.mission.target;
    drawnContractPhase_  =
        static_cast<std::uint8_t>(world.contract.phase);
    drawnObjectiveSeconds_ = objectiveSecondsLeft();
    drawnWeaponKey_      = world.weapons.visualKey();
    drawnReturnKey_      = world.returnFire.visualKey();
    drawnHealth_         = world.player.health();
    drawnArmor_          = world.player.armor();
    drawnZone_           = world.zone;
    drawnTintStep_       = world.dayNight.step();
    drawnBusted_         = world.bustedMs > 0;
    drawnBannerVisible_  = zoneBannerMs_ > 0;
    drawnBannerSerial_   = bannerSerial_;
    drawnHintVisible_    = hintVisible_;
    recordSpaceDrawn();
    drawnOnce_           = true;
}

void BaseCityScene::drawStars(gfx::Renderer& renderer) {
    // All five always drawn: a row that appeared only once you were wanted is
    // a row nobody had ever looked at. Unearned black, earned red -- black on
    // the light plate is legible without being loud. Except the top one while
    // it is running down, which is orange: the level falls only once the police
    // lose sight of you, and a rule the player cannot SEE is one they conclude
    // is random.
    const CityWorld& world = CityWorld::instance();
    renderer.drawFilledRectangle(kStarOriginX, kStarOriginY,
                                 kStarWidth, kStarHeight, hud::kPanel);
    for (std::uint8_t i = 0; i < wanted::kMaxStars; ++i) {
        const int x = kStarOriginX + kHudPadPx + i * (kStarSize + kStarGap);
        const bool earned = i < world.wanted.stars;
        const bool fading =
            earned && world.cooling
            && i == static_cast<std::uint8_t>(world.wanted.stars - 1);
        drawDiamond(renderer, x, kStarOriginY + kHudPadPx,
                    fading ? hud::kAccent
                           : earned ? hud::kDanger : hud::kEmptyStar);
    }
}

void BaseCityScene::drawTimer(gfx::Renderer& renderer) {
    // Behind the same ring that marks the objective on the ground and on the
    // radar: one colour, three places, learned once. TWO objectives now, so
    // two colours -- and the pip says which of them the number belongs to,
    // because the plate is otherwise identical either way. One row, and there
    // is not going to be a second: see kWeaponOriginY's assert, which leaves a
    // handful of pixels between this column and the prompt strip.
    renderer.drawFilledRectangle(kTimerOriginX, kTimerOriginY,
                                 kTimerWidth, kTimerHeight, hud::kPanel);

    // The plate stays and everything on it goes. One beat in the demo is
    // deliberately untimed -- the retreat after the Hit -- and the empty
    // panel is how the column keeps its shape while saying so; blanking the
    // plate too would shuffle nothing else, but a HUD that changes layout is
    // a HUD the eye has to re-learn mid-manhunt. See objectiveIsTimed.
    if (!objectiveIsTimed()) {
        return;
    }

    const int seconds = objectiveSecondsLeft();
    // Red outranks both, and that is the point of it: running out is the same
    // news whichever clock is running, and a warning the player has to decode
    // before reacting to is not a warning.
    const gfx::Color colour =
        seconds <= kTimerWarningSeconds
            ? hud::kDanger
            : (contractIsLive() ? hud::kContract : hud::kObjective);

    const int pipY = kTimerOriginY + (kTimerHeight - kMarkerSize) / 2;
    renderer.drawRectangle(kTimerOriginX + kHudPadPx, pipY,
                           kMarkerSize, kMarkerSize, colour);

    // Zero-padded so the readout never changes width: a number that moves as
    // it shrinks is one the eye has to re-find on every tick.
    char text[kTimerDigits + 1] = {};
    const int shown = seconds > 999 ? 999 : seconds;
    text[0] = static_cast<char>('0' + (shown / 100) % 10);
    text[1] = static_cast<char>('0' + (shown / 10) % 10);
    text[2] = static_cast<char>('0' + shown % 10);
    renderer.drawText(text,
                      kTimerOriginX + kHudPadPx + kMarkerSize + kHudPadPx,
                      kTimerOriginY + 2, colour, 1);
}

void BaseCityScene::drawWeapon(gfx::Renderer& renderer) {
    const CityWorld& world = CityWorld::instance();
    renderer.drawFilledRectangle(kWeaponOriginX, kWeaponOriginY,
                                 kWeaponWidth, kWeaponHeight, hud::kPanel);
    if (!world.weapons.armed()) {
        renderer.drawText("UNARMED", kWeaponOriginX + kHudPadPx,
                          kWeaponOriginY + 2, hud::kInkMuted, 1);
        return;
    }

    const weapons::WeaponSpec& gun = world.weapons.spec();
    const std::uint16_t ammo = world.weapons.ammo();

    // Name and count in one string, so they cannot drift apart on the panel
    // the way two draw calls with two origins can.
    char text[kWeaponNameChars + 1 + kWeaponAmmoChars + 1] = {};
    int i = 0;
    for (; i < kWeaponNameChars && gun.name[i] != '\0'; ++i) {
        text[i] = gun.name[i];
    }
    for (; i < kWeaponNameChars; ++i) {
        text[i] = ' ';
    }
    text[i++] = ' ';
    if (ammo == weapons::kInfiniteAmmo) {
        // Dashes rather than a number: an infinite magazine shown as 65535 is
        // a player counting down from a number that never moves.
        text[i++] = '-';
        text[i++] = '-';
        text[i++] = '-';
    } else {
        const std::uint16_t shown = ammo > 999 ? 999 : ammo;
        text[i++] = static_cast<char>('0' + (shown / 100) % 10);
        text[i++] = static_cast<char>('0' + (shown / 10) % 10);
        text[i++] = static_cast<char>('0' + shown % 10);
    }

    // Red on the last two shells: the point at which the player has to decide
    // whether to spend them or save them.
    const gfx::Color colour =
        (ammo != weapons::kInfiniteAmmo && ammo <= 2) ? hud::kDanger
                                                      : hud::kInk;
    renderer.drawText(text, kWeaponOriginX + kHudPadPx,
                      kWeaponOriginY + 2, colour, 1);
}

void BaseCityScene::drawBustedNotice(gfx::Renderer& renderer) {
    if (CityWorld::instance().bustedMs <= 0) {
        return;
    }
    // A full-width band rather than a fitted box: measuring the string to fit
    // a rectangle nobody is looking at is arithmetic for nothing.
    renderer.drawFilledRectangle(0, kBustedNoticeY, DISPLAY_WIDTH,
                                 kBustedNoticeH, hud::kPanel);
    renderer.drawLine(0, kBustedNoticeY, DISPLAY_WIDTH - 1, kBustedNoticeY,
                      hud::kDanger);
    renderer.drawLine(0, kBustedNoticeY + kBustedNoticeH - 1,
                      DISPLAY_WIDTH - 1, kBustedNoticeY + kBustedNoticeH - 1,
                      hud::kDanger);
    renderer.drawTextCentered("WANTED", kBustedNoticeY + kHudPadPx,
                              hud::kDanger, kBustedNoticeScale);
}

void BaseCityScene::drawZoneBanner(gfx::Renderer& renderer) {
    if (zoneBannerMs_ <= 0) {
        return;
    }
    renderer.drawFilledRectangle(0, 0, DISPLAY_WIDTH, kZoneBannerH,
                                 hud::kPanel);
    renderer.drawLine(0, kZoneBannerH, DISPLAY_WIDTH - 1, kZoneBannerH,
                      hud::kAccent);
    const char* label =
        bannerOverride_ != nullptr ? bannerOverride_ : spaceLabel();
    renderer.drawTextCentered(label, 4, hud::kInk, 1);
}

void BaseCityScene::drawCash(gfx::Renderer& renderer) {
    const CityWorld& world = CityWorld::instance();
    renderer.drawFilledRectangle(kCashOriginX, kCashOriginY,
                                 kCashWidth, kCashHeight, hud::kPanel);

    // Zero-padded and always four digits, like the timer and for the same
    // reason: a number that changes width as it grows is one the eye has to
    // re-find. Broke is drawn muted rather than hidden -- a row that appears
    // when you first get paid is a row nobody had ever looked at, which is
    // the mistake the star row already documents.
    char text[1 + kCashDigits + 1] = {};
    const std::uint16_t cash = world.purse.cash;
    text[0] = '$';
    text[1] = static_cast<char>('0' + (cash / 1000) % 10);
    text[2] = static_cast<char>('0' + (cash / 100) % 10);
    text[3] = static_cast<char>('0' + (cash / 10) % 10);
    text[4] = static_cast<char>('0' + cash % 10);
    renderer.drawText(text, kCashOriginX + kHudPadPx, kCashOriginY + 2,
                      cash > 0 ? hud::kInk : hud::kInkMuted, 1);
}

void BaseCityScene::drawClock(gfx::Renderer& renderer) {
    // Whole and half hours only, which is not a rounding shortcut -- it is the
    // tint step itself. A minute counter would change every frame and force a
    // full repaint of the city with it.
    // Zero-initialised, so index 5 is the terminator without being written.
    const CityDayNight& dayNight = CityWorld::instance().dayNight;
    char text[6] = {};
    const std::uint8_t hour   = dayNight.hour();
    const std::uint8_t minute = dayNight.minute();
    text[0] = static_cast<char>('0' + hour / 10);
    text[1] = static_cast<char>('0' + hour % 10);
    text[2] = ':';
    text[3] = static_cast<char>('0' + minute / 10);
    text[4] = static_cast<char>('0' + minute % 10);

    renderer.drawFilledRectangle(kClockOriginX, kClockOriginY - 2,
                                 kClockWidth, kClockHeight, hud::kPanel);
    renderer.drawText(text, kClockOriginX + 2, kClockOriginY, hud::kInk, 1);
}

void BaseCityScene::drawHealth(gfx::Renderer& renderer) {
    renderer.drawFilledRectangle(kHealthOriginX, kHealthOriginY,
                                 kHealthWidth, kHealthHeight, hud::kPanel);

    const std::uint8_t health = CityWorld::instance().player.health();
    // Red below a third, heart AND digits. Not a gradient: the panel is
    // 12-bit on hardware, where a ramp bands into three colours anyway.
    const gfx::Color colour =
        health * 3 <= kPlayerHealth ? hud::kDanger : hud::kInk;

    drawHeart(renderer, kHealthOriginX + kHudPadPx,
              kHealthOriginY + (kHealthHeight - kHeartSize) / 2, hud::kDanger);

    char text[kHealthDigits + 1] = {};
    text[0] = static_cast<char>('0' + (health / 100) % 10);
    text[1] = static_cast<char>('0' + (health / 10) % 10);
    text[2] = static_cast<char>('0' + health % 10);
    renderer.drawText(text,
                      kHealthOriginX + kHudPadPx + kHeartSize + kHudPadPx,
                      kHealthOriginY + 2, colour, 1);

    // The vest, on the outer edge of the same plate. Always drawn, even at
    // zero: a readout that appears only once it has a value is one the player
    // has never seen, so they never learn the shop sells the thing that fills
    // it. Muted when empty, so an unworn vest reads as an empty slot rather
    // than as armour of nought.
    const std::uint8_t worn = CityWorld::instance().player.armor();
    const gfx::Color armorColour = worn > 0 ? hud::kInk : hud::kInkMuted;
    drawShield(renderer, kHealthOriginX + kArmorOffsetX,
               kHealthOriginY + (kHealthHeight - kShieldSize) / 2,
               armorColour);

    char armorText[kArmorDigits + 1] = {};
    armorText[0] = static_cast<char>('0' + (worn / 10) % 10);
    armorText[1] = static_cast<char>('0' + worn % 10);
    renderer.drawText(armorText,
                      kHealthOriginX + kArmorOffsetX + kShieldSize + kHudPadPx,
                      kHealthOriginY + 2, armorColour, 1);
}

void BaseCityScene::drawHint(gfx::Renderer& renderer) {
    const char* label = hintLabel();
    if (label == nullptr) {
        return;
    }
    // One contextual button needs one line of text, or nobody finds it: the
    // demo has no menu to put controls in.
    const int top = DISPLAY_HEIGHT - kHintHeight;
    renderer.drawFilledRectangle(0, top, DISPLAY_WIDTH, kHintHeight,
                                 hud::kPanel);
    renderer.drawTextCentered(label, top + 2, hud::kInk, 1);
}

}  // namespace top_down_city
