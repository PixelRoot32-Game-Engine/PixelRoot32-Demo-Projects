#include "game/ShopRules.h"

namespace legend_of_clone {

uint16_t priceOf(ShopItem item) {
    switch (item) {
        case ShopItem::Shield: return 30;
        case ShopItem::Key:    return 20;
        case ShopItem::Potion: return 10;
    }
    return 0xFFFF;  // Unreachable for a valid item; unaffordable if it happens.
}

bool isInStock(const GameState& state, ShopItem item) {
    switch (item) {
        case ShopItem::Shield: return !state.hasShield;
        case ShopItem::Key:    return !state.hasKey;
        case ShopItem::Potion: return true;
    }
    return false;
}

bool canBuy(const GameState& state, ShopItem item) {
    return isInStock(state, item) && state.rupees >= priceOf(item);
}

bool buy(GameState& state, ShopItem item) {
    if (!canBuy(state, item)) return false;

    state.rupees = static_cast<uint16_t>(state.rupees - priceOf(item));
    switch (item) {
        case ShopItem::Shield: state.hasShield = true; break;
        case ShopItem::Key:    state.hasKey = true;    break;
        case ShopItem::Potion:
            // At 10 rupees each and one 40-rupee chest, 255 is out of reach;
            // saturate anyway rather than wrap to zero.
            if (state.potions < 0xFF) ++state.potions;
            break;
    }
    return true;
}

bool shopItemForTag(uint16_t tag, ShopItem& outItem) {
    switch (tag) {
        case TAG_BUY_SHIELD: outItem = ShopItem::Shield; return true;
        case TAG_BUY_KEY:    outItem = ShopItem::Key;    return true;
        case TAG_BUY_POTION: outItem = ShopItem::Potion; return true;
        default:             return false;
    }
}

pixelroot32::gameplay::LineId shopChoiceLine(const GameState& state) {
    if (state.hasShield && state.hasKey) return kShopMenuBothOwned;
    if (state.hasShield) return kShopMenuShieldOwned;
    if (state.hasKey) return kShopMenuKeyOwned;
    return kShopMenuNothingOwned;
}

} // namespace legend_of_clone
