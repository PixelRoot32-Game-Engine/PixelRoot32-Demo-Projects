/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Upstream's sounds.h: the two id enums and the two tables' row types.
 *
 * The numbering is data, not naming. Every call site in this port kept the id
 * it had in 1997 (`S_StartSound (mo, sfx_pistol)` is id 1, `sfx_radio` is 108),
 * and the SFX bank in src/doomc/ is indexed by the same numbers, so a single
 * inserted or reordered enumerator would silently turn a door into a pistol
 * shot. Both enums are therefore copied from linuxdoom-1.10 verbatim, values
 * and order included, and nothing here may be sorted or tidied.
 *
 * THE ONE STRUCTURAL DIFFERENCE: THE TABLES ARE CONST
 *
 * Upstream's S_sfx[] is mutable because a DMX mixer caches each sample in it
 * (`lumpnum`, `data`, `usefulness`). This port has no sample channel at all
 * (pure APU synthesis), so nothing ever writes those three fields, and the
 * whole table is `const` and lives in flash -- 3,924 B of `.rodata` instead of
 * 3,924 B of DRAM. The board has ~1.9 KB of `dram0_0_seg` free, so a mutable
 * copy of this table is not an option; keeping the fields (always 0) is what
 * lets a reader coming from the original recognise the record.
 *
 * `link` still points into the table (sfx_chgun -> sfx_pistol) and the two link
 * fields (`pitch`, `volume`) are upstream's: the port's channel layer applies
 * both, exactly as S_StartSoundAtVolume does.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point.
 */
#ifndef DOOM_SOUNDS_H
#define DOOM_SOUNDS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The sound effect record. Field order is upstream's, so the initialiser in
 * sounds.c reads as the 1993 table does:
 *
 *   name         up to six characters, the `DS`-less lump suffix ("pistol")
 *   singularity  only one may sound at a time (the APU layer's business, kept
 *                as data because upstream's table carries it)
 *   priority     lower is more important; S_getChannel replaces the first
 *                channel whose priority is >= the new sound's
 *   link         a sound that reuses another sound's sample; NULL otherwise
 *   pitch        a link's pitch (128 is normal)
 *   volume       a link's volume delta (0 for the one shipped link)
 *   data         the cached sample: always NULL here (no sample channel)
 *   usefulness   the cache's reference count: always 0 here
 *   lumpnum      the cached lump index: always 0 here
 */
typedef struct sfxinfo_struct sfxinfo_t;

struct sfxinfo_struct {
    const char* name;
    int32_t singularity;
    int32_t priority;
    const sfxinfo_t* link;
    int32_t pitch;
    int32_t volume;
    const void* data;
    int32_t usefulness;
    int32_t lumpnum;
};

/*
 * The music record. Same story as sfxinfo_t: `lumpnum`, `data` and `handle`
 * exist so the row has upstream's shape, and none of them is written here --
 * The music is converted from MUS offline (tools/mus2tracks.py) and played by id
 * through doom_music_start().
 */
typedef struct musicinfo {
    const char* name;
    int32_t lumpnum;
    const void* data;
    int32_t handle;
} musicinfo_t;

/* The complete set of sound effects, in upstream's order. Index 0 is the
 * dummy row and every other index is one sfxenum_t value. */
extern const sfxinfo_t S_sfx[];

/* The complete set of music, in upstream's order. */
extern const musicinfo_t S_music[];

/*
 * Identifiers for all music in game.
 *
 * Copied from linuxdoom-1.10's musicenum_t. NUMMUSIC is the count, so the last
 * valid id is NUMMUSIC - 1.
 */
typedef enum {
    mus_None,
    mus_e1m1,
    mus_e1m2,
    mus_e1m3,
    mus_e1m4,
    mus_e1m5,
    mus_e1m6,
    mus_e1m7,
    mus_e1m8,
    mus_e1m9,
    mus_e2m1,
    mus_e2m2,
    mus_e2m3,
    mus_e2m4,
    mus_e2m5,
    mus_e2m6,
    mus_e2m7,
    mus_e2m8,
    mus_e2m9,
    mus_e3m1,
    mus_e3m2,
    mus_e3m3,
    mus_e3m4,
    mus_e3m5,
    mus_e3m6,
    mus_e3m7,
    mus_e3m8,
    mus_e3m9,
    mus_inter,
    mus_intro,
    mus_bunny,
    mus_victor,
    mus_introa,
    mus_runnin,
    mus_stalks,
    mus_countd,
    mus_betwee,
    mus_doom,
    mus_the_da,
    mus_shawn,
    mus_ddtblu,
    mus_in_cit,
    mus_dead,
    mus_stlks2,
    mus_theda2,
    mus_doom2,
    mus_ddtbl2,
    mus_runni2,
    mus_dead2,
    mus_stlks3,
    mus_romero,
    mus_shawn2,
    mus_messag,
    mus_count2,
    mus_ddtbl3,
    mus_ampie,
    mus_theda3,
    mus_adrian,
    mus_messg2,
    mus_romer2,
    mus_tense,
    mus_shawn3,
    mus_openin,
    mus_evil,
    mus_ultima,
    mus_read_m,
    mus_dm2ttl,
    mus_dm2int,
    NUMMUSIC
} musicenum_t;

/*
 * Identifiers for all sfx in game.
 *
 * Copied from linuxdoom-1.10's sfxenum_t. NUMSFX is the count (109), so the
 * valid ids are 1..NUMSFX-1 and 0 is the silent row. Do not reorder.
 */
typedef enum {
    sfx_None,
    sfx_pistol,
    sfx_shotgn,
    sfx_sgcock,
    sfx_dshtgn,
    sfx_dbopn,
    sfx_dbcls,
    sfx_dbload,
    sfx_plasma,
    sfx_bfg,
    sfx_sawup,
    sfx_sawidl,
    sfx_sawful,
    sfx_sawhit,
    sfx_rlaunc,
    sfx_rxplod,
    sfx_firsht,
    sfx_firxpl,
    sfx_pstart,
    sfx_pstop,
    sfx_doropn,
    sfx_dorcls,
    sfx_stnmov,
    sfx_swtchn,
    sfx_swtchx,
    sfx_plpain,
    sfx_dmpain,
    sfx_popain,
    sfx_vipain,
    sfx_mnpain,
    sfx_pepain,
    sfx_slop,
    sfx_itemup,
    sfx_wpnup,
    sfx_oof,
    sfx_telept,
    sfx_posit1,
    sfx_posit2,
    sfx_posit3,
    sfx_bgsit1,
    sfx_bgsit2,
    sfx_sgtsit,
    sfx_cacsit,
    sfx_brssit,
    sfx_cybsit,
    sfx_spisit,
    sfx_bspsit,
    sfx_kntsit,
    sfx_vilsit,
    sfx_mansit,
    sfx_pesit,
    sfx_sklatk,
    sfx_sgtatk,
    sfx_skepch,
    sfx_vilatk,
    sfx_claw,
    sfx_skeswg,
    sfx_pldeth,
    sfx_pdiehi,
    sfx_podth1,
    sfx_podth2,
    sfx_podth3,
    sfx_bgdth1,
    sfx_bgdth2,
    sfx_sgtdth,
    sfx_cacdth,
    sfx_skldth,
    sfx_brsdth,
    sfx_cybdth,
    sfx_spidth,
    sfx_bspdth,
    sfx_vildth,
    sfx_kntdth,
    sfx_pedth,
    sfx_skedth,
    sfx_posact,
    sfx_bgact,
    sfx_dmact,
    sfx_bspact,
    sfx_bspwlk,
    sfx_vilact,
    sfx_noway,
    sfx_barexp,
    sfx_punch,
    sfx_hoof,
    sfx_metal,
    sfx_chgun,
    sfx_tink,
    sfx_bdopn,
    sfx_bdcls,
    sfx_itmbk,
    sfx_flame,
    sfx_flamst,
    sfx_getpow,
    sfx_bospit,
    sfx_boscub,
    sfx_bossit,
    sfx_bospn,
    sfx_bosdth,
    sfx_manatk,
    sfx_mandth,
    sfx_sssit,
    sfx_ssdth,
    sfx_keenpn,
    sfx_keendt,
    sfx_skeact,
    sfx_skesit,
    sfx_skeatk,
    sfx_radio,
    NUMSFX
} sfxenum_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_SOUNDS_H */
