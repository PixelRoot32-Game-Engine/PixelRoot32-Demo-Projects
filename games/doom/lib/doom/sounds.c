/*
 * Derived from id Software's DOOM (linuxdoom-1.10), Copyright (C) 1993-1996
 * id Software, Inc.  Rewritten for the PixelRoot32 port, 2026.  See NOTICE and
 * LICENSE in this directory for the provenance and the terms.
 */

/*
 * Upstream's sounds.c: S_music[] and S_sfx[] verbatim.
 *
 * The rows are linuxdoom-1.10's, in its order, with its names, singularity,
 * priority, link, pitch and volume. The three sample-cache fields
 * (`data`, `usefulness`, `lumpnum`) are always 0/NULL because this port has no
 * sample channel (pure APU synthesis), and the tables are `const` so they cost flash and no
 * DRAM (sounds.h says why).
 *
 * WHY THE IDS MUST NOT MOVE
 *
 * `sfx_pistol` is 1 and `sfx_radio` is 108 because that is what every one of
 * DOOM's call sites passes. The bank in src/doomc/SfxBank.cpp is indexed by
 * these numbers, and a test walks 1..NUMSFX-1 to prove every id resolves to a
 * cue, so a dropped row fails loudly rather than turning a door into a shot.
 * The priorities are the ones S_getChannel compares; `sfx_pldeth` at 32 is more
 * important than `sfx_sawidl` at 118, which is the direction the 1993 comment
 * "Look for lower priority" assumes.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no stdio, no
 * floating point.
 */
#include "sounds.h"

#include <stddef.h>

//
// Information about all the music
//

const musicinfo_t S_music[] = {
    {NULL, 0, NULL, 0},
    {"e1m1", 0, NULL, 0},
    {"e1m2", 0, NULL, 0},
    {"e1m3", 0, NULL, 0},
    {"e1m4", 0, NULL, 0},
    {"e1m5", 0, NULL, 0},
    {"e1m6", 0, NULL, 0},
    {"e1m7", 0, NULL, 0},
    {"e1m8", 0, NULL, 0},
    {"e1m9", 0, NULL, 0},
    {"e2m1", 0, NULL, 0},
    {"e2m2", 0, NULL, 0},
    {"e2m3", 0, NULL, 0},
    {"e2m4", 0, NULL, 0},
    {"e2m5", 0, NULL, 0},
    {"e2m6", 0, NULL, 0},
    {"e2m7", 0, NULL, 0},
    {"e2m8", 0, NULL, 0},
    {"e2m9", 0, NULL, 0},
    {"e3m1", 0, NULL, 0},
    {"e3m2", 0, NULL, 0},
    {"e3m3", 0, NULL, 0},
    {"e3m4", 0, NULL, 0},
    {"e3m5", 0, NULL, 0},
    {"e3m6", 0, NULL, 0},
    {"e3m7", 0, NULL, 0},
    {"e3m8", 0, NULL, 0},
    {"e3m9", 0, NULL, 0},
    {"inter", 0, NULL, 0},
    {"intro", 0, NULL, 0},
    {"bunny", 0, NULL, 0},
    {"victor", 0, NULL, 0},
    {"introa", 0, NULL, 0},
    {"runnin", 0, NULL, 0},
    {"stalks", 0, NULL, 0},
    {"countd", 0, NULL, 0},
    {"betwee", 0, NULL, 0},
    {"doom", 0, NULL, 0},
    {"the_da", 0, NULL, 0},
    {"shawn", 0, NULL, 0},
    {"ddtblu", 0, NULL, 0},
    {"in_cit", 0, NULL, 0},
    {"dead", 0, NULL, 0},
    {"stlks2", 0, NULL, 0},
    {"theda2", 0, NULL, 0},
    {"doom2", 0, NULL, 0},
    {"ddtbl2", 0, NULL, 0},
    {"runni2", 0, NULL, 0},
    {"dead2", 0, NULL, 0},
    {"stlks3", 0, NULL, 0},
    {"romero", 0, NULL, 0},
    {"shawn2", 0, NULL, 0},
    {"messag", 0, NULL, 0},
    {"count2", 0, NULL, 0},
    {"ddtbl3", 0, NULL, 0},
    {"ampie", 0, NULL, 0},
    {"theda3", 0, NULL, 0},
    {"adrian", 0, NULL, 0},
    {"messg2", 0, NULL, 0},
    {"romer2", 0, NULL, 0},
    {"tense", 0, NULL, 0},
    {"shawn3", 0, NULL, 0},
    {"openin", 0, NULL, 0},
    {"evil", 0, NULL, 0},
    {"ultima", 0, NULL, 0},
    {"read_m", 0, NULL, 0},
    {"dm2ttl", 0, NULL, 0},
    {"dm2int", 0, NULL, 0}
};

//
// Information about all the sfx
//

const sfxinfo_t S_sfx[] = {
    // S_sfx[0] needs to be a dummy for odd reasons.
    {"none", 0, 0, NULL, -1, -1, NULL, 0, 0},

    {"pistol", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"shotgn", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"sgcock", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"dshtgn", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"dbopn", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"dbcls", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"dbload", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"plasma", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"bfg", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"sawup", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"sawidl", 0, 118, NULL, -1, -1, NULL, 0, 0},
    {"sawful", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"sawhit", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"rlaunc", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"rxplod", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"firsht", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"firxpl", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"pstart", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"pstop", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"doropn", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"dorcls", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"stnmov", 0, 119, NULL, -1, -1, NULL, 0, 0},
    {"swtchn", 0, 78, NULL, -1, -1, NULL, 0, 0},
    {"swtchx", 0, 78, NULL, -1, -1, NULL, 0, 0},
    {"plpain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"dmpain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"popain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"vipain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"mnpain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"pepain", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"slop", 0, 78, NULL, -1, -1, NULL, 0, 0},
    {"itemup", 1, 78, NULL, -1, -1, NULL, 0, 0},
    {"wpnup", 1, 78, NULL, -1, -1, NULL, 0, 0},
    {"oof", 0, 96, NULL, -1, -1, NULL, 0, 0},
    {"telept", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"posit1", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"posit2", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"posit3", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"bgsit1", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"bgsit2", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"sgtsit", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"cacsit", 1, 98, NULL, -1, -1, NULL, 0, 0},
    {"brssit", 1, 94, NULL, -1, -1, NULL, 0, 0},
    {"cybsit", 1, 92, NULL, -1, -1, NULL, 0, 0},
    {"spisit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"bspsit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"kntsit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"vilsit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"mansit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"pesit", 1, 90, NULL, -1, -1, NULL, 0, 0},
    {"sklatk", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"sgtatk", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skepch", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"vilatk", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"claw", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skeswg", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"pldeth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"pdiehi", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"podth1", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"podth2", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"podth3", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"bgdth1", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"bgdth2", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"sgtdth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"cacdth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skldth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"brsdth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"cybdth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"spidth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"bspdth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"vildth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"kntdth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"pedth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"skedth", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"posact", 1, 120, NULL, -1, -1, NULL, 0, 0},
    {"bgact", 1, 120, NULL, -1, -1, NULL, 0, 0},
    {"dmact", 1, 120, NULL, -1, -1, NULL, 0, 0},
    {"bspact", 1, 100, NULL, -1, -1, NULL, 0, 0},
    {"bspwlk", 1, 100, NULL, -1, -1, NULL, 0, 0},
    {"vilact", 1, 100, NULL, -1, -1, NULL, 0, 0},
    {"noway", 0, 78, NULL, -1, -1, NULL, 0, 0},
    {"barexp", 0, 60, NULL, -1, -1, NULL, 0, 0},
    {"punch", 0, 64, NULL, -1, -1, NULL, 0, 0},
    {"hoof", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"metal", 0, 70, NULL, -1, -1, NULL, 0, 0},
    // The one link: the chaingun reuses the pistol's cue at pitch 150 and
    // volume +0, which S_StartSoundAtVolume applies before its own jitter.
    {"chgun", 0, 64, &S_sfx[sfx_pistol], 150, 0, NULL, 0, 0},
    {"tink", 0, 60, NULL, -1, -1, NULL, 0, 0},
    {"bdopn", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"bdcls", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"itmbk", 0, 100, NULL, -1, -1, NULL, 0, 0},
    {"flame", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"flamst", 0, 32, NULL, -1, -1, NULL, 0, 0},
    {"getpow", 0, 60, NULL, -1, -1, NULL, 0, 0},
    {"bospit", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"boscub", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"bossit", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"bospn", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"bosdth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"manatk", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"mandth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"sssit", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"ssdth", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"keenpn", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"keendt", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skeact", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skesit", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"skeatk", 0, 70, NULL, -1, -1, NULL, 0, 0},
    {"radio", 0, 60, NULL, -1, -1, NULL, 0, 0}
};
