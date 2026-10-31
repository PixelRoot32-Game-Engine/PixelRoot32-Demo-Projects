/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Sector lighting: the four light thinkers and the EV functions that start them.
 *
 * The reference is linuxdoom-1.10 p_lights.c: T_LightFlash, T_StrobeFlash,
 * T_Glow, T_FireFlicker, their P_Spawn* functions, EV_StartLightStrobing,
 * EV_TurnTagLightsOff and EV_LightTurnOn.
 *
 * ONE RECORD FOR FOUR THINKERS
 *
 * Upstream declares four small structs whose fields overlap almost completely.
 * This port uses one p_light_thinker_t with a `type` tag; the shared record is
 * what lets the thinker list stay one kind (P_THINKER_LIGHTFLASH) while the dump
 * can still name the state machine that owns the sector.
 *
 * THE RULES THAT ARE EASY TO GET WRONG
 *
 *   - A flash alternates between `maxlight` and `minlight`; the count is
 *     `(P_Random() & maxtime) + 1` on the way down and
 *     `(P_Random() & mintime) + 1` on the way up, so the period is random in
 *     both phases.
 *   - A strobe's count comes from its two fixed times (brighttime, darktime)
 *     and from one draw only at spawn (unless `inSync`, which starts at 1).
 *     `P_SpawnStrobeFlash` also clamps `minlight` to 0 when the surrounding
 *     minimum equals the sector's own level, so a strobe in an enclosed room
 *     still flashes.
 *   - `T_Glow` steps by GLOWSPEED and bounces by undoing the last step, so the
 *     level never lands outside [minlight, maxlight].
 *   - `T_FireFlicker`'s count is always 4 after a change, and its new level is
 *     `maxlight - (P_Random()&3)*16` clamped up to `minlight`.
 *   - The P_Spawn* functions clear `sector->special` (they own the sector now).
 *     `EV_StartLightStrobing` does not set `specialdata`: lights are not movers,
 *     which is upstream's behaviour and means a light can sit beside a mover.
 *
 * This is C99 only: no allocation, no globals, no stdio, no floating point.
 */
#ifndef DOOM_P_LIGHTS_H
#define DOOM_P_LIGHTS_H

#include <stdint.h>

#include "p_spec.h"
#include "p_tick.h"

#ifdef __cplusplus
extern "C" {
#endif

struct p_mobj_context;

/* Which state machine owns a p_light_thinker_t. Not a p_thinker_kind_t: every
 * light thinker is kind P_THINKER_LIGHTFLASH and this field names the machine. */
typedef enum p_light_type {
    P_LIGHT_FLASH = 0,
    P_LIGHT_STROBE = 1,
    P_LIGHT_GLOW = 2,
    P_LIGHT_FIRE_FLICKER = 3
} p_light_type_t;

/* The union of upstream's four light records. Fields a type does not use are
 * zero, so a dump can print all of them without reading uninitialised memory. */
typedef struct p_light_thinker {
    p_thinker_t thinker;
    uint16_t sector;
    int16_t type;
    int16_t direction;
    int32_t count;
    int32_t maxLight;
    int32_t minLight;
    int32_t maxTime;
    int32_t minTime;
    int32_t darkTime;
    int32_t brightTime;
} p_light_thinker_t;

/** P_SpawnLightFlash(): sector special 1. */
int p_spawn_light_flash(struct p_mobj_context* world, uint16_t sector);

/** P_SpawnStrobeFlash(): sector specials 2, 3, 4, 12 and 13. `inSync` starts the
 *  count at 1 so several sectors flash together. */
int p_spawn_strobe_flash(struct p_mobj_context* world, uint16_t sector,
                         int fastOrSlow, int inSync);

/** P_SpawnGlowingLight(): sector special 8. */
int p_spawn_glowing_light(struct p_mobj_context* world, uint16_t sector);

/** P_SpawnFireFlicker(): sector special 17. */
int p_spawn_fire_flicker(struct p_mobj_context* world, uint16_t sector);

/** EV_StartLightStrobing(): line specials 17 and its retriggers. */
int p_start_light_strobing(struct p_mobj_context* world, uint16_t lineIndex);

/**
 * @brief EV_LightTurnOn(): sets every sector with the line's tag to @p bright.
 *
 * `bright == 0` means "the brightest neighbour", and upstream accumulates that
 * into the same `bright` variable across sectors: the first tagged sector scans,
 * later ones reuse the accumulated value if it became nonzero. That is
 * observable and reproduced.
 */
int p_light_turn_on(struct p_mobj_context* world, uint16_t lineIndex, int bright);

/** EV_TurnTagLightsOff(): every sector with the line's tag takes the dimmest
 *  neighbouring light level. */
int p_turn_tag_lights_off(struct p_mobj_context* world, uint16_t lineIndex);

/** The entry point for all four light records. */
void p_light_thinker(p_thinker_t* thinker, void* context);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_P_LIGHTS_H */
