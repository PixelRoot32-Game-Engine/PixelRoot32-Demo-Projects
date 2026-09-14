/**
 * @brief The slice's game rules, with no dialog runner in the loop.
 *
 * Shop prices and stock, which shop menu a given inventory sees, which line
 * the old man opens with, the chest's one-time reward, and the cell in front
 * of the player. Every number here comes from
 * docs/audits/dialog-rpg-slice-requirements.md in the engine repository: one
 * chest pays 40, the shield costs 30, the key 20, the potion 10.
 */
#include <unity.h>

#include "game/DialogScripts.h"
#include "game/GameState.h"
#include "game/Interaction.h"
#include "game/ShopRules.h"
#include "game/StoryRules.h"

#include <type_traits>

using namespace legend_of_clone;
namespace gp = pixelroot32::gameplay;

void setUp() {}
void tearDown() {}

namespace {

GameState withRupees(uint16_t rupees) {
    GameState state{};
    state.rupees = rupees;
    return state;
}

}  // namespace

// --- Game state ----------------------------------------------------------

void test_game_state_is_a_pod_for_the_save_phase() {
    TEST_ASSERT_TRUE(std::is_trivial_v<GameState>);
    TEST_ASSERT_TRUE(std::is_standard_layout_v<GameState>);
}

void test_a_new_game_owns_nothing() {
    const GameState state{};
    TEST_ASSERT_FALSE(state.hasSword);
    TEST_ASSERT_FALSE(state.chestOpened);
    TEST_ASSERT_FALSE(state.hasShield);
    TEST_ASSERT_FALSE(state.hasKey);
    TEST_ASSERT_EQUAL_UINT8(0, state.potions);
    TEST_ASSERT_EQUAL_UINT16(0, state.rupees);
}

// --- Shop prices and stock -----------------------------------------------

void test_prices_match_the_slice() {
    TEST_ASSERT_EQUAL_UINT16(30, priceOf(ShopItem::Shield));
    TEST_ASSERT_EQUAL_UINT16(20, priceOf(ShopItem::Key));
    TEST_ASSERT_EQUAL_UINT16(10, priceOf(ShopItem::Potion));
}

void test_a_new_game_can_afford_nothing() {
    const GameState state{};
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Shield));
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Key));
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Potion));
}

void test_the_exact_price_is_affordable() {
    const GameState state = withRupees(20);
    TEST_ASSERT_TRUE(canBuy(state, ShopItem::Key));
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Shield));
}

void test_buying_deducts_the_price_and_grants_the_item() {
    GameState state = withRupees(40);
    TEST_ASSERT_TRUE(buy(state, ShopItem::Shield));
    TEST_ASSERT_TRUE(state.hasShield);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
}

void test_after_the_shield_the_key_is_unaffordable_and_the_potion_is_not() {
    GameState state = withRupees(40);
    TEST_ASSERT_TRUE(buy(state, ShopItem::Shield));
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Key));
    TEST_ASSERT_TRUE(canBuy(state, ShopItem::Potion));
}

void test_an_unaffordable_purchase_changes_nothing() {
    GameState state = withRupees(10);
    TEST_ASSERT_FALSE(buy(state, ShopItem::Key));
    TEST_ASSERT_FALSE(state.hasKey);
    TEST_ASSERT_EQUAL_UINT16(10, state.rupees);
}

void test_a_sold_out_item_cannot_be_bought_again() {
    GameState state = withRupees(40);
    state.hasKey = true;
    TEST_ASSERT_FALSE(isInStock(state, ShopItem::Key));
    TEST_ASSERT_FALSE(canBuy(state, ShopItem::Key));
    TEST_ASSERT_FALSE(buy(state, ShopItem::Key));
    TEST_ASSERT_EQUAL_UINT16(40, state.rupees);
}

void test_the_potion_is_repeatable() {
    GameState state = withRupees(20);
    TEST_ASSERT_TRUE(buy(state, ShopItem::Potion));
    TEST_ASSERT_TRUE(isInStock(state, ShopItem::Potion));
    TEST_ASSERT_TRUE(buy(state, ShopItem::Potion));
    TEST_ASSERT_EQUAL_UINT8(2, state.potions);
    TEST_ASSERT_EQUAL_UINT16(0, state.rupees);
    TEST_ASSERT_FALSE(buy(state, ShopItem::Potion));
    TEST_ASSERT_EQUAL_UINT8(2, state.potions);
}

void test_buy_tags_name_their_items() {
    ShopItem item = ShopItem::Potion;
    TEST_ASSERT_TRUE(shopItemForTag(TAG_BUY_SHIELD, item));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ShopItem::Shield), static_cast<uint8_t>(item));
    TEST_ASSERT_TRUE(shopItemForTag(TAG_BUY_KEY, item));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ShopItem::Key), static_cast<uint8_t>(item));
    TEST_ASSERT_TRUE(shopItemForTag(TAG_BUY_POTION, item));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ShopItem::Potion), static_cast<uint8_t>(item));
}

void test_other_tags_name_no_item() {
    ShopItem item = ShopItem::Potion;
    TEST_ASSERT_FALSE(shopItemForTag(TAG_NONE, item));
    TEST_ASSERT_FALSE(shopItemForTag(TAG_GIVE_SWORD, item));
    TEST_ASSERT_FALSE(shopItemForTag(TAG_CHEST_REWARD, item));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ShopItem::Potion), static_cast<uint8_t>(item));
}

// --- Shop menu selection (R8) --------------------------------------------

void test_each_ownership_state_has_its_own_menu_line() {
    GameState state{};
    const gp::LineId none = shopChoiceLine(state);
    state.hasShield = true;
    const gp::LineId shield = shopChoiceLine(state);
    state.hasShield = false;
    state.hasKey = true;
    const gp::LineId key = shopChoiceLine(state);
    state.hasShield = true;
    const gp::LineId both = shopChoiceLine(state);

    TEST_ASSERT_EQUAL_UINT16(kShopMenuNothingOwned, none);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuShieldOwned, shield);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuKeyOwned, key);
    TEST_ASSERT_EQUAL_UINT16(kShopMenuBothOwned, both);
}

void test_the_shop_has_exactly_four_choice_lines() {
    uint16_t choiceLines = 0;
    for (uint16_t i = 0; i < kShopScript.lineCount; ++i) {
        if (kShopScript.lines[i].kind == gp::LineKind::Choice) ++choiceLines;
    }
    TEST_ASSERT_EQUAL_UINT16(4, choiceLines);
}

void test_each_menu_lists_only_items_in_stock_then_leave() {
    for (uint8_t mask = 0; mask < 4; ++mask) {
        GameState state = withRupees(0);
        state.hasShield = (mask & 1) != 0;
        state.hasKey = (mask & 2) != 0;

        const gp::DialogLine& line = kShopScript.lines[shopChoiceLine(state)];
        TEST_ASSERT_TRUE(line.kind == gp::LineKind::Choice);
        TEST_ASSERT_TRUE((line.flags & gp::kLineFlagAllowCancel) != 0);
        TEST_ASSERT_TRUE(line.choiceCount <= 4);

        uint8_t inStock = 0;
        for (uint8_t item = 0; item < 3; ++item) {
            if (isInStock(state, static_cast<ShopItem>(item))) ++inStock;
        }
        TEST_ASSERT_EQUAL_UINT8(inStock + 1, line.choiceCount);

        for (uint8_t i = 0; i < line.choiceCount; ++i) {
            const gp::DialogChoice& choice = kShopScript.choices[line.firstChoice + i];
            // Every choice ends the dialog: the controller answers a buy
            // choice once feed() returns, LEAVE simply ends (R7).
            TEST_ASSERT_EQUAL_UINT16(gp::kNoLine, choice.next);
            ShopItem item = ShopItem::Shield;
            const bool isLast = (i + 1 == line.choiceCount);
            if (isLast) {
                TEST_ASSERT_EQUAL_UINT16(TAG_NONE, choice.tag);
                TEST_ASSERT_EQUAL_STRING("LEAVE", choice.text);
            } else {
                TEST_ASSERT_TRUE(shopItemForTag(choice.tag, item));
                TEST_ASSERT_TRUE(isInStock(state, item));
            }
        }
    }
}

// --- Old man (R4) and chest ----------------------------------------------

void test_the_old_man_opens_with_his_speech_before_the_sword() {
    const GameState state{};
    TEST_ASSERT_EQUAL_UINT16(kOldManIntro, oldManFirstLine(state));
}

void test_the_old_man_opens_with_directions_after_the_sword() {
    GameState state{};
    state.hasSword = true;
    TEST_ASSERT_EQUAL_UINT16(kOldManDirections, oldManFirstLine(state));
}

void test_the_speech_leads_to_the_sword_line_and_the_directions_do_not() {
    const gp::DialogLine& intro = kOldManScript.lines[kOldManIntro];
    TEST_ASSERT_EQUAL_UINT16(kOldManSword, intro.next);
    TEST_ASSERT_EQUAL_UINT16(TAG_GIVE_SWORD, kOldManScript.lines[kOldManSword].tag);
    TEST_ASSERT_EQUAL_UINT16(gp::kNoLine, kOldManScript.lines[kOldManSword].next);
    TEST_ASSERT_EQUAL_UINT16(gp::kNoLine, kOldManScript.lines[kOldManDirections].next);
}

void test_opening_the_chest_pays_forty_rupees_once() {
    GameState state = withRupees(5);
    TEST_ASSERT_TRUE(openChest(state));
    TEST_ASSERT_TRUE(state.chestOpened);
    TEST_ASSERT_EQUAL_UINT16(45, state.rupees);

    TEST_ASSERT_FALSE(openChest(state));
    TEST_ASSERT_TRUE(state.chestOpened);
    TEST_ASSERT_EQUAL_UINT16(45, state.rupees);
}

// --- Facing cell (R9) ----------------------------------------------------

void test_the_facing_cell_is_one_step_in_the_facing_direction() {
    const GridCell at{8, 17};
    GridCell cell = facingCell(at, Facing::Up);
    TEST_ASSERT_EQUAL_INT(8, cell.col);
    TEST_ASSERT_EQUAL_INT(16, cell.row);
    cell = facingCell(at, Facing::Down);
    TEST_ASSERT_EQUAL_INT(8, cell.col);
    TEST_ASSERT_EQUAL_INT(18, cell.row);
    cell = facingCell(at, Facing::Left);
    TEST_ASSERT_EQUAL_INT(7, cell.col);
    TEST_ASSERT_EQUAL_INT(17, cell.row);
    cell = facingCell(at, Facing::Right);
    TEST_ASSERT_EQUAL_INT(9, cell.col);
    TEST_ASSERT_EQUAL_INT(17, cell.row);
}

void test_the_facing_cell_may_leave_the_map() {
    // Bounds are the tile world's job: it answers "nothing here" off the map.
    const GridCell cell = facingCell(GridCell{0, 0}, Facing::Left);
    TEST_ASSERT_EQUAL_INT(-1, cell.col);
    TEST_ASSERT_EQUAL_INT(0, cell.row);
}

void test_the_player_cell_is_the_cell_under_the_sprite_centre() {
    // 16 px sprite: a player flush below the sign's row stands on row 17.
    GridCell cell = playerCellAt(8 * 16, 17 * 16);
    TEST_ASSERT_EQUAL_INT(8, cell.col);
    TEST_ASSERT_EQUAL_INT(17, cell.row);
    // Up to seven pixels off to either side still counts as the same column.
    cell = playerCellAt(8 * 16 + 7, 17 * 16);
    TEST_ASSERT_EQUAL_INT(8, cell.col);
    cell = playerCellAt(8 * 16 - 8, 17 * 16);
    TEST_ASSERT_EQUAL_INT(8, cell.col);
    cell = playerCellAt(8 * 16 + 8, 17 * 16);
    TEST_ASSERT_EQUAL_INT(9, cell.col);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_game_state_is_a_pod_for_the_save_phase);
    RUN_TEST(test_a_new_game_owns_nothing);
    RUN_TEST(test_prices_match_the_slice);
    RUN_TEST(test_a_new_game_can_afford_nothing);
    RUN_TEST(test_the_exact_price_is_affordable);
    RUN_TEST(test_buying_deducts_the_price_and_grants_the_item);
    RUN_TEST(test_after_the_shield_the_key_is_unaffordable_and_the_potion_is_not);
    RUN_TEST(test_an_unaffordable_purchase_changes_nothing);
    RUN_TEST(test_a_sold_out_item_cannot_be_bought_again);
    RUN_TEST(test_the_potion_is_repeatable);
    RUN_TEST(test_buy_tags_name_their_items);
    RUN_TEST(test_other_tags_name_no_item);
    RUN_TEST(test_each_ownership_state_has_its_own_menu_line);
    RUN_TEST(test_the_shop_has_exactly_four_choice_lines);
    RUN_TEST(test_each_menu_lists_only_items_in_stock_then_leave);
    RUN_TEST(test_the_old_man_opens_with_his_speech_before_the_sword);
    RUN_TEST(test_the_old_man_opens_with_directions_after_the_sword);
    RUN_TEST(test_the_speech_leads_to_the_sword_line_and_the_directions_do_not);
    RUN_TEST(test_opening_the_chest_pays_forty_rupees_once);
    RUN_TEST(test_the_facing_cell_is_one_step_in_the_facing_direction);
    RUN_TEST(test_the_facing_cell_may_leave_the_map);
    RUN_TEST(test_the_player_cell_is_the_cell_under_the_sprite_centre);
    return UNITY_END();
}
