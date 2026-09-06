#include "game/rules/Weapon.h"

namespace top_down_city::weapons {

namespace {

/*
 * The armoury. Each row is balanced against something rather than tuned by
 * feel, and the numbers below say against what.
 *
 * PISTOL. damage 50 against a person's 100 is two shots: one makes the crowd
 * paper, three makes the gun a suggestion. 14 steps is 224 ms, fast enough to
 * read as semi-automatic and slow enough that a shot is a decision. 96 px is
 * six tiles -- further than the player can react to on a 240 px viewport,
 * closer than the edge of the screen. 1536 sub-pixels a step is 375 px/s,
 * faster than the fastest car so a bullet cannot be outrun, slow enough that
 * the tracer is visible for the 16 steps it lives. Ten rounds, the newest
 * number in the table: see kPistolMagazine.
 *
 * POLICE. Weaker and slower on purpose: five hits to put the player down
 * against their two, 480 ms between rounds against 224. An officer who shoots
 * as well as you do turns every encounter into a coin toss, and three of them
 * turn it into a formality. The projectile is identical, because the
 * tunnelling check has to hold for this row exactly as for the other. The only
 * infinite row, and it has to be: an officer counts no rounds, has no counter
 * to walk to and no way to reload, so a finite one would end every chase with
 * three men standing in the road.
 *
 * SHOTGUN. The pistol's trade run the other way: three pellets at 34 is 102
 * against a person's 100, so a hit is one shot -- paid for at 56 px of range
 * against 96, 45 steps against 14, and twelve shells against an infinite
 * magazine. Three costs, which is enough.
 *
 * The spread is NOT a fourth cost, and it shipped as one. At 256 the fan
 * opened a pixel a step against four forward, so the outer pellets left a
 * six-pixel target four steps into a fourteen-step flight: past about a tile,
 * one pellet of three could land. 48 keeps the whole pattern on one person
 * for the weapon's whole stated range -- patternFitsSmallestTarget is what
 * picks this number now. Still a spread rather than a slug: six pixels where
 * the pellets expire is a person's width, so dead centre takes all three and
 * a pixel off takes two.
 */
constexpr WeaponSpec kWeapons[static_cast<std::uint8_t>(WeaponId::Count)] = {
    // name      damage  rate  range  magazine       speed  tracer  pel spread
    {  "PISTOL",     50,   14,    96, kPistolMagazine,  1536,     4,   1,     0 },
    {  "POLICE",     20,   30,    96, kInfiniteAmmo,  1536,      4,   1,     0 },
    {  "SHOTGUN",    34,   45,    56, kShotgunMagazine, 1024,     3,   3,    48 },
};

}  // namespace

const WeaponSpec& spec(WeaponId id) {
    const std::uint8_t index = static_cast<std::uint8_t>(id);
    // Clamped rather than asserted: this is called from the draw path on a
    // device with no console to print an assertion to, and a wrong gun is a
    // better failure than a wrong pointer.
    return kWeapons[index < static_cast<std::uint8_t>(WeaponId::Count)
                        ? index : 0];
}

std::int32_t pelletOffsetSub(const WeaponSpec& s, std::uint8_t index) {
    if (s.pellets <= 1 || index >= s.pellets) {
        return 0;
    }
    // Doubled and halved rather than divided: with an even count there is no
    // centre pellet, and integer division would put two of them on the same
    // line instead of straddling it.
    const std::int32_t doubled =
        2 * static_cast<std::int32_t>(index) - (s.pellets - 1);
    return doubled * static_cast<std::int32_t>(s.spreadSub) / 2;
}

std::uint16_t lifetimeSteps(const WeaponSpec& s) {
    if (s.projectileSpeedSub == 0) {
        return 0;
    }
    const std::uint32_t rangeSub = static_cast<std::uint32_t>(s.rangePx) << 8;
    const std::uint32_t speed    = s.projectileSpeedSub;
    // Ceiling division: see the header. The +speed-1 cannot overflow because
    // rangePx is 16 bits and the shift leaves 8 bits of headroom in 32.
    return static_cast<std::uint16_t>((rangeSub + speed - 1) / speed);
}

State load(const WeaponSpec& s) {
    return State{s.magazine, 0};
}

bool isEmpty(const State& st) {
    return st.ammo == 0;
}

bool canFire(const State& st) {
    return st.cooldownSteps == 0 && !isEmpty(st);
}

void onFired(State& st, const WeaponSpec& s) {
    st.cooldownSteps = s.fireRateSteps;
    if (s.magazine != kInfiniteAmmo && st.ammo > 0) {
        --st.ammo;
    }
}

void tick(State& st) {
    if (st.cooldownSteps > 0) {
        --st.cooldownSteps;
    }
}

bool hitsEveryTargetOnItsPath(const WeaponSpec& s) {
    // The spread is added to the forward speed rather than combined with it: a
    // pellet's real step is the hypotenuse, shorter than the sum, so this
    // over-counts -- the only safe direction for a check whose failure is a
    // bullet passing through somebody.
    const std::uint32_t worstSub =
        static_cast<std::uint32_t>(s.projectileSpeedSub) + s.spreadSub;
    const int stepPx = static_cast<int>((worstSub + 255) / 256);
    return stepPx <= kProjectileBoxPx + kSmallestTargetPx - 1;
}

int patternHalfWidthPx(const WeaponSpec& s) {
    if (s.pellets <= 1 || s.spreadSub == 0) {
        return 0;
    }
    // The last pellet is the furthest out by construction: pelletOffsetSub is
    // symmetric and monotonic, so asking it beats re-deriving the arithmetic
    // here and getting a different answer from the code that flies them.
    const std::int32_t widest =
        pelletOffsetSub(s, static_cast<std::uint8_t>(s.pellets - 1));
    const std::int32_t sideways = widest < 0 ? -widest : widest;
    const std::int32_t total = sideways
                             * static_cast<std::int32_t>(lifetimeSteps(s));
    return static_cast<int>((total + 255) / 256);   // round up; see the header
}

bool patternFitsSmallestTarget(const WeaponSpec& s) {
    return patternHalfWidthPx(s)
           <= (kProjectileBoxPx + kSmallestTargetPx) / 2 - 1;
}

Aim aimOf(Facing facing) {
    switch (facing) {
        case Facing::Up:    return Aim{ 0, -1};
        case Facing::Left:  return Aim{-1,  0};
        case Facing::Right: return Aim{ 1,  0};
        case Facing::Down:  break;
    }
    return Aim{0, 1};
}

}  // namespace top_down_city::weapons
