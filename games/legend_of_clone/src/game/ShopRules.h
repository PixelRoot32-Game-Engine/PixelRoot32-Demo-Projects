#pragma once

#include "game/DialogScripts.h"
#include "game/GameState.h"

#include <cstdint>

namespace legend_of_clone {

/// What the shopkeeper sells, in menu order.
enum class ShopItem : uint8_t { Shield = 0, Key, Potion };

/// Price in rupees. Shield 30, key 20, potion 10.
uint16_t priceOf(ShopItem item);

/// Shield and key sell once; the potion never sells out.
bool isInStock(const GameState& state, ShopItem item);

/// In stock and the player has at least its price.
bool canBuy(const GameState& state, ShopItem item);

/**
 * @brief Deducts the price and grants the item.
 * @return false, changing nothing, when canBuy() is false.
 */
bool buy(GameState& state, ShopItem item);

/**
 * @brief The item a TAG_BUY_* choice tag names.
 * @return false, leaving `outItem` untouched, for any other tag.
 */
bool shopItemForTag(uint16_t tag, ShopItem& outItem);

/// The shop menu line listing exactly what `state` can still buy.
pixelroot32::gameplay::LineId shopChoiceLine(const GameState& state);

} // namespace legend_of_clone
