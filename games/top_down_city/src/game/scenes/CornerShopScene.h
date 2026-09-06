#pragma once

#include <cstdint>

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

    /// The selected line is the only thing in this room that changes without
    /// the player moving, and it changes the prompt strip. Without these two
    /// the frame skip would hold the previous product on screen until the
    /// player took a step.
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
     * @brief Sell whatever line is showing.
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
    bool serveAtCounter();

    /*
     * WHY THIS PICKER IS DRAWN BY HAND
     *
     * The panel below is Renderer primitives -- a plate, a border, a row of
     * text each -- and this demo builds with PIXELROOT32_ENABLE_UI_SYSTEM=0.
     * Not because the engine lacks the widget: engine 1.9.0 ships
     * `UIVerticalLayout`, a button-driven list with `setNavigationButtons`,
     * `setSelectedIndex` and rising-edge detection already solved, and
     * `UIButton` is focusable and reads an InputManager. The touch-only path is
     * `UIManager`, and nothing here would have gone through it.
     *
     * The reason is timing. A dialogue system is coming to the engine, aimed
     * squarely at this shape of interaction -- a modal panel, a list of
     * choices, a confirm -- and it will sit on the UI system properly. Porting
     * these two methods onto `UIVerticalLayout` now buys the same screen twice.
     * Two smaller things also argue for waiting: enabling the UI system pulls
     * in the whole module for three rows that never change, at a flash cost
     * that would have to be measured rather than assumed; and a row here is two
     * columns -- label left, price right, the price recoloured when the purse
     * cannot cover it -- where a `UIButton` carries one label and one style.
     *
     * When the dialogue system ships, THIS is the thing to replace, and the
     * replacement is small: `stepPicker` and `drawModal` are the whole of it.
     * The geometry lives in CityConstants.h and the catalogue in rules/Shop.h,
     * and neither one knows the engine exists.
     */
    /// Walk the picker one row, if it is open. Edge-detected against
    /// `heldUp_`/`heldDown_` rather than read as a level: the logic step runs
    /// 62 times a second and a held direction would scroll the list past
    /// everything in it before the player let go.
    void stepPicker(const StepInput& in);

    /// Is the picker up? While it is, the player does not move, the trigger
    /// does not fire, and the prompt strip is silent -- the panel says
    /// everything the strip would have.
    bool pickerOpen_;

    /// What the picker is highlighting. Reset to the cheapest line every time
    /// the panel OPENS rather than remembered: a player who walks back in
    /// after a delivery is looking for the thing they have just saved up for,
    /// and the list is ordered so the walk down it is the shortest way there.
    shop::Line line_;

    /// Edge detection for the two directions the picker reads. Local rather
    /// than shared through CityWorld like RUN and FIRE, because the picker is
    /// the only thing in the demo that reads the D-pad as a press instead of a
    /// direction -- and it cannot be open at a doorway, which is the whole
    /// reason those two live in CityWorld.
    bool heldUp_;
    bool heldDown_;

    /// What the last drawn frame had on it. The picker is the one thing in
    /// this room that changes without the player moving.
    bool       drawnPickerOpen_;
    shop::Line drawnLine_;
};

}  // namespace top_down_city
