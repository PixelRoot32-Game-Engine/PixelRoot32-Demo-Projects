#include "game/rules/Economy.h"

namespace top_down_city::economy {

Purse clear() {
    Purse purse;
    purse.cash = 0;
    return purse;
}

std::uint16_t deliveryFee(std::uint8_t streak) {
    const std::uint8_t counted =
        streak > kStreakBonusCap ? kStreakBonusCap : streak;
    // Widened before the multiply: eight bonuses of ten is small, but the
    // cap is the only thing keeping it so, and a constant somebody raises is
    // not a reason for a fee to wrap.
    const std::uint32_t fee = static_cast<std::uint32_t>(kDeliveryFee)
                            + static_cast<std::uint32_t>(kStreakBonus)
                                  * counted;
    return fee > kMaxCash ? kMaxCash : static_cast<std::uint16_t>(fee);
}

void earn(Purse& purse, std::uint16_t amount) {
    const std::uint32_t total =
        static_cast<std::uint32_t>(purse.cash) + amount;
    purse.cash = total > kMaxCash ? kMaxCash
                                  : static_cast<std::uint16_t>(total);
}

bool canAfford(const Purse& purse, std::uint16_t price) {
    return purse.cash >= price;
}

bool spend(Purse& purse, std::uint16_t price) {
    if (!canAfford(purse, price)) {
        return false;
    }
    purse.cash = static_cast<std::uint16_t>(purse.cash - price);
    return true;
}

}  // namespace top_down_city::economy
