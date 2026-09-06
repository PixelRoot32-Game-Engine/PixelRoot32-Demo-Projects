#pragma once
#include <cstdint>

/**
 * The vest: hit points that are bought rather than healed. One subtraction,
 * engine-free like DayNight.h and Weapon.h because nothing here draws -- a
 * vest that absorbs the wrong half of a hit looks identical to one that works,
 * and the only way to notice is to count how many rounds the player survived.
 * `pio test -e host_test` counts them.
 *
 * **Not simply more health.** Health is what the station steps give back for
 * free; armour is what the counter sells. One number would mean either an
 * arrest that hands back a paid-for vest or a heal that has to know what the
 * player bought; two, one of which the respawn refuses to restore, is cheaper
 * than either. **And `wear` SETS rather than adds**, or the vest would be the
 * one thing money buys without limit -- see
 * `test_a_second_vest_is_not_a_thicker_one`.
 */
namespace top_down_city::armor {

/**
 * @brief What a vest is worth.
 *
 * Bounded at both ends, and both ends are the item. Under a single police
 * round (20) it is a decoration the player pays for and never notices. At or
 * over a full life (kPlayerHealth, 100) it is a second life, which would make
 * the arrest -- the only real consequence in the game -- something money
 * switches off. Fifty is two and a half police rounds, or one shotgun pattern
 * minus a pellet: enough to change the outcome of an exchange, not enough to
 * change whether one is worth avoiding.
 */
constexpr std::uint8_t kVestPoints = 50;

/// What the player is wearing. A struct rather than a bare integer for the
/// same reason `economy::Purse` is one: a `uint8_t&` at a call site says
/// nothing about which uint8_t.
struct Vest {
    std::uint8_t points;
};

/// No vest. What a run starts in, and what an arrest puts the player back to.
Vest none();

bool worn(const Vest& vest);

/// Put a full vest on. SETS the points rather than adding them -- see the
/// file comment.
void wear(Vest& vest);

/// Take it off, whatever was left of it. What the arrest does: the weapon is
/// already confiscated at the door, and a vest that survived the cells would
/// be the one purchase a lost fight cannot cost.
void strip(Vest& vest);

/**
 * @brief Spend the vest on a hit, and report what is left of the hit.
 * @return What reaches the player -- zero while the vest still holds, the
 *         remainder once it does not, never more than it was given.
 *
 * Host-tested across the whole `uint8_t` range, because the arithmetic is
 * unsigned and one careless subtraction away from underflowing to 255: a hit
 * that KILLS through a vest that stopped it.
 */
std::uint8_t absorb(Vest& vest, std::uint8_t damage);

}  // namespace top_down_city::armor
