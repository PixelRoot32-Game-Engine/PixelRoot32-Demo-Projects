#pragma once

#include <cstdint>

#include <core/Scene.h>
#include <graphics/Camera2D.h>
#include <graphics/Renderer.h>

#include "game/CityConstants.h"
#include "game/entities/PedestrianActor.h"   // PersonHit
#include "game/rules/Wanted.h"
#include "game/scenes/CityWorld.h"

namespace top_down_city {

class StationStaff;

/**
 * @class BaseCityScene
 * @brief What every space in this city has in common.
 *
 * Ownership splits three ways: CityWorld holds the RUN, which must not restart
 * at a doorway; this class holds BEING a space -- camera, fixed logic step,
 * HUD, contextual button, arrest, frame skip; the subclass holds the space
 * itself -- tilemaps, populations, doors. A subclass answers the questions
 * below and gets everything else, so an interior is a new file rather than a
 * new branch in an existing one.
 *
 * @note Draw order is explicit rather than delegated to render layers. The
 *       engine's MAX_LAYERS is 4 and layer 0 switches the renderer to the
 *       BACKGROUND palette context, leaving three sprite layers for four
 *       things that must stack in a fixed order.
 */
class BaseCityScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

    /// Engine::draw() -- beginFrame, the whole scene, present() -- is skipped
    /// entirely when this returns false, which on hardware is the whole 36.5 ms
    /// of a 40 ms frame: 21 ms of drawing and 15.5 ms of present. Both halves,
    /// not just the transfer -- an earlier version of this comment had present
    /// at twenty times the cost of the draw, and the board has it cheaper.
    ///
    /// Worth knowing what this actually buys: outdoors it never fires, because
    /// traffic and the crowd move on every frame. It pays in the interiors,
    /// standing still. See ROADMAP.md.
    bool shouldRedrawFramebuffer() const override;

    /**
     * @brief The duty staff, if this space is the one that has any. nullptr
     *        everywhere else, which is everywhere but the police station.
     *
     * One vtable slot, and it deletes the only downcast in the demo. Chapter 2
     * is taken OUTDOORS and reaches into the station to put the man it names
     * back on his feet, so `CityScene` holds a pointer to a room it is not --
     * out of `interiorScenes[]`, a hand-ordered table in both platform headers
     * indexed by `kDoorways` rows, whose own comment admits the failure mode:
     * get the order wrong and both doors still work, they open onto each
     * other's rooms. A `static_cast` would make that swap undefined behaviour
     * instead of a visible mistake -- RTTI is off, so the cast cannot fail, and
     * the write lands on a corner shop's bytes and draws as an ordinary frame
     * -- while a virtual answers nullptr into a null check the caller needs
     * anyway. The tables agree today; no test in this repository could tell
     * you the day they stop.
     *
     * Public rather than protected on purpose: the caller is a SIBLING scene,
     * and protected access through another branch of the hierarchy is not
     * something C++ grants.
     */
    virtual StationStaff* stationStaff() { return nullptr; }

protected:
    BaseCityScene();

    /// One frame's worth of the pad. Read in update() rather than per space
    /// because the edge detection behind two of the buttons lives in CityWorld
    /// and must not be done twice.
    struct StepInput {
        bool up;
        bool down;
        bool left;
        bool right;
        bool run;
    };

    // =====================================================================
    // What a space has to answer, in the order a frame asks it.
    // =====================================================================

    /// Put this space back under the player: collision space, camera bounds,
    /// populations, position.
    /// @param firstEntry True only on the very first init of the run. A return
    ///        trip must NOT re-seed pools or re-roll the delivery.
    virtual void onEnterSpace(bool firstEntry) = 0;

    /// RUN, edge-detected. @return true when the press was consumed.
    virtual bool onActionPressed() = 0;

    /**
     * @brief FIRE, edge-detected, offered to the space BEFORE the weapon.
     *
     * Only the corner shop answers it, and only at the till -- the trick the
     * shop's menu is built on. Six buttons left none for a catalogue, but FIRE
     * at that one tile was already the only press in the demo that did nothing
     * worth doing: the room is empty by design, so a round there hits a wall.
     *
     * @return true when the press was consumed, which also means the gun does
     *         NOT go off. A shop that took the money and fired a shell into
     *         the shelves would raise the wanted level for shopping.
     */
    virtual bool onFirePressed() { return false; }

    /// One fixed logic step of everything that lives here.
    /// @return false when the player went down, which ends the step loop.
    virtual bool stepSpace(const StepInput& in) = 0;

    /// The space behind the HUD, ending with Scene::draw() so the player is
    /// drawn at the right depth. Tracers and HUD follow, in draw().
    virtual void drawSpace(pixelroot32::graphics::Renderer& renderer) = 0;

    /// Anything this space puts over the HUD's top strip. The radar, outdoors.
    virtual void drawSpaceOverlay(pixelroot32::graphics::Renderer& renderer) {
        (void)renderer;
    }

    /**
     * @brief A panel this space puts over EVERYTHING, including the HUD. The
     *        shop's picker, indoors.
     *
     * Drawn after the whole HUD and before the caught notice, which is the
     * only thing allowed above it: a modal that covered `WANTED` would hide
     * the one message the player is being asked to read.
     */
    virtual void drawModal(pixelroot32::graphics::Renderer& renderer) {
        (void)renderer;
    }

    virtual PersonHit hitPersonBox(int left, int top, int width, int height,
                                   std::uint8_t damage) = 0;
    virtual bool crowdSees(int x, int y, int rangePx) = 0;
    virtual void alarmCrowd(int x, int y, int rangePx) = 0;

    /// Whatever walks around in here, as one number the frame skip compares.
    virtual std::uint32_t crowdVisualKey() const = 0;

    /// What the camera is following, as one number.
    virtual std::uint32_t focusVisualKey() const;

    /// Anything else on screen that moves without the player touching it.
    virtual bool spaceNeedsRedraw() const { return false; }
    virtual void recordSpaceDrawn() {}

    virtual const char* spaceLabel() const = 0;

    /// The bottom hint, or nullptr. Not const: outdoors the answer is "is
    /// there a car within reach", the same sweep the button itself runs.
    virtual const char* hintLabel() = 0;

    /// The star ladder moved. Outdoors the crowd's officer spawn rate follows
    /// it; the duty staff are whoever the map declared, so indoors it does not.
    virtual void onWantedChanged(std::uint8_t stars) { (void)stars; }

    /// True in the space the arrest is actually served into. Only the city has
    /// a kerb outside a police station to be turned out on.
    virtual bool respawnsHere() const { return false; }

    /// Anything this space must let go of when the player is taken in.
    virtual void onRespawn() {}

    virtual bool isDriving() const { return false; }
    virtual std::uint8_t drivingColor() const { return 0; }

    /// The district under the focus. A room is not in one, so the default is
    /// whatever the city last said.
    virtual std::uint8_t zoneUnderFocus() const;

    /// What the camera follows and which tile the HUD reports. The player,
    /// unless the space has something bigger for them to sit in.
    virtual int focusCentreX() const;
    virtual int focusCentreY() const;
    virtual int focusTileX() const;
    virtual int focusTileY() const;

    /// The box the police shoot at, and what a round is worth against it.
    virtual void focusHitBox(int& left, int& top,
                             int& width, int& height) const;
    virtual std::uint8_t incomingDamage(std::uint8_t damage) const {
        return damage;
    }

    // =====================================================================
    // What a space gets. Separate pieces rather than one template method
    // because the spaces run them in different orders: a room has no traffic to
    // step and no car to be arriving in, the city no duty staff to shoot. What
    // they do NOT get to differ on is where the objective clock is judged --
    // see objectiveExpired(), last in every space that has one.
    // =====================================================================

    /// The same clamp Camera2D applies, computed here because a streaming pool
    /// needs the value before the camera has been moved. The bound comes from
    /// the collision space, so indoors -- one viewport wide -- it is 0.
    int cameraOriginX() const;
    int cameraOriginY() const;

    /// Bounds first: Camera2D clamps inside setPosition, and a camera left on
    /// the city's bounds indoors could scroll 1808 px off the side of a room.
    void applySpaceBounds();

    /// One funnel rather than a wanted::report at each call site, because
    /// every crime also has to re-derive the officer spawn rate.
    void reportCrime(wanted::Crime crime);

    /// Shared by the bullet and the bonnet, which is why it takes the result
    /// rather than the cause.
    void reportPersonHit(const PersonHit& hit);

    /// One body, offered to chapter 3. A no-op unless the rampage is running
    /// -- `contract::culled` decides that, not this -- and the payout and the
    /// banners hang off the transition it reports back.
    ///
    /// Here rather than in CityScene because a death is: the crowd under a car
    /// in the street, an officer in the station lobby, a bystander caught by a
    /// pellet through a shop window. One call site, `reportPersonHit`, which is
    /// the one place a body is reported.
    void countRampageBody();

    /// Put "N LEFT" up for a third of a banner. Writes into `frenzyLabel_`
    /// and not a local, because `showBanner` keeps the POINTER: a stack
    /// buffer would be drawn from after it stopped existing.
    void showBodiesLeft(std::uint8_t left);

    /// Shared by the three ways a leg ends -- delivered, timed out, taken in.
    void nextLeg(bool delivered);

    /**
     * @brief Is the main mission the thing the player is being asked to do?
     *
     * "One objective at a time" hangs off this: while it is true the courier
     * leg is FROZEN -- not ticked, not expired, not drawn -- and the HUD's one
     * timer plate belongs to the chapter. It resumes with the clock it had,
     * because a leg that quietly re-rolled while the player was doing something
     * else is a leg they never chose to abandon.
     */
    bool contractIsLive() const;

    /// Whole seconds left on whichever of the two clocks `contractIsLive()`
    /// says is running. Seconds, not steps: the counter behind this changes 62
    /// times a second and the readout changes once.
    ///
    /// Meaningless unless `objectiveIsTimed()` -- ask that first.
    int objectiveSecondsLeft() const;

    /// Is EITHER clock running? Two objectives and a beat with neither: the
    /// retreat after the Hit is untimed on purpose, and this is what keeps the
    /// plate from filling that silence with the courier's frozen number.
    bool objectiveIsTimed() const;

    /// The one input to wanted::tick. False at zero stars without asking
    /// anybody, which is most of a session, so the rays usually cost nothing.
    bool policeCanSee();

    /// Put a word across the top strip for a while, instead of the district.
    void showBanner(const char* label, int ms = kZoneBannerMs);

    /// Before anybody moves, so the answer is about the frame the player just
    /// saw rather than positions they have not been shown yet.
    void stepWantedClock();

    /// One fixed step off the live objective's clock, and only that one. Both
    /// spaces call it, because neither the drop outside nor the job clock
    /// waits while the player is at a till.
    void stepObjectiveClock();

    /**
     * @brief The live objective ran out: the leg is re-rolled or the chapter
     *        is handed back to the phone.
     *
     * Every space calls it, for the same reason every space ticks -- a clock
     * that can run out indoors has to be answerable indoors, or the plate
     * sits at 000 until the player finds a door.
     *
     * @warning CALL THIS LAST IN A STEP, after everything that could FINISH
     *          the objective it is about to fail. Not a style note: this is a
     *          verdict, and `contract::failed`/`mission::failed` move the state
     *          the completion paths are gated on. Since `contract::struck` is a
     *          no-op from every phase but `ToTarget` and `contract::delivered`
     *          from every phase but `ToDrop`, on the one step where the clock
     *          hits zero and the player finishes anyway whichever runs first
     *          wins outright -- and the player who was exactly on time is the
     *          one who loses. Ticking is bookkeeping and belongs at the top of
     *          a step; judging is not, and belongs at the bottom.
     *
     *          Written here rather than only at the call sites because all
     *          THREE spaces obey it and no two the same way: outdoors in the
     *          else arm of the arrival test ending `CityScene::stepSpace`; in
     *          the station after `stepGuns` and the mark check ending
     *          `PoliceStationScene::stepSpace`; in the shop after `stepGuns`,
     *          where nothing in the room can finish an objective and the order
     *          therefore costs nothing today -- kept anyway, because a room
     *          that answered its clock somewhere else is exactly what the next
     *          space would copy from. A FOURTH space gets the rule and not a
     *          precedent to copy.
     *
     * @return true when a clock ran out, which is news once per leg or job.
     */
    bool objectiveExpired();

    /// Walk, and put a footstep under it. Position before/after, not the
    /// input, is what "moved" means: a player held into a wall gets no
    /// footfall, the same way the walk animation freezes against one.
    void movePlayerOnFoot(const StepInput& in);

    /// Both weapon systems. Ordered before whoever is hunting the player, so a
    /// round fired last step has moved before it is answered.
    void stepGuns();

    /// @return true when the player went down, which puts the notice up.
    bool checkPlayerDown();

    /// Respawn at the station steps, healed and unarmed, street calmed.
    void respawn();

    pixelroot32::graphics::Camera2D camera_;
    unsigned long                   accumulatorMs_;
    int                             zoneBannerMs_;
    /// What the top banner says instead of the district, or nullptr. The pad
    /// has no button left for a prompt and the panel no room for a second
    /// readout, so the district strip carries every one-line notice.
    const char*                     bannerOverride_;
    /// Where `showBodiesLeft` builds the rampage counter: every other banner in
    /// the demo is a string literal, and `bannerOverride_` above only borrows
    /// the pointer. Eight bytes holds "99 LEFT".
    char                            frenzyLabel_[8];
    /// Which CALL put the current banner up, rather than what it says or
    /// where the text lives. The dirty check needs to know a banner changed,
    /// and neither of the other two answers can tell it: two notices in a row
    /// leave `zoneBannerMs_ > 0` true across the swap, and the rampage
    /// counter rewrites `frenzyLabel_` in place, so its address never moves
    /// either. A serial is the one thing that is different every time.
    std::uint16_t                   bannerSerial_;
    bool                            hintVisible_;

private:
    /// Installed on CityWorld::weapons at every entry: it routes to whichever
    /// space is live, and the other one is frozen in a coordinate space
    /// nothing is looking at.
    static bool hitTarget(void* context, int left, int top, int width,
                          int height, std::uint8_t damage);

    /// The police's: the player, and only the player. Their rounds pass over
    /// the crowd -- friendly fire on a 240x240 screen is noise, not drama.
    static bool hitPlayer(void* context, int left, int top, int width,
                          int height, std::uint8_t damage);

    void drawHealth(pixelroot32::graphics::Renderer& renderer);
    void drawStars(pixelroot32::graphics::Renderer& renderer);
    void drawTimer(pixelroot32::graphics::Renderer& renderer);
    void drawWeapon(pixelroot32::graphics::Renderer& renderer);
    void drawCash(pixelroot32::graphics::Renderer& renderer);
    void drawZoneBanner(pixelroot32::graphics::Renderer& renderer);
    void drawClock(pixelroot32::graphics::Renderer& renderer);
    void drawHint(pixelroot32::graphics::Renderer& renderer);
    void drawBustedNotice(pixelroot32::graphics::Renderer& renderer);

    /// What the last DRAWN frame contained. shouldRedrawFramebuffer()
    /// compares live state against this; draw() refreshes it. Skipped frames
    /// touch neither, so the comparison stays correct across any number.
    std::uint32_t drawnFocusKey_;
    std::uint32_t drawnCrowdKey_;
    std::uint32_t drawnWeaponKey_;
    std::uint32_t drawnReturnKey_;
    int           drawnObjectiveSeconds_;
    std::uint16_t drawnAmmo_;
    std::uint16_t drawnCash_;
    std::uint8_t  drawnStars_;
    std::uint8_t  drawnHealth_;
    std::uint8_t  drawnArmor_;
    std::uint8_t  drawnMissionTarget_;
    /// The phase, not the vehicle and drop indices under it: those two are
    /// written once by contract::begin and never again, while the phase is what
    /// moves the marker from the phone to the car to the drop, without any of
    /// the terms above changing. The car it points at in between is parked, so
    /// it does not move until the step that boards it.
    std::uint8_t  drawnContractPhase_;
    std::uint8_t  drawnZone_;
    std::uint8_t  drawnTintStep_;
    bool          drawnCooling_;
    bool          drawnBusted_;
    bool          drawnBannerVisible_;
    std::uint16_t drawnBannerSerial_;
    bool          drawnHintVisible_;
    /// False after any entry, so the first frame of a space is never skipped:
    /// the framebuffer still holds the other one.
    bool          drawnOnce_;
};

}  // namespace top_down_city
