/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * DOOM's channel policy, ported statement for statement from linuxdoom-1.10's
 * s_sound.c.
 *
 * WHAT IS UPSTREAM'S HERE, AND WHY IT IS KEPT
 *
 *   - one sound per origin: S_StopSound() runs before a new sound is given a
 *     channel, and S_getChannel() also reclaims a channel whose origin matches;
 *   - the eight-channel table, and the priority steal: the first channel whose
 *     `S_sfx[].priority` is >= the newcomer's is the victim, and when there is
 *     no such channel the sound is refused. In `S_sfx[]` a *lower* number is
 *     more important (`sfx_pldeth` 32, `sfx_sawidl` 118), which is the
 *     direction the 1993 "Look for lower priority" comment assumes;
 *   - the `link` arm (the chaingun reuses the pistol at pitch 150, volume +0);
 *   - S_AdjustSoundParams(): the approximated distance, the 1200-unit clipping
 *     distance, the 160-unit full-volume radius, the linear attenuation
 *     between them, the map-8 full-volume-to-15 ladder, and the stereo
 *     separation term;
 *   - the two pitch jitters and their clamps, drawn from M_Random in exactly
 *     the places upstream draws them (a culled sound draws nothing);
 *   - S_Start()'s music number arithmetic and the episode-4 `spmus[]` table.
 *
 * WHAT IS DIFFERENT, AND WHY
 *
 *   1. No allocation, no zone, no sample cache. The channels are one static
 *      array of S_NUMCHANNELS and the S_sfx[] rows are const (sounds.h).
 *      Upstream's `usefulness` bookkeeping is absent because nothing is ever
 *      cached or purged.
 *   2. `players[consoleplayer].mo`, `gamemap` and `gameepisode` are not
 *      globals here: S_SetListener() and S_SetGameLevel() are how the game
 *      hands them over (s_sound.h says why).
 *   3. A sector-originated sound carries an S_SectorOrigin() token instead of
 *      `&sector->soundorg`, because p_sector_t has no such field. The token
 *      keeps identity (one door sound per sector, S_StopSound by sector) but
 *      has no coordinates, so such a cue is non-local: full volume, no
 *      distance culling.
 *   4. I_Error() has no equivalent: a bad sfx id is silence, and a volume out
 *      of range is clamped.
 *
 * This is C99 only: <stdint.h>/<stdbool.h>, no allocation, no stdio, no
 * floating point. The only file-scope state is the channel table and the
 * volumes, which is what this interface is about.
 */
#include "s_sound.h"

#include <stddef.h>

#include "m_fixed.h"
#include "m_random.h"
#include "p_map.h"
#include "r_tables.h"

/* Upstream's constants, verbatim. */
#define S_MAX_VOLUME_LOCAL 127

/* When to clip out sounds. Does not fit the large outdoor areas. */
#define S_CLIPPING_DIST (1200 * 0x10000)

/* Distance to origin when sounds should be maxed out. This should relate to
 * movement clipping resolution (see BLOCKMAP handling). Originally:
 * (200*0x10000). */
#define S_CLOSE_DIST (160 * 0x10000)

#define S_ATTENUATOR ((S_CLIPPING_DIST - S_CLOSE_DIST) >> FRACBITS)

#define NORM_VOLUME (snd_SfxVolume)

#define NORM_PITCH 128
#define NORM_PRIORITY 64
#define NORM_SEP 128

/* Defined by upstream and unused there too: the two jitters below are the
 * literal `8 - (M_Random()&15)` and `16 - (M_Random()&31)`. Kept so a reader
 * looking for the name finds it and the next person does not "restore" a draw
 * that would shift the sequence. */
#define S_PITCH_PERTURB 1

#define S_STEREO_SWING (96 * 0x10000)

/* Percent attenuation from front to back: unused without a stereo mixer, kept
 * as upstream keeps it. */
#define S_IFRACVOL 30

/* The port's sector-origin token range. s_sound.h documents the choice: a real
 * pointer on this target (DRAM >= 0x3FFB0000, flash >= 0x40000000, the host
 * heap far above 0x10000) never lands in [0x10000, 0x20000). */
#define S_SECTOR_ORIGIN_BASE 0x10000u
#define S_SECTOR_ORIGIN_LIMIT (S_SECTOR_ORIGIN_BASE + 0x10000u)

typedef struct {
    /* sound information (if null, channel avail.) */
    const sfxinfo_t* sfxinfo;

    /* origin of sound; may be an S_SectorOrigin() token */
    const r_mobj_t* origin;

    /* handle of the sound being played; 0 when none */
    int32_t handle;
} channel_t;

/* The set of channels available. Static, unlike upstream's zone block: the
 * count is a compile-time 8 and the board's DRAM cannot spare an allocation
 * for something this small. */
static channel_t channels[S_NUMCHANNELS];

/* These are not used by this port yet (T49 wires the menu's setting), but they
 * are the two volumes every calculation below reads, exactly as upstream. */
int snd_SfxVolume = 15;
int snd_MusicVolume = 15;

/* Whether songs are paused. */
static bool mus_paused;

/* Music currently being played. */
static const musicinfo_t* mus_playing;

/* The number of channels available, upstream's M_misc default. */
int numChannels = S_NUMCHANNELS;

/* The port's substitutes for upstream's global player and level. */
static const r_mobj_t* s_listener;
static int s_gameEpisode = 1;
static int s_gameMap = 1;

/* Upstream's rndindex: M_Random is the same table with an index that is not
 * P_Random's, so a jitter never moves the simulation's sequence. */
static m_random_t s_effectRandom;

/* True when @p origin is an S_SectorOrigin() token rather than a thing. */
static int origin_is_sector_token(const r_mobj_t* origin) {
    const uintptr_t value = (uintptr_t)origin;

    return value >= (uintptr_t)S_SECTOR_ORIGIN_BASE
        && value < (uintptr_t)S_SECTOR_ORIGIN_LIMIT;
}

/* Upstream's `abs` on a 16.16 difference. INT32_MIN is unreachable from map
 * coordinates (the level's own extents are far inside the world's +-32,767
 * units); the conditional keeps the function total anyway. */
static fixed_t sound_abs(fixed_t value) {
    return value < 0 ? -value : value;
}

/* --- Initialisation ------------------------------------------------------ */

void S_Init(int sfxVolume, int musicVolume) {
    int i;

    S_SetSfxVolume(sfxVolume);
    S_SetMusicVolume(musicVolume);

    /* Upstream allocates `numChannels` channels from the zone here. The table
     * is static, so this only frees every slot. */
    numChannels = S_NUMCHANNELS;
    for (i = 0; i < numChannels; ++i) {
        channels[i].sfxinfo = NULL;
        channels[i].origin = NULL;
        channels[i].handle = 0;
    }

    /* no sounds are playing, and they are not mus_paused */
    mus_paused = false;
    mus_playing = NULL;

    /* Upstream marks every S_sfx[] entry uncached here. There is no cache, so
     * the equivalent is the jitter's own sequence: a documented, deterministic
     * start, not the previous level's position. */
    m_random_init(&s_effectRandom);
    s_listener = NULL;
}

/* --- Per-level start ----------------------------------------------------- */

void S_Start(void) {
    int cnum;
    int mnum;

    /* "kill all playing sounds at start of level (trust me - a good idea)" */
    for (cnum = 0; cnum < numChannels; ++cnum) {
        if (channels[cnum].sfxinfo != NULL) {
            S_StopChannel(cnum);
        }
    }

    /* start new music for the level */
    mus_paused = false;

    /* `gamemode` is always shareware here, so upstream's commercial arm
     * (`mus_runnin + gamemap - 1`) has no reachable form and is not written. */
    if (s_gameEpisode < 4) {
        mnum = mus_e1m1 + (s_gameEpisode - 1) * 9 + s_gameMap - 1;
    } else {
        /* Song - Who? - Where? -- upstream's episode-4 table, verbatim. */
        static const int spmus[] = {
            mus_e3m4,  /* American   e4m1 */
            mus_e3m2,  /* Romero     e4m2 */
            mus_e3m3,  /* Shawn      e4m3 */
            mus_e1m5,  /* American   e4m4 */
            mus_e2m7,  /* Tim        e4m5 */
            mus_e2m4,  /* Romero     e4m6 */
            mus_e2m6,  /* J.Anderson e4m7 CHIRON.WAD */
            mus_e2m5,  /* Shawn      e4m8 */
            mus_e1m9   /* Tim        e4m9 */
        };

        if (s_gameMap < 1 || s_gameMap > 9) {
            return;
        }
        mnum = spmus[s_gameMap - 1];
    }

    S_ChangeMusic(mnum, 1);
}

/* --- S_StartSound -------------------------------------------------------- */

void S_StartSoundAtVolume(const r_mobj_t* origin, int sfx_id, int volume) {
    const sfxinfo_t* sfx;
    int sep = NORM_SEP;
    int pitch;
    int cnum;
    int rc;

    /* "check for bogus sound #" -- upstream calls I_Error; here it is the
     * silent no-op the bank seam documents, and the id never reaches it. */
    if (sfx_id < 1 || sfx_id >= NUMSFX) {
        return;
    }

    sfx = &S_sfx[sfx_id];

    /* Initialize sound parameters */
    if (sfx->link != NULL) {
        pitch = sfx->pitch;
        volume += sfx->volume;

        if (volume < 1) {
            return;
        }
        if (volume > snd_SfxVolume) {
            volume = snd_SfxVolume;
        }
    } else {
        pitch = NORM_PITCH;
    }

    /* Check to see if it is audible, and if not, modify the params. A sector
     * token has no coordinates, so it takes the local arm and plays at full
     * volume -- the stated cost of p_sector_t having no soundorg. */
    if (origin != NULL && !origin_is_sector_token(origin)
        && origin != s_listener) {
        if (s_listener == NULL) {
            sep = NORM_SEP;
        } else {
            rc = S_AdjustSoundParams(s_listener, origin, &volume, &sep, &pitch);
            if (origin->x == s_listener->x && origin->y == s_listener->y) {
                sep = NORM_SEP;
            }
            if (!rc) {
                return;
            }
        }
    } else {
        sep = NORM_SEP;
    }

    /* hacks to vary the sfx pitches */
    if (sfx_id >= (int)sfx_sawup && sfx_id <= (int)sfx_sawhit) {
        pitch += 8 - (int)(m_random(&s_effectRandom) & 15);

        if (pitch < 0) {
            pitch = 0;
        } else if (pitch > 255) {
            pitch = 255;
        }
    } else if (sfx_id != (int)sfx_itemup && sfx_id != (int)sfx_tink) {
        pitch += 16 - (int)(m_random(&s_effectRandom) & 31);

        if (pitch < 0) {
            pitch = 0;
        } else if (pitch > 255) {
            pitch = 255;
        }
    }

    /* kill old sound */
    S_StopSound(origin);

    /* try to find a channel */
    cnum = S_getChannel(origin, sfx);
    if (cnum < 0) {
        return;
    }

    /* Upstream assigns I_StartSound()'s handle here. The seam is asked after
     * the channel is claimed, so a refused cue (-1) leaves the channel to be
     * freed by the next S_UpdateSounds(), exactly as a DMX refusal does. */
    channels[cnum].handle = doom_sound_start(sfx_id, volume, pitch, origin);

    /* `sep` is upstream's stereo-separation output. The APU is mono and the
     * seam takes no separation, so the value is computed (and therefore the
     * angle is exercised) and dropped here, deliberately. */
    (void)sep;
}

void S_StartSound(const r_mobj_t* origin, int sfx_id) {
    S_StartSoundAtVolume(origin, sfx_id, snd_SfxVolume);
}

void S_StopSound(const r_mobj_t* origin) {
    int cnum;

    for (cnum = 0; cnum < numChannels; ++cnum) {
        if (channels[cnum].sfxinfo != NULL
            && channels[cnum].origin == origin) {
            S_StopChannel(cnum);
            break;
        }
    }
}

/* --- Music --------------------------------------------------------------- */

void S_PauseSound(void) {
    if (mus_playing != NULL && !mus_paused) {
        doom_music_pause(true);
        mus_paused = true;
    }
}

void S_ResumeSound(void) {
    if (mus_playing != NULL && mus_paused) {
        doom_music_pause(false);
        mus_paused = false;
    }
}

void S_StartMusic(int m_id) { S_ChangeMusic(m_id, 0); }

void S_ChangeMusic(int musicnum, int looping) {
    const musicinfo_t* music;

    if (musicnum <= (int)mus_None || musicnum >= (int)NUMMUSIC) {
        return;
    }
    music = &S_music[musicnum];

    if (mus_playing == music) {
        return;
    }

    /* shutdown old music */
    S_StopMusic();

    /* Upstream looks up `d_<name>`, caches it and registers it. The port's
     * tracks are converted offline (T47) and played by id, so the seam owns
     * the lookup; a false answer means "this build has no track for that id"
     * and the level stays quiet rather than holding a request that never
     * plays. */
    if (!doom_music_start((int32_t)musicnum, looping != 0)) {
        return;
    }

    mus_playing = music;
}

void S_StopMusic(void) {
    if (mus_playing != NULL) {
        if (mus_paused) {
            doom_music_pause(false);
        }
        doom_music_stop();
        mus_playing = NULL;
        mus_paused = false;
    }
}

/* --- Per-tic maintenance ------------------------------------------------- */

void S_UpdateSounds(const r_mobj_t* listener) {
    int cnum;

    if (listener != NULL) {
        s_listener = listener;
    }

    /* The APU has no free per-voice query; the cue's own duration is the tic
     * budget doom_sound_playing() reads. One tick per simulated tic, which is
     * the cadence upstream's I_SoundIsPlaying() is polled at. */
    doom_sound_tick();

    for (cnum = 0; cnum < numChannels; ++cnum) {
        channel_t* c = &channels[cnum];
        const sfxinfo_t* sfx = c->sfxinfo;
        int volume;
        int sep;
        int pitch;

        if (sfx == NULL) {
            continue;
        }

        if (!doom_sound_playing(c->handle)) {
            /* "if channel is allocated but sound has stopped, free it" */
            S_StopChannel(cnum);
            continue;
        }

        volume = snd_SfxVolume;
        pitch = NORM_PITCH;
        sep = NORM_SEP;

        if (sfx->link != NULL) {
            pitch = sfx->pitch;
            volume += sfx->volume;
            if (volume < 1) {
                S_StopChannel(cnum);
                continue;
            } else if (volume > snd_SfxVolume) {
                volume = snd_SfxVolume;
            }
        }

        /* check non-local sounds for distance clipping or modify their
         * params */
        if (c->origin != NULL && listener != NULL && c->origin != listener
            && !origin_is_sector_token(c->origin)) {
            const int audible = S_AdjustSoundParams(listener, c->origin,
                                                    &volume, &sep, &pitch);

            if (!audible) {
                S_StopChannel(cnum);
            } else {
                doom_sound_update(c->handle, volume, pitch);
            }
        }
        (void)sep;
    }
}

/* --- Volumes ------------------------------------------------------------- */

void S_SetMusicVolume(int volume) {
    /* Upstream calls I_Error outside 0..127 and then double-sets the DMX
     * volume as a workaround. There is no abort path and no DMX mixer here, so
     * the value is clamped and stored; T49 turns it into an APU master
     * volume. */
    if (volume < 0) {
        volume = 0;
    } else if (volume > S_MAX_VOLUME_LOCAL) {
        volume = S_MAX_VOLUME_LOCAL;
    }
    snd_MusicVolume = volume;
}

void S_SetSfxVolume(int volume) {
    if (volume < 0) {
        volume = 0;
    } else if (volume > S_MAX_VOLUME_LOCAL) {
        volume = S_MAX_VOLUME_LOCAL;
    }
    snd_SfxVolume = volume;
}

/* --- Channel table ------------------------------------------------------- */

void S_StopChannel(int cnum) {
    channel_t* c;

    if (cnum < 0 || cnum >= numChannels) {
        return;
    }
    c = &channels[cnum];
    if (c->sfxinfo == NULL) {
        return;
    }

    /* Upstream tests I_SoundIsPlaying() first; the seam's stop is idempotent
     * and the playing query must stay pure, so the stop is unconditional. */
    if (c->handle > 0) {
        doom_sound_stop(c->handle);
    }

    c->sfxinfo = NULL;
    c->origin = NULL;
    c->handle = 0;
}

int S_getChannel(const r_mobj_t* origin, const sfxinfo_t* sfxinfo) {
    int cnum;

    if (numChannels <= 0 || numChannels > S_NUMCHANNELS) {
        return -1;
    }
    if (sfxinfo == NULL) {
        return -1;
    }

    /* Find an open channel */
    for (cnum = 0; cnum < numChannels; ++cnum) {
        if (channels[cnum].sfxinfo == NULL) {
            break;
        } else if (origin != NULL && channels[cnum].origin == origin) {
            S_StopChannel(cnum);
            break;
        }
    }

    /* None available: look for lower priority. `priority >=` is upstream's
     * comparison and means "this channel is no more important than the new
     * sound", i.e. it may be taken. */
    if (cnum == numChannels) {
        for (cnum = 0; cnum < numChannels; ++cnum) {
            if (channels[cnum].sfxinfo->priority >= sfxinfo->priority) {
                break;
            }
        }

        if (cnum == numChannels) {
            return -1;
        }
        S_StopChannel(cnum);
    }

    channels[cnum].sfxinfo = sfxinfo;
    channels[cnum].origin = origin;
    channels[cnum].handle = 0;
    return cnum;
}

/* --- S_AdjustSoundParams ------------------------------------------------- */

int S_AdjustSoundParams(const r_mobj_t* listener, const r_mobj_t* source,
                        int* vol, int* sep, int* pitch) {
    fixed_t approx_dist;
    fixed_t adx;
    fixed_t ady;
    angle_t angle;

    if (listener == NULL || source == NULL || vol == NULL || sep == NULL
        || pitch == NULL) {
        return 0;
    }
    (void)pitch; /* upstream leaves it untouched */

    /* calculate the distance to the sound origin and clip it if necessary */
    adx = sound_abs(listener->x - source->x);
    ady = sound_abs(listener->y - source->y);

    /* From _GG1_ p.428. Approx. euclidean distance fast. */
    approx_dist = adx + ady - ((adx < ady ? adx : ady) >> 1);

    if (s_gameMap != 8 && approx_dist > S_CLIPPING_DIST) {
        return 0;
    }

    /* angle of source to listener */
    angle = p_point_to_angle(source->x - listener->x, source->y - listener->y);

    if (angle > listener->angle) {
        angle = angle - listener->angle;
    } else {
        angle = angle + (0xffffffffu - listener->angle);
    }

    angle >>= ANGLETOFINESHIFT;

    /* stereo separation */
    *sep = 128
        - (fixed_mul(S_STEREO_SWING, r_fine_sine[angle]) >> FRACBITS);

    /* volume calculation */
    if (approx_dist < S_CLOSE_DIST) {
        *vol = snd_SfxVolume;
    } else if (s_gameMap == 8) {
        if (approx_dist > S_CLIPPING_DIST) {
            approx_dist = S_CLIPPING_DIST;
        }

        *vol = 15 + ((snd_SfxVolume - 15)
                     * ((S_CLIPPING_DIST - approx_dist) >> FRACBITS))
            / S_ATTENUATOR;
    } else {
        /* distance effect */
        *vol = (snd_SfxVolume
                * ((S_CLIPPING_DIST - approx_dist) >> FRACBITS))
            / S_ATTENUATOR;
    }

    return (*vol > 0);
}

/* --- The port's additions ------------------------------------------------ */

void S_SetListener(const r_mobj_t* listener) { s_listener = listener; }

void S_SetGameLevel(int episode, int map) {
    s_gameEpisode = episode;
    s_gameMap = map;
}

const r_mobj_t* S_SectorOrigin(uint16_t sector) {
    return (const r_mobj_t*)(uintptr_t)(S_SECTOR_ORIGIN_BASE + (uintptr_t)sector);
}
