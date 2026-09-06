#include "game/rules/Shop.h"

#include "game/rules/Armor.h"
#include "game/rules/Economy.h"

namespace top_down_city::shop {

namespace {

/*
 * The catalogue. Three lines, cheapest first, and the whole of the shop.
 *
 * PISTOL. With one weapon slot, "a loaded pistol" and "a box of rounds" are
 * the same purchase -- `WeaponSystem::equip` fills the magazine -- so this one
 * line is both, priced as the reload rather than as the gun: more than the
 * first delivery pays and less than the second, so running dry costs a drop.
 * It is the floor under the whole economy; the one pistol lying in the city
 * does not come back, so from the moment it is emptied this row is the only
 * weapon left in the demo.
 *
 * VEST. The only line that is not a gun, and the only one that survives being
 * shot at rather than being spent. Priced above the best single delivery so
 * it comes out of a streak.
 *
 * SHOTGUN. Unchanged from when it was the counter's only line, and still the
 * top of the ladder. Twelve shells, and no fallback when they are gone: empty
 * means unarmed, which is what makes it a trade rather than a free upgrade.
 */
constexpr Offer kOffers[kLineCount] = {
    {"PISTOL",  economy::kPistolPrice,  weapons::WeaponId::Pistol,
     true,  false, 0},
    {"VEST",    economy::kVestPrice,    weapons::WeaponId::Pistol,
     false, true,  armor::kVestPoints},
    {"SHOTGUN", economy::kShotgunPrice, weapons::WeaponId::Shotgun,
     true,  false, 0},
};

bool inRange(Line line) {
    return static_cast<std::uint8_t>(line) < kLineCount;
}

}  // namespace

Line first() {
    return Line::Pistol;
}

Line next(Line line) {
    if (!inRange(line)) {
        return first();
    }
    const std::uint8_t index = static_cast<std::uint8_t>(line) + 1;
    return index < kLineCount ? static_cast<Line>(index) : first();
}

Line prev(Line line) {
    if (!inRange(line)) {
        return first();
    }
    const std::uint8_t index = static_cast<std::uint8_t>(line);
    // Tested for zero BEFORE the subtraction, never after. `index - 1` on a
    // uint8_t at zero is 255, which is inside no table in this demo.
    return index == 0 ? static_cast<Line>(kLineCount - 1)
                      : static_cast<Line>(index - 1);
}

const Offer& offer(Line line) {
    // Clamped rather than asserted, like `next` and `weapons::spec`: the
    // wrong product is a better failure than a wrong pointer.
    return kOffers[inRange(line) ? static_cast<std::uint8_t>(line)
                                 : static_cast<std::uint8_t>(first())];
}

}  // namespace top_down_city::shop
