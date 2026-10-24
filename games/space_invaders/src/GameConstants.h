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

    /** Player ship sprite dimensions (raw pixels) */
    constexpr int PLAYER_SPRITE_W = 13;
    constexpr int PLAYER_SPRITE_H = 8;
    /** Player collision dimensions (raw pixels at 1:1) */
    constexpr int PLAYER_WIDTH = PLAYER_SPRITE_W;
    constexpr int PLAYER_HEIGHT = PLAYER_SPRITE_H;

    /** Spawn centred in the playfield, resting on the cannon baseline */
    constexpr float PLAYER_START_X = FIELD_X + (FIELD_W - PLAYER_WIDTH) * 0.5f;
    constexpr float PLAYER_START_Y = CANNON_TOP_Y;

    /** Projectile collision dimensions (1 px wide, 4 px tall) */
    constexpr int PROJECTILE_WIDTH = 1;
    constexpr int PROJECTILE_HEIGHT = 4;
    /**
     * Enemy-shot speed in pixels per second. The player shot no longer uses it:
     * it steps rules::PlayerShotStepY per tick (S8). Kept until P4 replaces the
     * enemy cadence.
     */
    constexpr float PROJECTILE_SPEED = 80.0f;

    /** Bunker layout and dimensions */
    constexpr int BUNKER_COUNT = 4;
    constexpr int BUNKER_WIDTH = 24;
    constexpr int BUNKER_HEIGHT = 16;
    /** Bottom line the scene subtracts BUNKER_HEIGHT from, so the shields start at SHIELD_TOP_Y */
    constexpr float BUNKER_Y = SHIELD_TOP_Y + BUNKER_HEIGHT;

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

    /** Legacy march timer values; the rack owns movement now (T3.5 removes these) */
    constexpr unsigned long BASE_STEP_DELAY = 417;  ///< 72 BPM (eighth note)
    constexpr unsigned long INITIAL_STEP_DELAY = 380;
    constexpr unsigned long MIN_STEP_DELAY = 150;

    /** Music tempo scaling based on alien proximity (threat factor) */
    constexpr float GAME_OVER_Y = 200.0f;
    constexpr float Y_RANGE = 160.0f;              ///< Fixed tempo range, not derived from the layout
    constexpr float INV_Y_RANGE = 0.00625f;        ///< 1.0 / Y_RANGE

}
