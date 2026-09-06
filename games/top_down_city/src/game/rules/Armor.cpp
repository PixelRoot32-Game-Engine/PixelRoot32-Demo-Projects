#include "game/rules/Armor.h"

namespace top_down_city::armor {

Vest none() {
    Vest vest;
    vest.points = 0;
    return vest;
}

bool worn(const Vest& vest) {
    return vest.points > 0;
}

void wear(Vest& vest) {
    vest.points = kVestPoints;
}

void strip(Vest& vest) {
    vest.points = 0;
}

std::uint8_t absorb(Vest& vest, std::uint8_t damage) {
    if (damage <= vest.points) {
        // The whole hit is the vest's, including a hit of zero -- which is
        // reachable, because `incomingDamage` halves a bumper and a weak
        // enough one rounds away. A vest that spent a point on it would be
        // worn through by traffic that never hurt anybody.
        vest.points = static_cast<std::uint8_t>(vest.points - damage);
        return 0;
    }
    // Subtracted in this order, not the other: `damage - vest.points` is only
    // safe because the branch above has already proved the vest is smaller.
    const std::uint8_t through =
        static_cast<std::uint8_t>(damage - vest.points);
    vest.points = 0;
    return through;
}

}  // namespace top_down_city::armor
