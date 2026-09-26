#pragma once

#include <cstdint>

#include "game/dialog/ShopPicker.h"
#include "game/rules/Shop.h"
#include "game/scenes/BaseCityScene.h"

namespace top_down_city {

/**
 * @class CornerShopScene
 * @brief A room with nobody in it, which is the whole point of it.
 *
 * The second interior, and the one that makes the machinery worth having: it
 * is PoliceStationScene with the staff taken out, and that is the entire diff
 * -- the argument for interiors being scenes rather than modes, since a third
 * room is another file of this size and not a third arm of every branch in the
 * city's.
 *
 * It is also the room the pad found a menu in. A menu needs a button and the
 * six on the pad were spent -- everywhere except at the till, where FIRE was
 * already the one press that did nothing worth doing. RUN opens a picker over
 * the room, and while it is up it OWNS the pad: UP and DOWN walk the list,
 * FIRE buys the highlighted line, RUN closes it. Nothing is taken away by
 * that, because there is nobody in here to shoot and nowhere to walk to; a
 * modal in any other space would be a player frozen in traffic. It is the
 * chess demo's promotion picker with a D-pad where that one has a finger: a
 * bordered plate, a title, one row per choice with the current one marked, a
 * footer naming the buttons. The list is game/rules/Shop.h, the geometry
 * CityConstants.h; this file is the till.
 *
 * It is also the room that needed a rule. `wanted::tick` sheds a star for
 * every six seconds nobody has eyes on the player, and an empty room is six
 * seconds of that forever -- left alone this door would be a button marked
 * CLEAR MY WANTED LEVEL. So `crowdSees` here does not ask who is in the room,
 * it asks whether the force watched the player come through the door, which is
 * game/rules/Hideout.h.
 *
 * Nothing here owns any of the run: the player, the wanted level, the delivery
 * clock, both weapon systems and the burn all belong to CityWorld and keep
 * running while the player is inside. The room is exactly one viewport, so the
 * camera does not move in here.
 */
class CornerShopScene : public BaseCityScene {
public:
    CornerShopScene();

protected:
    void onEnterSpace(bool firstEntry) override;
    bool onActionPressed() override;
    bool onFirePressed() override;
    bool stepSpace(const StepInput& in) override;
    void drawSpace(pixelroot32::graphics::Renderer& renderer) override;

    PersonHit hitPersonBox(int left, int top, int width, int height,
                           std::uint8_t damage) override;
    bool crowdSees(int x, int y, int rangePx) override;
    void alarmCrowd(int x, int y, int rangePx) override;
    std::uint32_t crowdVisualKey() const override;

    /// The picker is the only thing in this room that changes without the
    /// player moving, and it changes the prompt strip. Without these two the
    /// frame skip would hold the previous product on screen until the player
    /// took a step.
    bool spaceNeedsRedraw() const override;
    void recordSpaceDrawn() override;

    void drawModal(pixelroot32::graphics::Renderer& renderer) override;

    const char* spaceLabel() const override { return "CORNER SHOP"; }
    const char* hintLabel() override;

private:
    bool atShopExit() const;

    /// Standing at the till, which is the floor cell in front of the counter
    /// -- the counter itself is solid, so nobody can stand on it.
    bool atCounter() const;

    /**
     * @brief Sell `picked`, the line the picker just confirmed.
     *
     * Every line arrives loaded, the honest consequence of one weapon slot:
     * `WeaponSystem` holds a single spec and a single magazine, so "a box of
     * rounds" and "a loaded gun" are the same purchase and a separate
     * ammunition line would have nothing to be stored in. See WeaponSystem.h
     * for what a second slot would cost.
     *
     * @return true when the press was consumed, including when the answer was
     *         no -- a refusal the player can read is still an answer.
     */
    bool serveAtCounter(shop::Line picked);

    /*
     * WHY THIS PICKER IS STILL DRAWN BY HAND
     *
     * What is open and what is highlighted now live in the engine's
     * DialogRunner, through game/dialog/ShopPicker.h. The panel itself is still
     * Renderer primitives -- a plate, a border, a row of text each -- because a
     * row here is two columns: label left, price right, both recoloured when
     * the purse cannot cover it. The engine's `DialogBox` draws one text per
     * row in one ink, so adopting it would lose the price column and the red.
     * Multi-column rows are on the Dialog System's Future list; when they
     * land, `drawModal` is what they replace. The geometry lives in
     * CityConstants.h and the catalogue in rules/Shop.h, and neither one knows
     * the engine exists.
     */
    /// Walk the picker one row, if it is open. Edge-detected against
    /// `heldUp_`/`heldDown_` rather than read as a level: the logic step runs
    /// 62 times a second and a held direction would scroll the list past
    /// everything in it before the player let go. The runner cannot do this
    /// for us: it is fed semantic actions and never sees the pad.
    void stepPicker(const StepInput& in);

    /// The picker. While it is open the player does not move, the trigger
    /// does not fire, and the prompt strip is silent -- the panel says
    /// everything the strip would have. It opens on the cheapest line every
    /// time rather than remembering: a player who walks back in after a
    /// delivery is looking for the thing they have just saved up for, and the
    /// list is ordered so the walk down it is the shortest way there.
    ShopPicker picker_;

    /// Edge detection for the two directions the picker reads. Local rather
    /// than shared through CityWorld like RUN and FIRE, because the picker is
    /// the only thing in the demo that reads the D-pad as a press instead of a
    /// direction -- and it cannot be open at a doorway, which is the whole
    /// reason those two live in CityWorld.
    bool heldUp_;
    bool heldDown_;

    /// The picker's revision on the last drawn frame. It moves on open, close,
    /// every row walked and every sale.
    std::uint16_t drawnPickerRevision_;
};

}  // namespace top_down_city
