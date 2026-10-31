/*
 * DOOM's state and thing tables, generated from upstream linuxdoom-1.10.
 *
 * GENERATED FILE -- DO NOT EDIT. Every byte below is parsed output.
 *
 * Regenerate from the pinned upstream commit:
 *   python tools/gen_info.py
 * or, offline, from copies of info.h/info.c/sounds.h/p_mobj.h:
 *   python tools/gen_info.py --sources <dir>
 * `python tools/gen_info.py --check` fails if this file has drifted from
 * its source; that is the check a reviewer should run.
 *
 * Upstream: id-Software/DOOM linuxdoom-1.10 at commit
 *   4eb368a960647c8cc82d721d0183629ae10759d1
 * ("The DOOM sources as originally released on December 23, 1997").
 *
 * WHAT IT HOLDS, AND WHY THE SHAPES ARE THE PORT'S OWN
 *
 *   mobjinfo[]   137 records, one per mobjtype_t, in enum
 *                order. The
 *                fields are upstream's 23 in upstream's order; the brief
 *                for D4.1a named a subset, and the rest are what the
 *                action dispatch of D4.2 reads (painstate, deathstate,
 *                xdeathstate, reactiontime, the sound numbers), emitted
 *                now so this file is generated once and not per phase.
 *   states[]     967 records, one per statenum_t, in enum order.
 *                `action` is an action_t -- an enum, not the function
 *                pointer upstream stores -- so the table is pure data in
 *                flash and p_action.c owns the dispatch. `frame` is int32
 *                because upstream's flash states carry 32768+ (the
 *                FF_FULLBRIGHT bit), which does not fit a signed 16-bit.
 *   sprnames[]   the four-character sprite names sprnames[] carries.
 *   MF_*         the mobj flag values the `flags` column is expressed with.
 *
 * ONE TRANSLATION UNIT DEFINES THE TABLES
 *
 * The arrays are declared `extern` here and defined only in the file that
 * defines DOOM_INFO_TABLES_DEFINE before including this header (p_mobj.c)
 * so a second includer cannot create a second copy in flash.
 *
 * This is C99 only: <stdint.h>, no allocation, no globals, no floating
 * point. The tables are `const` and therefore live in .rodata (flash).
 */
#ifndef DOOM_INFO_TABLES_H
#define DOOM_INFO_TABLES_H

#include <stdint.h>

/* FRACUNIT: the radius and height columns are 16.16. */
#include "m_fixed.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The upstream commit these tables were parsed from. */
#define DOOM_INFO_UPSTREAM_COMMIT "4eb368a960647c8cc82d721d0183629ae10759d1"

#define NUM_SPRITES 138
#define NUM_STATES 967
#define NUM_MOBJ_TYPES 137

/* --- Sprites (upstream spritenum_t) ------------------------------------- */

typedef enum spritenum {
    SPR_TROO = 0,
    SPR_SHTG = 1,
    SPR_PUNG = 2,
    SPR_PISG = 3,
    SPR_PISF = 4,
    SPR_SHTF = 5,
    SPR_SHT2 = 6,
    SPR_CHGG = 7,
    SPR_CHGF = 8,
    SPR_MISG = 9,
    SPR_MISF = 10,
    SPR_SAWG = 11,
    SPR_PLSG = 12,
    SPR_PLSF = 13,
    SPR_BFGG = 14,
    SPR_BFGF = 15,
    SPR_BLUD = 16,
    SPR_PUFF = 17,
    SPR_BAL1 = 18,
    SPR_BAL2 = 19,
    SPR_PLSS = 20,
    SPR_PLSE = 21,
    SPR_MISL = 22,
    SPR_BFS1 = 23,
    SPR_BFE1 = 24,
    SPR_BFE2 = 25,
    SPR_TFOG = 26,
    SPR_IFOG = 27,
    SPR_PLAY = 28,
    SPR_POSS = 29,
    SPR_SPOS = 30,
    SPR_VILE = 31,
    SPR_FIRE = 32,
    SPR_FATB = 33,
    SPR_FBXP = 34,
    SPR_SKEL = 35,
    SPR_MANF = 36,
    SPR_FATT = 37,
    SPR_CPOS = 38,
    SPR_SARG = 39,
    SPR_HEAD = 40,
    SPR_BAL7 = 41,
    SPR_BOSS = 42,
    SPR_BOS2 = 43,
    SPR_SKUL = 44,
    SPR_SPID = 45,
    SPR_BSPI = 46,
    SPR_APLS = 47,
    SPR_APBX = 48,
    SPR_CYBR = 49,
    SPR_PAIN = 50,
    SPR_SSWV = 51,
    SPR_KEEN = 52,
    SPR_BBRN = 53,
    SPR_BOSF = 54,
    SPR_ARM1 = 55,
    SPR_ARM2 = 56,
    SPR_BAR1 = 57,
    SPR_BEXP = 58,
    SPR_FCAN = 59,
    SPR_BON1 = 60,
    SPR_BON2 = 61,
    SPR_BKEY = 62,
    SPR_RKEY = 63,
    SPR_YKEY = 64,
    SPR_BSKU = 65,
    SPR_RSKU = 66,
    SPR_YSKU = 67,
    SPR_STIM = 68,
    SPR_MEDI = 69,
    SPR_SOUL = 70,
    SPR_PINV = 71,
    SPR_PSTR = 72,
    SPR_PINS = 73,
    SPR_MEGA = 74,
    SPR_SUIT = 75,
    SPR_PMAP = 76,
    SPR_PVIS = 77,
    SPR_CLIP = 78,
    SPR_AMMO = 79,
    SPR_ROCK = 80,
    SPR_BROK = 81,
    SPR_CELL = 82,
    SPR_CELP = 83,
    SPR_SHEL = 84,
    SPR_SBOX = 85,
    SPR_BPAK = 86,
    SPR_BFUG = 87,
    SPR_MGUN = 88,
    SPR_CSAW = 89,
    SPR_LAUN = 90,
    SPR_PLAS = 91,
    SPR_SHOT = 92,
    SPR_SGN2 = 93,
    SPR_COLU = 94,
    SPR_SMT2 = 95,
    SPR_GOR1 = 96,
    SPR_POL2 = 97,
    SPR_POL5 = 98,
    SPR_POL4 = 99,
    SPR_POL3 = 100,
    SPR_POL1 = 101,
    SPR_POL6 = 102,
    SPR_GOR2 = 103,
    SPR_GOR3 = 104,
    SPR_GOR4 = 105,
    SPR_GOR5 = 106,
    SPR_SMIT = 107,
    SPR_COL1 = 108,
    SPR_COL2 = 109,
    SPR_COL3 = 110,
    SPR_COL4 = 111,
    SPR_CAND = 112,
    SPR_CBRA = 113,
    SPR_COL6 = 114,
    SPR_TRE1 = 115,
    SPR_TRE2 = 116,
    SPR_ELEC = 117,
    SPR_CEYE = 118,
    SPR_FSKU = 119,
    SPR_COL5 = 120,
    SPR_TBLU = 121,
    SPR_TGRN = 122,
    SPR_TRED = 123,
    SPR_SMBT = 124,
    SPR_SMGT = 125,
    SPR_SMRT = 126,
    SPR_HDB1 = 127,
    SPR_HDB2 = 128,
    SPR_HDB3 = 129,
    SPR_HDB4 = 130,
    SPR_HDB5 = 131,
    SPR_HDB6 = 132,
    SPR_POB1 = 133,
    SPR_POB2 = 134,
    SPR_BRS1 = 135,
    SPR_TLMP = 136,
    SPR_TLP2 = 137,
} spritenum_t;

/* --- States (upstream statenum_t) --------------------------------------- */

/* 967 names, and the table below is only correct if
 * index i is name i. */
typedef enum statenum {
    S_NULL = 0,
    S_LIGHTDONE = 1,
    S_PUNCH = 2,
    S_PUNCHDOWN = 3,
    S_PUNCHUP = 4,
    S_PUNCH1 = 5,
    S_PUNCH2 = 6,
    S_PUNCH3 = 7,
    S_PUNCH4 = 8,
    S_PUNCH5 = 9,
    S_PISTOL = 10,
    S_PISTOLDOWN = 11,
    S_PISTOLUP = 12,
    S_PISTOL1 = 13,
    S_PISTOL2 = 14,
    S_PISTOL3 = 15,
    S_PISTOL4 = 16,
    S_PISTOLFLASH = 17,
    S_SGUN = 18,
    S_SGUNDOWN = 19,
    S_SGUNUP = 20,
    S_SGUN1 = 21,
    S_SGUN2 = 22,
    S_SGUN3 = 23,
    S_SGUN4 = 24,
    S_SGUN5 = 25,
    S_SGUN6 = 26,
    S_SGUN7 = 27,
    S_SGUN8 = 28,
    S_SGUN9 = 29,
    S_SGUNFLASH1 = 30,
    S_SGUNFLASH2 = 31,
    S_DSGUN = 32,
    S_DSGUNDOWN = 33,
    S_DSGUNUP = 34,
    S_DSGUN1 = 35,
    S_DSGUN2 = 36,
    S_DSGUN3 = 37,
    S_DSGUN4 = 38,
    S_DSGUN5 = 39,
    S_DSGUN6 = 40,
    S_DSGUN7 = 41,
    S_DSGUN8 = 42,
    S_DSGUN9 = 43,
    S_DSGUN10 = 44,
    S_DSNR1 = 45,
    S_DSNR2 = 46,
    S_DSGUNFLASH1 = 47,
    S_DSGUNFLASH2 = 48,
    S_CHAIN = 49,
    S_CHAINDOWN = 50,
    S_CHAINUP = 51,
    S_CHAIN1 = 52,
    S_CHAIN2 = 53,
    S_CHAIN3 = 54,
    S_CHAINFLASH1 = 55,
    S_CHAINFLASH2 = 56,
    S_MISSILE = 57,
    S_MISSILEDOWN = 58,
    S_MISSILEUP = 59,
    S_MISSILE1 = 60,
    S_MISSILE2 = 61,
    S_MISSILE3 = 62,
    S_MISSILEFLASH1 = 63,
    S_MISSILEFLASH2 = 64,
    S_MISSILEFLASH3 = 65,
    S_MISSILEFLASH4 = 66,
    S_SAW = 67,
    S_SAWB = 68,
    S_SAWDOWN = 69,
    S_SAWUP = 70,
    S_SAW1 = 71,
    S_SAW2 = 72,
    S_SAW3 = 73,
    S_PLASMA = 74,
    S_PLASMADOWN = 75,
    S_PLASMAUP = 76,
    S_PLASMA1 = 77,
    S_PLASMA2 = 78,
    S_PLASMAFLASH1 = 79,
    S_PLASMAFLASH2 = 80,
    S_BFG = 81,
    S_BFGDOWN = 82,
    S_BFGUP = 83,
    S_BFG1 = 84,
    S_BFG2 = 85,
    S_BFG3 = 86,
    S_BFG4 = 87,
    S_BFGFLASH1 = 88,
    S_BFGFLASH2 = 89,
    S_BLOOD1 = 90,
    S_BLOOD2 = 91,
    S_BLOOD3 = 92,
    S_PUFF1 = 93,
    S_PUFF2 = 94,
    S_PUFF3 = 95,
    S_PUFF4 = 96,
    S_TBALL1 = 97,
    S_TBALL2 = 98,
    S_TBALLX1 = 99,
    S_TBALLX2 = 100,
    S_TBALLX3 = 101,
    S_RBALL1 = 102,
    S_RBALL2 = 103,
    S_RBALLX1 = 104,
    S_RBALLX2 = 105,
    S_RBALLX3 = 106,
    S_PLASBALL = 107,
    S_PLASBALL2 = 108,
    S_PLASEXP = 109,
    S_PLASEXP2 = 110,
    S_PLASEXP3 = 111,
    S_PLASEXP4 = 112,
    S_PLASEXP5 = 113,
    S_ROCKET = 114,
    S_BFGSHOT = 115,
    S_BFGSHOT2 = 116,
    S_BFGLAND = 117,
    S_BFGLAND2 = 118,
    S_BFGLAND3 = 119,
    S_BFGLAND4 = 120,
    S_BFGLAND5 = 121,
    S_BFGLAND6 = 122,
    S_BFGEXP = 123,
    S_BFGEXP2 = 124,
    S_BFGEXP3 = 125,
    S_BFGEXP4 = 126,
    S_EXPLODE1 = 127,
    S_EXPLODE2 = 128,
    S_EXPLODE3 = 129,
    S_TFOG = 130,
    S_TFOG01 = 131,
    S_TFOG02 = 132,
    S_TFOG2 = 133,
    S_TFOG3 = 134,
    S_TFOG4 = 135,
    S_TFOG5 = 136,
    S_TFOG6 = 137,
    S_TFOG7 = 138,
    S_TFOG8 = 139,
    S_TFOG9 = 140,
    S_TFOG10 = 141,
    S_IFOG = 142,
    S_IFOG01 = 143,
    S_IFOG02 = 144,
    S_IFOG2 = 145,
    S_IFOG3 = 146,
    S_IFOG4 = 147,
    S_IFOG5 = 148,
    S_PLAY = 149,
    S_PLAY_RUN1 = 150,
    S_PLAY_RUN2 = 151,
    S_PLAY_RUN3 = 152,
    S_PLAY_RUN4 = 153,
    S_PLAY_ATK1 = 154,
    S_PLAY_ATK2 = 155,
    S_PLAY_PAIN = 156,
    S_PLAY_PAIN2 = 157,
    S_PLAY_DIE1 = 158,
    S_PLAY_DIE2 = 159,
    S_PLAY_DIE3 = 160,
    S_PLAY_DIE4 = 161,
    S_PLAY_DIE5 = 162,
    S_PLAY_DIE6 = 163,
    S_PLAY_DIE7 = 164,
    S_PLAY_XDIE1 = 165,
    S_PLAY_XDIE2 = 166,
    S_PLAY_XDIE3 = 167,
    S_PLAY_XDIE4 = 168,
    S_PLAY_XDIE5 = 169,
    S_PLAY_XDIE6 = 170,
    S_PLAY_XDIE7 = 171,
    S_PLAY_XDIE8 = 172,
    S_PLAY_XDIE9 = 173,
    S_POSS_STND = 174,
    S_POSS_STND2 = 175,
    S_POSS_RUN1 = 176,
    S_POSS_RUN2 = 177,
    S_POSS_RUN3 = 178,
    S_POSS_RUN4 = 179,
    S_POSS_RUN5 = 180,
    S_POSS_RUN6 = 181,
    S_POSS_RUN7 = 182,
    S_POSS_RUN8 = 183,
    S_POSS_ATK1 = 184,
    S_POSS_ATK2 = 185,
    S_POSS_ATK3 = 186,
    S_POSS_PAIN = 187,
    S_POSS_PAIN2 = 188,
    S_POSS_DIE1 = 189,
    S_POSS_DIE2 = 190,
    S_POSS_DIE3 = 191,
    S_POSS_DIE4 = 192,
    S_POSS_DIE5 = 193,
    S_POSS_XDIE1 = 194,
    S_POSS_XDIE2 = 195,
    S_POSS_XDIE3 = 196,
    S_POSS_XDIE4 = 197,
    S_POSS_XDIE5 = 198,
    S_POSS_XDIE6 = 199,
    S_POSS_XDIE7 = 200,
    S_POSS_XDIE8 = 201,
    S_POSS_XDIE9 = 202,
    S_POSS_RAISE1 = 203,
    S_POSS_RAISE2 = 204,
    S_POSS_RAISE3 = 205,
    S_POSS_RAISE4 = 206,
    S_SPOS_STND = 207,
    S_SPOS_STND2 = 208,
    S_SPOS_RUN1 = 209,
    S_SPOS_RUN2 = 210,
    S_SPOS_RUN3 = 211,
    S_SPOS_RUN4 = 212,
    S_SPOS_RUN5 = 213,
    S_SPOS_RUN6 = 214,
    S_SPOS_RUN7 = 215,
    S_SPOS_RUN8 = 216,
    S_SPOS_ATK1 = 217,
    S_SPOS_ATK2 = 218,
    S_SPOS_ATK3 = 219,
    S_SPOS_PAIN = 220,
    S_SPOS_PAIN2 = 221,
    S_SPOS_DIE1 = 222,
    S_SPOS_DIE2 = 223,
    S_SPOS_DIE3 = 224,
    S_SPOS_DIE4 = 225,
    S_SPOS_DIE5 = 226,
    S_SPOS_XDIE1 = 227,
    S_SPOS_XDIE2 = 228,
    S_SPOS_XDIE3 = 229,
    S_SPOS_XDIE4 = 230,
    S_SPOS_XDIE5 = 231,
    S_SPOS_XDIE6 = 232,
    S_SPOS_XDIE7 = 233,
    S_SPOS_XDIE8 = 234,
    S_SPOS_XDIE9 = 235,
    S_SPOS_RAISE1 = 236,
    S_SPOS_RAISE2 = 237,
    S_SPOS_RAISE3 = 238,
    S_SPOS_RAISE4 = 239,
    S_SPOS_RAISE5 = 240,
    S_VILE_STND = 241,
    S_VILE_STND2 = 242,
    S_VILE_RUN1 = 243,
    S_VILE_RUN2 = 244,
    S_VILE_RUN3 = 245,
    S_VILE_RUN4 = 246,
    S_VILE_RUN5 = 247,
    S_VILE_RUN6 = 248,
    S_VILE_RUN7 = 249,
    S_VILE_RUN8 = 250,
    S_VILE_RUN9 = 251,
    S_VILE_RUN10 = 252,
    S_VILE_RUN11 = 253,
    S_VILE_RUN12 = 254,
    S_VILE_ATK1 = 255,
    S_VILE_ATK2 = 256,
    S_VILE_ATK3 = 257,
    S_VILE_ATK4 = 258,
    S_VILE_ATK5 = 259,
    S_VILE_ATK6 = 260,
    S_VILE_ATK7 = 261,
    S_VILE_ATK8 = 262,
    S_VILE_ATK9 = 263,
    S_VILE_ATK10 = 264,
    S_VILE_ATK11 = 265,
    S_VILE_HEAL1 = 266,
    S_VILE_HEAL2 = 267,
    S_VILE_HEAL3 = 268,
    S_VILE_PAIN = 269,
    S_VILE_PAIN2 = 270,
    S_VILE_DIE1 = 271,
    S_VILE_DIE2 = 272,
    S_VILE_DIE3 = 273,
    S_VILE_DIE4 = 274,
    S_VILE_DIE5 = 275,
    S_VILE_DIE6 = 276,
    S_VILE_DIE7 = 277,
    S_VILE_DIE8 = 278,
    S_VILE_DIE9 = 279,
    S_VILE_DIE10 = 280,
    S_FIRE1 = 281,
    S_FIRE2 = 282,
    S_FIRE3 = 283,
    S_FIRE4 = 284,
    S_FIRE5 = 285,
    S_FIRE6 = 286,
    S_FIRE7 = 287,
    S_FIRE8 = 288,
    S_FIRE9 = 289,
    S_FIRE10 = 290,
    S_FIRE11 = 291,
    S_FIRE12 = 292,
    S_FIRE13 = 293,
    S_FIRE14 = 294,
    S_FIRE15 = 295,
    S_FIRE16 = 296,
    S_FIRE17 = 297,
    S_FIRE18 = 298,
    S_FIRE19 = 299,
    S_FIRE20 = 300,
    S_FIRE21 = 301,
    S_FIRE22 = 302,
    S_FIRE23 = 303,
    S_FIRE24 = 304,
    S_FIRE25 = 305,
    S_FIRE26 = 306,
    S_FIRE27 = 307,
    S_FIRE28 = 308,
    S_FIRE29 = 309,
    S_FIRE30 = 310,
    S_SMOKE1 = 311,
    S_SMOKE2 = 312,
    S_SMOKE3 = 313,
    S_SMOKE4 = 314,
    S_SMOKE5 = 315,
    S_TRACER = 316,
    S_TRACER2 = 317,
    S_TRACEEXP1 = 318,
    S_TRACEEXP2 = 319,
    S_TRACEEXP3 = 320,
    S_SKEL_STND = 321,
    S_SKEL_STND2 = 322,
    S_SKEL_RUN1 = 323,
    S_SKEL_RUN2 = 324,
    S_SKEL_RUN3 = 325,
    S_SKEL_RUN4 = 326,
    S_SKEL_RUN5 = 327,
    S_SKEL_RUN6 = 328,
    S_SKEL_RUN7 = 329,
    S_SKEL_RUN8 = 330,
    S_SKEL_RUN9 = 331,
    S_SKEL_RUN10 = 332,
    S_SKEL_RUN11 = 333,
    S_SKEL_RUN12 = 334,
    S_SKEL_FIST1 = 335,
    S_SKEL_FIST2 = 336,
    S_SKEL_FIST3 = 337,
    S_SKEL_FIST4 = 338,
    S_SKEL_MISS1 = 339,
    S_SKEL_MISS2 = 340,
    S_SKEL_MISS3 = 341,
    S_SKEL_MISS4 = 342,
    S_SKEL_PAIN = 343,
    S_SKEL_PAIN2 = 344,
    S_SKEL_DIE1 = 345,
    S_SKEL_DIE2 = 346,
    S_SKEL_DIE3 = 347,
    S_SKEL_DIE4 = 348,
    S_SKEL_DIE5 = 349,
    S_SKEL_DIE6 = 350,
    S_SKEL_RAISE1 = 351,
    S_SKEL_RAISE2 = 352,
    S_SKEL_RAISE3 = 353,
    S_SKEL_RAISE4 = 354,
    S_SKEL_RAISE5 = 355,
    S_SKEL_RAISE6 = 356,
    S_FATSHOT1 = 357,
    S_FATSHOT2 = 358,
    S_FATSHOTX1 = 359,
    S_FATSHOTX2 = 360,
    S_FATSHOTX3 = 361,
    S_FATT_STND = 362,
    S_FATT_STND2 = 363,
    S_FATT_RUN1 = 364,
    S_FATT_RUN2 = 365,
    S_FATT_RUN3 = 366,
    S_FATT_RUN4 = 367,
    S_FATT_RUN5 = 368,
    S_FATT_RUN6 = 369,
    S_FATT_RUN7 = 370,
    S_FATT_RUN8 = 371,
    S_FATT_RUN9 = 372,
    S_FATT_RUN10 = 373,
    S_FATT_RUN11 = 374,
    S_FATT_RUN12 = 375,
    S_FATT_ATK1 = 376,
    S_FATT_ATK2 = 377,
    S_FATT_ATK3 = 378,
    S_FATT_ATK4 = 379,
    S_FATT_ATK5 = 380,
    S_FATT_ATK6 = 381,
    S_FATT_ATK7 = 382,
    S_FATT_ATK8 = 383,
    S_FATT_ATK9 = 384,
    S_FATT_ATK10 = 385,
    S_FATT_PAIN = 386,
    S_FATT_PAIN2 = 387,
    S_FATT_DIE1 = 388,
    S_FATT_DIE2 = 389,
    S_FATT_DIE3 = 390,
    S_FATT_DIE4 = 391,
    S_FATT_DIE5 = 392,
    S_FATT_DIE6 = 393,
    S_FATT_DIE7 = 394,
    S_FATT_DIE8 = 395,
    S_FATT_DIE9 = 396,
    S_FATT_DIE10 = 397,
    S_FATT_RAISE1 = 398,
    S_FATT_RAISE2 = 399,
    S_FATT_RAISE3 = 400,
    S_FATT_RAISE4 = 401,
    S_FATT_RAISE5 = 402,
    S_FATT_RAISE6 = 403,
    S_FATT_RAISE7 = 404,
    S_FATT_RAISE8 = 405,
    S_CPOS_STND = 406,
    S_CPOS_STND2 = 407,
    S_CPOS_RUN1 = 408,
    S_CPOS_RUN2 = 409,
    S_CPOS_RUN3 = 410,
    S_CPOS_RUN4 = 411,
    S_CPOS_RUN5 = 412,
    S_CPOS_RUN6 = 413,
    S_CPOS_RUN7 = 414,
    S_CPOS_RUN8 = 415,
    S_CPOS_ATK1 = 416,
    S_CPOS_ATK2 = 417,
    S_CPOS_ATK3 = 418,
    S_CPOS_ATK4 = 419,
    S_CPOS_PAIN = 420,
    S_CPOS_PAIN2 = 421,
    S_CPOS_DIE1 = 422,
    S_CPOS_DIE2 = 423,
    S_CPOS_DIE3 = 424,
    S_CPOS_DIE4 = 425,
    S_CPOS_DIE5 = 426,
    S_CPOS_DIE6 = 427,
    S_CPOS_DIE7 = 428,
    S_CPOS_XDIE1 = 429,
    S_CPOS_XDIE2 = 430,
    S_CPOS_XDIE3 = 431,
    S_CPOS_XDIE4 = 432,
    S_CPOS_XDIE5 = 433,
    S_CPOS_XDIE6 = 434,
    S_CPOS_RAISE1 = 435,
    S_CPOS_RAISE2 = 436,
    S_CPOS_RAISE3 = 437,
    S_CPOS_RAISE4 = 438,
    S_CPOS_RAISE5 = 439,
    S_CPOS_RAISE6 = 440,
    S_CPOS_RAISE7 = 441,
    S_TROO_STND = 442,
    S_TROO_STND2 = 443,
    S_TROO_RUN1 = 444,
    S_TROO_RUN2 = 445,
    S_TROO_RUN3 = 446,
    S_TROO_RUN4 = 447,
    S_TROO_RUN5 = 448,
    S_TROO_RUN6 = 449,
    S_TROO_RUN7 = 450,
    S_TROO_RUN8 = 451,
    S_TROO_ATK1 = 452,
    S_TROO_ATK2 = 453,
    S_TROO_ATK3 = 454,
    S_TROO_PAIN = 455,
    S_TROO_PAIN2 = 456,
    S_TROO_DIE1 = 457,
    S_TROO_DIE2 = 458,
    S_TROO_DIE3 = 459,
    S_TROO_DIE4 = 460,
    S_TROO_DIE5 = 461,
    S_TROO_XDIE1 = 462,
    S_TROO_XDIE2 = 463,
    S_TROO_XDIE3 = 464,
    S_TROO_XDIE4 = 465,
    S_TROO_XDIE5 = 466,
    S_TROO_XDIE6 = 467,
    S_TROO_XDIE7 = 468,
    S_TROO_XDIE8 = 469,
    S_TROO_RAISE1 = 470,
    S_TROO_RAISE2 = 471,
    S_TROO_RAISE3 = 472,
    S_TROO_RAISE4 = 473,
    S_TROO_RAISE5 = 474,
    S_SARG_STND = 475,
    S_SARG_STND2 = 476,
    S_SARG_RUN1 = 477,
    S_SARG_RUN2 = 478,
    S_SARG_RUN3 = 479,
    S_SARG_RUN4 = 480,
    S_SARG_RUN5 = 481,
    S_SARG_RUN6 = 482,
    S_SARG_RUN7 = 483,
    S_SARG_RUN8 = 484,
    S_SARG_ATK1 = 485,
    S_SARG_ATK2 = 486,
    S_SARG_ATK3 = 487,
    S_SARG_PAIN = 488,
    S_SARG_PAIN2 = 489,
    S_SARG_DIE1 = 490,
    S_SARG_DIE2 = 491,
    S_SARG_DIE3 = 492,
    S_SARG_DIE4 = 493,
    S_SARG_DIE5 = 494,
    S_SARG_DIE6 = 495,
    S_SARG_RAISE1 = 496,
    S_SARG_RAISE2 = 497,
    S_SARG_RAISE3 = 498,
    S_SARG_RAISE4 = 499,
    S_SARG_RAISE5 = 500,
    S_SARG_RAISE6 = 501,
    S_HEAD_STND = 502,
    S_HEAD_RUN1 = 503,
    S_HEAD_ATK1 = 504,
    S_HEAD_ATK2 = 505,
    S_HEAD_ATK3 = 506,
    S_HEAD_PAIN = 507,
    S_HEAD_PAIN2 = 508,
    S_HEAD_PAIN3 = 509,
    S_HEAD_DIE1 = 510,
    S_HEAD_DIE2 = 511,
    S_HEAD_DIE3 = 512,
    S_HEAD_DIE4 = 513,
    S_HEAD_DIE5 = 514,
    S_HEAD_DIE6 = 515,
    S_HEAD_RAISE1 = 516,
    S_HEAD_RAISE2 = 517,
    S_HEAD_RAISE3 = 518,
    S_HEAD_RAISE4 = 519,
    S_HEAD_RAISE5 = 520,
    S_HEAD_RAISE6 = 521,
    S_BRBALL1 = 522,
    S_BRBALL2 = 523,
    S_BRBALLX1 = 524,
    S_BRBALLX2 = 525,
    S_BRBALLX3 = 526,
    S_BOSS_STND = 527,
    S_BOSS_STND2 = 528,
    S_BOSS_RUN1 = 529,
    S_BOSS_RUN2 = 530,
    S_BOSS_RUN3 = 531,
    S_BOSS_RUN4 = 532,
    S_BOSS_RUN5 = 533,
    S_BOSS_RUN6 = 534,
    S_BOSS_RUN7 = 535,
    S_BOSS_RUN8 = 536,
    S_BOSS_ATK1 = 537,
    S_BOSS_ATK2 = 538,
    S_BOSS_ATK3 = 539,
    S_BOSS_PAIN = 540,
    S_BOSS_PAIN2 = 541,
    S_BOSS_DIE1 = 542,
    S_BOSS_DIE2 = 543,
    S_BOSS_DIE3 = 544,
    S_BOSS_DIE4 = 545,
    S_BOSS_DIE5 = 546,
    S_BOSS_DIE6 = 547,
    S_BOSS_DIE7 = 548,
    S_BOSS_RAISE1 = 549,
    S_BOSS_RAISE2 = 550,
    S_BOSS_RAISE3 = 551,
    S_BOSS_RAISE4 = 552,
    S_BOSS_RAISE5 = 553,
    S_BOSS_RAISE6 = 554,
    S_BOSS_RAISE7 = 555,
    S_BOS2_STND = 556,
    S_BOS2_STND2 = 557,
    S_BOS2_RUN1 = 558,
    S_BOS2_RUN2 = 559,
    S_BOS2_RUN3 = 560,
    S_BOS2_RUN4 = 561,
    S_BOS2_RUN5 = 562,
    S_BOS2_RUN6 = 563,
    S_BOS2_RUN7 = 564,
    S_BOS2_RUN8 = 565,
    S_BOS2_ATK1 = 566,
    S_BOS2_ATK2 = 567,
    S_BOS2_ATK3 = 568,
    S_BOS2_PAIN = 569,
    S_BOS2_PAIN2 = 570,
    S_BOS2_DIE1 = 571,
    S_BOS2_DIE2 = 572,
    S_BOS2_DIE3 = 573,
    S_BOS2_DIE4 = 574,
    S_BOS2_DIE5 = 575,
    S_BOS2_DIE6 = 576,
    S_BOS2_DIE7 = 577,
    S_BOS2_RAISE1 = 578,
    S_BOS2_RAISE2 = 579,
    S_BOS2_RAISE3 = 580,
    S_BOS2_RAISE4 = 581,
    S_BOS2_RAISE5 = 582,
    S_BOS2_RAISE6 = 583,
    S_BOS2_RAISE7 = 584,
    S_SKULL_STND = 585,
    S_SKULL_STND2 = 586,
    S_SKULL_RUN1 = 587,
    S_SKULL_RUN2 = 588,
    S_SKULL_ATK1 = 589,
    S_SKULL_ATK2 = 590,
    S_SKULL_ATK3 = 591,
    S_SKULL_ATK4 = 592,
    S_SKULL_PAIN = 593,
    S_SKULL_PAIN2 = 594,
    S_SKULL_DIE1 = 595,
    S_SKULL_DIE2 = 596,
    S_SKULL_DIE3 = 597,
    S_SKULL_DIE4 = 598,
    S_SKULL_DIE5 = 599,
    S_SKULL_DIE6 = 600,
    S_SPID_STND = 601,
    S_SPID_STND2 = 602,
    S_SPID_RUN1 = 603,
    S_SPID_RUN2 = 604,
    S_SPID_RUN3 = 605,
    S_SPID_RUN4 = 606,
    S_SPID_RUN5 = 607,
    S_SPID_RUN6 = 608,
    S_SPID_RUN7 = 609,
    S_SPID_RUN8 = 610,
    S_SPID_RUN9 = 611,
    S_SPID_RUN10 = 612,
    S_SPID_RUN11 = 613,
    S_SPID_RUN12 = 614,
    S_SPID_ATK1 = 615,
    S_SPID_ATK2 = 616,
    S_SPID_ATK3 = 617,
    S_SPID_ATK4 = 618,
    S_SPID_PAIN = 619,
    S_SPID_PAIN2 = 620,
    S_SPID_DIE1 = 621,
    S_SPID_DIE2 = 622,
    S_SPID_DIE3 = 623,
    S_SPID_DIE4 = 624,
    S_SPID_DIE5 = 625,
    S_SPID_DIE6 = 626,
    S_SPID_DIE7 = 627,
    S_SPID_DIE8 = 628,
    S_SPID_DIE9 = 629,
    S_SPID_DIE10 = 630,
    S_SPID_DIE11 = 631,
    S_BSPI_STND = 632,
    S_BSPI_STND2 = 633,
    S_BSPI_SIGHT = 634,
    S_BSPI_RUN1 = 635,
    S_BSPI_RUN2 = 636,
    S_BSPI_RUN3 = 637,
    S_BSPI_RUN4 = 638,
    S_BSPI_RUN5 = 639,
    S_BSPI_RUN6 = 640,
    S_BSPI_RUN7 = 641,
    S_BSPI_RUN8 = 642,
    S_BSPI_RUN9 = 643,
    S_BSPI_RUN10 = 644,
    S_BSPI_RUN11 = 645,
    S_BSPI_RUN12 = 646,
    S_BSPI_ATK1 = 647,
    S_BSPI_ATK2 = 648,
    S_BSPI_ATK3 = 649,
    S_BSPI_ATK4 = 650,
    S_BSPI_PAIN = 651,
    S_BSPI_PAIN2 = 652,
    S_BSPI_DIE1 = 653,
    S_BSPI_DIE2 = 654,
    S_BSPI_DIE3 = 655,
    S_BSPI_DIE4 = 656,
    S_BSPI_DIE5 = 657,
    S_BSPI_DIE6 = 658,
    S_BSPI_DIE7 = 659,
    S_BSPI_RAISE1 = 660,
    S_BSPI_RAISE2 = 661,
    S_BSPI_RAISE3 = 662,
    S_BSPI_RAISE4 = 663,
    S_BSPI_RAISE5 = 664,
    S_BSPI_RAISE6 = 665,
    S_BSPI_RAISE7 = 666,
    S_ARACH_PLAZ = 667,
    S_ARACH_PLAZ2 = 668,
    S_ARACH_PLEX = 669,
    S_ARACH_PLEX2 = 670,
    S_ARACH_PLEX3 = 671,
    S_ARACH_PLEX4 = 672,
    S_ARACH_PLEX5 = 673,
    S_CYBER_STND = 674,
    S_CYBER_STND2 = 675,
    S_CYBER_RUN1 = 676,
    S_CYBER_RUN2 = 677,
    S_CYBER_RUN3 = 678,
    S_CYBER_RUN4 = 679,
    S_CYBER_RUN5 = 680,
    S_CYBER_RUN6 = 681,
    S_CYBER_RUN7 = 682,
    S_CYBER_RUN8 = 683,
    S_CYBER_ATK1 = 684,
    S_CYBER_ATK2 = 685,
    S_CYBER_ATK3 = 686,
    S_CYBER_ATK4 = 687,
    S_CYBER_ATK5 = 688,
    S_CYBER_ATK6 = 689,
    S_CYBER_PAIN = 690,
    S_CYBER_DIE1 = 691,
    S_CYBER_DIE2 = 692,
    S_CYBER_DIE3 = 693,
    S_CYBER_DIE4 = 694,
    S_CYBER_DIE5 = 695,
    S_CYBER_DIE6 = 696,
    S_CYBER_DIE7 = 697,
    S_CYBER_DIE8 = 698,
    S_CYBER_DIE9 = 699,
    S_CYBER_DIE10 = 700,
    S_PAIN_STND = 701,
    S_PAIN_RUN1 = 702,
    S_PAIN_RUN2 = 703,
    S_PAIN_RUN3 = 704,
    S_PAIN_RUN4 = 705,
    S_PAIN_RUN5 = 706,
    S_PAIN_RUN6 = 707,
    S_PAIN_ATK1 = 708,
    S_PAIN_ATK2 = 709,
    S_PAIN_ATK3 = 710,
    S_PAIN_ATK4 = 711,
    S_PAIN_PAIN = 712,
    S_PAIN_PAIN2 = 713,
    S_PAIN_DIE1 = 714,
    S_PAIN_DIE2 = 715,
    S_PAIN_DIE3 = 716,
    S_PAIN_DIE4 = 717,
    S_PAIN_DIE5 = 718,
    S_PAIN_DIE6 = 719,
    S_PAIN_RAISE1 = 720,
    S_PAIN_RAISE2 = 721,
    S_PAIN_RAISE3 = 722,
    S_PAIN_RAISE4 = 723,
    S_PAIN_RAISE5 = 724,
    S_PAIN_RAISE6 = 725,
    S_SSWV_STND = 726,
    S_SSWV_STND2 = 727,
    S_SSWV_RUN1 = 728,
    S_SSWV_RUN2 = 729,
    S_SSWV_RUN3 = 730,
    S_SSWV_RUN4 = 731,
    S_SSWV_RUN5 = 732,
    S_SSWV_RUN6 = 733,
    S_SSWV_RUN7 = 734,
    S_SSWV_RUN8 = 735,
    S_SSWV_ATK1 = 736,
    S_SSWV_ATK2 = 737,
    S_SSWV_ATK3 = 738,
    S_SSWV_ATK4 = 739,
    S_SSWV_ATK5 = 740,
    S_SSWV_ATK6 = 741,
    S_SSWV_PAIN = 742,
    S_SSWV_PAIN2 = 743,
    S_SSWV_DIE1 = 744,
    S_SSWV_DIE2 = 745,
    S_SSWV_DIE3 = 746,
    S_SSWV_DIE4 = 747,
    S_SSWV_DIE5 = 748,
    S_SSWV_XDIE1 = 749,
    S_SSWV_XDIE2 = 750,
    S_SSWV_XDIE3 = 751,
    S_SSWV_XDIE4 = 752,
    S_SSWV_XDIE5 = 753,
    S_SSWV_XDIE6 = 754,
    S_SSWV_XDIE7 = 755,
    S_SSWV_XDIE8 = 756,
    S_SSWV_XDIE9 = 757,
    S_SSWV_RAISE1 = 758,
    S_SSWV_RAISE2 = 759,
    S_SSWV_RAISE3 = 760,
    S_SSWV_RAISE4 = 761,
    S_SSWV_RAISE5 = 762,
    S_KEENSTND = 763,
    S_COMMKEEN = 764,
    S_COMMKEEN2 = 765,
    S_COMMKEEN3 = 766,
    S_COMMKEEN4 = 767,
    S_COMMKEEN5 = 768,
    S_COMMKEEN6 = 769,
    S_COMMKEEN7 = 770,
    S_COMMKEEN8 = 771,
    S_COMMKEEN9 = 772,
    S_COMMKEEN10 = 773,
    S_COMMKEEN11 = 774,
    S_COMMKEEN12 = 775,
    S_KEENPAIN = 776,
    S_KEENPAIN2 = 777,
    S_BRAIN = 778,
    S_BRAIN_PAIN = 779,
    S_BRAIN_DIE1 = 780,
    S_BRAIN_DIE2 = 781,
    S_BRAIN_DIE3 = 782,
    S_BRAIN_DIE4 = 783,
    S_BRAINEYE = 784,
    S_BRAINEYESEE = 785,
    S_BRAINEYE1 = 786,
    S_SPAWN1 = 787,
    S_SPAWN2 = 788,
    S_SPAWN3 = 789,
    S_SPAWN4 = 790,
    S_SPAWNFIRE1 = 791,
    S_SPAWNFIRE2 = 792,
    S_SPAWNFIRE3 = 793,
    S_SPAWNFIRE4 = 794,
    S_SPAWNFIRE5 = 795,
    S_SPAWNFIRE6 = 796,
    S_SPAWNFIRE7 = 797,
    S_SPAWNFIRE8 = 798,
    S_BRAINEXPLODE1 = 799,
    S_BRAINEXPLODE2 = 800,
    S_BRAINEXPLODE3 = 801,
    S_ARM1 = 802,
    S_ARM1A = 803,
    S_ARM2 = 804,
    S_ARM2A = 805,
    S_BAR1 = 806,
    S_BAR2 = 807,
    S_BEXP = 808,
    S_BEXP2 = 809,
    S_BEXP3 = 810,
    S_BEXP4 = 811,
    S_BEXP5 = 812,
    S_BBAR1 = 813,
    S_BBAR2 = 814,
    S_BBAR3 = 815,
    S_BON1 = 816,
    S_BON1A = 817,
    S_BON1B = 818,
    S_BON1C = 819,
    S_BON1D = 820,
    S_BON1E = 821,
    S_BON2 = 822,
    S_BON2A = 823,
    S_BON2B = 824,
    S_BON2C = 825,
    S_BON2D = 826,
    S_BON2E = 827,
    S_BKEY = 828,
    S_BKEY2 = 829,
    S_RKEY = 830,
    S_RKEY2 = 831,
    S_YKEY = 832,
    S_YKEY2 = 833,
    S_BSKULL = 834,
    S_BSKULL2 = 835,
    S_RSKULL = 836,
    S_RSKULL2 = 837,
    S_YSKULL = 838,
    S_YSKULL2 = 839,
    S_STIM = 840,
    S_MEDI = 841,
    S_SOUL = 842,
    S_SOUL2 = 843,
    S_SOUL3 = 844,
    S_SOUL4 = 845,
    S_SOUL5 = 846,
    S_SOUL6 = 847,
    S_PINV = 848,
    S_PINV2 = 849,
    S_PINV3 = 850,
    S_PINV4 = 851,
    S_PSTR = 852,
    S_PINS = 853,
    S_PINS2 = 854,
    S_PINS3 = 855,
    S_PINS4 = 856,
    S_MEGA = 857,
    S_MEGA2 = 858,
    S_MEGA3 = 859,
    S_MEGA4 = 860,
    S_SUIT = 861,
    S_PMAP = 862,
    S_PMAP2 = 863,
    S_PMAP3 = 864,
    S_PMAP4 = 865,
    S_PMAP5 = 866,
    S_PMAP6 = 867,
    S_PVIS = 868,
    S_PVIS2 = 869,
    S_CLIP = 870,
    S_AMMO = 871,
    S_ROCK = 872,
    S_BROK = 873,
    S_CELL = 874,
    S_CELP = 875,
    S_SHEL = 876,
    S_SBOX = 877,
    S_BPAK = 878,
    S_BFUG = 879,
    S_MGUN = 880,
    S_CSAW = 881,
    S_LAUN = 882,
    S_PLAS = 883,
    S_SHOT = 884,
    S_SHOT2 = 885,
    S_COLU = 886,
    S_STALAG = 887,
    S_BLOODYTWITCH = 888,
    S_BLOODYTWITCH2 = 889,
    S_BLOODYTWITCH3 = 890,
    S_BLOODYTWITCH4 = 891,
    S_DEADTORSO = 892,
    S_DEADBOTTOM = 893,
    S_HEADSONSTICK = 894,
    S_GIBS = 895,
    S_HEADONASTICK = 896,
    S_HEADCANDLES = 897,
    S_HEADCANDLES2 = 898,
    S_DEADSTICK = 899,
    S_LIVESTICK = 900,
    S_LIVESTICK2 = 901,
    S_MEAT2 = 902,
    S_MEAT3 = 903,
    S_MEAT4 = 904,
    S_MEAT5 = 905,
    S_STALAGTITE = 906,
    S_TALLGRNCOL = 907,
    S_SHRTGRNCOL = 908,
    S_TALLREDCOL = 909,
    S_SHRTREDCOL = 910,
    S_CANDLESTIK = 911,
    S_CANDELABRA = 912,
    S_SKULLCOL = 913,
    S_TORCHTREE = 914,
    S_BIGTREE = 915,
    S_TECHPILLAR = 916,
    S_EVILEYE = 917,
    S_EVILEYE2 = 918,
    S_EVILEYE3 = 919,
    S_EVILEYE4 = 920,
    S_FLOATSKULL = 921,
    S_FLOATSKULL2 = 922,
    S_FLOATSKULL3 = 923,
    S_HEARTCOL = 924,
    S_HEARTCOL2 = 925,
    S_BLUETORCH = 926,
    S_BLUETORCH2 = 927,
    S_BLUETORCH3 = 928,
    S_BLUETORCH4 = 929,
    S_GREENTORCH = 930,
    S_GREENTORCH2 = 931,
    S_GREENTORCH3 = 932,
    S_GREENTORCH4 = 933,
    S_REDTORCH = 934,
    S_REDTORCH2 = 935,
    S_REDTORCH3 = 936,
    S_REDTORCH4 = 937,
    S_BTORCHSHRT = 938,
    S_BTORCHSHRT2 = 939,
    S_BTORCHSHRT3 = 940,
    S_BTORCHSHRT4 = 941,
    S_GTORCHSHRT = 942,
    S_GTORCHSHRT2 = 943,
    S_GTORCHSHRT3 = 944,
    S_GTORCHSHRT4 = 945,
    S_RTORCHSHRT = 946,
    S_RTORCHSHRT2 = 947,
    S_RTORCHSHRT3 = 948,
    S_RTORCHSHRT4 = 949,
    S_HANGNOGUTS = 950,
    S_HANGBNOBRAIN = 951,
    S_HANGTLOOKDN = 952,
    S_HANGTSKULL = 953,
    S_HANGTLOOKUP = 954,
    S_HANGTNOBRAIN = 955,
    S_COLONGIBS = 956,
    S_SMALLPOOL = 957,
    S_BRAINSTEM = 958,
    S_TECHLAMP = 959,
    S_TECHLAMP2 = 960,
    S_TECHLAMP3 = 961,
    S_TECHLAMP4 = 962,
    S_TECH2LAMP = 963,
    S_TECH2LAMP2 = 964,
    S_TECH2LAMP3 = 965,
    S_TECH2LAMP4 = 966,
} statenum_t;

/* --- Thing types (upstream mobjtype_t) ---------------------------------- */

typedef enum mobjtype {
    MT_PLAYER = 0,
    MT_POSSESSED = 1,
    MT_SHOTGUY = 2,
    MT_VILE = 3,
    MT_FIRE = 4,
    MT_UNDEAD = 5,
    MT_TRACER = 6,
    MT_SMOKE = 7,
    MT_FATSO = 8,
    MT_FATSHOT = 9,
    MT_CHAINGUY = 10,
    MT_TROOP = 11,
    MT_SERGEANT = 12,
    MT_SHADOWS = 13,
    MT_HEAD = 14,
    MT_BRUISER = 15,
    MT_BRUISERSHOT = 16,
    MT_KNIGHT = 17,
    MT_SKULL = 18,
    MT_SPIDER = 19,
    MT_BABY = 20,
    MT_CYBORG = 21,
    MT_PAIN = 22,
    MT_WOLFSS = 23,
    MT_KEEN = 24,
    MT_BOSSBRAIN = 25,
    MT_BOSSSPIT = 26,
    MT_BOSSTARGET = 27,
    MT_SPAWNSHOT = 28,
    MT_SPAWNFIRE = 29,
    MT_BARREL = 30,
    MT_TROOPSHOT = 31,
    MT_HEADSHOT = 32,
    MT_ROCKET = 33,
    MT_PLASMA = 34,
    MT_BFG = 35,
    MT_ARACHPLAZ = 36,
    MT_PUFF = 37,
    MT_BLOOD = 38,
    MT_TFOG = 39,
    MT_IFOG = 40,
    MT_TELEPORTMAN = 41,
    MT_EXTRABFG = 42,
    MT_MISC0 = 43,
    MT_MISC1 = 44,
    MT_MISC2 = 45,
    MT_MISC3 = 46,
    MT_MISC4 = 47,
    MT_MISC5 = 48,
    MT_MISC6 = 49,
    MT_MISC7 = 50,
    MT_MISC8 = 51,
    MT_MISC9 = 52,
    MT_MISC10 = 53,
    MT_MISC11 = 54,
    MT_MISC12 = 55,
    MT_INV = 56,
    MT_MISC13 = 57,
    MT_INS = 58,
    MT_MISC14 = 59,
    MT_MISC15 = 60,
    MT_MISC16 = 61,
    MT_MEGA = 62,
    MT_CLIP = 63,
    MT_MISC17 = 64,
    MT_MISC18 = 65,
    MT_MISC19 = 66,
    MT_MISC20 = 67,
    MT_MISC21 = 68,
    MT_MISC22 = 69,
    MT_MISC23 = 70,
    MT_MISC24 = 71,
    MT_MISC25 = 72,
    MT_CHAINGUN = 73,
    MT_MISC26 = 74,
    MT_MISC27 = 75,
    MT_MISC28 = 76,
    MT_SHOTGUN = 77,
    MT_SUPERSHOTGUN = 78,
    MT_MISC29 = 79,
    MT_MISC30 = 80,
    MT_MISC31 = 81,
    MT_MISC32 = 82,
    MT_MISC33 = 83,
    MT_MISC34 = 84,
    MT_MISC35 = 85,
    MT_MISC36 = 86,
    MT_MISC37 = 87,
    MT_MISC38 = 88,
    MT_MISC39 = 89,
    MT_MISC40 = 90,
    MT_MISC41 = 91,
    MT_MISC42 = 92,
    MT_MISC43 = 93,
    MT_MISC44 = 94,
    MT_MISC45 = 95,
    MT_MISC46 = 96,
    MT_MISC47 = 97,
    MT_MISC48 = 98,
    MT_MISC49 = 99,
    MT_MISC50 = 100,
    MT_MISC51 = 101,
    MT_MISC52 = 102,
    MT_MISC53 = 103,
    MT_MISC54 = 104,
    MT_MISC55 = 105,
    MT_MISC56 = 106,
    MT_MISC57 = 107,
    MT_MISC58 = 108,
    MT_MISC59 = 109,
    MT_MISC60 = 110,
    MT_MISC61 = 111,
    MT_MISC62 = 112,
    MT_MISC63 = 113,
    MT_MISC64 = 114,
    MT_MISC65 = 115,
    MT_MISC66 = 116,
    MT_MISC67 = 117,
    MT_MISC68 = 118,
    MT_MISC69 = 119,
    MT_MISC70 = 120,
    MT_MISC71 = 121,
    MT_MISC72 = 122,
    MT_MISC73 = 123,
    MT_MISC74 = 124,
    MT_MISC75 = 125,
    MT_MISC76 = 126,
    MT_MISC77 = 127,
    MT_MISC78 = 128,
    MT_MISC79 = 129,
    MT_MISC80 = 130,
    MT_MISC81 = 131,
    MT_MISC82 = 132,
    MT_MISC83 = 133,
    MT_MISC84 = 134,
    MT_MISC85 = 135,
    MT_MISC86 = 136,
} mobjtype_t;

/* --- Actions ------------------------------------------------------------ */

/*
 * The state action, as an enum instead of upstream's `actionf_t` function
 * pointer. p_action.c maps each value to its implementation and reports the
 * ones it does not implement yet through a counter; a function pointer in
 * the table would make `states[]` a relocated pointer table and would leave
 * no single place to count what is missing.
 *
 * ACT_NULL is upstream's `{NULL}`: no action is called at all, so it is not
 * an unimplemented action and is never counted as one.
 */
typedef enum action {
    ACT_NULL = 0,
    A_BFGSpray = 1,
    A_BFGsound = 2,
    A_BabyMetal = 3,
    A_BossDeath = 4,
    A_BrainAwake = 5,
    A_BrainDie = 6,
    A_BrainExplode = 7,
    A_BrainPain = 8,
    A_BrainScream = 9,
    A_BrainSpit = 10,
    A_BruisAttack = 11,
    A_BspiAttack = 12,
    A_CPosAttack = 13,
    A_CPosRefire = 14,
    A_Chase = 15,
    A_CheckReload = 16,
    A_CloseShotgun2 = 17,
    A_CyberAttack = 18,
    A_Explode = 19,
    A_FaceTarget = 20,
    A_Fall = 21,
    A_FatAttack1 = 22,
    A_FatAttack2 = 23,
    A_FatAttack3 = 24,
    A_FatRaise = 25,
    A_Fire = 26,
    A_FireBFG = 27,
    A_FireCGun = 28,
    A_FireCrackle = 29,
    A_FireMissile = 30,
    A_FirePistol = 31,
    A_FirePlasma = 32,
    A_FireShotgun = 33,
    A_FireShotgun2 = 34,
    A_GunFlash = 35,
    A_HeadAttack = 36,
    A_Hoof = 37,
    A_KeenDie = 38,
    A_Light0 = 39,
    A_Light1 = 40,
    A_Light2 = 41,
    A_LoadShotgun2 = 42,
    A_Look = 43,
    A_Lower = 44,
    A_Metal = 45,
    A_OpenShotgun2 = 46,
    A_Pain = 47,
    A_PainAttack = 48,
    A_PainDie = 49,
    A_PlayerScream = 50,
    A_PosAttack = 51,
    A_Punch = 52,
    A_Raise = 53,
    A_ReFire = 54,
    A_SPosAttack = 55,
    A_SargAttack = 56,
    A_Saw = 57,
    A_Scream = 58,
    A_SkelFist = 59,
    A_SkelMissile = 60,
    A_SkelWhoosh = 61,
    A_SkullAttack = 62,
    A_SpawnFly = 63,
    A_SpawnSound = 64,
    A_SpidRefire = 65,
    A_StartFire = 66,
    A_Tracer = 67,
    A_TroopAttack = 68,
    A_VileAttack = 69,
    A_VileChase = 70,
    A_VileStart = 71,
    A_VileTarget = 72,
    A_WeaponReady = 73,
    A_XScream = 74,
} action_t;
#define NUM_ACTIONS 75

/* --- Mobj flags (upstream p_mobj.h's mobjflag_t) ------------------------ */

#define MF_SPECIAL 0x00000001u
#define MF_SOLID 0x00000002u
#define MF_SHOOTABLE 0x00000004u
#define MF_NOSECTOR 0x00000008u
#define MF_NOBLOCKMAP 0x00000010u
#define MF_AMBUSH 0x00000020u
#define MF_JUSTHIT 0x00000040u
#define MF_JUSTATTACKED 0x00000080u
#define MF_SPAWNCEILING 0x00000100u
#define MF_NOGRAVITY 0x00000200u
#define MF_DROPOFF 0x00000400u
#define MF_PICKUP 0x00000800u
#define MF_NOCLIP 0x00001000u
#define MF_SLIDE 0x00002000u
#define MF_FLOAT 0x00004000u
#define MF_TELEPORT 0x00008000u
#define MF_MISSILE 0x00010000u
#define MF_DROPPED 0x00020000u
#define MF_SHADOW 0x00040000u
#define MF_NOBLOOD 0x00080000u
#define MF_CORPSE 0x00100000u
#define MF_INFLOAT 0x00200000u
#define MF_COUNTKILL 0x00400000u
#define MF_COUNTITEM 0x00800000u
#define MF_SKULLFLY 0x01000000u
#define MF_NOTDMATCH 0x02000000u
#define MF_TRANSLATION 0x0C000000u
#define MF_TRANSSHIFT 0x0000001Au

/* --- The tables --------------------------------------------------------- */

/* One mobjinfo record: upstream's 23 fields, in upstream's order. radius,
 * height and the fixed-point speeds are 16.16; doomednum is the THINGS type
 * that spawns this mobj. */
typedef struct doom_mobjinfo {
    int32_t doomednum;
    int32_t spawnstate;
    int32_t spawnhealth;
    int32_t seestate;
    int32_t seesound;
    int32_t reactiontime;
    int32_t attacksound;
    int32_t painstate;
    int32_t painchance;
    int32_t painsound;
    int32_t meleestate;
    int32_t missilestate;
    int32_t deathstate;
    int32_t xdeathstate;
    int32_t deathsound;
    int32_t speed;
    int32_t radius;
    int32_t height;
    int32_t mass;
    int32_t damage;
    int32_t activesound;
    uint32_t flags;
    int32_t raisestate;
} doom_mobjinfo_t;

/* One state record. `action` indexes action_t; `nextstate` and `sprite`
 * index statenum_t and spritenum_t. `frame` is first so the record packs to
 * 12 bytes on a 32-bit target instead of 16. */
typedef struct doom_state {
    int32_t frame;     /* may carry FF_FULLBRIGHT (0x8000) */
    int16_t sprite;    /* spritenum_t */
    int16_t tics;      /* -1 is a permanent state */
    int16_t nextstate; /* statenum_t */
    uint8_t action;    /* action_t */
} doom_state_t;

#ifdef DOOM_INFO_TABLES_DEFINE

const doom_mobjinfo_t mobjinfo[NUM_MOBJ_TYPES] = {
    {-1, 149, 100, 150, 0, 0, 0, 156, 255, 25, 0, 154, 158, 165, 57, 0, 16 * FRACUNIT, 56 * FRACUNIT, 100, 0, 0, 0x02000C06u, 0},
    {3004, 174, 20, 176, 36, 8, 1, 187, 200, 27, 0, 184, 189, 194, 59, 8, 20 * FRACUNIT, 56 * FRACUNIT, 100, 0, 75, 0x00400006u, 203},
    {9, 207, 30, 209, 37, 8, 0, 220, 170, 27, 0, 217, 222, 227, 60, 8, 20 * FRACUNIT, 56 * FRACUNIT, 100, 0, 75, 0x00400006u, 236},
    {64, 241, 700, 243, 48, 8, 0, 269, 10, 28, 0, 255, 271, 0, 71, 15, 20 * FRACUNIT, 56 * FRACUNIT, 500, 0, 80, 0x00400006u, 0},
    {-1, 281, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {66, 321, 300, 323, 106, 8, 0, 343, 100, 27, 335, 339, 345, 0, 74, 10, 20 * FRACUNIT, 56 * FRACUNIT, 500, 0, 105, 0x00400006u, 351},
    {-1, 316, 1000, 0, 107, 8, 0, 0, 0, 0, 0, 0, 318, 0, 82, 655360, 11 * FRACUNIT, 8 * FRACUNIT, 100, 10, 0, 0x00010610u, 0},
    {-1, 311, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {67, 362, 600, 364, 49, 8, 0, 386, 80, 29, 0, 376, 388, 0, 100, 8, 48 * FRACUNIT, 64 * FRACUNIT, 1000, 0, 75, 0x00400006u, 398},
    {-1, 357, 1000, 0, 16, 8, 0, 0, 0, 0, 0, 0, 359, 0, 17, 1310720, 6 * FRACUNIT, 8 * FRACUNIT, 100, 8, 0, 0x00010610u, 0},
    {65, 406, 70, 408, 37, 8, 0, 420, 170, 27, 0, 416, 422, 429, 60, 8, 20 * FRACUNIT, 56 * FRACUNIT, 100, 0, 75, 0x00400006u, 435},
    {3001, 442, 60, 444, 39, 8, 0, 455, 200, 27, 452, 452, 457, 462, 62, 8, 20 * FRACUNIT, 56 * FRACUNIT, 100, 0, 76, 0x00400006u, 470},
    {3002, 475, 150, 477, 41, 8, 52, 488, 180, 26, 485, 0, 490, 0, 64, 10, 30 * FRACUNIT, 56 * FRACUNIT, 400, 0, 77, 0x00400006u, 496},
    {58, 475, 150, 477, 41, 8, 52, 488, 180, 26, 485, 0, 490, 0, 64, 10, 30 * FRACUNIT, 56 * FRACUNIT, 400, 0, 77, 0x00440006u, 496},
    {3005, 502, 400, 503, 42, 8, 0, 507, 128, 26, 0, 504, 510, 0, 65, 8, 31 * FRACUNIT, 56 * FRACUNIT, 400, 0, 77, 0x00404206u, 516},
    {3003, 527, 1000, 529, 43, 8, 0, 540, 50, 26, 537, 537, 542, 0, 67, 8, 24 * FRACUNIT, 64 * FRACUNIT, 1000, 0, 77, 0x00400006u, 549},
    {-1, 522, 1000, 0, 16, 8, 0, 0, 0, 0, 0, 0, 524, 0, 17, 983040, 6 * FRACUNIT, 8 * FRACUNIT, 100, 8, 0, 0x00010610u, 0},
    {69, 556, 500, 558, 47, 8, 0, 569, 50, 26, 566, 566, 571, 0, 72, 8, 24 * FRACUNIT, 64 * FRACUNIT, 1000, 0, 77, 0x00400006u, 578},
    {3006, 585, 100, 587, 0, 8, 51, 593, 256, 26, 0, 589, 595, 0, 17, 8, 16 * FRACUNIT, 56 * FRACUNIT, 50, 3, 77, 0x00004206u, 0},
    {7, 601, 3000, 603, 45, 8, 2, 619, 40, 26, 0, 615, 621, 0, 69, 12, 128 * FRACUNIT, 100 * FRACUNIT, 1000, 0, 77, 0x00400006u, 0},
    {68, 632, 500, 634, 46, 8, 0, 651, 128, 26, 0, 647, 653, 0, 70, 12, 64 * FRACUNIT, 64 * FRACUNIT, 600, 0, 78, 0x00400006u, 660},
    {16, 674, 4000, 676, 44, 8, 0, 690, 20, 26, 0, 684, 691, 0, 68, 16, 40 * FRACUNIT, 110 * FRACUNIT, 1000, 0, 77, 0x00400006u, 0},
    {71, 701, 400, 702, 50, 8, 0, 712, 128, 30, 0, 708, 714, 0, 73, 8, 31 * FRACUNIT, 56 * FRACUNIT, 400, 0, 77, 0x00404206u, 720},
    {84, 726, 50, 728, 101, 8, 0, 742, 170, 27, 0, 736, 744, 749, 102, 8, 20 * FRACUNIT, 56 * FRACUNIT, 100, 0, 75, 0x00400006u, 758},
    {72, 763, 100, 0, 0, 8, 0, 776, 256, 103, 0, 0, 764, 0, 104, 0, 16 * FRACUNIT, 72 * FRACUNIT, 10000000, 0, 0, 0x00400306u, 0},
    {88, 778, 250, 0, 0, 8, 0, 779, 255, 97, 0, 0, 780, 0, 98, 0, 16 * FRACUNIT, 16 * FRACUNIT, 10000000, 0, 0, 0x00000006u, 0},
    {89, 784, 1000, 785, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 32 * FRACUNIT, 100, 0, 0, 0x00000018u, 0},
    {87, 0, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 32 * FRACUNIT, 100, 0, 0, 0x00000018u, 0},
    {-1, 787, 1000, 0, 94, 8, 0, 0, 0, 0, 0, 0, 0, 0, 17, 655360, 6 * FRACUNIT, 32 * FRACUNIT, 100, 3, 0, 0x00011610u, 0},
    {-1, 791, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {2035, 806, 20, 0, 0, 8, 0, 0, 0, 0, 0, 0, 808, 0, 82, 0, 10 * FRACUNIT, 42 * FRACUNIT, 100, 0, 0, 0x00080006u, 0},
    {-1, 97, 1000, 0, 16, 8, 0, 0, 0, 0, 0, 0, 99, 0, 17, 655360, 6 * FRACUNIT, 8 * FRACUNIT, 100, 3, 0, 0x00010610u, 0},
    {-1, 102, 1000, 0, 16, 8, 0, 0, 0, 0, 0, 0, 104, 0, 17, 655360, 6 * FRACUNIT, 8 * FRACUNIT, 100, 5, 0, 0x00010610u, 0},
    {-1, 114, 1000, 0, 14, 8, 0, 0, 0, 0, 0, 0, 127, 0, 82, 1310720, 11 * FRACUNIT, 8 * FRACUNIT, 100, 20, 0, 0x00010610u, 0},
    {-1, 107, 1000, 0, 8, 8, 0, 0, 0, 0, 0, 0, 109, 0, 17, 1638400, 13 * FRACUNIT, 8 * FRACUNIT, 100, 5, 0, 0x00010610u, 0},
    {-1, 115, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 117, 0, 15, 1638400, 13 * FRACUNIT, 8 * FRACUNIT, 100, 100, 0, 0x00010610u, 0},
    {-1, 667, 1000, 0, 8, 8, 0, 0, 0, 0, 0, 0, 669, 0, 17, 1638400, 13 * FRACUNIT, 8 * FRACUNIT, 100, 5, 0, 0x00010610u, 0},
    {-1, 93, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {-1, 90, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000010u, 0},
    {-1, 130, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {-1, 142, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {14, 0, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000018u, 0},
    {-1, 123, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000210u, 0},
    {2018, 802, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2019, 804, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2014, 816, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2015, 822, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {5, 828, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {13, 830, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {6, 832, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {39, 838, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {38, 836, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {40, 834, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x02000001u, 0},
    {2011, 840, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2012, 841, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2013, 842, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2022, 848, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2023, 852, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2024, 853, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2025, 861, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2026, 862, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2045, 868, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {83, 857, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00800001u, 0},
    {2007, 870, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2048, 871, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2010, 872, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2046, 873, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2047, 874, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {17, 875, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2008, 876, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2049, 877, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {8, 878, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2006, 879, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2002, 880, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2005, 881, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2003, 882, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2004, 883, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {2001, 884, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {82, 885, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000001u, 0},
    {85, 959, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {86, 963, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {2028, 886, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {30, 907, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {31, 908, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {32, 909, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {33, 910, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {37, 913, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {36, 924, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {41, 917, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {42, 921, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {43, 914, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {44, 926, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {45, 930, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {46, 934, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {55, 938, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {56, 942, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {57, 946, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {47, 906, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {48, 916, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {34, 911, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {35, 912, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {49, 888, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 68 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {50, 902, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 84 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {51, 903, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 84 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {52, 904, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 68 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {53, 905, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 52 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {59, 902, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 84 * FRACUNIT, 100, 0, 0, 0x00000300u, 0},
    {60, 904, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 68 * FRACUNIT, 100, 0, 0, 0x00000300u, 0},
    {61, 903, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 52 * FRACUNIT, 100, 0, 0, 0x00000300u, 0},
    {62, 905, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 52 * FRACUNIT, 100, 0, 0, 0x00000300u, 0},
    {63, 888, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 68 * FRACUNIT, 100, 0, 0, 0x00000300u, 0},
    {22, 515, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {15, 164, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {18, 193, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {21, 495, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {23, 600, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {20, 461, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {19, 226, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {10, 173, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {12, 173, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {28, 894, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {24, 895, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000000u, 0},
    {27, 896, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {29, 897, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {25, 899, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {26, 900, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {54, 915, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 32 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {70, 813, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000002u, 0},
    {73, 950, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 88 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {74, 951, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 88 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {75, 952, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 64 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {76, 953, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 64 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {77, 954, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 64 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {78, 955, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 16 * FRACUNIT, 64 * FRACUNIT, 100, 0, 0, 0x00000302u, 0},
    {79, 956, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000010u, 0},
    {80, 957, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000010u, 0},
    {81, 958, 1000, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20 * FRACUNIT, 16 * FRACUNIT, 100, 0, 0, 0x00000010u, 0},
};

const doom_state_t states[NUM_STATES] = {
    {0, 0, -1, 0, ACT_NULL},  /* S_NULL */
    {4, 1, 0, 0, A_Light0},  /* S_LIGHTDONE */
    {0, 2, 1, 2, A_WeaponReady},  /* S_PUNCH */
    {0, 2, 1, 3, A_Lower},  /* S_PUNCHDOWN */
    {0, 2, 1, 4, A_Raise},  /* S_PUNCHUP */
    {1, 2, 4, 6, ACT_NULL},  /* S_PUNCH1 */
    {2, 2, 4, 7, A_Punch},  /* S_PUNCH2 */
    {3, 2, 5, 8, ACT_NULL},  /* S_PUNCH3 */
    {2, 2, 4, 9, ACT_NULL},  /* S_PUNCH4 */
    {1, 2, 5, 2, A_ReFire},  /* S_PUNCH5 */
    {0, 3, 1, 10, A_WeaponReady},  /* S_PISTOL */
    {0, 3, 1, 11, A_Lower},  /* S_PISTOLDOWN */
    {0, 3, 1, 12, A_Raise},  /* S_PISTOLUP */
    {0, 3, 4, 14, ACT_NULL},  /* S_PISTOL1 */
    {1, 3, 6, 15, A_FirePistol},  /* S_PISTOL2 */
    {2, 3, 4, 16, ACT_NULL},  /* S_PISTOL3 */
    {1, 3, 5, 10, A_ReFire},  /* S_PISTOL4 */
    {32768, 4, 7, 1, A_Light1},  /* S_PISTOLFLASH */
    {0, 1, 1, 18, A_WeaponReady},  /* S_SGUN */
    {0, 1, 1, 19, A_Lower},  /* S_SGUNDOWN */
    {0, 1, 1, 20, A_Raise},  /* S_SGUNUP */
    {0, 1, 3, 22, ACT_NULL},  /* S_SGUN1 */
    {0, 1, 7, 23, A_FireShotgun},  /* S_SGUN2 */
    {1, 1, 5, 24, ACT_NULL},  /* S_SGUN3 */
    {2, 1, 5, 25, ACT_NULL},  /* S_SGUN4 */
    {3, 1, 4, 26, ACT_NULL},  /* S_SGUN5 */
    {2, 1, 5, 27, ACT_NULL},  /* S_SGUN6 */
    {1, 1, 5, 28, ACT_NULL},  /* S_SGUN7 */
    {0, 1, 3, 29, ACT_NULL},  /* S_SGUN8 */
    {0, 1, 7, 18, A_ReFire},  /* S_SGUN9 */
    {32768, 5, 4, 31, A_Light1},  /* S_SGUNFLASH1 */
    {32769, 5, 3, 1, A_Light2},  /* S_SGUNFLASH2 */
    {0, 6, 1, 32, A_WeaponReady},  /* S_DSGUN */
    {0, 6, 1, 33, A_Lower},  /* S_DSGUNDOWN */
    {0, 6, 1, 34, A_Raise},  /* S_DSGUNUP */
    {0, 6, 3, 36, ACT_NULL},  /* S_DSGUN1 */
    {0, 6, 7, 37, A_FireShotgun2},  /* S_DSGUN2 */
    {1, 6, 7, 38, ACT_NULL},  /* S_DSGUN3 */
    {2, 6, 7, 39, A_CheckReload},  /* S_DSGUN4 */
    {3, 6, 7, 40, A_OpenShotgun2},  /* S_DSGUN5 */
    {4, 6, 7, 41, ACT_NULL},  /* S_DSGUN6 */
    {5, 6, 7, 42, A_LoadShotgun2},  /* S_DSGUN7 */
    {6, 6, 6, 43, ACT_NULL},  /* S_DSGUN8 */
    {7, 6, 6, 44, A_CloseShotgun2},  /* S_DSGUN9 */
    {0, 6, 5, 32, A_ReFire},  /* S_DSGUN10 */
    {1, 6, 7, 46, ACT_NULL},  /* S_DSNR1 */
    {0, 6, 3, 33, ACT_NULL},  /* S_DSNR2 */
    {32776, 6, 5, 48, A_Light1},  /* S_DSGUNFLASH1 */
    {32777, 6, 4, 1, A_Light2},  /* S_DSGUNFLASH2 */
    {0, 7, 1, 49, A_WeaponReady},  /* S_CHAIN */
    {0, 7, 1, 50, A_Lower},  /* S_CHAINDOWN */
    {0, 7, 1, 51, A_Raise},  /* S_CHAINUP */
    {0, 7, 4, 53, A_FireCGun},  /* S_CHAIN1 */
    {1, 7, 4, 54, A_FireCGun},  /* S_CHAIN2 */
    {1, 7, 0, 49, A_ReFire},  /* S_CHAIN3 */
    {32768, 8, 5, 1, A_Light1},  /* S_CHAINFLASH1 */
    {32769, 8, 5, 1, A_Light2},  /* S_CHAINFLASH2 */
    {0, 9, 1, 57, A_WeaponReady},  /* S_MISSILE */
    {0, 9, 1, 58, A_Lower},  /* S_MISSILEDOWN */
    {0, 9, 1, 59, A_Raise},  /* S_MISSILEUP */
    {1, 9, 8, 61, A_GunFlash},  /* S_MISSILE1 */
    {1, 9, 12, 62, A_FireMissile},  /* S_MISSILE2 */
    {1, 9, 0, 57, A_ReFire},  /* S_MISSILE3 */
    {32768, 10, 3, 64, A_Light1},  /* S_MISSILEFLASH1 */
    {32769, 10, 4, 65, ACT_NULL},  /* S_MISSILEFLASH2 */
    {32770, 10, 4, 66, A_Light2},  /* S_MISSILEFLASH3 */
    {32771, 10, 4, 1, A_Light2},  /* S_MISSILEFLASH4 */
    {2, 11, 4, 68, A_WeaponReady},  /* S_SAW */
    {3, 11, 4, 67, A_WeaponReady},  /* S_SAWB */
    {2, 11, 1, 69, A_Lower},  /* S_SAWDOWN */
    {2, 11, 1, 70, A_Raise},  /* S_SAWUP */
    {0, 11, 4, 72, A_Saw},  /* S_SAW1 */
    {1, 11, 4, 73, A_Saw},  /* S_SAW2 */
    {1, 11, 0, 67, A_ReFire},  /* S_SAW3 */
    {0, 12, 1, 74, A_WeaponReady},  /* S_PLASMA */
    {0, 12, 1, 75, A_Lower},  /* S_PLASMADOWN */
    {0, 12, 1, 76, A_Raise},  /* S_PLASMAUP */
    {0, 12, 3, 78, A_FirePlasma},  /* S_PLASMA1 */
    {1, 12, 20, 74, A_ReFire},  /* S_PLASMA2 */
    {32768, 13, 4, 1, A_Light1},  /* S_PLASMAFLASH1 */
    {32769, 13, 4, 1, A_Light1},  /* S_PLASMAFLASH2 */
    {0, 14, 1, 81, A_WeaponReady},  /* S_BFG */
    {0, 14, 1, 82, A_Lower},  /* S_BFGDOWN */
    {0, 14, 1, 83, A_Raise},  /* S_BFGUP */
    {0, 14, 20, 85, A_BFGsound},  /* S_BFG1 */
    {1, 14, 10, 86, A_GunFlash},  /* S_BFG2 */
    {1, 14, 10, 87, A_FireBFG},  /* S_BFG3 */
    {1, 14, 20, 81, A_ReFire},  /* S_BFG4 */
    {32768, 15, 11, 89, A_Light1},  /* S_BFGFLASH1 */
    {32769, 15, 6, 1, A_Light2},  /* S_BFGFLASH2 */
    {2, 16, 8, 91, ACT_NULL},  /* S_BLOOD1 */
    {1, 16, 8, 92, ACT_NULL},  /* S_BLOOD2 */
    {0, 16, 8, 0, ACT_NULL},  /* S_BLOOD3 */
    {32768, 17, 4, 94, ACT_NULL},  /* S_PUFF1 */
    {1, 17, 4, 95, ACT_NULL},  /* S_PUFF2 */
    {2, 17, 4, 96, ACT_NULL},  /* S_PUFF3 */
    {3, 17, 4, 0, ACT_NULL},  /* S_PUFF4 */
    {32768, 18, 4, 98, ACT_NULL},  /* S_TBALL1 */
    {32769, 18, 4, 97, ACT_NULL},  /* S_TBALL2 */
    {32770, 18, 6, 100, ACT_NULL},  /* S_TBALLX1 */
    {32771, 18, 6, 101, ACT_NULL},  /* S_TBALLX2 */
    {32772, 18, 6, 0, ACT_NULL},  /* S_TBALLX3 */
    {32768, 19, 4, 103, ACT_NULL},  /* S_RBALL1 */
    {32769, 19, 4, 102, ACT_NULL},  /* S_RBALL2 */
    {32770, 19, 6, 105, ACT_NULL},  /* S_RBALLX1 */
    {32771, 19, 6, 106, ACT_NULL},  /* S_RBALLX2 */
    {32772, 19, 6, 0, ACT_NULL},  /* S_RBALLX3 */
    {32768, 20, 6, 108, ACT_NULL},  /* S_PLASBALL */
    {32769, 20, 6, 107, ACT_NULL},  /* S_PLASBALL2 */
    {32768, 21, 4, 110, ACT_NULL},  /* S_PLASEXP */
    {32769, 21, 4, 111, ACT_NULL},  /* S_PLASEXP2 */
    {32770, 21, 4, 112, ACT_NULL},  /* S_PLASEXP3 */
    {32771, 21, 4, 113, ACT_NULL},  /* S_PLASEXP4 */
    {32772, 21, 4, 0, ACT_NULL},  /* S_PLASEXP5 */
    {32768, 22, 1, 114, ACT_NULL},  /* S_ROCKET */
    {32768, 23, 4, 116, ACT_NULL},  /* S_BFGSHOT */
    {32769, 23, 4, 115, ACT_NULL},  /* S_BFGSHOT2 */
    {32768, 24, 8, 118, ACT_NULL},  /* S_BFGLAND */
    {32769, 24, 8, 119, ACT_NULL},  /* S_BFGLAND2 */
    {32770, 24, 8, 120, A_BFGSpray},  /* S_BFGLAND3 */
    {32771, 24, 8, 121, ACT_NULL},  /* S_BFGLAND4 */
    {32772, 24, 8, 122, ACT_NULL},  /* S_BFGLAND5 */
    {32773, 24, 8, 0, ACT_NULL},  /* S_BFGLAND6 */
    {32768, 25, 8, 124, ACT_NULL},  /* S_BFGEXP */
    {32769, 25, 8, 125, ACT_NULL},  /* S_BFGEXP2 */
    {32770, 25, 8, 126, ACT_NULL},  /* S_BFGEXP3 */
    {32771, 25, 8, 0, ACT_NULL},  /* S_BFGEXP4 */
    {32769, 22, 8, 128, A_Explode},  /* S_EXPLODE1 */
    {32770, 22, 6, 129, ACT_NULL},  /* S_EXPLODE2 */
    {32771, 22, 4, 0, ACT_NULL},  /* S_EXPLODE3 */
    {32768, 26, 6, 131, ACT_NULL},  /* S_TFOG */
    {32769, 26, 6, 132, ACT_NULL},  /* S_TFOG01 */
    {32768, 26, 6, 133, ACT_NULL},  /* S_TFOG02 */
    {32769, 26, 6, 134, ACT_NULL},  /* S_TFOG2 */
    {32770, 26, 6, 135, ACT_NULL},  /* S_TFOG3 */
    {32771, 26, 6, 136, ACT_NULL},  /* S_TFOG4 */
    {32772, 26, 6, 137, ACT_NULL},  /* S_TFOG5 */
    {32773, 26, 6, 138, ACT_NULL},  /* S_TFOG6 */
    {32774, 26, 6, 139, ACT_NULL},  /* S_TFOG7 */
    {32775, 26, 6, 140, ACT_NULL},  /* S_TFOG8 */
    {32776, 26, 6, 141, ACT_NULL},  /* S_TFOG9 */
    {32777, 26, 6, 0, ACT_NULL},  /* S_TFOG10 */
    {32768, 27, 6, 143, ACT_NULL},  /* S_IFOG */
    {32769, 27, 6, 144, ACT_NULL},  /* S_IFOG01 */
    {32768, 27, 6, 145, ACT_NULL},  /* S_IFOG02 */
    {32769, 27, 6, 146, ACT_NULL},  /* S_IFOG2 */
    {32770, 27, 6, 147, ACT_NULL},  /* S_IFOG3 */
    {32771, 27, 6, 148, ACT_NULL},  /* S_IFOG4 */
    {32772, 27, 6, 0, ACT_NULL},  /* S_IFOG5 */
    {0, 28, -1, 0, ACT_NULL},  /* S_PLAY */
    {0, 28, 4, 151, ACT_NULL},  /* S_PLAY_RUN1 */
    {1, 28, 4, 152, ACT_NULL},  /* S_PLAY_RUN2 */
    {2, 28, 4, 153, ACT_NULL},  /* S_PLAY_RUN3 */
    {3, 28, 4, 150, ACT_NULL},  /* S_PLAY_RUN4 */
    {4, 28, 12, 149, ACT_NULL},  /* S_PLAY_ATK1 */
    {32773, 28, 6, 154, ACT_NULL},  /* S_PLAY_ATK2 */
    {6, 28, 4, 157, ACT_NULL},  /* S_PLAY_PAIN */
    {6, 28, 4, 149, A_Pain},  /* S_PLAY_PAIN2 */
    {7, 28, 10, 159, ACT_NULL},  /* S_PLAY_DIE1 */
    {8, 28, 10, 160, A_PlayerScream},  /* S_PLAY_DIE2 */
    {9, 28, 10, 161, A_Fall},  /* S_PLAY_DIE3 */
    {10, 28, 10, 162, ACT_NULL},  /* S_PLAY_DIE4 */
    {11, 28, 10, 163, ACT_NULL},  /* S_PLAY_DIE5 */
    {12, 28, 10, 164, ACT_NULL},  /* S_PLAY_DIE6 */
    {13, 28, -1, 0, ACT_NULL},  /* S_PLAY_DIE7 */
    {14, 28, 5, 166, ACT_NULL},  /* S_PLAY_XDIE1 */
    {15, 28, 5, 167, A_XScream},  /* S_PLAY_XDIE2 */
    {16, 28, 5, 168, A_Fall},  /* S_PLAY_XDIE3 */
    {17, 28, 5, 169, ACT_NULL},  /* S_PLAY_XDIE4 */
    {18, 28, 5, 170, ACT_NULL},  /* S_PLAY_XDIE5 */
    {19, 28, 5, 171, ACT_NULL},  /* S_PLAY_XDIE6 */
    {20, 28, 5, 172, ACT_NULL},  /* S_PLAY_XDIE7 */
    {21, 28, 5, 173, ACT_NULL},  /* S_PLAY_XDIE8 */
    {22, 28, -1, 0, ACT_NULL},  /* S_PLAY_XDIE9 */
    {0, 29, 10, 175, A_Look},  /* S_POSS_STND */
    {1, 29, 10, 174, A_Look},  /* S_POSS_STND2 */
    {0, 29, 4, 177, A_Chase},  /* S_POSS_RUN1 */
    {0, 29, 4, 178, A_Chase},  /* S_POSS_RUN2 */
    {1, 29, 4, 179, A_Chase},  /* S_POSS_RUN3 */
    {1, 29, 4, 180, A_Chase},  /* S_POSS_RUN4 */
    {2, 29, 4, 181, A_Chase},  /* S_POSS_RUN5 */
    {2, 29, 4, 182, A_Chase},  /* S_POSS_RUN6 */
    {3, 29, 4, 183, A_Chase},  /* S_POSS_RUN7 */
    {3, 29, 4, 176, A_Chase},  /* S_POSS_RUN8 */
    {4, 29, 10, 185, A_FaceTarget},  /* S_POSS_ATK1 */
    {5, 29, 8, 186, A_PosAttack},  /* S_POSS_ATK2 */
    {4, 29, 8, 176, ACT_NULL},  /* S_POSS_ATK3 */
    {6, 29, 3, 188, ACT_NULL},  /* S_POSS_PAIN */
    {6, 29, 3, 176, A_Pain},  /* S_POSS_PAIN2 */
    {7, 29, 5, 190, ACT_NULL},  /* S_POSS_DIE1 */
    {8, 29, 5, 191, A_Scream},  /* S_POSS_DIE2 */
    {9, 29, 5, 192, A_Fall},  /* S_POSS_DIE3 */
    {10, 29, 5, 193, ACT_NULL},  /* S_POSS_DIE4 */
    {11, 29, -1, 0, ACT_NULL},  /* S_POSS_DIE5 */
    {12, 29, 5, 195, ACT_NULL},  /* S_POSS_XDIE1 */
    {13, 29, 5, 196, A_XScream},  /* S_POSS_XDIE2 */
    {14, 29, 5, 197, A_Fall},  /* S_POSS_XDIE3 */
    {15, 29, 5, 198, ACT_NULL},  /* S_POSS_XDIE4 */
    {16, 29, 5, 199, ACT_NULL},  /* S_POSS_XDIE5 */
    {17, 29, 5, 200, ACT_NULL},  /* S_POSS_XDIE6 */
    {18, 29, 5, 201, ACT_NULL},  /* S_POSS_XDIE7 */
    {19, 29, 5, 202, ACT_NULL},  /* S_POSS_XDIE8 */
    {20, 29, -1, 0, ACT_NULL},  /* S_POSS_XDIE9 */
    {10, 29, 5, 204, ACT_NULL},  /* S_POSS_RAISE1 */
    {9, 29, 5, 205, ACT_NULL},  /* S_POSS_RAISE2 */
    {8, 29, 5, 206, ACT_NULL},  /* S_POSS_RAISE3 */
    {7, 29, 5, 176, ACT_NULL},  /* S_POSS_RAISE4 */
    {0, 30, 10, 208, A_Look},  /* S_SPOS_STND */
    {1, 30, 10, 207, A_Look},  /* S_SPOS_STND2 */
    {0, 30, 3, 210, A_Chase},  /* S_SPOS_RUN1 */
    {0, 30, 3, 211, A_Chase},  /* S_SPOS_RUN2 */
    {1, 30, 3, 212, A_Chase},  /* S_SPOS_RUN3 */
    {1, 30, 3, 213, A_Chase},  /* S_SPOS_RUN4 */
    {2, 30, 3, 214, A_Chase},  /* S_SPOS_RUN5 */
    {2, 30, 3, 215, A_Chase},  /* S_SPOS_RUN6 */
    {3, 30, 3, 216, A_Chase},  /* S_SPOS_RUN7 */
    {3, 30, 3, 209, A_Chase},  /* S_SPOS_RUN8 */
    {4, 30, 10, 218, A_FaceTarget},  /* S_SPOS_ATK1 */
    {32773, 30, 10, 219, A_SPosAttack},  /* S_SPOS_ATK2 */
    {4, 30, 10, 209, ACT_NULL},  /* S_SPOS_ATK3 */
    {6, 30, 3, 221, ACT_NULL},  /* S_SPOS_PAIN */
    {6, 30, 3, 209, A_Pain},  /* S_SPOS_PAIN2 */
    {7, 30, 5, 223, ACT_NULL},  /* S_SPOS_DIE1 */
    {8, 30, 5, 224, A_Scream},  /* S_SPOS_DIE2 */
    {9, 30, 5, 225, A_Fall},  /* S_SPOS_DIE3 */
    {10, 30, 5, 226, ACT_NULL},  /* S_SPOS_DIE4 */
    {11, 30, -1, 0, ACT_NULL},  /* S_SPOS_DIE5 */
    {12, 30, 5, 228, ACT_NULL},  /* S_SPOS_XDIE1 */
    {13, 30, 5, 229, A_XScream},  /* S_SPOS_XDIE2 */
    {14, 30, 5, 230, A_Fall},  /* S_SPOS_XDIE3 */
    {15, 30, 5, 231, ACT_NULL},  /* S_SPOS_XDIE4 */
    {16, 30, 5, 232, ACT_NULL},  /* S_SPOS_XDIE5 */
    {17, 30, 5, 233, ACT_NULL},  /* S_SPOS_XDIE6 */
    {18, 30, 5, 234, ACT_NULL},  /* S_SPOS_XDIE7 */
    {19, 30, 5, 235, ACT_NULL},  /* S_SPOS_XDIE8 */
    {20, 30, -1, 0, ACT_NULL},  /* S_SPOS_XDIE9 */
    {11, 30, 5, 237, ACT_NULL},  /* S_SPOS_RAISE1 */
    {10, 30, 5, 238, ACT_NULL},  /* S_SPOS_RAISE2 */
    {9, 30, 5, 239, ACT_NULL},  /* S_SPOS_RAISE3 */
    {8, 30, 5, 240, ACT_NULL},  /* S_SPOS_RAISE4 */
    {7, 30, 5, 209, ACT_NULL},  /* S_SPOS_RAISE5 */
    {0, 31, 10, 242, A_Look},  /* S_VILE_STND */
    {1, 31, 10, 241, A_Look},  /* S_VILE_STND2 */
    {0, 31, 2, 244, A_VileChase},  /* S_VILE_RUN1 */
    {0, 31, 2, 245, A_VileChase},  /* S_VILE_RUN2 */
    {1, 31, 2, 246, A_VileChase},  /* S_VILE_RUN3 */
    {1, 31, 2, 247, A_VileChase},  /* S_VILE_RUN4 */
    {2, 31, 2, 248, A_VileChase},  /* S_VILE_RUN5 */
    {2, 31, 2, 249, A_VileChase},  /* S_VILE_RUN6 */
    {3, 31, 2, 250, A_VileChase},  /* S_VILE_RUN7 */
    {3, 31, 2, 251, A_VileChase},  /* S_VILE_RUN8 */
    {4, 31, 2, 252, A_VileChase},  /* S_VILE_RUN9 */
    {4, 31, 2, 253, A_VileChase},  /* S_VILE_RUN10 */
    {5, 31, 2, 254, A_VileChase},  /* S_VILE_RUN11 */
    {5, 31, 2, 243, A_VileChase},  /* S_VILE_RUN12 */
    {32774, 31, 0, 256, A_VileStart},  /* S_VILE_ATK1 */
    {32774, 31, 10, 257, A_FaceTarget},  /* S_VILE_ATK2 */
    {32775, 31, 8, 258, A_VileTarget},  /* S_VILE_ATK3 */
    {32776, 31, 8, 259, A_FaceTarget},  /* S_VILE_ATK4 */
    {32777, 31, 8, 260, A_FaceTarget},  /* S_VILE_ATK5 */
    {32778, 31, 8, 261, A_FaceTarget},  /* S_VILE_ATK6 */
    {32779, 31, 8, 262, A_FaceTarget},  /* S_VILE_ATK7 */
    {32780, 31, 8, 263, A_FaceTarget},  /* S_VILE_ATK8 */
    {32781, 31, 8, 264, A_FaceTarget},  /* S_VILE_ATK9 */
    {32782, 31, 8, 265, A_VileAttack},  /* S_VILE_ATK10 */
    {32783, 31, 20, 243, ACT_NULL},  /* S_VILE_ATK11 */
    {32794, 31, 10, 267, ACT_NULL},  /* S_VILE_HEAL1 */
    {32795, 31, 10, 268, ACT_NULL},  /* S_VILE_HEAL2 */
    {32796, 31, 10, 243, ACT_NULL},  /* S_VILE_HEAL3 */
    {16, 31, 5, 270, ACT_NULL},  /* S_VILE_PAIN */
    {16, 31, 5, 243, A_Pain},  /* S_VILE_PAIN2 */
    {16, 31, 7, 272, ACT_NULL},  /* S_VILE_DIE1 */
    {17, 31, 7, 273, A_Scream},  /* S_VILE_DIE2 */
    {18, 31, 7, 274, A_Fall},  /* S_VILE_DIE3 */
    {19, 31, 7, 275, ACT_NULL},  /* S_VILE_DIE4 */
    {20, 31, 7, 276, ACT_NULL},  /* S_VILE_DIE5 */
    {21, 31, 7, 277, ACT_NULL},  /* S_VILE_DIE6 */
    {22, 31, 7, 278, ACT_NULL},  /* S_VILE_DIE7 */
    {23, 31, 5, 279, ACT_NULL},  /* S_VILE_DIE8 */
    {24, 31, 5, 280, ACT_NULL},  /* S_VILE_DIE9 */
    {25, 31, -1, 0, ACT_NULL},  /* S_VILE_DIE10 */
    {32768, 32, 2, 282, A_StartFire},  /* S_FIRE1 */
    {32769, 32, 2, 283, A_Fire},  /* S_FIRE2 */
    {32768, 32, 2, 284, A_Fire},  /* S_FIRE3 */
    {32769, 32, 2, 285, A_Fire},  /* S_FIRE4 */
    {32770, 32, 2, 286, A_FireCrackle},  /* S_FIRE5 */
    {32769, 32, 2, 287, A_Fire},  /* S_FIRE6 */
    {32770, 32, 2, 288, A_Fire},  /* S_FIRE7 */
    {32769, 32, 2, 289, A_Fire},  /* S_FIRE8 */
    {32770, 32, 2, 290, A_Fire},  /* S_FIRE9 */
    {32771, 32, 2, 291, A_Fire},  /* S_FIRE10 */
    {32770, 32, 2, 292, A_Fire},  /* S_FIRE11 */
    {32771, 32, 2, 293, A_Fire},  /* S_FIRE12 */
    {32770, 32, 2, 294, A_Fire},  /* S_FIRE13 */
    {32771, 32, 2, 295, A_Fire},  /* S_FIRE14 */
    {32772, 32, 2, 296, A_Fire},  /* S_FIRE15 */
    {32771, 32, 2, 297, A_Fire},  /* S_FIRE16 */
    {32772, 32, 2, 298, A_Fire},  /* S_FIRE17 */
    {32771, 32, 2, 299, A_Fire},  /* S_FIRE18 */
    {32772, 32, 2, 300, A_FireCrackle},  /* S_FIRE19 */
    {32773, 32, 2, 301, A_Fire},  /* S_FIRE20 */
    {32772, 32, 2, 302, A_Fire},  /* S_FIRE21 */
    {32773, 32, 2, 303, A_Fire},  /* S_FIRE22 */
    {32772, 32, 2, 304, A_Fire},  /* S_FIRE23 */
    {32773, 32, 2, 305, A_Fire},  /* S_FIRE24 */
    {32774, 32, 2, 306, A_Fire},  /* S_FIRE25 */
    {32775, 32, 2, 307, A_Fire},  /* S_FIRE26 */
    {32774, 32, 2, 308, A_Fire},  /* S_FIRE27 */
    {32775, 32, 2, 309, A_Fire},  /* S_FIRE28 */
    {32774, 32, 2, 310, A_Fire},  /* S_FIRE29 */
    {32775, 32, 2, 0, A_Fire},  /* S_FIRE30 */
    {1, 17, 4, 312, ACT_NULL},  /* S_SMOKE1 */
    {2, 17, 4, 313, ACT_NULL},  /* S_SMOKE2 */
    {1, 17, 4, 314, ACT_NULL},  /* S_SMOKE3 */
    {2, 17, 4, 315, ACT_NULL},  /* S_SMOKE4 */
    {3, 17, 4, 0, ACT_NULL},  /* S_SMOKE5 */
    {32768, 33, 2, 317, A_Tracer},  /* S_TRACER */
    {32769, 33, 2, 316, A_Tracer},  /* S_TRACER2 */
    {32768, 34, 8, 319, ACT_NULL},  /* S_TRACEEXP1 */
    {32769, 34, 6, 320, ACT_NULL},  /* S_TRACEEXP2 */
    {32770, 34, 4, 0, ACT_NULL},  /* S_TRACEEXP3 */
    {0, 35, 10, 322, A_Look},  /* S_SKEL_STND */
    {1, 35, 10, 321, A_Look},  /* S_SKEL_STND2 */
    {0, 35, 2, 324, A_Chase},  /* S_SKEL_RUN1 */
    {0, 35, 2, 325, A_Chase},  /* S_SKEL_RUN2 */
    {1, 35, 2, 326, A_Chase},  /* S_SKEL_RUN3 */
    {1, 35, 2, 327, A_Chase},  /* S_SKEL_RUN4 */
    {2, 35, 2, 328, A_Chase},  /* S_SKEL_RUN5 */
    {2, 35, 2, 329, A_Chase},  /* S_SKEL_RUN6 */
    {3, 35, 2, 330, A_Chase},  /* S_SKEL_RUN7 */
    {3, 35, 2, 331, A_Chase},  /* S_SKEL_RUN8 */
    {4, 35, 2, 332, A_Chase},  /* S_SKEL_RUN9 */
    {4, 35, 2, 333, A_Chase},  /* S_SKEL_RUN10 */
    {5, 35, 2, 334, A_Chase},  /* S_SKEL_RUN11 */
    {5, 35, 2, 323, A_Chase},  /* S_SKEL_RUN12 */
    {6, 35, 0, 336, A_FaceTarget},  /* S_SKEL_FIST1 */
    {6, 35, 6, 337, A_SkelWhoosh},  /* S_SKEL_FIST2 */
    {7, 35, 6, 338, A_FaceTarget},  /* S_SKEL_FIST3 */
    {8, 35, 6, 323, A_SkelFist},  /* S_SKEL_FIST4 */
    {32777, 35, 0, 340, A_FaceTarget},  /* S_SKEL_MISS1 */
    {32777, 35, 10, 341, A_FaceTarget},  /* S_SKEL_MISS2 */
    {10, 35, 10, 342, A_SkelMissile},  /* S_SKEL_MISS3 */
    {10, 35, 10, 323, A_FaceTarget},  /* S_SKEL_MISS4 */
    {11, 35, 5, 344, ACT_NULL},  /* S_SKEL_PAIN */
    {11, 35, 5, 323, A_Pain},  /* S_SKEL_PAIN2 */
    {11, 35, 7, 346, ACT_NULL},  /* S_SKEL_DIE1 */
    {12, 35, 7, 347, ACT_NULL},  /* S_SKEL_DIE2 */
    {13, 35, 7, 348, A_Scream},  /* S_SKEL_DIE3 */
    {14, 35, 7, 349, A_Fall},  /* S_SKEL_DIE4 */
    {15, 35, 7, 350, ACT_NULL},  /* S_SKEL_DIE5 */
    {16, 35, -1, 0, ACT_NULL},  /* S_SKEL_DIE6 */
    {16, 35, 5, 352, ACT_NULL},  /* S_SKEL_RAISE1 */
    {15, 35, 5, 353, ACT_NULL},  /* S_SKEL_RAISE2 */
    {14, 35, 5, 354, ACT_NULL},  /* S_SKEL_RAISE3 */
    {13, 35, 5, 355, ACT_NULL},  /* S_SKEL_RAISE4 */
    {12, 35, 5, 356, ACT_NULL},  /* S_SKEL_RAISE5 */
    {11, 35, 5, 323, ACT_NULL},  /* S_SKEL_RAISE6 */
    {32768, 36, 4, 358, ACT_NULL},  /* S_FATSHOT1 */
    {32769, 36, 4, 357, ACT_NULL},  /* S_FATSHOT2 */
    {32769, 22, 8, 360, ACT_NULL},  /* S_FATSHOTX1 */
    {32770, 22, 6, 361, ACT_NULL},  /* S_FATSHOTX2 */
    {32771, 22, 4, 0, ACT_NULL},  /* S_FATSHOTX3 */
    {0, 37, 15, 363, A_Look},  /* S_FATT_STND */
    {1, 37, 15, 362, A_Look},  /* S_FATT_STND2 */
    {0, 37, 4, 365, A_Chase},  /* S_FATT_RUN1 */
    {0, 37, 4, 366, A_Chase},  /* S_FATT_RUN2 */
    {1, 37, 4, 367, A_Chase},  /* S_FATT_RUN3 */
    {1, 37, 4, 368, A_Chase},  /* S_FATT_RUN4 */
    {2, 37, 4, 369, A_Chase},  /* S_FATT_RUN5 */
    {2, 37, 4, 370, A_Chase},  /* S_FATT_RUN6 */
    {3, 37, 4, 371, A_Chase},  /* S_FATT_RUN7 */
    {3, 37, 4, 372, A_Chase},  /* S_FATT_RUN8 */
    {4, 37, 4, 373, A_Chase},  /* S_FATT_RUN9 */
    {4, 37, 4, 374, A_Chase},  /* S_FATT_RUN10 */
    {5, 37, 4, 375, A_Chase},  /* S_FATT_RUN11 */
    {5, 37, 4, 364, A_Chase},  /* S_FATT_RUN12 */
    {6, 37, 20, 377, A_FatRaise},  /* S_FATT_ATK1 */
    {32775, 37, 10, 378, A_FatAttack1},  /* S_FATT_ATK2 */
    {8, 37, 5, 379, A_FaceTarget},  /* S_FATT_ATK3 */
    {6, 37, 5, 380, A_FaceTarget},  /* S_FATT_ATK4 */
    {32775, 37, 10, 381, A_FatAttack2},  /* S_FATT_ATK5 */
    {8, 37, 5, 382, A_FaceTarget},  /* S_FATT_ATK6 */
    {6, 37, 5, 383, A_FaceTarget},  /* S_FATT_ATK7 */
    {32775, 37, 10, 384, A_FatAttack3},  /* S_FATT_ATK8 */
    {8, 37, 5, 385, A_FaceTarget},  /* S_FATT_ATK9 */
    {6, 37, 5, 364, A_FaceTarget},  /* S_FATT_ATK10 */
    {9, 37, 3, 387, ACT_NULL},  /* S_FATT_PAIN */
    {9, 37, 3, 364, A_Pain},  /* S_FATT_PAIN2 */
    {10, 37, 6, 389, ACT_NULL},  /* S_FATT_DIE1 */
    {11, 37, 6, 390, A_Scream},  /* S_FATT_DIE2 */
    {12, 37, 6, 391, A_Fall},  /* S_FATT_DIE3 */
    {13, 37, 6, 392, ACT_NULL},  /* S_FATT_DIE4 */
    {14, 37, 6, 393, ACT_NULL},  /* S_FATT_DIE5 */
    {15, 37, 6, 394, ACT_NULL},  /* S_FATT_DIE6 */
    {16, 37, 6, 395, ACT_NULL},  /* S_FATT_DIE7 */
    {17, 37, 6, 396, ACT_NULL},  /* S_FATT_DIE8 */
    {18, 37, 6, 397, ACT_NULL},  /* S_FATT_DIE9 */
    {19, 37, -1, 0, A_BossDeath},  /* S_FATT_DIE10 */
    {17, 37, 5, 399, ACT_NULL},  /* S_FATT_RAISE1 */
    {16, 37, 5, 400, ACT_NULL},  /* S_FATT_RAISE2 */
    {15, 37, 5, 401, ACT_NULL},  /* S_FATT_RAISE3 */
    {14, 37, 5, 402, ACT_NULL},  /* S_FATT_RAISE4 */
    {13, 37, 5, 403, ACT_NULL},  /* S_FATT_RAISE5 */
    {12, 37, 5, 404, ACT_NULL},  /* S_FATT_RAISE6 */
    {11, 37, 5, 405, ACT_NULL},  /* S_FATT_RAISE7 */
    {10, 37, 5, 364, ACT_NULL},  /* S_FATT_RAISE8 */
    {0, 38, 10, 407, A_Look},  /* S_CPOS_STND */
    {1, 38, 10, 406, A_Look},  /* S_CPOS_STND2 */
    {0, 38, 3, 409, A_Chase},  /* S_CPOS_RUN1 */
    {0, 38, 3, 410, A_Chase},  /* S_CPOS_RUN2 */
    {1, 38, 3, 411, A_Chase},  /* S_CPOS_RUN3 */
    {1, 38, 3, 412, A_Chase},  /* S_CPOS_RUN4 */
    {2, 38, 3, 413, A_Chase},  /* S_CPOS_RUN5 */
    {2, 38, 3, 414, A_Chase},  /* S_CPOS_RUN6 */
    {3, 38, 3, 415, A_Chase},  /* S_CPOS_RUN7 */
    {3, 38, 3, 408, A_Chase},  /* S_CPOS_RUN8 */
    {4, 38, 10, 417, A_FaceTarget},  /* S_CPOS_ATK1 */
    {32773, 38, 4, 418, A_CPosAttack},  /* S_CPOS_ATK2 */
    {32772, 38, 4, 419, A_CPosAttack},  /* S_CPOS_ATK3 */
    {5, 38, 1, 417, A_CPosRefire},  /* S_CPOS_ATK4 */
    {6, 38, 3, 421, ACT_NULL},  /* S_CPOS_PAIN */
    {6, 38, 3, 408, A_Pain},  /* S_CPOS_PAIN2 */
    {7, 38, 5, 423, ACT_NULL},  /* S_CPOS_DIE1 */
    {8, 38, 5, 424, A_Scream},  /* S_CPOS_DIE2 */
    {9, 38, 5, 425, A_Fall},  /* S_CPOS_DIE3 */
    {10, 38, 5, 426, ACT_NULL},  /* S_CPOS_DIE4 */
    {11, 38, 5, 427, ACT_NULL},  /* S_CPOS_DIE5 */
    {12, 38, 5, 428, ACT_NULL},  /* S_CPOS_DIE6 */
    {13, 38, -1, 0, ACT_NULL},  /* S_CPOS_DIE7 */
    {14, 38, 5, 430, ACT_NULL},  /* S_CPOS_XDIE1 */
    {15, 38, 5, 431, A_XScream},  /* S_CPOS_XDIE2 */
    {16, 38, 5, 432, A_Fall},  /* S_CPOS_XDIE3 */
    {17, 38, 5, 433, ACT_NULL},  /* S_CPOS_XDIE4 */
    {18, 38, 5, 434, ACT_NULL},  /* S_CPOS_XDIE5 */
    {19, 38, -1, 0, ACT_NULL},  /* S_CPOS_XDIE6 */
    {13, 38, 5, 436, ACT_NULL},  /* S_CPOS_RAISE1 */
    {12, 38, 5, 437, ACT_NULL},  /* S_CPOS_RAISE2 */
    {11, 38, 5, 438, ACT_NULL},  /* S_CPOS_RAISE3 */
    {10, 38, 5, 439, ACT_NULL},  /* S_CPOS_RAISE4 */
    {9, 38, 5, 440, ACT_NULL},  /* S_CPOS_RAISE5 */
    {8, 38, 5, 441, ACT_NULL},  /* S_CPOS_RAISE6 */
    {7, 38, 5, 408, ACT_NULL},  /* S_CPOS_RAISE7 */
    {0, 0, 10, 443, A_Look},  /* S_TROO_STND */
    {1, 0, 10, 442, A_Look},  /* S_TROO_STND2 */
    {0, 0, 3, 445, A_Chase},  /* S_TROO_RUN1 */
    {0, 0, 3, 446, A_Chase},  /* S_TROO_RUN2 */
    {1, 0, 3, 447, A_Chase},  /* S_TROO_RUN3 */
    {1, 0, 3, 448, A_Chase},  /* S_TROO_RUN4 */
    {2, 0, 3, 449, A_Chase},  /* S_TROO_RUN5 */
    {2, 0, 3, 450, A_Chase},  /* S_TROO_RUN6 */
    {3, 0, 3, 451, A_Chase},  /* S_TROO_RUN7 */
    {3, 0, 3, 444, A_Chase},  /* S_TROO_RUN8 */
    {4, 0, 8, 453, A_FaceTarget},  /* S_TROO_ATK1 */
    {5, 0, 8, 454, A_FaceTarget},  /* S_TROO_ATK2 */
    {6, 0, 6, 444, A_TroopAttack},  /* S_TROO_ATK3 */
    {7, 0, 2, 456, ACT_NULL},  /* S_TROO_PAIN */
    {7, 0, 2, 444, A_Pain},  /* S_TROO_PAIN2 */
    {8, 0, 8, 458, ACT_NULL},  /* S_TROO_DIE1 */
    {9, 0, 8, 459, A_Scream},  /* S_TROO_DIE2 */
    {10, 0, 6, 460, ACT_NULL},  /* S_TROO_DIE3 */
    {11, 0, 6, 461, A_Fall},  /* S_TROO_DIE4 */
    {12, 0, -1, 0, ACT_NULL},  /* S_TROO_DIE5 */
    {13, 0, 5, 463, ACT_NULL},  /* S_TROO_XDIE1 */
    {14, 0, 5, 464, A_XScream},  /* S_TROO_XDIE2 */
    {15, 0, 5, 465, ACT_NULL},  /* S_TROO_XDIE3 */
    {16, 0, 5, 466, A_Fall},  /* S_TROO_XDIE4 */
    {17, 0, 5, 467, ACT_NULL},  /* S_TROO_XDIE5 */
    {18, 0, 5, 468, ACT_NULL},  /* S_TROO_XDIE6 */
    {19, 0, 5, 469, ACT_NULL},  /* S_TROO_XDIE7 */
    {20, 0, -1, 0, ACT_NULL},  /* S_TROO_XDIE8 */
    {12, 0, 8, 471, ACT_NULL},  /* S_TROO_RAISE1 */
    {11, 0, 8, 472, ACT_NULL},  /* S_TROO_RAISE2 */
    {10, 0, 6, 473, ACT_NULL},  /* S_TROO_RAISE3 */
    {9, 0, 6, 474, ACT_NULL},  /* S_TROO_RAISE4 */
    {8, 0, 6, 444, ACT_NULL},  /* S_TROO_RAISE5 */
    {0, 39, 10, 476, A_Look},  /* S_SARG_STND */
    {1, 39, 10, 475, A_Look},  /* S_SARG_STND2 */
    {0, 39, 2, 478, A_Chase},  /* S_SARG_RUN1 */
    {0, 39, 2, 479, A_Chase},  /* S_SARG_RUN2 */
    {1, 39, 2, 480, A_Chase},  /* S_SARG_RUN3 */
    {1, 39, 2, 481, A_Chase},  /* S_SARG_RUN4 */
    {2, 39, 2, 482, A_Chase},  /* S_SARG_RUN5 */
    {2, 39, 2, 483, A_Chase},  /* S_SARG_RUN6 */
    {3, 39, 2, 484, A_Chase},  /* S_SARG_RUN7 */
    {3, 39, 2, 477, A_Chase},  /* S_SARG_RUN8 */
    {4, 39, 8, 486, A_FaceTarget},  /* S_SARG_ATK1 */
    {5, 39, 8, 487, A_FaceTarget},  /* S_SARG_ATK2 */
    {6, 39, 8, 477, A_SargAttack},  /* S_SARG_ATK3 */
    {7, 39, 2, 489, ACT_NULL},  /* S_SARG_PAIN */
    {7, 39, 2, 477, A_Pain},  /* S_SARG_PAIN2 */
    {8, 39, 8, 491, ACT_NULL},  /* S_SARG_DIE1 */
    {9, 39, 8, 492, A_Scream},  /* S_SARG_DIE2 */
    {10, 39, 4, 493, ACT_NULL},  /* S_SARG_DIE3 */
    {11, 39, 4, 494, A_Fall},  /* S_SARG_DIE4 */
    {12, 39, 4, 495, ACT_NULL},  /* S_SARG_DIE5 */
    {13, 39, -1, 0, ACT_NULL},  /* S_SARG_DIE6 */
    {13, 39, 5, 497, ACT_NULL},  /* S_SARG_RAISE1 */
    {12, 39, 5, 498, ACT_NULL},  /* S_SARG_RAISE2 */
    {11, 39, 5, 499, ACT_NULL},  /* S_SARG_RAISE3 */
    {10, 39, 5, 500, ACT_NULL},  /* S_SARG_RAISE4 */
    {9, 39, 5, 501, ACT_NULL},  /* S_SARG_RAISE5 */
    {8, 39, 5, 477, ACT_NULL},  /* S_SARG_RAISE6 */
    {0, 40, 10, 502, A_Look},  /* S_HEAD_STND */
    {0, 40, 3, 503, A_Chase},  /* S_HEAD_RUN1 */
    {1, 40, 5, 505, A_FaceTarget},  /* S_HEAD_ATK1 */
    {2, 40, 5, 506, A_FaceTarget},  /* S_HEAD_ATK2 */
    {32771, 40, 5, 503, A_HeadAttack},  /* S_HEAD_ATK3 */
    {4, 40, 3, 508, ACT_NULL},  /* S_HEAD_PAIN */
    {4, 40, 3, 509, A_Pain},  /* S_HEAD_PAIN2 */
    {5, 40, 6, 503, ACT_NULL},  /* S_HEAD_PAIN3 */
    {6, 40, 8, 511, ACT_NULL},  /* S_HEAD_DIE1 */
    {7, 40, 8, 512, A_Scream},  /* S_HEAD_DIE2 */
    {8, 40, 8, 513, ACT_NULL},  /* S_HEAD_DIE3 */
    {9, 40, 8, 514, ACT_NULL},  /* S_HEAD_DIE4 */
    {10, 40, 8, 515, A_Fall},  /* S_HEAD_DIE5 */
    {11, 40, -1, 0, ACT_NULL},  /* S_HEAD_DIE6 */
    {11, 40, 8, 517, ACT_NULL},  /* S_HEAD_RAISE1 */
    {10, 40, 8, 518, ACT_NULL},  /* S_HEAD_RAISE2 */
    {9, 40, 8, 519, ACT_NULL},  /* S_HEAD_RAISE3 */
    {8, 40, 8, 520, ACT_NULL},  /* S_HEAD_RAISE4 */
    {7, 40, 8, 521, ACT_NULL},  /* S_HEAD_RAISE5 */
    {6, 40, 8, 503, ACT_NULL},  /* S_HEAD_RAISE6 */
    {32768, 41, 4, 523, ACT_NULL},  /* S_BRBALL1 */
    {32769, 41, 4, 522, ACT_NULL},  /* S_BRBALL2 */
    {32770, 41, 6, 525, ACT_NULL},  /* S_BRBALLX1 */
    {32771, 41, 6, 526, ACT_NULL},  /* S_BRBALLX2 */
    {32772, 41, 6, 0, ACT_NULL},  /* S_BRBALLX3 */
    {0, 42, 10, 528, A_Look},  /* S_BOSS_STND */
    {1, 42, 10, 527, A_Look},  /* S_BOSS_STND2 */
    {0, 42, 3, 530, A_Chase},  /* S_BOSS_RUN1 */
    {0, 42, 3, 531, A_Chase},  /* S_BOSS_RUN2 */
    {1, 42, 3, 532, A_Chase},  /* S_BOSS_RUN3 */
    {1, 42, 3, 533, A_Chase},  /* S_BOSS_RUN4 */
    {2, 42, 3, 534, A_Chase},  /* S_BOSS_RUN5 */
    {2, 42, 3, 535, A_Chase},  /* S_BOSS_RUN6 */
    {3, 42, 3, 536, A_Chase},  /* S_BOSS_RUN7 */
    {3, 42, 3, 529, A_Chase},  /* S_BOSS_RUN8 */
    {4, 42, 8, 538, A_FaceTarget},  /* S_BOSS_ATK1 */
    {5, 42, 8, 539, A_FaceTarget},  /* S_BOSS_ATK2 */
    {6, 42, 8, 529, A_BruisAttack},  /* S_BOSS_ATK3 */
    {7, 42, 2, 541, ACT_NULL},  /* S_BOSS_PAIN */
    {7, 42, 2, 529, A_Pain},  /* S_BOSS_PAIN2 */
    {8, 42, 8, 543, ACT_NULL},  /* S_BOSS_DIE1 */
    {9, 42, 8, 544, A_Scream},  /* S_BOSS_DIE2 */
    {10, 42, 8, 545, ACT_NULL},  /* S_BOSS_DIE3 */
    {11, 42, 8, 546, A_Fall},  /* S_BOSS_DIE4 */
    {12, 42, 8, 547, ACT_NULL},  /* S_BOSS_DIE5 */
    {13, 42, 8, 548, ACT_NULL},  /* S_BOSS_DIE6 */
    {14, 42, -1, 0, A_BossDeath},  /* S_BOSS_DIE7 */
    {14, 42, 8, 550, ACT_NULL},  /* S_BOSS_RAISE1 */
    {13, 42, 8, 551, ACT_NULL},  /* S_BOSS_RAISE2 */
    {12, 42, 8, 552, ACT_NULL},  /* S_BOSS_RAISE3 */
    {11, 42, 8, 553, ACT_NULL},  /* S_BOSS_RAISE4 */
    {10, 42, 8, 554, ACT_NULL},  /* S_BOSS_RAISE5 */
    {9, 42, 8, 555, ACT_NULL},  /* S_BOSS_RAISE6 */
    {8, 42, 8, 529, ACT_NULL},  /* S_BOSS_RAISE7 */
    {0, 43, 10, 557, A_Look},  /* S_BOS2_STND */
    {1, 43, 10, 556, A_Look},  /* S_BOS2_STND2 */
    {0, 43, 3, 559, A_Chase},  /* S_BOS2_RUN1 */
    {0, 43, 3, 560, A_Chase},  /* S_BOS2_RUN2 */
    {1, 43, 3, 561, A_Chase},  /* S_BOS2_RUN3 */
    {1, 43, 3, 562, A_Chase},  /* S_BOS2_RUN4 */
    {2, 43, 3, 563, A_Chase},  /* S_BOS2_RUN5 */
    {2, 43, 3, 564, A_Chase},  /* S_BOS2_RUN6 */
    {3, 43, 3, 565, A_Chase},  /* S_BOS2_RUN7 */
    {3, 43, 3, 558, A_Chase},  /* S_BOS2_RUN8 */
    {4, 43, 8, 567, A_FaceTarget},  /* S_BOS2_ATK1 */
    {5, 43, 8, 568, A_FaceTarget},  /* S_BOS2_ATK2 */
    {6, 43, 8, 558, A_BruisAttack},  /* S_BOS2_ATK3 */
    {7, 43, 2, 570, ACT_NULL},  /* S_BOS2_PAIN */
    {7, 43, 2, 558, A_Pain},  /* S_BOS2_PAIN2 */
    {8, 43, 8, 572, ACT_NULL},  /* S_BOS2_DIE1 */
    {9, 43, 8, 573, A_Scream},  /* S_BOS2_DIE2 */
    {10, 43, 8, 574, ACT_NULL},  /* S_BOS2_DIE3 */
    {11, 43, 8, 575, A_Fall},  /* S_BOS2_DIE4 */
    {12, 43, 8, 576, ACT_NULL},  /* S_BOS2_DIE5 */
    {13, 43, 8, 577, ACT_NULL},  /* S_BOS2_DIE6 */
    {14, 43, -1, 0, ACT_NULL},  /* S_BOS2_DIE7 */
    {14, 43, 8, 579, ACT_NULL},  /* S_BOS2_RAISE1 */
    {13, 43, 8, 580, ACT_NULL},  /* S_BOS2_RAISE2 */
    {12, 43, 8, 581, ACT_NULL},  /* S_BOS2_RAISE3 */
    {11, 43, 8, 582, ACT_NULL},  /* S_BOS2_RAISE4 */
    {10, 43, 8, 583, ACT_NULL},  /* S_BOS2_RAISE5 */
    {9, 43, 8, 584, ACT_NULL},  /* S_BOS2_RAISE6 */
    {8, 43, 8, 558, ACT_NULL},  /* S_BOS2_RAISE7 */
    {32768, 44, 10, 586, A_Look},  /* S_SKULL_STND */
    {32769, 44, 10, 585, A_Look},  /* S_SKULL_STND2 */
    {32768, 44, 6, 588, A_Chase},  /* S_SKULL_RUN1 */
    {32769, 44, 6, 587, A_Chase},  /* S_SKULL_RUN2 */
    {32770, 44, 10, 590, A_FaceTarget},  /* S_SKULL_ATK1 */
    {32771, 44, 4, 591, A_SkullAttack},  /* S_SKULL_ATK2 */
    {32770, 44, 4, 592, ACT_NULL},  /* S_SKULL_ATK3 */
    {32771, 44, 4, 591, ACT_NULL},  /* S_SKULL_ATK4 */
    {32772, 44, 3, 594, ACT_NULL},  /* S_SKULL_PAIN */
    {32772, 44, 3, 587, A_Pain},  /* S_SKULL_PAIN2 */
    {32773, 44, 6, 596, ACT_NULL},  /* S_SKULL_DIE1 */
    {32774, 44, 6, 597, A_Scream},  /* S_SKULL_DIE2 */
    {32775, 44, 6, 598, ACT_NULL},  /* S_SKULL_DIE3 */
    {32776, 44, 6, 599, A_Fall},  /* S_SKULL_DIE4 */
    {9, 44, 6, 600, ACT_NULL},  /* S_SKULL_DIE5 */
    {10, 44, 6, 0, ACT_NULL},  /* S_SKULL_DIE6 */
    {0, 45, 10, 602, A_Look},  /* S_SPID_STND */
    {1, 45, 10, 601, A_Look},  /* S_SPID_STND2 */
    {0, 45, 3, 604, A_Metal},  /* S_SPID_RUN1 */
    {0, 45, 3, 605, A_Chase},  /* S_SPID_RUN2 */
    {1, 45, 3, 606, A_Chase},  /* S_SPID_RUN3 */
    {1, 45, 3, 607, A_Chase},  /* S_SPID_RUN4 */
    {2, 45, 3, 608, A_Metal},  /* S_SPID_RUN5 */
    {2, 45, 3, 609, A_Chase},  /* S_SPID_RUN6 */
    {3, 45, 3, 610, A_Chase},  /* S_SPID_RUN7 */
    {3, 45, 3, 611, A_Chase},  /* S_SPID_RUN8 */
    {4, 45, 3, 612, A_Metal},  /* S_SPID_RUN9 */
    {4, 45, 3, 613, A_Chase},  /* S_SPID_RUN10 */
    {5, 45, 3, 614, A_Chase},  /* S_SPID_RUN11 */
    {5, 45, 3, 603, A_Chase},  /* S_SPID_RUN12 */
    {32768, 45, 20, 616, A_FaceTarget},  /* S_SPID_ATK1 */
    {32774, 45, 4, 617, A_SPosAttack},  /* S_SPID_ATK2 */
    {32775, 45, 4, 618, A_SPosAttack},  /* S_SPID_ATK3 */
    {32775, 45, 1, 616, A_SpidRefire},  /* S_SPID_ATK4 */
    {8, 45, 3, 620, ACT_NULL},  /* S_SPID_PAIN */
    {8, 45, 3, 603, A_Pain},  /* S_SPID_PAIN2 */
    {9, 45, 20, 622, A_Scream},  /* S_SPID_DIE1 */
    {10, 45, 10, 623, A_Fall},  /* S_SPID_DIE2 */
    {11, 45, 10, 624, ACT_NULL},  /* S_SPID_DIE3 */
    {12, 45, 10, 625, ACT_NULL},  /* S_SPID_DIE4 */
    {13, 45, 10, 626, ACT_NULL},  /* S_SPID_DIE5 */
    {14, 45, 10, 627, ACT_NULL},  /* S_SPID_DIE6 */
    {15, 45, 10, 628, ACT_NULL},  /* S_SPID_DIE7 */
    {16, 45, 10, 629, ACT_NULL},  /* S_SPID_DIE8 */
    {17, 45, 10, 630, ACT_NULL},  /* S_SPID_DIE9 */
    {18, 45, 30, 631, ACT_NULL},  /* S_SPID_DIE10 */
    {18, 45, -1, 0, A_BossDeath},  /* S_SPID_DIE11 */
    {0, 46, 10, 633, A_Look},  /* S_BSPI_STND */
    {1, 46, 10, 632, A_Look},  /* S_BSPI_STND2 */
    {0, 46, 20, 635, ACT_NULL},  /* S_BSPI_SIGHT */
    {0, 46, 3, 636, A_BabyMetal},  /* S_BSPI_RUN1 */
    {0, 46, 3, 637, A_Chase},  /* S_BSPI_RUN2 */
    {1, 46, 3, 638, A_Chase},  /* S_BSPI_RUN3 */
    {1, 46, 3, 639, A_Chase},  /* S_BSPI_RUN4 */
    {2, 46, 3, 640, A_Chase},  /* S_BSPI_RUN5 */
    {2, 46, 3, 641, A_Chase},  /* S_BSPI_RUN6 */
    {3, 46, 3, 642, A_BabyMetal},  /* S_BSPI_RUN7 */
    {3, 46, 3, 643, A_Chase},  /* S_BSPI_RUN8 */
    {4, 46, 3, 644, A_Chase},  /* S_BSPI_RUN9 */
    {4, 46, 3, 645, A_Chase},  /* S_BSPI_RUN10 */
    {5, 46, 3, 646, A_Chase},  /* S_BSPI_RUN11 */
    {5, 46, 3, 635, A_Chase},  /* S_BSPI_RUN12 */
    {32768, 46, 20, 648, A_FaceTarget},  /* S_BSPI_ATK1 */
    {32774, 46, 4, 649, A_BspiAttack},  /* S_BSPI_ATK2 */
    {32775, 46, 4, 650, ACT_NULL},  /* S_BSPI_ATK3 */
    {32775, 46, 1, 648, A_SpidRefire},  /* S_BSPI_ATK4 */
    {8, 46, 3, 652, ACT_NULL},  /* S_BSPI_PAIN */
    {8, 46, 3, 635, A_Pain},  /* S_BSPI_PAIN2 */
    {9, 46, 20, 654, A_Scream},  /* S_BSPI_DIE1 */
    {10, 46, 7, 655, A_Fall},  /* S_BSPI_DIE2 */
    {11, 46, 7, 656, ACT_NULL},  /* S_BSPI_DIE3 */
    {12, 46, 7, 657, ACT_NULL},  /* S_BSPI_DIE4 */
    {13, 46, 7, 658, ACT_NULL},  /* S_BSPI_DIE5 */
    {14, 46, 7, 659, ACT_NULL},  /* S_BSPI_DIE6 */
    {15, 46, -1, 0, A_BossDeath},  /* S_BSPI_DIE7 */
    {15, 46, 5, 661, ACT_NULL},  /* S_BSPI_RAISE1 */
    {14, 46, 5, 662, ACT_NULL},  /* S_BSPI_RAISE2 */
    {13, 46, 5, 663, ACT_NULL},  /* S_BSPI_RAISE3 */
    {12, 46, 5, 664, ACT_NULL},  /* S_BSPI_RAISE4 */
    {11, 46, 5, 665, ACT_NULL},  /* S_BSPI_RAISE5 */
    {10, 46, 5, 666, ACT_NULL},  /* S_BSPI_RAISE6 */
    {9, 46, 5, 635, ACT_NULL},  /* S_BSPI_RAISE7 */
    {32768, 47, 5, 668, ACT_NULL},  /* S_ARACH_PLAZ */
    {32769, 47, 5, 667, ACT_NULL},  /* S_ARACH_PLAZ2 */
    {32768, 48, 5, 670, ACT_NULL},  /* S_ARACH_PLEX */
    {32769, 48, 5, 671, ACT_NULL},  /* S_ARACH_PLEX2 */
    {32770, 48, 5, 672, ACT_NULL},  /* S_ARACH_PLEX3 */
    {32771, 48, 5, 673, ACT_NULL},  /* S_ARACH_PLEX4 */
    {32772, 48, 5, 0, ACT_NULL},  /* S_ARACH_PLEX5 */
    {0, 49, 10, 675, A_Look},  /* S_CYBER_STND */
    {1, 49, 10, 674, A_Look},  /* S_CYBER_STND2 */
    {0, 49, 3, 677, A_Hoof},  /* S_CYBER_RUN1 */
    {0, 49, 3, 678, A_Chase},  /* S_CYBER_RUN2 */
    {1, 49, 3, 679, A_Chase},  /* S_CYBER_RUN3 */
    {1, 49, 3, 680, A_Chase},  /* S_CYBER_RUN4 */
    {2, 49, 3, 681, A_Chase},  /* S_CYBER_RUN5 */
    {2, 49, 3, 682, A_Chase},  /* S_CYBER_RUN6 */
    {3, 49, 3, 683, A_Metal},  /* S_CYBER_RUN7 */
    {3, 49, 3, 676, A_Chase},  /* S_CYBER_RUN8 */
    {4, 49, 6, 685, A_FaceTarget},  /* S_CYBER_ATK1 */
    {5, 49, 12, 686, A_CyberAttack},  /* S_CYBER_ATK2 */
    {4, 49, 12, 687, A_FaceTarget},  /* S_CYBER_ATK3 */
    {5, 49, 12, 688, A_CyberAttack},  /* S_CYBER_ATK4 */
    {4, 49, 12, 689, A_FaceTarget},  /* S_CYBER_ATK5 */
    {5, 49, 12, 676, A_CyberAttack},  /* S_CYBER_ATK6 */
    {6, 49, 10, 676, A_Pain},  /* S_CYBER_PAIN */
    {7, 49, 10, 692, ACT_NULL},  /* S_CYBER_DIE1 */
    {8, 49, 10, 693, A_Scream},  /* S_CYBER_DIE2 */
    {9, 49, 10, 694, ACT_NULL},  /* S_CYBER_DIE3 */
    {10, 49, 10, 695, ACT_NULL},  /* S_CYBER_DIE4 */
    {11, 49, 10, 696, ACT_NULL},  /* S_CYBER_DIE5 */
    {12, 49, 10, 697, A_Fall},  /* S_CYBER_DIE6 */
    {13, 49, 10, 698, ACT_NULL},  /* S_CYBER_DIE7 */
    {14, 49, 10, 699, ACT_NULL},  /* S_CYBER_DIE8 */
    {15, 49, 30, 700, ACT_NULL},  /* S_CYBER_DIE9 */
    {15, 49, -1, 0, A_BossDeath},  /* S_CYBER_DIE10 */
    {0, 50, 10, 701, A_Look},  /* S_PAIN_STND */
    {0, 50, 3, 703, A_Chase},  /* S_PAIN_RUN1 */
    {0, 50, 3, 704, A_Chase},  /* S_PAIN_RUN2 */
    {1, 50, 3, 705, A_Chase},  /* S_PAIN_RUN3 */
    {1, 50, 3, 706, A_Chase},  /* S_PAIN_RUN4 */
    {2, 50, 3, 707, A_Chase},  /* S_PAIN_RUN5 */
    {2, 50, 3, 702, A_Chase},  /* S_PAIN_RUN6 */
    {3, 50, 5, 709, A_FaceTarget},  /* S_PAIN_ATK1 */
    {4, 50, 5, 710, A_FaceTarget},  /* S_PAIN_ATK2 */
    {32773, 50, 5, 711, A_FaceTarget},  /* S_PAIN_ATK3 */
    {32773, 50, 0, 702, A_PainAttack},  /* S_PAIN_ATK4 */
    {6, 50, 6, 713, ACT_NULL},  /* S_PAIN_PAIN */
    {6, 50, 6, 702, A_Pain},  /* S_PAIN_PAIN2 */
    {32775, 50, 8, 715, ACT_NULL},  /* S_PAIN_DIE1 */
    {32776, 50, 8, 716, A_Scream},  /* S_PAIN_DIE2 */
    {32777, 50, 8, 717, ACT_NULL},  /* S_PAIN_DIE3 */
    {32778, 50, 8, 718, ACT_NULL},  /* S_PAIN_DIE4 */
    {32779, 50, 8, 719, A_PainDie},  /* S_PAIN_DIE5 */
    {32780, 50, 8, 0, ACT_NULL},  /* S_PAIN_DIE6 */
    {12, 50, 8, 721, ACT_NULL},  /* S_PAIN_RAISE1 */
    {11, 50, 8, 722, ACT_NULL},  /* S_PAIN_RAISE2 */
    {10, 50, 8, 723, ACT_NULL},  /* S_PAIN_RAISE3 */
    {9, 50, 8, 724, ACT_NULL},  /* S_PAIN_RAISE4 */
    {8, 50, 8, 725, ACT_NULL},  /* S_PAIN_RAISE5 */
    {7, 50, 8, 702, ACT_NULL},  /* S_PAIN_RAISE6 */
    {0, 51, 10, 727, A_Look},  /* S_SSWV_STND */
    {1, 51, 10, 726, A_Look},  /* S_SSWV_STND2 */
    {0, 51, 3, 729, A_Chase},  /* S_SSWV_RUN1 */
    {0, 51, 3, 730, A_Chase},  /* S_SSWV_RUN2 */
    {1, 51, 3, 731, A_Chase},  /* S_SSWV_RUN3 */
    {1, 51, 3, 732, A_Chase},  /* S_SSWV_RUN4 */
    {2, 51, 3, 733, A_Chase},  /* S_SSWV_RUN5 */
    {2, 51, 3, 734, A_Chase},  /* S_SSWV_RUN6 */
    {3, 51, 3, 735, A_Chase},  /* S_SSWV_RUN7 */
    {3, 51, 3, 728, A_Chase},  /* S_SSWV_RUN8 */
    {4, 51, 10, 737, A_FaceTarget},  /* S_SSWV_ATK1 */
    {5, 51, 10, 738, A_FaceTarget},  /* S_SSWV_ATK2 */
    {32774, 51, 4, 739, A_CPosAttack},  /* S_SSWV_ATK3 */
    {5, 51, 6, 740, A_FaceTarget},  /* S_SSWV_ATK4 */
    {32774, 51, 4, 741, A_CPosAttack},  /* S_SSWV_ATK5 */
    {5, 51, 1, 737, A_CPosRefire},  /* S_SSWV_ATK6 */
    {7, 51, 3, 743, ACT_NULL},  /* S_SSWV_PAIN */
    {7, 51, 3, 728, A_Pain},  /* S_SSWV_PAIN2 */
    {8, 51, 5, 745, ACT_NULL},  /* S_SSWV_DIE1 */
    {9, 51, 5, 746, A_Scream},  /* S_SSWV_DIE2 */
    {10, 51, 5, 747, A_Fall},  /* S_SSWV_DIE3 */
    {11, 51, 5, 748, ACT_NULL},  /* S_SSWV_DIE4 */
    {12, 51, -1, 0, ACT_NULL},  /* S_SSWV_DIE5 */
    {13, 51, 5, 750, ACT_NULL},  /* S_SSWV_XDIE1 */
    {14, 51, 5, 751, A_XScream},  /* S_SSWV_XDIE2 */
    {15, 51, 5, 752, A_Fall},  /* S_SSWV_XDIE3 */
    {16, 51, 5, 753, ACT_NULL},  /* S_SSWV_XDIE4 */
    {17, 51, 5, 754, ACT_NULL},  /* S_SSWV_XDIE5 */
    {18, 51, 5, 755, ACT_NULL},  /* S_SSWV_XDIE6 */
    {19, 51, 5, 756, ACT_NULL},  /* S_SSWV_XDIE7 */
    {20, 51, 5, 757, ACT_NULL},  /* S_SSWV_XDIE8 */
    {21, 51, -1, 0, ACT_NULL},  /* S_SSWV_XDIE9 */
    {12, 51, 5, 759, ACT_NULL},  /* S_SSWV_RAISE1 */
    {11, 51, 5, 760, ACT_NULL},  /* S_SSWV_RAISE2 */
    {10, 51, 5, 761, ACT_NULL},  /* S_SSWV_RAISE3 */
    {9, 51, 5, 762, ACT_NULL},  /* S_SSWV_RAISE4 */
    {8, 51, 5, 728, ACT_NULL},  /* S_SSWV_RAISE5 */
    {0, 52, -1, 763, ACT_NULL},  /* S_KEENSTND */
    {0, 52, 6, 765, ACT_NULL},  /* S_COMMKEEN */
    {1, 52, 6, 766, ACT_NULL},  /* S_COMMKEEN2 */
    {2, 52, 6, 767, A_Scream},  /* S_COMMKEEN3 */
    {3, 52, 6, 768, ACT_NULL},  /* S_COMMKEEN4 */
    {4, 52, 6, 769, ACT_NULL},  /* S_COMMKEEN5 */
    {5, 52, 6, 770, ACT_NULL},  /* S_COMMKEEN6 */
    {6, 52, 6, 771, ACT_NULL},  /* S_COMMKEEN7 */
    {7, 52, 6, 772, ACT_NULL},  /* S_COMMKEEN8 */
    {8, 52, 6, 773, ACT_NULL},  /* S_COMMKEEN9 */
    {9, 52, 6, 774, ACT_NULL},  /* S_COMMKEEN10 */
    {10, 52, 6, 775, A_KeenDie},  /* S_COMMKEEN11 */
    {11, 52, -1, 0, ACT_NULL},  /* S_COMMKEEN12 */
    {12, 52, 4, 777, ACT_NULL},  /* S_KEENPAIN */
    {12, 52, 8, 763, A_Pain},  /* S_KEENPAIN2 */
    {0, 53, -1, 0, ACT_NULL},  /* S_BRAIN */
    {1, 53, 36, 778, A_BrainPain},  /* S_BRAIN_PAIN */
    {0, 53, 100, 781, A_BrainScream},  /* S_BRAIN_DIE1 */
    {0, 53, 10, 782, ACT_NULL},  /* S_BRAIN_DIE2 */
    {0, 53, 10, 783, ACT_NULL},  /* S_BRAIN_DIE3 */
    {0, 53, -1, 0, A_BrainDie},  /* S_BRAIN_DIE4 */
    {0, 51, 10, 784, A_Look},  /* S_BRAINEYE */
    {0, 51, 181, 786, A_BrainAwake},  /* S_BRAINEYESEE */
    {0, 51, 150, 786, A_BrainSpit},  /* S_BRAINEYE1 */
    {32768, 54, 3, 788, A_SpawnSound},  /* S_SPAWN1 */
    {32769, 54, 3, 789, A_SpawnFly},  /* S_SPAWN2 */
    {32770, 54, 3, 790, A_SpawnFly},  /* S_SPAWN3 */
    {32771, 54, 3, 787, A_SpawnFly},  /* S_SPAWN4 */
    {32768, 32, 4, 792, A_Fire},  /* S_SPAWNFIRE1 */
    {32769, 32, 4, 793, A_Fire},  /* S_SPAWNFIRE2 */
    {32770, 32, 4, 794, A_Fire},  /* S_SPAWNFIRE3 */
    {32771, 32, 4, 795, A_Fire},  /* S_SPAWNFIRE4 */
    {32772, 32, 4, 796, A_Fire},  /* S_SPAWNFIRE5 */
    {32773, 32, 4, 797, A_Fire},  /* S_SPAWNFIRE6 */
    {32774, 32, 4, 798, A_Fire},  /* S_SPAWNFIRE7 */
    {32775, 32, 4, 0, A_Fire},  /* S_SPAWNFIRE8 */
    {32769, 22, 10, 800, ACT_NULL},  /* S_BRAINEXPLODE1 */
    {32770, 22, 10, 801, ACT_NULL},  /* S_BRAINEXPLODE2 */
    {32771, 22, 10, 0, A_BrainExplode},  /* S_BRAINEXPLODE3 */
    {0, 55, 6, 803, ACT_NULL},  /* S_ARM1 */
    {32769, 55, 7, 802, ACT_NULL},  /* S_ARM1A */
    {0, 56, 6, 805, ACT_NULL},  /* S_ARM2 */
    {32769, 56, 6, 804, ACT_NULL},  /* S_ARM2A */
    {0, 57, 6, 807, ACT_NULL},  /* S_BAR1 */
    {1, 57, 6, 806, ACT_NULL},  /* S_BAR2 */
    {32768, 58, 5, 809, ACT_NULL},  /* S_BEXP */
    {32769, 58, 5, 810, A_Scream},  /* S_BEXP2 */
    {32770, 58, 5, 811, ACT_NULL},  /* S_BEXP3 */
    {32771, 58, 10, 812, A_Explode},  /* S_BEXP4 */
    {32772, 58, 10, 0, ACT_NULL},  /* S_BEXP5 */
    {32768, 59, 4, 814, ACT_NULL},  /* S_BBAR1 */
    {32769, 59, 4, 815, ACT_NULL},  /* S_BBAR2 */
    {32770, 59, 4, 813, ACT_NULL},  /* S_BBAR3 */
    {0, 60, 6, 817, ACT_NULL},  /* S_BON1 */
    {1, 60, 6, 818, ACT_NULL},  /* S_BON1A */
    {2, 60, 6, 819, ACT_NULL},  /* S_BON1B */
    {3, 60, 6, 820, ACT_NULL},  /* S_BON1C */
    {2, 60, 6, 821, ACT_NULL},  /* S_BON1D */
    {1, 60, 6, 816, ACT_NULL},  /* S_BON1E */
    {0, 61, 6, 823, ACT_NULL},  /* S_BON2 */
    {1, 61, 6, 824, ACT_NULL},  /* S_BON2A */
    {2, 61, 6, 825, ACT_NULL},  /* S_BON2B */
    {3, 61, 6, 826, ACT_NULL},  /* S_BON2C */
    {2, 61, 6, 827, ACT_NULL},  /* S_BON2D */
    {1, 61, 6, 822, ACT_NULL},  /* S_BON2E */
    {0, 62, 10, 829, ACT_NULL},  /* S_BKEY */
    {32769, 62, 10, 828, ACT_NULL},  /* S_BKEY2 */
    {0, 63, 10, 831, ACT_NULL},  /* S_RKEY */
    {32769, 63, 10, 830, ACT_NULL},  /* S_RKEY2 */
    {0, 64, 10, 833, ACT_NULL},  /* S_YKEY */
    {32769, 64, 10, 832, ACT_NULL},  /* S_YKEY2 */
    {0, 65, 10, 835, ACT_NULL},  /* S_BSKULL */
    {32769, 65, 10, 834, ACT_NULL},  /* S_BSKULL2 */
    {0, 66, 10, 837, ACT_NULL},  /* S_RSKULL */
    {32769, 66, 10, 836, ACT_NULL},  /* S_RSKULL2 */
    {0, 67, 10, 839, ACT_NULL},  /* S_YSKULL */
    {32769, 67, 10, 838, ACT_NULL},  /* S_YSKULL2 */
    {0, 68, -1, 0, ACT_NULL},  /* S_STIM */
    {0, 69, -1, 0, ACT_NULL},  /* S_MEDI */
    {32768, 70, 6, 843, ACT_NULL},  /* S_SOUL */
    {32769, 70, 6, 844, ACT_NULL},  /* S_SOUL2 */
    {32770, 70, 6, 845, ACT_NULL},  /* S_SOUL3 */
    {32771, 70, 6, 846, ACT_NULL},  /* S_SOUL4 */
    {32770, 70, 6, 847, ACT_NULL},  /* S_SOUL5 */
    {32769, 70, 6, 842, ACT_NULL},  /* S_SOUL6 */
    {32768, 71, 6, 849, ACT_NULL},  /* S_PINV */
    {32769, 71, 6, 850, ACT_NULL},  /* S_PINV2 */
    {32770, 71, 6, 851, ACT_NULL},  /* S_PINV3 */
    {32771, 71, 6, 848, ACT_NULL},  /* S_PINV4 */
    {32768, 72, -1, 0, ACT_NULL},  /* S_PSTR */
    {32768, 73, 6, 854, ACT_NULL},  /* S_PINS */
    {32769, 73, 6, 855, ACT_NULL},  /* S_PINS2 */
    {32770, 73, 6, 856, ACT_NULL},  /* S_PINS3 */
    {32771, 73, 6, 853, ACT_NULL},  /* S_PINS4 */
    {32768, 74, 6, 858, ACT_NULL},  /* S_MEGA */
    {32769, 74, 6, 859, ACT_NULL},  /* S_MEGA2 */
    {32770, 74, 6, 860, ACT_NULL},  /* S_MEGA3 */
    {32771, 74, 6, 857, ACT_NULL},  /* S_MEGA4 */
    {32768, 75, -1, 0, ACT_NULL},  /* S_SUIT */
    {32768, 76, 6, 863, ACT_NULL},  /* S_PMAP */
    {32769, 76, 6, 864, ACT_NULL},  /* S_PMAP2 */
    {32770, 76, 6, 865, ACT_NULL},  /* S_PMAP3 */
    {32771, 76, 6, 866, ACT_NULL},  /* S_PMAP4 */
    {32770, 76, 6, 867, ACT_NULL},  /* S_PMAP5 */
    {32769, 76, 6, 862, ACT_NULL},  /* S_PMAP6 */
    {32768, 77, 6, 869, ACT_NULL},  /* S_PVIS */
    {1, 77, 6, 868, ACT_NULL},  /* S_PVIS2 */
    {0, 78, -1, 0, ACT_NULL},  /* S_CLIP */
    {0, 79, -1, 0, ACT_NULL},  /* S_AMMO */
    {0, 80, -1, 0, ACT_NULL},  /* S_ROCK */
    {0, 81, -1, 0, ACT_NULL},  /* S_BROK */
    {0, 82, -1, 0, ACT_NULL},  /* S_CELL */
    {0, 83, -1, 0, ACT_NULL},  /* S_CELP */
    {0, 84, -1, 0, ACT_NULL},  /* S_SHEL */
    {0, 85, -1, 0, ACT_NULL},  /* S_SBOX */
    {0, 86, -1, 0, ACT_NULL},  /* S_BPAK */
    {0, 87, -1, 0, ACT_NULL},  /* S_BFUG */
    {0, 88, -1, 0, ACT_NULL},  /* S_MGUN */
    {0, 89, -1, 0, ACT_NULL},  /* S_CSAW */
    {0, 90, -1, 0, ACT_NULL},  /* S_LAUN */
    {0, 91, -1, 0, ACT_NULL},  /* S_PLAS */
    {0, 92, -1, 0, ACT_NULL},  /* S_SHOT */
    {0, 93, -1, 0, ACT_NULL},  /* S_SHOT2 */
    {32768, 94, -1, 0, ACT_NULL},  /* S_COLU */
    {0, 95, -1, 0, ACT_NULL},  /* S_STALAG */
    {0, 96, 10, 889, ACT_NULL},  /* S_BLOODYTWITCH */
    {1, 96, 15, 890, ACT_NULL},  /* S_BLOODYTWITCH2 */
    {2, 96, 8, 891, ACT_NULL},  /* S_BLOODYTWITCH3 */
    {1, 96, 6, 888, ACT_NULL},  /* S_BLOODYTWITCH4 */
    {13, 28, -1, 0, ACT_NULL},  /* S_DEADTORSO */
    {18, 28, -1, 0, ACT_NULL},  /* S_DEADBOTTOM */
    {0, 97, -1, 0, ACT_NULL},  /* S_HEADSONSTICK */
    {0, 98, -1, 0, ACT_NULL},  /* S_GIBS */
    {0, 99, -1, 0, ACT_NULL},  /* S_HEADONASTICK */
    {32768, 100, 6, 898, ACT_NULL},  /* S_HEADCANDLES */
    {32769, 100, 6, 897, ACT_NULL},  /* S_HEADCANDLES2 */
    {0, 101, -1, 0, ACT_NULL},  /* S_DEADSTICK */
    {0, 102, 6, 901, ACT_NULL},  /* S_LIVESTICK */
    {1, 102, 8, 900, ACT_NULL},  /* S_LIVESTICK2 */
    {0, 103, -1, 0, ACT_NULL},  /* S_MEAT2 */
    {0, 104, -1, 0, ACT_NULL},  /* S_MEAT3 */
    {0, 105, -1, 0, ACT_NULL},  /* S_MEAT4 */
    {0, 106, -1, 0, ACT_NULL},  /* S_MEAT5 */
    {0, 107, -1, 0, ACT_NULL},  /* S_STALAGTITE */
    {0, 108, -1, 0, ACT_NULL},  /* S_TALLGRNCOL */
    {0, 109, -1, 0, ACT_NULL},  /* S_SHRTGRNCOL */
    {0, 110, -1, 0, ACT_NULL},  /* S_TALLREDCOL */
    {0, 111, -1, 0, ACT_NULL},  /* S_SHRTREDCOL */
    {32768, 112, -1, 0, ACT_NULL},  /* S_CANDLESTIK */
    {32768, 113, -1, 0, ACT_NULL},  /* S_CANDELABRA */
    {0, 114, -1, 0, ACT_NULL},  /* S_SKULLCOL */
    {0, 115, -1, 0, ACT_NULL},  /* S_TORCHTREE */
    {0, 116, -1, 0, ACT_NULL},  /* S_BIGTREE */
    {0, 117, -1, 0, ACT_NULL},  /* S_TECHPILLAR */
    {32768, 118, 6, 918, ACT_NULL},  /* S_EVILEYE */
    {32769, 118, 6, 919, ACT_NULL},  /* S_EVILEYE2 */
    {32770, 118, 6, 920, ACT_NULL},  /* S_EVILEYE3 */
    {32769, 118, 6, 917, ACT_NULL},  /* S_EVILEYE4 */
    {32768, 119, 6, 922, ACT_NULL},  /* S_FLOATSKULL */
    {32769, 119, 6, 923, ACT_NULL},  /* S_FLOATSKULL2 */
    {32770, 119, 6, 921, ACT_NULL},  /* S_FLOATSKULL3 */
    {0, 120, 14, 925, ACT_NULL},  /* S_HEARTCOL */
    {1, 120, 14, 924, ACT_NULL},  /* S_HEARTCOL2 */
    {32768, 121, 4, 927, ACT_NULL},  /* S_BLUETORCH */
    {32769, 121, 4, 928, ACT_NULL},  /* S_BLUETORCH2 */
    {32770, 121, 4, 929, ACT_NULL},  /* S_BLUETORCH3 */
    {32771, 121, 4, 926, ACT_NULL},  /* S_BLUETORCH4 */
    {32768, 122, 4, 931, ACT_NULL},  /* S_GREENTORCH */
    {32769, 122, 4, 932, ACT_NULL},  /* S_GREENTORCH2 */
    {32770, 122, 4, 933, ACT_NULL},  /* S_GREENTORCH3 */
    {32771, 122, 4, 930, ACT_NULL},  /* S_GREENTORCH4 */
    {32768, 123, 4, 935, ACT_NULL},  /* S_REDTORCH */
    {32769, 123, 4, 936, ACT_NULL},  /* S_REDTORCH2 */
    {32770, 123, 4, 937, ACT_NULL},  /* S_REDTORCH3 */
    {32771, 123, 4, 934, ACT_NULL},  /* S_REDTORCH4 */
    {32768, 124, 4, 939, ACT_NULL},  /* S_BTORCHSHRT */
    {32769, 124, 4, 940, ACT_NULL},  /* S_BTORCHSHRT2 */
    {32770, 124, 4, 941, ACT_NULL},  /* S_BTORCHSHRT3 */
    {32771, 124, 4, 938, ACT_NULL},  /* S_BTORCHSHRT4 */
    {32768, 125, 4, 943, ACT_NULL},  /* S_GTORCHSHRT */
    {32769, 125, 4, 944, ACT_NULL},  /* S_GTORCHSHRT2 */
    {32770, 125, 4, 945, ACT_NULL},  /* S_GTORCHSHRT3 */
    {32771, 125, 4, 942, ACT_NULL},  /* S_GTORCHSHRT4 */
    {32768, 126, 4, 947, ACT_NULL},  /* S_RTORCHSHRT */
    {32769, 126, 4, 948, ACT_NULL},  /* S_RTORCHSHRT2 */
    {32770, 126, 4, 949, ACT_NULL},  /* S_RTORCHSHRT3 */
    {32771, 126, 4, 946, ACT_NULL},  /* S_RTORCHSHRT4 */
    {0, 127, -1, 0, ACT_NULL},  /* S_HANGNOGUTS */
    {0, 128, -1, 0, ACT_NULL},  /* S_HANGBNOBRAIN */
    {0, 129, -1, 0, ACT_NULL},  /* S_HANGTLOOKDN */
    {0, 130, -1, 0, ACT_NULL},  /* S_HANGTSKULL */
    {0, 131, -1, 0, ACT_NULL},  /* S_HANGTLOOKUP */
    {0, 132, -1, 0, ACT_NULL},  /* S_HANGTNOBRAIN */
    {0, 133, -1, 0, ACT_NULL},  /* S_COLONGIBS */
    {0, 134, -1, 0, ACT_NULL},  /* S_SMALLPOOL */
    {0, 135, -1, 0, ACT_NULL},  /* S_BRAINSTEM */
    {32768, 136, 4, 960, ACT_NULL},  /* S_TECHLAMP */
    {32769, 136, 4, 961, ACT_NULL},  /* S_TECHLAMP2 */
    {32770, 136, 4, 962, ACT_NULL},  /* S_TECHLAMP3 */
    {32771, 136, 4, 959, ACT_NULL},  /* S_TECHLAMP4 */
    {32768, 137, 4, 964, ACT_NULL},  /* S_TECH2LAMP */
    {32769, 137, 4, 965, ACT_NULL},  /* S_TECH2LAMP2 */
    {32770, 137, 4, 966, ACT_NULL},  /* S_TECH2LAMP3 */
    {32771, 137, 4, 963, ACT_NULL},  /* S_TECH2LAMP4 */
};

const char* const sprnames[NUM_SPRITES] = {
    "TROO", "SHTG", "PUNG", "PISG", "PISF", "SHTF", "SHT2", "CHGG",
    "CHGF", "MISG", "MISF", "SAWG", "PLSG", "PLSF", "BFGG", "BFGF",
    "BLUD", "PUFF", "BAL1", "BAL2", "PLSS", "PLSE", "MISL", "BFS1",
    "BFE1", "BFE2", "TFOG", "IFOG", "PLAY", "POSS", "SPOS", "VILE",
    "FIRE", "FATB", "FBXP", "SKEL", "MANF", "FATT", "CPOS", "SARG",
    "HEAD", "BAL7", "BOSS", "BOS2", "SKUL", "SPID", "BSPI", "APLS",
    "APBX", "CYBR", "PAIN", "SSWV", "KEEN", "BBRN", "BOSF", "ARM1",
    "ARM2", "BAR1", "BEXP", "FCAN", "BON1", "BON2", "BKEY", "RKEY",
    "YKEY", "BSKU", "RSKU", "YSKU", "STIM", "MEDI", "SOUL", "PINV",
    "PSTR", "PINS", "MEGA", "SUIT", "PMAP", "PVIS", "CLIP", "AMMO",
    "ROCK", "BROK", "CELL", "CELP", "SHEL", "SBOX", "BPAK", "BFUG",
    "MGUN", "CSAW", "LAUN", "PLAS", "SHOT", "SGN2", "COLU", "SMT2",
    "GOR1", "POL2", "POL5", "POL4", "POL3", "POL1", "POL6", "GOR2",
    "GOR3", "GOR4", "GOR5", "SMIT", "COL1", "COL2", "COL3", "COL4",
    "CAND", "CBRA", "COL6", "TRE1", "TRE2", "ELEC", "CEYE", "FSKU",
    "COL5", "TBLU", "TGRN", "TRED", "SMBT", "SMGT", "SMRT", "HDB1",
    "HDB2", "HDB3", "HDB4", "HDB5", "HDB6", "POB1", "POB2", "BRS1",
    "TLMP", "TLP2",
};

#else  /* DOOM_INFO_TABLES_DEFINE */

extern const doom_mobjinfo_t mobjinfo[NUM_MOBJ_TYPES];
extern const doom_state_t states[NUM_STATES];
extern const char* const sprnames[NUM_SPRITES];

#endif /* DOOM_INFO_TABLES_DEFINE */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DOOM_INFO_TABLES_H */
