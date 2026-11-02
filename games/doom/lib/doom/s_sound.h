/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * The not-so-system-specific sound interface.
 *
 * This is upstream's s_sound.h with one structural difference: the half that
 * talked to a DMX mixer is a narrow C seam whose definitions live in
 * src/doomc/DoomAudio.cpp. Everything upstream decides -- the eight-channel
 * table, one sound per origin, `S_sfx[]` priority and `link` arithmetic,
 * distance culling and the pitch jitter drawn from M_Random -- is in
 * s_sound.c and stays engine-free and allocation-free. The seam only starts,
 * stops, retunes and polls one APU voice; it is where the 8 -> 4 reduction
 * happens and where its steals are counted.
 *
 * WHY THE ORIGIN IS NOT A GLOBAL PLAYER
 *
 * Upstream's S_StartSoundAtVolume() reads `players[consoleplayer].mo` to
 * measure the distance from the listener. This port keeps no globals in
 * lib/doom, so the listener is input: S_SetListener() (and every
 * S_UpdateSounds() call) is how the game hands it over. Until one is set, a
 * sound is treated as local -- full volume, no culling -- which is exactly
 * upstream's `!origin || origin == listener` arm.
 *
 * The same reason adds S_SetGameLevel(): `gamemap` is read by
 * S_AdjustSoundParams()' map-8 exception and by S_Start()'s music choice.
 *
 * SECTOR ORIGINS
 *
 * A door, a lift and a floor mover start their sound from the *sector*:
 * upstream plays `S_StartSound ((mobj_t*)&door->sector->soundorg, ...)`. This
 * port's p_sector_t has no `soundorg` field (p_spec.h lists what it keeps), so
 * the caller passes S_SectorOrigin(index) instead: a stable, never-dereferenced
 * token that keeps the one-sound-per-origin rule and S_StopSound()'s identity
 * test working. A token has no coordinates, so its cue is non-local: full
 * volume, no distance culling. That is the one fidelity the missing field
 * costs, and it is stated here and at every call site.
 *
 * This is C99 only: <stdbool.h>, <stdint.h>, no allocation, no globals (the
 * channel table is the exception the interface is about), no stdio, no
 * floating point.
 */
#ifndef DOOM_S_SOUND_H
#define DOOM_S_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#include "p_mobj.h"
#include "sounds.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream's S_MAX_VOLUME. S_SetSfxVolume() and S_SetMusicVolume() take
 * 0..127. */
#define S_MAX_VOLUME 127

/* Upstream's channel count, M_misc's default. Eight logical channels exercise
 * vanilla's rules; the APU has four SFX voices (slots 4..7), so the C++ side
 * reports every reduction as a steal or a refusal. */
#define S_NUMCHANNELS 8

/* Upstream's globals. `numChannels` is fixed at S_NUMCHANNELS by S_Init(). */
extern int numChannels;
extern int snd_SfxVolume;
extern int snd_MusicVolume;

/*
 * The call-site gate.
 *
 * The port's DOOM core is compiled into builds that have no sound at all (the
 * 200-row `_show` image, the frame probes, every host test). In those a call to
 * S_StartSound() would still need the seven seam symbols to link, and the seam
 * lives in the application's src/doomc, not in this library. `DOOM_SOUND_START`
 * is therefore the real call where DOOM_AUDIO is defined and a no-op where it
 * is not: a build without audio carries no S_* reference, no S_sfx[] table and
 * no channel state, and every existing pixel/frame gate stays byte-identical.
 *
 * The placeholder comments at each call site keep saying which sound upstream
 * plays, so the mapping is readable without building the audio image.
 */
#if defined(DOOM_AUDIO)
#define DOOM_SOUND_START(origin, sound_id) S_StartSound((origin), (sound_id))
#else
/* The arguments are still "used": several call sites pass a parameter that
 * exists only for the sound (a weapon id, an actor, a picked-up cue), and a
 * gate build compiles with warnings as errors. `sizeof` marks the origin
 * expression used without evaluating it, so a sector-origin token never
 * becomes a reference to S_SectorOrigin() in a build that does not link
 * s_sound.c. */
#define DOOM_SOUND_START(origin, sound_id) \
    ((void)sizeof(origin), (void)(sound_id))
#endif

/**
 * @brief Initialises the channel table and the two volumes.
 *
 * Upstream allocates `numChannels` channels from the zone and marks every
 * S_sfx[] cache entry unused. This port has a static table and no sample
 * cache, so the call only clears the channels, sets `numChannels` to
 * S_NUMCHANNELS, resets the M_Random position (the pitch jitter is a data
 * sequence) and stores the two volumes.
 *
 * @param sfxVolume  0..127; out of range is clamped, never fatal (upstream
 *                   calls I_Error, which this port does not have).
 * @param musicVolume 0..127, same rule.
 */
void S_Init(int sfxVolume, int musicVolume);

/**
 * @brief Per-level start: kills every playing sound and starts the level's
 *        music.
 *
 * The music number is upstream's rule:
 * `mus_e1m1 + (gameepisode-1)*9 + gamemap-1` for episodes 1..3, and the
 * `spmus[]` table for episode 4. `gamemode` is always shareware here, so
 * upstream's commercial arm is not written. The request reaches the seam as
 * `doom_music_start()`; T47 supplies the tracks, and until then the seam
 * answers false and the level is simply quiet.
 */
void S_Start(void);

/**
 * @brief Starts @p sfx_id from @p origin at the current SFX volume.
 *
 * `sfx_id` is an sfxenum_t value: 1..NUMSFX-1. 0 and anything outside that
 * range are silent no-ops (upstream calls I_Error; a port with no abort path
 * answers with silence, and the bank seam counts an id it does not know).
 */
void S_StartSound(const r_mobj_t* origin, int sfx_id);

/**
 * @brief Starts @p sfx_id with an explicit 0..127 volume.
 *
 * Upstream's body: the `link` arm replaces pitch/priority and adds the link's
 * volume; a non-local origin is distance-adjusted and culled; the saw and
 * general pitch jitters draw from M_Random; the origin's old sound is stopped;
 * a channel is claimed by priority; and the seam is asked to play it.
 */
void S_StartSoundAtVolume(const r_mobj_t* origin, int sfx_id, int volume);

/** @brief Stops the sound playing from @p origin, if any. */
void S_StopSound(const r_mobj_t* origin);

/** @brief `S_ChangeMusic (music_id, false)`: a one-shot cue. */
void S_StartMusic(int music_id);

/**
 * @brief Starts @p music_id, looping or not, replacing whatever plays.
 *
 * Upstream looks the `d_<name>` lump up and registers it with the mixer. This
 * port hands the id to `doom_music_start()`; the asset side is T47's and the
 * seam decides whether a track exists.
 */
void S_ChangeMusic(int music_id, int looping);

/** @brief Stops the music and forgets it. */
void S_StopMusic(void);

/** @brief Pauses/resumes the music, as the menu's pause does. */
void S_PauseSound(void);
void S_ResumeSound(void);

/**
 * @brief Per-tic sound maintenance: expire finished cues, re-attenuate the
 *        rest, and remember @p listener for S_StartSound's distance rule.
 *
 * Upstream calls this from G_Ticker once per tic, after P_Ticker. `listener`
 * may be NULL, which leaves the remembered listener alone.
 */
void S_UpdateSounds(const r_mobj_t* listener);

/** @brief Sets the two volumes; 0..127, clamped.
 *
 * `S_SetSfxVolume()` is a real gain: `S_StartSoundAtVolume()` multiplies every
 * event by it. `S_SetMusicVolume()` is upstream's global only -- the port's
 * music level is the engine's post-mix master, applied by `DoomScene` (T49,
 * `src/doomc/AudioLevels.h`), because the APU has no music-only gain and
 * scaling a track twice would be worse than not scaling it here. */
void S_SetMusicVolume(int volume);
void S_SetSfxVolume(int volume);

/**
 * @brief R_PointToAngle2's distance and angle rule, as upstream wrote it.
 *
 * Computes the approximate distance from @p listener to @p source, rejects a
 * source past S_CLIPPING_DIST (unless the level is map 8), and rewrites
 * *vol, *sep and *pitch. *sep is 128 at the centre and swings by
 * S_STEREO_SWING with the angle; the APU is mono, so the seam does not consume
 * it, but the rule is ported and tested because S16 names it.
 *
 * @return 1 when audible, 0 when clipped out.
 */
int S_AdjustSoundParams(const r_mobj_t* listener, const r_mobj_t* source,
                        int* vol, int* sep, int* pitch);

/**
 * @brief Claims a channel, or -1 when every one holds a sound at least as
 *        important as @p sfxinfo.
 *
 * A channel already playing from @p origin is stopped and reused. The victim
 * search is upstream's: the first channel whose priority is >= the new
 * sound's (lower numbers are more important in `S_sfx[]`).
 */
int S_getChannel(const r_mobj_t* origin, const sfxinfo_t* sfxinfo);

/**
 * @brief Frees one channel and stops its seam handle.
 *
 * Always calls `doom_sound_stop()` for a live handle rather than testing
 * `doom_sound_playing()` first, because the stop is idempotent and the
 * fade-out query must stay free of side effects.
 */
void S_StopChannel(int cnum);

/* --- The port's additions, all for globals upstream reads ----------------- */

/** @brief Remembers the listener S_StartSound measures distance from. */
void S_SetListener(const r_mobj_t* listener);

/** @brief Stores upstream's `gameepisode`/`gamemap` (1-based) for S_Start and
 *         the map-8 exception. Out-of-range values are stored as given and
 *         only ever compared. */
void S_SetGameLevel(int episode, int map);

/**
 * @brief A stable origin token for a sound whose source is a sector.
 *
 * The value is `(const r_mobj_t*)(0x10000 + sector)`: never dereferenced,
 * compared only for identity. The base is chosen so a real pointer (DRAM,
 * flash or host heap, all far above 0x10000) can never collide with a token;
 * s_sound.c tests the range before it reads x/y.
 */
const r_mobj_t* S_SectorOrigin(uint16_t sector);

/* --- The I_* half: src/doomc/DoomAudio.cpp --------------------------------- */

/**
 * @brief Starts one cue on an APU SFX voice (slots 4..7).
 *
 * @param sfx_id  The sfxenum_t value the C layer already validated.
 * @param volume  0..127 (upstream's scale).
 * @param pitch   0..255, 128 normal (upstream's scale).
 * @param origin  Identity only; the seam never dereferences it, because it may
 *                be an S_SectorOrigin() token.
 * @return A positive handle, or -1 when no voice could be had.
 */
int32_t doom_sound_start(int32_t sfx_id, int32_t volume, int32_t pitch,
                         const r_mobj_t* origin);

/** @brief Stops a handle and frees its voice. Idempotent. */
void doom_sound_stop(int32_t handle);

/** @brief True while the handle's cue still has life left. Pure. */
bool doom_sound_playing(int32_t handle);

/**
 * @brief Applies the per-tic volume/pitch a distance change produced.
 *
 * The APU has no command that retunes a sounding one-shot without restarting
 * it, so a non-looping cue is left alone (its start already applied the
 * distance) and only a looping cue is re-submitted. T45's bank has no looping
 * cue; T47's music is a separate transport.
 */
void doom_sound_update(int32_t handle, int32_t volume, int32_t pitch);

/**
 * @brief Advances every cue one tic (35 Hz) and frees the expired ones.
 *
 * Called once per simulated tic from S_UpdateSounds(). The APU exposes no
 * cheap per-voice "still playing" query that a game should poll, so the sound
 * layer keeps the cue's duration as a tic budget instead.
 */
void doom_sound_tick(void);

/** @brief The music transport the seam supplies (T47). */
bool doom_music_start(int32_t music_id, bool looping);
void doom_music_stop(void);
void doom_music_pause(bool paused);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_S_SOUND_H */
