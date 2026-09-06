#pragma once
#include <cstdint>

#include "game/rules/Facing.h"

/**
 * What a weapon IS, separately from what firing one does. A table, so that a
 * second weapon is a row rather than a refactor. Deliberately NOT here: the
 * projectiles -- a live bullet has a position, hits things and is drawn, so it
 * belongs with WeaponSystem.
 *
 * Engine-free, like DayNight.h and MinimapRuns.h and for the same reason:
 * everything below is integer bookkeeping that fails SILENTLY. A fire rate off
 * by one step is a gun nobody can hear is wrong; a range that truncates instead
 * of rounding up stops a pixel short of what the table promised; an infinite
 * magazine that decrements wraps to 65535 and jams half an hour later, once.
 * `pio test -e host_test` covers all of it.
 */
namespace top_down_city::weapons {

/// A magazine that never runs down. A sentinel and not a flag, because a
/// magazine size is exactly where that fact belongs. Only the police carry
/// one now -- see kPistolMagazine.
constexpr std::uint16_t kInfiniteAmmo = 0xFFFF;

/**
 * What a pistol holds. Infinite for four stages, on the argument that an
 * emptied shotgun leaves the player unarmed and the pistol is what they walk
 * back TO. The shop killed that argument: what they walk back to is now the
 * counter, and a weapon that never runs out is a counter nobody visits --
 * which would make every price in Economy.h decoration.
 *
 * Ten rounds is five people at 50 damage each, or two exchanges with an
 * officer: enough that the gun is worth carrying, few enough that carrying it
 * is a thing that ends. Priced as `economy::kPistolPrice` against exactly that
 * -- a reload costs a delivery, so shooting has a running cost and the courier
 * run is what pays it.
 */
constexpr std::uint16_t kPistolMagazine = 10;

/// What a shotgun holds. Its own constant rather than a literal in the table,
/// and rather than one shared with the pistol: the two rows are balanced
/// against different things -- ten rounds is a running cost, twelve shells is
/// one of the shotgun's three prices -- and a single constant covering both is
/// one careless rename away from silently retuning a weapon nobody edited.
constexpr std::uint16_t kShotgunMagazine = 12;

enum class WeaponId : std::uint8_t {
    Pistol = 0,
    /// What the police carry: a whole second weapon, with its own damage and
    /// rate of fire, for one line.
    PolicePistol,
    /// The first weapon that needed the table to GROW rather than be filled in
    /// -- pellets and spread are two columns nothing before it used. It was
    /// also the first finite magazine, and for four stages the only one, until
    /// the shop made a pistol the thing you buy rounds for -- see
    /// `kPistolMagazine`.
    Shotgun,
    Count,
};

/**
 * @brief One weapon's numbers. Everything the system needs, nothing it does.
 *
 * A row is 12 bytes plus the name pointer, so the table stays in flash and
 * stays small. Rates and speeds are in the scene's fixed 16 ms logic step
 * rather than milliseconds, because that is the clock the shooting runs on --
 * converting at the call site would put a division in a hot loop.
 */
struct WeaponSpec {
    const char*   name;
    std::uint8_t  damage;              ///< Hit points removed per projectile.
    std::uint8_t  fireRateSteps;       ///< Logic steps between shots.
    std::uint16_t rangePx;             ///< How far a projectile reaches.
    std::uint16_t magazine;            ///< Rounds, or kInfiniteAmmo.
    std::uint16_t projectileSpeedSub;  ///< Sub-pixels per logic step.
    std::uint8_t  tracerLengthPx;      ///< How long the streak is drawn.
    std::uint8_t  pellets;             ///< Projectiles per pull. Never 0.
    std::uint16_t spreadSub;           ///< Sideways sub-pixels per step, per
                                       ///< pellet away from the centre line.
};

/// The sideways velocity of pellet `index` of `pellets`. Symmetric about
/// zero, so an odd count always sends one straight down the middle.
std::int32_t pelletOffsetSub(const WeaponSpec& s, std::uint8_t index);

/// The table. Out of line so the array has one definition rather than one per
/// translation unit that mentions a gun.
const WeaponSpec& spec(WeaponId id);

/// How many logic steps a projectile of this weapon stays alive. Rounded UP: a
/// bullet that expires one step early under-delivers at exactly the distance
/// the player was aiming from, and invisibly -- a bullet that vanishes short
/// looks identical to one that missed.
std::uint16_t lifetimeSteps(const WeaponSpec& s);

/// The mutable half: what this gun has left and when it may fire again.
/// Separate from the spec because the spec is flash and this is not.
struct State {
    std::uint16_t ammo;
    std::uint8_t  cooldownSteps;
};

/// A full magazine, ready to fire.
State load(const WeaponSpec& s);

bool canFire(const State& st);
bool isEmpty(const State& st);

/// Charge the cooldown and spend a round. Spends nothing when the magazine is
/// infinite -- the one place `kInfiniteAmmo` means anything, so nothing else
/// has to remember the sentinel exists.
void onFired(State& st, const WeaponSpec& s);

/// One logic step of cooling. Saturates at zero.
void tick(State& st);

/* ------------------------------------------------------------------------
 * Tunnelling
 * ---------------------------------------------------------------------- */

/// The projectile's own collision box, square. A bullet tested as a single
/// point slips diagonally between two solid tiles meeting at a corner; two
/// pixels is the cheapest box that cannot.
constexpr int kProjectileBoxPx = 2;

/// The shortest side of anything a projectile is expected to hit: the crowd's
/// box, six pixels tall because a top-down character collides with its feet.
/// CityConstants.h static_asserts the real box has not since become smaller.
constexpr int kSmallestTargetPx = 6;

/// The hit points of the same target. Here rather than in CityConstants.h so a
/// weapon's damage can be checked against what it is meant to stop without this
/// header pulling in the engine; static_asserted equal over there, and what
/// makes "three pellets at 34" a number with a reason.
constexpr int kSmallestTargetHealth = 100;

/**
 * @brief Can this weapon's projectile pass a target without ever overlapping
 *        it?
 *
 * A projectile is moved in whole steps and tested only where it lands, so one
 * fast enough to clear a short box in a single step passes straight through
 * people -- reading as the player missing, sometimes, in one of the four
 * directions, rather than as a bug. A projectile box of B and a target of S
 * overlap for `B + S - 1` consecutive integer positions, so a step of at most
 * that always lands in the window; rounded up, because a sub-pixel speed
 * alternates between floor and ceiling and it is the long step that misses.
 *
 * @return true when every target on the path is hit. Host-tested for every
 *         row, so a fast weapon added later fails the suite, not the player.
 */
bool hitsEveryTargetOnItsPath(const WeaponSpec& s);

/**
 * @brief How far the outermost pellet has strayed from the aim line by the
 *        time the projectiles expire, in whole pixels. Zero for a weapon that
 *        fires one thing.
 *
 * Rounded UP, like lifetimeSteps(): when deciding whether a pattern still fits,
 * the safe direction is to assume the fan is wider than it is.
 */
int patternHalfWidthPx(const WeaponSpec& s);

/**
 * @brief Can the outermost pellet still be on the same smallest target as the
 *        centre one, at the weapon's maximum range?
 *
 * The failure that shipped: every other number in the shotgun row was checked
 * against something, and nothing checked whether the pellets were still near
 * each other when they ARRIVED (the numbers are on the spread in the kWeapons
 * table). A pattern wider than the target is not a wide spread; it is three
 * bullets, two of which always miss. The arithmetic is the tunnelling window
 * read sideways: boxes of B and S overlap while their centres are within
 * (B + S) / 2, and the last integer offset strictly inside is one less.
 * Checked at MAXIMUM range because that is what the table promises -- a weapon
 * whose pattern only holds for half its range has half the range it claims.
 *
 * @return true when the whole pattern can land on one smallest target.
 */
bool patternFitsSmallestTarget(const WeaponSpec& s);

/// A unit direction, exactly one axis non-zero: the character has three sets
/// of frames plus a mirror, so a bullet leaving at an angle the sprite is not
/// pointing would read as a bug in the art.
struct Aim {
    std::int8_t dx;
    std::int8_t dy;
};

Aim aimOf(Facing facing);

}  // namespace top_down_city::weapons
