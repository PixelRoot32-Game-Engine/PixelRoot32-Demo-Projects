#pragma once
#include "platforms/EngineConfig.h"
#include "rules/RulesConstants.h"
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
     * Screen model (S1). The playfield is 224 px wide and runs the full screen
     * height; every sprite is drawn 1:1, so there is no scale factor left to
     * derive sizes from.
     *
     * The build picks the origin, because the field is centred on the logical
     * canvas: 8 px on the 240 px square panel, 48 px on the 320 px landscape
     * CYD. Every playfield-derived position -- rack, shields, HUD, reserve
     * ships, respawn column, saucer bounds -- reads FIELD_X, so the whole
     * layout moves with this one value and nothing else has to know.
     */
#ifndef SPACE_INVADERS_FIELD_X
#define SPACE_INVADERS_FIELD_X 8
#endif
    constexpr int FIELD_X = SPACE_INVADERS_FIELD_X;
    constexpr int FIELD_W = 224;
    constexpr int FIELD_H = 240;
    constexpr int HUD_HEIGHT = 16;

    /**
     * @brief The logical canvas, measured after the engine's rotation.
     *
     * DisplayConfig swaps the logical width and height for a quarter turn, so
     * LOGICAL_WIDTH/LOGICAL_HEIGHT describe the panel as mounted, not the
     * framebuffer the scene draws into: the 240x320 CYD at DISPLAY_ROTATION=3
     * renders into 320x240. Rotation also arrives as 0..3 or as degrees, hence
     * the normalisation to a turn count.
     */
    constexpr int QUARTER_TURNS =
        (DISPLAY_ROTATION >= 4 ? DISPLAY_ROTATION / 90 : DISPLAY_ROTATION) % 4;
    constexpr int CANVAS_WIDTH = (QUARTER_TURNS % 2 == 0) ? LOGICAL_WIDTH : LOGICAL_HEIGHT;
    constexpr int CANVAS_HEIGHT = (QUARTER_TURNS % 2 == 0) ? LOGICAL_HEIGHT : LOGICAL_WIDTH;

    // A field that does not fit would draw off-panel instead of failing loudly,
    // so the two numbers that place it are checked against the real canvas.
    static_assert(FIELD_X + FIELD_W <= CANVAS_WIDTH,
                  "Space Invaders: the 224 px playfield must fit the logical canvas width");
    static_assert(FIELD_H <= CANVAS_HEIGHT,
                  "Space Invaders: the 240 px playfield must fit the logical canvas height");

    // The shield row is playfield geometry too (the scene draws it at FIELD_X +
    // ShieldX), so it has to fit the same field as the rack. A layout change
    // that pushed the last shield past the field edge fails here instead of
    // drawing a shield half outside the playfield.
    static_assert(rules::ShieldX[0] >= 0 &&
                      rules::ShieldX[rules::ShieldCount - 1] + rules::ShieldWidth <= FIELD_W,
                  "Space Invaders: the four shields must fit inside the playfield");

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

}
