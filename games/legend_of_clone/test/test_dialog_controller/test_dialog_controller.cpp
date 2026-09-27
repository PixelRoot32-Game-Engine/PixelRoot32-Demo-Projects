/**
 * @brief DialogController frame by frame, on the engine's real DialogRunner.
 *
 * One update() call is one game frame. The controller sees only press edges
 * and what the player is facing, so each test spells out the frames a player
 * would produce: walk up, press A, press A again.
 *
 * The three behaviours that are easy to get wrong and invisible in a
 * screenshot are pinned here: the A press that opens a dialog is not also fed
 * to the runner, a shop purchase is answered within the same frame but outside
 * the runner's callback (the runner drops a start() made inside it), and the
 * player never gets a frame to walk between the menu and the answer.
 */
#include <unity.h>

#include "game/DialogController.h"
#include "game/DialogScripts.h"
#include "game/GameState.h"

using namespace legend_of_clone;
namespace gp = pixelroot32::gameplay;

void setUp() {}
void tearDown() {}

namespace {

constexpr unsigned long kFrameMs = 16;

const DialogInput kNoInput{};

DialogInput pressA() {
    DialogInput input;
    input.confirm = true;
    return input;
}

DialogInput pressB() {
    DialogInput input;
    input.cancel = true;
    return input;
}

DialogInput pressDown() {
    DialogInput input;
    input.down = true;
    return input;
}

void frame(DialogController& controller, const DialogInput& input,
           Interactable facing = Interactable::None) {
    controller.update(input, facing, kFrameMs);
}

/// Every event the runner emits, in order, forwarded to the controller.
struct EventRecorder {
    DialogController* controller = nullptr;
    gp::DialogEventType types[16]{};
    gp::LineId lines[16]{};
    uint8_t count = 0;
};

void recordAndForward(void* owner, const gp::DialogEvent& event) {
    auto* recorder = static_cast<EventRecorder*>(owner);
    if (recorder->count < 16) {
        recorder->types[recorder->count] = event.type;
        recorder->lines[recorder->count] = event.line;
        ++recorder->count;
    }
    DialogController::onDialogEvent(recorder->controller, event);
}

/// Walks the shop menu down to `index` and confirms it.
void confirmMenuRow(DialogController& controller, uint8_t index) {
    for (uint8_t i = 0; i < index; ++i) frame(controller, pressDown(), Interactable::Shopkeeper);
    frame(controller, pressA(), Interactable::Shopkeeper);
}

}  // namespace

// --- Opening -------------------------------------------------------------

void test_nothing_starts_without_a_press() {
    GameState state{};
    DialogController controller(state);
    frame(controller, kNoInput, Interactable::Sign);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
}

void test_a_press_facing_nothing_starts_nothing() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::None);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
}

void test_the_press_that_opens_the_sign_does_not_advance_it() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Sign);

    TEST_ASSERT_TRUE(controller.runner().isActive());
    TEST_ASSERT_TRUE(controller.runner().currentLine() == &kSignScript.lines[kSignLine]);
    TEST_ASSERT_TRUE(controller.runner().state() == gp::DialogState::AwaitingAdvance);
}

void test_the_next_press_closes_the_sign() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Sign);
    frame(controller, pressA(), Interactable::Sign);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
}

void test_a_press_on_the_closing_frame_does_not_reopen_the_dialog() {
    // The frame that ends a dialog feeds A to the runner and nothing else, so
    // a player still facing the sign needs a new press to read it again.
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Sign);
    frame(controller, pressA(), Interactable::Sign);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    frame(controller, pressA(), Interactable::Sign);
    TEST_ASSERT_TRUE(controller.runner().isActive());
}

// --- Freezing the player (R11) -------------------------------------------

void test_the_player_is_frozen_while_a_dialog_is_active() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Sign);
    TEST_ASSERT_TRUE(controller.blocksPlayer());
    frame(controller, kNoInput, Interactable::None);
    TEST_ASSERT_TRUE(controller.blocksPlayer());
}

// --- Old man (R3, R4, R6) ------------------------------------------------

void test_the_old_man_gives_the_sword_on_the_first_visit() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::OldMan);
    TEST_ASSERT_EQUAL_UINT16(kOldManIntro, controller.runner().currentLineId());
    TEST_ASSERT_FALSE(state.hasSword);

    frame(controller, pressA(), Interactable::OldMan);
    TEST_ASSERT_EQUAL_UINT16(kOldManSword, controller.runner().currentLineId());
    TEST_ASSERT_TRUE(state.hasSword);

    frame(controller, pressA(), Interactable::OldMan);
    TEST_ASSERT_FALSE(controller.runner().isActive());
}

void test_the_old_man_gives_directions_on_later_visits() {
    GameState state{};
    state.hasSword = true;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::OldMan);
    TEST_ASSERT_EQUAL_UINT16(kOldManDirections, controller.runner().currentLineId());
    frame(controller, pressA(), Interactable::OldMan);
    TEST_ASSERT_FALSE(controller.runner().isActive());
}

// --- Chest ---------------------------------------------------------------

void test_the_chest_pays_when_its_line_appears() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Chest);
    TEST_ASSERT_TRUE(controller.runner().isActive());
    TEST_ASSERT_TRUE(state.chestOpened);
    TEST_ASSERT_EQUAL_UINT16(40, state.rupees);
}

void test_an_open_chest_starts_nothing_and_pays_nothing() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Chest);
    frame(controller, pressA(), Interactable::Chest);
    TEST_ASSERT_FALSE(controller.runner().isActive());

    frame(controller, pressA(), Interactable::Chest);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
    TEST_ASSERT_EQUAL_UINT16(40, state.rupees);
}

// --- Shop (R5, R7, R8) ---------------------------------------------------

void test_the_shop_opens_on_the_menu_for_what_the_player_owns() {
    GameState state{};
    state.hasShield = true;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuShieldOwned, controller.runner().currentLineId());
    TEST_ASSERT_TRUE(controller.runner().state() == gp::DialogState::ShowingChoices);
    TEST_ASSERT_EQUAL_UINT8(0, controller.runner().selectedChoice());
}

void test_leave_ends_the_shop() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    confirmMenuRow(controller, 3);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
    frame(controller, kNoInput, Interactable::Shopkeeper);
    TEST_ASSERT_FALSE(controller.runner().isActive());
}

void test_cancel_ends_the_shop() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    frame(controller, pressB(), Interactable::Shopkeeper);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
}

void test_cancel_does_nothing_on_a_text_line() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Sign);
    frame(controller, pressB(), Interactable::Sign);
    TEST_ASSERT_TRUE(controller.runner().isActive());
}

void test_a_purchase_is_answered_in_the_same_frame() {
    GameState state{};
    state.rupees = 40;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_TRUE(controller.runner().state() == gp::DialogState::ShowingChoices);

    confirmMenuRow(controller, 0);  // SHIELD

    // The frame that confirmed the choice already shows the answer: there is
    // no frame in which the scene would draw a Finished or inactive runner.
    TEST_ASSERT_TRUE(controller.runner().isActive());
    TEST_ASSERT_TRUE(controller.runner().state() == gp::DialogState::AwaitingAdvance);
    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, controller.runner().currentLineId());
    TEST_ASSERT_TRUE(state.hasShield);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
    TEST_ASSERT_TRUE(controller.blocksPlayer());
}

void test_the_answer_starts_after_the_menu_ends_not_inside_its_callback() {
    // A start() made inside the runner's callback is dropped by its
    // reentrancy guard, so an answer that only started there would never
    // appear. The recorder shows the menu's ChoiceConfirmed and Ended were
    // both fully dispatched before the answer's LineEnter, each exactly once.
    GameState state{};
    state.rupees = 40;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);

    EventRecorder recorder;
    recorder.controller = &controller;
    controller.runner().configure(&recorder, &recordAndForward);

    frame(controller, pressA(), Interactable::Shopkeeper);  // SHIELD

    TEST_ASSERT_EQUAL_UINT8(3, recorder.count);
    TEST_ASSERT_TRUE(recorder.types[0] == gp::DialogEventType::ChoiceConfirmed);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuNothingOwned, recorder.lines[0]);
    TEST_ASSERT_TRUE(recorder.types[1] == gp::DialogEventType::Ended);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuNothingOwned, recorder.lines[1]);
    TEST_ASSERT_TRUE(recorder.types[2] == gp::DialogEventType::LineEnter);
    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, recorder.lines[2]);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
}

void test_the_buying_press_does_not_also_advance_the_answer() {
    GameState state{};
    state.rupees = 40;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    confirmMenuRow(controller, 0);
    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, controller.runner().currentLineId());

    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
}

void test_cancel_pressed_with_the_buying_confirm_does_not_dismiss_the_answer() {
    // Confirm is fed before Cancel, so Cancel lands on the answer, a text
    // line, where it does nothing. The item is charged once.
    GameState state{};
    state.rupees = 40;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);

    DialogInput both;
    both.confirm = true;
    both.cancel = true;
    frame(controller, both, Interactable::Shopkeeper);

    TEST_ASSERT_TRUE(controller.runner().isActive());
    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, controller.runner().currentLineId());
    TEST_ASSERT_TRUE(state.hasShield);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
}

void test_down_and_confirm_in_one_frame_buy_the_row_moved_to() {
    GameState state{};
    state.rupees = 20;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);

    DialogInput both;
    both.down = true;
    both.confirm = true;
    frame(controller, both, Interactable::Shopkeeper);  // KEY

    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, controller.runner().currentLineId());
    TEST_ASSERT_TRUE(state.hasKey);
    TEST_ASSERT_FALSE(state.hasShield);
    TEST_ASSERT_EQUAL_UINT16(0, state.rupees);
}

void test_an_unaffordable_purchase_answers_not_enough_and_charges_nothing() {
    GameState state{};
    state.rupees = 10;
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    confirmMenuRow(controller, 1);  // KEY

    TEST_ASSERT_EQUAL_UINT16(kShopNotEnough, controller.runner().currentLineId());
    TEST_ASSERT_FALSE(state.hasKey);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);

    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
}

void test_the_whole_slice_economy_in_one_run() {
    // Chest, shield, then the key is refused and a potion is not.
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Chest);
    frame(controller, pressA(), Interactable::Chest);

    frame(controller, pressA(), Interactable::Shopkeeper);
    confirmMenuRow(controller, 0);  // SHIELD on the nothing-owned menu
    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);

    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuShieldOwned, controller.runner().currentLineId());
    confirmMenuRow(controller, 0);  // KEY on the shield-owned menu
    TEST_ASSERT_EQUAL_UINT16(kShopNotEnough, controller.runner().currentLineId());
    frame(controller, pressA(), Interactable::Shopkeeper);

    frame(controller, pressA(), Interactable::Shopkeeper);
    confirmMenuRow(controller, 1);  // POTION on the shield-owned menu
    TEST_ASSERT_EQUAL_UINT16(kShopThankYou, controller.runner().currentLineId());
    TEST_ASSERT_EQUAL_UINT8(1, state.potions);
    TEST_ASSERT_EQUAL_UINT16(0, state.rupees);
    TEST_ASSERT_FALSE(state.hasKey);
}

void test_reset_drops_an_active_dialog() {
    GameState state{};
    DialogController controller(state);
    frame(controller, pressA(), Interactable::Shopkeeper);
    TEST_ASSERT_TRUE(controller.blocksPlayer());

    controller.reset();
    TEST_ASSERT_FALSE(controller.runner().isActive());
    TEST_ASSERT_FALSE(controller.blocksPlayer());
    frame(controller, kNoInput, Interactable::None);
    TEST_ASSERT_FALSE(controller.runner().isActive());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_starts_without_a_press);
    RUN_TEST(test_a_press_facing_nothing_starts_nothing);
    RUN_TEST(test_the_press_that_opens_the_sign_does_not_advance_it);
    RUN_TEST(test_the_next_press_closes_the_sign);
    RUN_TEST(test_a_press_on_the_closing_frame_does_not_reopen_the_dialog);
    RUN_TEST(test_the_player_is_frozen_while_a_dialog_is_active);
    RUN_TEST(test_the_old_man_gives_the_sword_on_the_first_visit);
    RUN_TEST(test_the_old_man_gives_directions_on_later_visits);
    RUN_TEST(test_the_chest_pays_when_its_line_appears);
    RUN_TEST(test_an_open_chest_starts_nothing_and_pays_nothing);
    RUN_TEST(test_the_shop_opens_on_the_menu_for_what_the_player_owns);
    RUN_TEST(test_leave_ends_the_shop);
    RUN_TEST(test_cancel_ends_the_shop);
    RUN_TEST(test_cancel_does_nothing_on_a_text_line);
    RUN_TEST(test_a_purchase_is_answered_in_the_same_frame);
    RUN_TEST(test_the_answer_starts_after_the_menu_ends_not_inside_its_callback);
    RUN_TEST(test_the_buying_press_does_not_also_advance_the_answer);
    RUN_TEST(test_cancel_pressed_with_the_buying_confirm_does_not_dismiss_the_answer);
    RUN_TEST(test_down_and_confirm_in_one_frame_buy_the_row_moved_to);
    RUN_TEST(test_an_unaffordable_purchase_answers_not_enough_and_charges_nothing);
    RUN_TEST(test_the_whole_slice_economy_in_one_run);
    RUN_TEST(test_reset_drops_an_active_dialog);
    return UNITY_END();
}
