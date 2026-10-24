#pragma once
#include "platforms/EngineConfig.h"
#include <cstdint>

namespace spaceinvaders {

/**
 * @file GameConstants.h
 * @brief Centralized game configuration for Space Invaders.
 *
 * Defines layout, dimensions, timing, and input mapping.
 * Collision layers: 1=Player, 2=Enemy, 3=PlayerProjectile, 4=EnemyProjectile, 5=Bunker.
 */

    /** Input button IDs (platform-specific mapping) */
    constexpr uint8_t BTN_LEFT = 2;
    constexpr uint8_t BTN_RIGHT = 3;
    constexpr uint8_t BTN_FIRE = 4;

    /**
     * Screen model (S1). The playfield is 224 px wide starting at x=8 and runs
     * the full screen height; every sprite is drawn 1:1, so there is no scale
     * factor left to derive sizes from.
     */
    constexpr int FIELD_X = 8;
    constexpr int FIELD_W = 224;
    constexpr int FIELD_H = 240;
    constexpr int HUD_HEIGHT = 16;

    /** Vertical band origins, in screen pixels (S1) */
    constexpr float SAUCER_Y = 24.0f;
    constexpr float RACK_TOP_Y = 48.0f;
    constexpr float SHIELD_TOP_Y = 184.0f;
    constexpr float CANNON_TOP_Y = 208.0f;   ///< Top of the 8 px cannon; baseline is 216
    constexpr float RESERVE_Y = 224.0f;

    /**
     * Mystery saucer mask size (S11). The actor's body and the entry/exit
     * bounds both need it, and the asset is already 16x7, so the numbers live
     * here instead of being repeated as literals in the movement code.
     */
    constexpr int SAUCER_WIDTH = 16;
    constexpr int SAUCER_HEIGHT = 7;

    /** Player ship sprite dimensions (raw pixels) */
    constexpr int PLAYER_SPRITE_W = 13;
    constexpr int PLAYER_SPRITE_H = 8;
    /** Player collision dimensions (raw pixels at 1:1) */
    constexpr int PLAYER_WIDTH = PLAYER_SPRITE_W;
    constexpr int PLAYER_HEIGHT = PLAYER_SPRITE_H;

    /** Spawn centred in the playfield, resting on the cannon baseline */
    constexpr float PLAYER_START_X = FIELD_X + (FIELD_W - PLAYER_WIDTH) * 0.5f;
    constexpr float PLAYER_START_Y = CANNON_TOP_Y;

    /**
     * Projectile collision dimensions (1 px wide, 4 px tall). The enemy bolt is
     * its own 3x7 mask (S9) and carries no velocity at all: the scene steps it.
     */
    constexpr int PROJECTILE_WIDTH = 1;
    constexpr int PROJECTILE_HEIGHT = 4;

    /**
     * Enemy shot mask and collision box (S9). It is a 3x7 zigzag with two
     * frames, not the player's 1x4 bar, so it cannot share the numbers above.
     */
    constexpr int ENEMY_SHOT_WIDTH = 3;
    constexpr int ENEMY_SHOT_HEIGHT = 7;

    /**
     * Seed for the run's enemy-fire generator (S20). A run reseeds to this
     * value, so the same input script always draws the same shooter columns
     * instead of following whatever std::rand() happened to hold.
     */
    constexpr uint32_t RUN_SEED = 0x1978u;

    /**
     * Alien mask dimensions used by the sprite descriptors in
     * assets/AlienSprites.h. The rules layer owns the formation geometry, the
     * hit box and the score; these stay because a Sprite descriptor is a
     * compile-time aggregate that cannot call rules::alienWidth().
     */
    constexpr int ALIEN_SQUID_SPRITE_W = 8;
    constexpr int ALIEN_SQUID_SPRITE_H = 8;
    constexpr int ALIEN_CRAB_SPRITE_W = 11;
    constexpr int ALIEN_CRAB_SPRITE_H = 8;
    constexpr int ALIEN_OCTOPUS_SPRITE_W = 12;
    constexpr int ALIEN_OCTOPUS_SPRITE_H = 8;

    /** Music tempo scaling based on alien proximity (threat factor) */
    constexpr float Y_RANGE = 160.0f;              ///< Fixed tempo range, not derived from the layout
    constexpr float INV_Y_RANGE = 1.0f / Y_RANGE;  ///< Reciprocal, precomputed for the per-tick scale

}
