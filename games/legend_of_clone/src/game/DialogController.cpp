#include "game/DialogController.h"

#include "game/StoryRules.h"

namespace legend_of_clone {

namespace gp = pixelroot32::gameplay;

DialogController::DialogController(GameState& state) : state_(state) {
    runner_.configure(this, &DialogController::onDialogEvent);
}

void DialogController::update(const DialogInput& input, Interactable facing,
                              unsigned long deltaTimeMs) {
    if (runner_.isActive()) {
        if (input.up)      runner_.feed(gp::DialogAction::Up);
        if (input.down)    runner_.feed(gp::DialogAction::Down);
        if (input.confirm) confirm();
        // After Confirm: if Confirm just answered a purchase, Cancel lands on
        // the answer, a text line, where the runner ignores it.
        if (input.cancel)  runner_.feed(gp::DialogAction::Cancel);
        runner_.update(deltaTimeMs);
        return;
    }

    if (input.confirm) {
        startFor(facing);
    }
}

void DialogController::reset() {
    runner_.stop();
}

bool DialogController::blocksPlayer() const {
    return runner_.isActive();
}

void DialogController::startFor(Interactable target) {
    switch (target) {
        case Interactable::Sign:
            runner_.start(kSignScript, kSignLine);
            break;
        case Interactable::OldMan:
            runner_.start(kOldManScript, oldManFirstLine(state_));
            break;
        case Interactable::Chest:
            // An open chest is just furniture.
            if (!state_.chestOpened) runner_.start(kChestScript, kChestLine);
            break;
        case Interactable::Shopkeeper:
            runner_.start(kShopScript, shopChoiceLine(state_));
            break;
        case Interactable::None:
            break;
    }
}

void DialogController::confirm() {
    // Read which row is about to be confirmed BEFORE feeding it. Once Confirm
    // leaves the menu, the runner no longer exposes the choice, and remembering
    // it from ChoiceConfirmed would need a member to carry it out of the
    // callback. choice() is nullptr on a text line, so this only matches a
    // highlighted buy row.
    const gp::DialogChoice* row = runner_.choice(runner_.selectedChoice());
    ShopItem item = ShopItem::Shield;
    const bool buying = (row != nullptr) && shopItemForTag(row->tag, item);

    // Confirm aliases Advance on a text line, so A both advances and picks.
    runner_.feed(gp::DialogAction::Confirm);

    // Every buy row points at kNoLine, so a confirmed purchase leaves the
    // runner Finished. feed() has returned and its reentrancy guard is
    // released, so this start() is honoured -- inside the callback it would
    // have been dropped. The scene has not drawn since the menu, so the
    // Finished state is never on screen.
    if (!buying || runner_.state() != gp::DialogState::Finished) return;
    runner_.start(kShopScript, buy(state_, item) ? kShopThankYou : kShopNotEnough);
}

void DialogController::onDialogEvent(void* owner, const gp::DialogEvent& event) {
    static_cast<DialogController*>(owner)->handleDialogEvent(event);
}

void DialogController::handleDialogEvent(const gp::DialogEvent& event) {
    switch (event.type) {
        case gp::DialogEventType::LineEnter:
            if (event.tag == TAG_GIVE_SWORD) {
                state_.hasSword = true;
            } else if (event.tag == TAG_CHEST_REWARD) {
                (void)openChest(state_);
            }
            break;

        case gp::DialogEventType::ChoiceConfirmed:
            // Purchases are answered by confirm() once feed() returns: a
            // start() made here would be dropped by the reentrancy guard.
            break;

        case gp::DialogEventType::Cancelled:
            // The runner reports Cancel but does not end the line itself.
            // stop() is the one call that is legal from inside this callback.
            runner_.stop();
            break;

        case gp::DialogEventType::Ended:
            // Deliberately nothing. The menu's Ended arrives just before a
            // purchase answer starts, and the scene reads blocksPlayer() only
            // after update() returns, so reacting here would be premature.
            break;
    }
}

} // namespace legend_of_clone
