#pragma once
#include <cstdint>

#include <graphics/Renderer.h>

#include "game/CityConstants.h"
#include "game/rules/Weapon.h"

namespace top_down_city {

/**
 * @brief The gun lying in the street, and whether it has been taken.
 *
 * The player starts unarmed, which is the point: a weapon handed over on the
 * first frame is a control, and one you walk to is a place. The generator lays
 * out ONE pistol, within sight of the spawn, and it is the only free weapon in
 * the demo.
 *
 * A pickup is an actor, not a tile -- nothing is written to a layer. The
 * positions come out of the generator as `WEAPON_PICKUPS`, they are drawn
 * beneath everything that walks, and the whole state is one bit each. There is
 * no pick-up button and no room for one: the pad has six and every one is
 * spoken for, so walking over it is the interaction.
 *
 * **A taken pickup does NOT come back**, and that is the change the economy is
 * built on. It used to: three guns lay around the island, each returning after
 * a four-block walk, because an emptied shotgun left the player unarmed and the
 * street had to be the way back to a weapon. The counter is that way back now,
 * and a street that also handed out free guns on a timer would be a shop with
 * nothing to sell -- every price in game/rules/Economy.h optional, and the
 * courier run back to being a score. So the free pistol is a seed, not a
 * supply; what refills it is `shop::Line::Pistol`, and what pays for that is a
 * delivery.
 */
class WeaponPickups {
public:
    WeaponPickups();

    /// Put every gun back on the ground. Call from the scene's init(), which
    /// runs once a RUN -- this is not a respawn, it is the start of one.
    void reset();

    /**
     * @brief Collect whatever the player is standing on; `weapon` is what was
     *        taken, valid only when this returns true.
     *
     * Tested against the player's collision box rather than their sprite cell:
     * a 16x16 cell overlaps a pickup a tile away, and a gun that jumps into
     * your hand from across the pavement reads as a bug.
     */
    bool collect(int boxLeft, int boxTop, int boxWidth, int boxHeight,
                 weapons::WeaponId& weapon);

    /// The ones still on the ground, in world coordinates. Drawn before the
    /// actors, so somebody standing on a pistol covers it.
    void draw(pixelroot32::graphics::Renderer& renderer,
              int cameraX, int cameraY);

    /// Fingerprint for the frame skip. It changes exactly once per pickup, on
    /// the step it is taken -- a frame that must not be skipped, because the
    /// sprite has to come off the ground. Once every bit is set it never
    /// changes again: a permanent state rather than a countdown.
    std::uint32_t visualKey() const { return takenMask_; }

private:
    static_assert(scene::NUM_WEAPON_PICKUPS <= 32,
                  "the taken set is a 32-bit mask");

    /// One bit per pickup: set means taken, and taken is forever. A mask
    /// rather than an array of bools because it doubles as the visual key for
    /// free -- and with the respawn gone it is the whole of the state.
    std::uint32_t takenMask_;
};

}  // namespace top_down_city
