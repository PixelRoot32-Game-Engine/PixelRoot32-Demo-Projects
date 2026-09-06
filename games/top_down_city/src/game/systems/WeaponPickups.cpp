#include "game/systems/WeaponPickups.h"

namespace top_down_city {

namespace gfx = pixelroot32::graphics;

namespace {

bool boxesOverlap(int aL, int aT, int aW, int aH,
                  int bL, int bT, int bW, int bH) {
    return aL < bL + bW && bL < aL + aW
        && aT < bT + bH && bT < aT + aH;
}

}  // namespace

WeaponPickups::WeaponPickups()
    : takenMask_(0) {
}

void WeaponPickups::reset() {
    takenMask_ = 0;
}

bool WeaponPickups::collect(int boxLeft, int boxTop, int boxWidth,
                            int boxHeight, weapons::WeaponId& weapon) {
    for (std::uint8_t i = 0; i < scene::NUM_WEAPON_PICKUPS; ++i) {
        const std::uint32_t bit = 1u << i;
        if (takenMask_ & bit) {
            continue;
        }
        const scene::WeaponPickup& p = scene::WEAPON_PICKUPS[i];
        // The pickup's own footprint is the middle of its tile rather than
        // the whole 16 px cell. The sprite is drawn small and centred, so a
        // full-cell test would collect it from a step away -- close enough to
        // look like the gun jumped.
        const int left = p.tileX * kTilePx + (kTilePx - kPickupBoxPx) / 2;
        const int top  = p.tileY * kTilePx + (kTilePx - kPickupBoxPx) / 2;
        if (!boxesOverlap(boxLeft, boxTop, boxWidth, boxHeight,
                          left, top, kPickupBoxPx, kPickupBoxPx)) {
            continue;
        }
        takenMask_ |= bit;
        weapon = static_cast<weapons::WeaponId>(p.weapon);
        return true;
    }
    return false;
}

void WeaponPickups::draw(gfx::Renderer& renderer, int cameraX, int cameraY) {
    for (std::uint8_t i = 0; i < scene::NUM_WEAPON_PICKUPS; ++i) {
        if (takenMask_ & (1u << i)) {
            continue;
        }
        const scene::WeaponPickup& p = scene::WEAPON_PICKUPS[i];
        const int px = p.tileX * kTilePx;
        const int py = p.tileY * kTilePx;
        // Culled against the viewport like every other actor. The one gun
        // scene::NUM_WEAPON_PICKUPS now holds would be affordable undrawn;
        // the three that used to lie around the island would have been three
        // sprites a frame for the renderer to clip for the whole run, and the
        // loop is written for whatever the table turns out to hold.
        if (px + kPlayerSpriteW <= cameraX || px >= cameraX + DISPLAY_WIDTH
            || py + kPlayerSpriteH <= cameraY || py >= cameraY + DISPLAY_HEIGHT) {
            continue;
        }
        // Slot 7, the player's: the pickup is drawn from the player palette
        // and so follows the day/night tint. A gun lying in the street is
        // part of the city, unlike the tracer that leaves it.
        // Indexed by the id the generator wrote, so a shotgun on the ground
        // looks like a shotgun. Clamped rather than trusted: the table is
        // flash and so is the id, but a row edited out from under the other
        // would be a read past the end on a device with nothing to catch it.
        const std::uint8_t kind =
            p.weapon < static_cast<std::uint8_t>(weapons::WeaponId::Count)
                ? p.weapon : 0;
        renderer.drawSprite(kWeaponPickups[kind], px, py,
                            kPlayerPaletteSlot, false);
    }
}

}  // namespace top_down_city
