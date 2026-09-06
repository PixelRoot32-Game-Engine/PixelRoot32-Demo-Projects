#pragma once
#include <cstdint>

/**
 * @brief What a doorway is worth to somebody the police are looking for.
 *
 * An interior is a room with nobody on the force standing in it, and
 * `wanted::tick` sheds a star for every six seconds nobody has eyes on the
 * player -- so every door in the city is a button marked CLEAR MY WANTED
 * LEVEL, the exact failure `Wanted.h` describes: a manhunt you can wait out
 * is a queue. The police station closes that hole by being full of police;
 * everywhere else needs this rule -- a doorway only hides you if nobody
 * watched you use it. Seen going in, the force stands outside it for
 * `kBurnSteps`, and the way to clear it is to step back out, break the line
 * of sight, and use a door nobody is looking at.
 *
 * Deliberately NOT permanent: a burn that lasted until the player left would
 * punish one mistake with a room they cannot use and no way to see why. Eight
 * seconds costs more than the star it would otherwise have bought
 * (`test_a_watched_door_costs_more_than_a_star`) and still leaves waiting it
 * out a decision rather than a sentence. Engine-free like the rest of
 * `game/rules/`: none of this draws, so a burn that never fires and a burn
 * that never expires look identical from the outside -- and only the star
 * readout tells them apart.
 */
namespace top_down_city::hideout {

/// Logic steps the police keep watching a doorway they saw the player use.
/// At 62.5 steps a second that is eight seconds, which is deliberately longer
/// than `wanted::kCoolSteps`: a burn cheaper than the star it prevents is a
/// rule that costs nothing.
constexpr std::uint16_t kBurnSteps = 500;

/// How long the door the player is standing behind is still being watched.
/// One counter and nothing else: WHICH door does not matter, because the
/// player can only be inside one of them.
struct Burn {
    std::uint16_t stepsLeft;
};

/// Nothing watched. What a run starts with.
Burn clear();

/**
 * @brief Arm the rule for a doorway the player has just stepped through.
 *
 * Replaces rather than extends: stepping out and coming back in unseen is the
 * intended way to clear a burn, so a second entry must be able to put the
 * counter DOWN. Accumulating would turn the fix into the punishment.
 *
 * @param stars  the level at the moment of entry. Zero burns nothing: there
 *               is no manhunt to hide from, and an officer who happens to be
 *               facing the door has watched somebody go shopping.
 */
Burn onEntry(std::uint8_t stars, bool seenAtDoor);

/// One fixed logic step. Saturates at zero rather than wrapping: this is
/// ticked every step whether or not anything is lit.
void tick(Burn& burn);

/// Is the room the player is in currently NOT hiding them?
bool watching(const Burn& burn);

}  // namespace top_down_city::hideout
