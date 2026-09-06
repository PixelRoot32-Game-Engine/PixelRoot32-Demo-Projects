#include "game/scenes/CornerShopScene.h"

#include "audio/AudioDirector.h"
#include "game/rules/Economy.h"
#include "game/rules/Hideout.h"
#include "game/rules/Shop.h"
#include "game/systems/CityCollision.h"
#include "generated/tilemaps/corner_shop.h"

namespace pr32 = pixelroot32;

namespace top_down_city {

namespace gfx = pr32::graphics;

CornerShopScene::CornerShopScene()
    : pickerOpen_(false),
      line_(shop::first()),
      heldUp_(false),
      heldDown_(false),
      drawnPickerOpen_(false),
      drawnLine_(shop::first()) {
}

void CornerShopScene::onEnterSpace(bool firstEntry) {
    (void)firstEntry;   // the run always starts outdoors; this is never it

    CityWorld& world = CityWorld::instance();

    // Nobody arrives mid-purchase. A panel that survived a doorway would be
    // drawn over a room the player has not seen yet.
    pickerOpen_ = false;
    line_ = shop::first();

    collision::setSpace(collision::Space::CornerShop);

    // The cycle is a sky effect and there is no sky in here. The clock keeps
    // running -- the HUD readout still moves -- but the room is lit by its own
    // ceiling, at whatever hour it is outside.
    world.dayNight.setSuspended(true);

    // A round still in the air holds city pixels, and the room's start again
    // at zero: left alone it would arrive somewhere arbitrary in the shop.
    world.weapons.dropProjectiles();
    world.returnFire.dropProjectiles();

    // Same reason: the city's blocker would report a car among the shelves
    // wherever the two coordinate spaces happen to overlap.
    collision::setDynamicBlocker(nullptr, nullptr);

    applySpaceBounds();

    world.player.placeAt(shop_room::SPAWN_TILE_X * kTilePx,
                         shop_room::SPAWN_TILE_Y * kTilePx,
                         Facing::Up);
    world.player.setVisible(true);

    // The one thing this room says out loud. Everywhere else the rule reads off
    // the star readout -- it stops falling -- but the star row is at the top of
    // the screen and the thing that just happened was at the bottom, so the
    // banner names it once, on the frame it applies to.
    if (hideout::watching(world.hideBurn)) {
        showBanner("SEEN GOING IN");
    }
}

bool CornerShopScene::atShopExit() const {
    const PlayerActor& player = CityWorld::instance().player;
    return player.tileX() == shop_room::EXIT_TILE_X
        && player.tileY() == shop_room::EXIT_TILE_Y;
}

bool CornerShopScene::atCounter() const {
    const PlayerActor& player = CityWorld::instance().player;
    return player.tileX() == shop_room::COUNTER_TILE_X
        && player.tileY() == shop_room::COUNTER_TILE_Y;
}

bool CornerShopScene::serveAtCounter() {
    CityWorld& world = CityWorld::instance();
    const shop::Offer& line = shop::offer(line_);

    // Everything that can refuse is asked BEFORE the money is looked at.
    // Charging for something the player cannot take is the one outcome a shop
    // must never have.
    if (line.arms) {
        const weapons::WeaponSpec& gun = weapons::spec(line.weapon);
        // Pointer identity against the table, not a stored id: WeaponSystem
        // keeps the spec it was equipped with and `weapons::spec` hands out
        // references into one static array, so the two are the same object or
        // they are different weapons.
        const bool carryingIt =
            world.weapons.armed() && &world.weapons.spec() == &gun;
        if (carryingIt && world.weapons.ammo() >= gun.magazine) {
            showBanner("ALREADY LOADED");
            return true;
        }
    } else if (world.player.armor() >= line.armor) {
        // A vest that is still whole. `armor::wear` sets rather than adds, so
        // buying a second one over the first would be paying to lose nothing
        // -- which is worse than being refused, because the money is gone and
        // the screen looks identical.
        showBanner("VEST INTACT");
        return true;
    }

    if (!economy::spend(world.purse, line.price)) {
        showBanner("NOT ENOUGH CASH");
        return true;
    }

    if (line.arms) {
        // equip() reloads to a full magazine, so this is the same call
        // whether the player walked in with an empty gun, with the other one,
        // or with nothing. One slot, one outcome -- which is also why a
        // weapon and its ammunition are the same line: see the header.
        world.weapons.equip(line.weapon);
        world.player.setArmed(true);
    } else {
        world.player.wearVest();
    }
    // The same cue as the pistol off the street, and deliberately: it is the
    // same event -- the player now carries something they did not. A second
    // sound would be a second thing to learn for no second meaning.
    AudioDirector::instance().playCue(audio_cues::Cue::WeaponPickup);
    showBanner(line.label);
    return true;
}

bool CornerShopScene::onFirePressed() {
    // Only while the panel is up. With it closed the trigger is still the
    // trigger anywhere in this room, the till included: the shelves can be
    // shot at, it costs ammunition and it raises the level -- the shop is
    // cover, not an alibi.
    if (!pickerOpen_) {
        return false;
    }
    return serveAtCounter();
}

bool CornerShopScene::onActionPressed() {
    if (pickerOpen_) {
        // The same button both ways round, which is what makes it safe to
        // press: whatever RUN did to get here, RUN undoes.
        pickerOpen_ = false;
        return true;
    }
    if (atCounter()) {
        pickerOpen_ = true;
        // Cheapest line first, every time the panel opens. See the header.
        line_ = shop::first();
        // Nothing to do about a direction still held from walking to the
        // till: `stepPicker` takes both edges on every logic step whether the
        // panel is open or not, so the latches already hold what the pad
        // holds and a key down since before the press is not a press.
        return true;
    }
    if (!atShopExit()) {
        return false;
    }
    CityWorld& world = CityWorld::instance();
    AudioDirector::instance().playCue(audio_cues::Cue::Doorway);
    world.requestScene(world.cityScene());
    return true;
}

const char* CornerShopScene::hintLabel() {
    if (pickerOpen_) {
        // The panel has its own footer naming both buttons, and two prompt
        // lines saying different things is one more than a player will read.
        return nullptr;
    }
    if (atCounter()) {
        return "RUN: SHOP";
    }
    return atShopExit() ? "RUN: STEP OUT" : nullptr;
}

bool CornerShopScene::spaceNeedsRedraw() const {
    return pickerOpen_ != drawnPickerOpen_ || line_ != drawnLine_;
}

void CornerShopScene::recordSpaceDrawn() {
    drawnPickerOpen_ = pickerOpen_;
    drawnLine_ = line_;
}

void CornerShopScene::stepPicker(const StepInput& in) {
    // Both edges are taken every step whether the panel is open or not, so
    // closing it and reopening it does not arrive with a stale latch.
    const bool upPressed   = in.up && !heldUp_;
    const bool downPressed = in.down && !heldDown_;
    heldUp_   = in.up;
    heldDown_ = in.down;

    if (!pickerOpen_) {
        return;
    }
    // Down before up rather than either order, because both held at once is
    // reachable on a real D-pad and one of them has to win deterministically.
    if (downPressed) {
        line_ = shop::next(line_);
    } else if (upPressed) {
        line_ = shop::prev(line_);
    }
}

void CornerShopScene::drawModal(gfx::Renderer& renderer) {
    if (!pickerOpen_) {
        return;
    }

    renderer.drawFilledRectangle(kShopPickerX, kShopPickerY,
                                 kShopPickerW, kShopPickerH, hud::kPanel);
    renderer.drawRectangle(kShopPickerX, kShopPickerY,
                           kShopPickerW, kShopPickerH, hud::kEdge);
    // drawTextCentered centres on the PANEL, not on the plate -- which is the
    // same thing only because kShopPickerX centres the plate. The chess demo
    // needs its own helper here because its picker is centred on the board
    // rather than on the screen.
    renderer.drawTextCentered("CORNER SHOP", kShopPickerY + 3, hud::kInk, 1);

    const economy::Purse& purse = CityWorld::instance().purse;

    for (std::uint8_t i = 0; i < shop::kLineCount; ++i) {
        const shop::Offer& line = shop::offer(static_cast<shop::Line>(i));
        const int rowY = kShopRowsY + i * kShopRowH;
        const bool selected = static_cast<std::uint8_t>(line_) == i;

        // Unaffordable in red, both the label and the price. It is the one
        // piece of information the player came in with -- how much they have
        // -- answered against the one they came in for, and answering it in
        // the list saves a purchase that can only be refused.
        const gfx::Color ink =
            economy::canAfford(purse, line.price) ? hud::kInk : hud::kDanger;

        if (selected) {
            // A caret rather than a filled bar alone. On a 12-bit panel two
            // shades of the same plate are one colour, and a highlight that
            // survives that is a shape, not a tint.
            renderer.drawText(">", kShopPickerX + kHudPadPx, rowY + 2,
                              hud::kAccent, 1);
        }
        renderer.drawText(line.label,
                          kShopPickerX + kHudPadPx + kShopMarkerW, rowY + 2,
                          ink, 1);

        // Formatted here rather than carried in the table, so the number on
        // screen IS the number `serveAtCounter` charges: there is no second
        // copy of it to drift. Leading zeros are blanked rather than drawn --
        // "$0060" reads as a part number.
        char price[kShopPriceChars + 1] = {};
        price[0] = '$';
        price[1] = static_cast<char>('0' + (line.price / 1000) % 10);
        price[2] = static_cast<char>('0' + (line.price / 100) % 10);
        price[3] = static_cast<char>('0' + (line.price / 10) % 10);
        price[4] = static_cast<char>('0' + line.price % 10);
        // Blanked, never past the last digit: a free line would still read
        // "$0" rather than "$".
        for (int digit = 1; digit < kShopPriceChars - 1
                            && price[digit] == '0'; ++digit) {
            price[digit] = ' ';
        }
        renderer.drawText(price, kShopPriceX, rowY + 2, ink, 1);
    }

    renderer.drawTextCentered("FIRE BUY  RUN CLOSE",
                              kShopPickerY + kShopPickerH - kShopFooterH + 1,
                              hud::kInkMuted, 1);
}

PersonHit CornerShopScene::hitPersonBox(int left, int top, int width,
                                        int height, std::uint8_t damage) {
    (void)left;
    (void)top;
    (void)width;
    (void)height;
    (void)damage;
    // Nobody is in here, so a round crosses the room and hits the far wall.
    // Emptying a magazine into the shelves is still ShotFired, which the
    // weapon system reports on its own -- the shop is cover, not an alibi.
    PersonHit hit;
    hit.any     = false;
    hit.killed  = false;
    hit.officer = false;
    return hit;
}

bool CornerShopScene::crowdSees(int x, int y, int rangePx) {
    (void)x;
    (void)y;
    (void)rangePx;
    // Not a question about this room -- there is nobody in it to ask. It is a
    // question about the doorway: the force that watched the player come
    // through it is standing outside, and for as long as they are, the star
    // clock is held exactly as if they were in here.
    return hideout::watching(CityWorld::instance().hideBurn);
}

void CornerShopScene::alarmCrowd(int x, int y, int rangePx) {
    (void)x;
    (void)y;
    (void)rangePx;
    // Nobody to alarm. Deliberately not routed out to the street: the point of
    // the room is that what happens in it is not seen, and a gunshot that
    // alerted the crowd outside would make the walls one-way.
}

std::uint32_t CornerShopScene::crowdVisualKey() const {
    // Nothing in this room moves except the player, whom BaseCityScene already
    // keys. A constant is what tells the frame skip so.
    return 0;
}

bool CornerShopScene::stepSpace(const StepInput& in) {
    CityWorld& world = CityWorld::instance();

    // Before the wanted clock, not after: the burn is one of the two inputs
    // that clock reads, and reading it a step stale would give the player one
    // free step of cooling on the step the police gave up.
    hideout::tick(world.hideBurn);

    stepWantedClock();
    // Neither clock stops at the door: the drop is outside and so is the car,
    // so pausing them would only be a rule to explain -- and a job you could
    // park indefinitely by standing at a till is not a job. Ticked here, judged
    // at the bottom of the step: see the @warning over
    // BaseCityScene::objectiveExpired, which covers this room's case.
    stepObjectiveClock();

    // Before the walk, because it decides whether there is one: the picker
    // owns the pad while it is up.
    stepPicker(in);
    if (!pickerOpen_) {
        movePlayerOnFoot(in);
    }

    // No hunt and no return fire: there is nobody in here to run either. What
    // remains is the player's own weapon, which still fires, still costs ammo
    // and still raises the level -- see hitPersonBox(). Rounds already in the
    // air keep travelling with the panel up: freezing them would make the
    // picker a pause button, and this room is cover for a reason.
    stepGuns();
    objectiveExpired();
    return !checkPlayerDown();
}

void CornerShopScene::drawSpace(gfx::Renderer& renderer) {
    // Two layers, not three: a room has no third storey of decoration. The
    // camera is pinned at the origin, so this is the whole frame.
    renderer.drawTileMap(shop_room::background, 0, 0, gfx::LayerType::Static);
    renderer.drawTileMap(shop_room::items, 0, 0, gfx::LayerType::Static);

    Scene::draw(renderer);   // the player, and in here that is everybody
}

}  // namespace top_down_city
