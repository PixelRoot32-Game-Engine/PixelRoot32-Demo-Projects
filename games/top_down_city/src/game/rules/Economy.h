#pragma once
#include <cstdint>

/**
 * What a delivery is worth, and what it buys.
 *
 * The courier run deliberately paid nothing: `Mission.h` said an economy grows
 * a shop, a currency, a HUD row and a balance problem. The first three came
 * cheap -- a room that already exists, a `uint16_t`, one plate in a column
 * that had space -- and the balance problem is what this file is, the only one
 * of the four with no visual failure of its own. A fee too small is a counter
 * nobody ever reaches; a fee too large is one everybody reaches on their first
 * drop. Neither draws anything wrong, so the counter's three prices are pinned
 * in `test_economy` against `deliveryFee` rather than against a number
 * somebody once liked: dearer than the best single delivery, affordable inside
 * one clean streak. They are listed cheapest first, and that order is the
 * progression -- a reload the second delivery pays for, a vest that costs more
 * than the best single drop, a shotgun that costs more again.
 *
 * **What the money is for, and what it is not.** It buys every weapon in the
 * demo, the rounds in them, and the vest; the city's one loose pistol does not
 * come back, so the counter is the only armoury there is, which is what turns
 * the purse from a readout into the thing the courier run is FOR. It is not
 * score -- `mission::State::best` is, and is deliberately the only thing that
 * survives a mistake -- and it deliberately survives an arrest, the one
 * decision here worth stating out loud: the arrest already costs the weapon,
 * the vest, the streak and the walk back, and taking the money as well would
 * put the shop out of reach of exactly the players most likely to need it,
 * who with the street's guns gone have no other way back to a weapon at all.
 */
namespace top_down_city::economy {

/// Four digits, which is what the HUD row draws. A cap rather than a wrap:
/// see `earn`.
constexpr std::uint16_t kMaxCash = 9999;

/// What any delivery pays before the streak is counted.
constexpr std::uint16_t kDeliveryFee = 20;

/// And what each delivery of the current streak adds on top. This is the
/// whole reason the streak exists as a number rather than as a note: before
/// the shop, `mission::State::streak` was drawn nowhere and bought nothing.
constexpr std::uint16_t kStreakBonus = 10;

/// Where the bonus stops climbing. Without it a player who never misses is
/// eventually paid more per drop than the shop sells anything for, and the
/// money stops being a decision -- see
/// `test_the_streak_bonus_stops_climbing`.
constexpr std::uint8_t kStreakBonusCap = 8;

/* ------------------------------------------------------------------------
 * The counter's three prices. The shop walks its list cheapest-first on one
 * button, so the order below is what the player is shown as they press it.
 * ---------------------------------------------------------------------- */

/// A loaded pistol -- which, with one weapon slot, is also what a box of
/// rounds is. More than the opening delivery pays and less than two, so
/// running dry costs a drop rather than a session: this is the line a player
/// who lost a fight comes back to, and one they cannot reach is a run unarmed.
constexpr std::uint16_t kPistolPrice = 60;

/// The vest. Dearer than the best single delivery, so it is bought out of a
/// streak rather than out of a drop.
constexpr std::uint16_t kVestPrice = 120;

/// A loaded shotgun. The top of the ladder, and unchanged from when it was
/// the counter's only line: three to four clean drops, which is a walk worth
/// making and not a wall.
constexpr std::uint16_t kShotgunPrice = 150;

/// What a boost pays: the whole job, not a leg of one. Chapter 1 of
/// `contract::` is one clock over a walk to a car and a drive to a drop -- a
/// courier run's worth of distance with a state machine on top -- paid once.
///
/// Priced against the counter rather than against the fee, because what it has
/// to do is arm the player: the chapter after it hands them something to shoot
/// at, and the city's one loose pistol may already be gone. So one boost must
/// cover `kPistolPrice` (`test_a_boost_arms_the_player_for_the_next_chapter`)
/// and must not reach `kShotgunPrice` -- a single mission that buys the top of
/// the ladder spends the courier loop, the streak and the shop on the demo's
/// first cutscene. 100 sits between them with room at both ends and lands on
/// exactly what a capped clean drop pays: a boost is worth a courier's best
/// possible delivery, without asking for eight of them strung together first.
constexpr std::uint16_t kBoostFee = 100;

/// What the Hit pays. Chapter 2 is a walk into the police station, a marked
/// officer put down, and a walk back out under as much as five stars -- and
/// the player cannot start it unarmed, so the counter has usually already sold
/// them the gun that made it possible.
///
/// Three bounds, all pinned in `test_economy` against the numbers above rather
/// than against a feeling: **more than `kBoostFee`**, since the payouts are a
/// ladder and a second chapter worth no more than the first stops being worth
/// continuing at exactly the point it gets harder; **less than
/// `kShotgunPrice`**, the same ceiling the boost is held under, because one
/// mission that buys the top of the counter outright spends the courier loop,
/// the streak and the shop in a single payout; and **more than `kPistolPrice`
/// plus a cold delivery**, or the payout only covers what the job cost, leaves
/// the player where they started one chapter later, and reads as the story
/// charging admission.
///
/// 125 sits between the first two with 25 either side -- enough that moving
/// any single price by a rung fails a test rather than silently inverting the
/// ladder -- and clears the third by 45, better than a third of `kVestPrice`:
/// the Hit is a start on the armour the chapter after it will want, without
/// funding the shotgun as well.
constexpr std::uint16_t kHitFee = 125;

/// What the Frenzy pays, and the one fee on this page allowed to clear
/// `kShotgunPrice`. Every other payout is held under the top of the counter
/// because buying the shotgun outright would spend the courier loop, the
/// streak and the shop in one go; chapter 3 is where that argument runs out
/// rather than where it is waived. The rampage asks for more bodies than a
/// pistol magazine holds (`test_contract`'s
/// `test_a_rampage_costs_more_than_the_pistol_holds`), so the player has
/// ALREADY bought the shotgun by the time this is paid, and a payout that
/// could not cover it would be the ladder's top rung reimbursing less than the
/// rung cost -- with no chapter after this one for the saved money to matter
/// to.
///
/// Two bounds, pinned in `test_economy`: **more than `kHitFee`**, keeping the
/// ladder a ladder, the same rule the Hit is held to against the boost; and
/// **more than `kShotgunPrice`**, this fee's own rule and the inverse of the
/// other two -- the gun the chapter demands is the gun the chapter repays.
///
/// 200 clears the shotgun by 50 -- change, though short of a reload at
/// `kPistolPrice` -- and beats the Hit by 75, the widest step on the ladder:
/// the last chapter is worth the most and the only one that leaves the player
/// better armed than it found them.
constexpr std::uint16_t kFrenzyFee = 200;

/// What the player is carrying. A struct rather than a bare integer for the
/// same reason `wanted::State` is one: it is the thing the rules take by
/// reference, and a `uint16_t&` at a call site says nothing about which
/// uint16_t.
struct Purse {
    std::uint16_t cash;
};

/// Broke. What a run starts with, and what a run stays at until the first
/// delivery lands.
Purse clear();

/**
 * @brief What a delivered leg pays.
 * @param streak  The streak AFTER the delivery, so the first drop is 1.
 *                Reading it before would pay the base fee for every drop of a
 *                perfect run and make the streak decorative.
 *
 * Flat plus a capped streak bonus rather than scaled by distance: the player
 * does not pick the target, so distance is something the dice decide and
 * paying for it would be paying for the roll.
 */
std::uint16_t deliveryFee(std::uint8_t streak);

/// Take the money. Saturates at `kMaxCash` rather than wrapping: a purse that
/// rolls to nothing after ten minutes of clean deliveries reads as the game
/// taking it back.
void earn(Purse& purse, std::uint16_t amount);

bool canAfford(const Purse& purse, std::uint16_t price);

/// Pay, if the money is there.
/// @return false and leaves the purse untouched when it is not -- a spend
///         that debits on the way to refusing is a shop that charges for
///         saying no.
bool spend(Purse& purse, std::uint16_t price);

}  // namespace top_down_city::economy
