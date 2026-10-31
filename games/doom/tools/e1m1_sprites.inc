/*
 * The sprite fixture: lookup key -> sprite lump.
 *
 * GENERATED FILE -- do not edit. Regenerate with:
 *     python tools/gen_e1m1_sprites.py --wad <path-to-DOOM1.WAD>
 *
 * `type` is the DoomEd number for a thing the map places, and the
 * mobjtype number (lib/doom/info_tables.h) for a thing the simulation
 * creates at runtime: no THINGS record can produce one of those. The first
 * row of a type is its spawnstate -- spriteRule() answers a THINGS lookup
 * with the first row of its type -- and the remaining rows are every other
 * state its chain can enter (see, pain, melee, missile, death, xdeath,
 * raise). They are named here so the container bakes every frame a live
 * mobj of that type can ask for.
 *
 * Fixture data, not renderer code: it is compiled into the host
 * harness (tools/rsegs_check.c) and is never included by lib/doom/.
 * The transcription, the spawnstate rule and the rotation rule are
 * documented in tools/e1m1_sprites.py.
 */
#ifndef DOOM_E1M1_SPRITES_INC
#define DOOM_E1M1_SPRITES_INC

typedef struct e1m1_sprite_lump {
    char name[9];
    unsigned char flip;
} e1m1_sprite_lump_t;

typedef struct e1m1_sprite_rule {
    short type;
    signed char frame;
    unsigned char fullbright;
    unsigned char rotations;
    char sprite[5];
    e1m1_sprite_lump_t lumps[8];
} e1m1_sprite_rule_t;

static const e1m1_sprite_rule_t kE1M1Sprites[] = {
    { 9, 0, 0, 1, "SPOS", {
        { "SPOSA1", 0 },
        { "SPOSA2A8", 0 },
        { "SPOSA3A7", 0 },
        { "SPOSA4A6", 0 },
        { "SPOSA5", 0 },
        { "SPOSA4A6", 1 },
        { "SPOSA3A7", 1 },
        { "SPOSA2A8", 1 },
    } },
    { 9, 1, 0, 1, "SPOS", {
        { "SPOSB1", 0 },
        { "SPOSB2B8", 0 },
        { "SPOSB3B7", 0 },
        { "SPOSB4B6", 0 },
        { "SPOSB5", 0 },
        { "SPOSB4B6", 1 },
        { "SPOSB3B7", 1 },
        { "SPOSB2B8", 1 },
    } },
    { 9, 0, 0, 1, "SPOS", {
        { "SPOSA1", 0 },
        { "SPOSA2A8", 0 },
        { "SPOSA3A7", 0 },
        { "SPOSA4A6", 0 },
        { "SPOSA5", 0 },
        { "SPOSA4A6", 1 },
        { "SPOSA3A7", 1 },
        { "SPOSA2A8", 1 },
    } },
    { 9, 0, 0, 1, "SPOS", {
        { "SPOSA1", 0 },
        { "SPOSA2A8", 0 },
        { "SPOSA3A7", 0 },
        { "SPOSA4A6", 0 },
        { "SPOSA5", 0 },
        { "SPOSA4A6", 1 },
        { "SPOSA3A7", 1 },
        { "SPOSA2A8", 1 },
    } },
    { 9, 1, 0, 1, "SPOS", {
        { "SPOSB1", 0 },
        { "SPOSB2B8", 0 },
        { "SPOSB3B7", 0 },
        { "SPOSB4B6", 0 },
        { "SPOSB5", 0 },
        { "SPOSB4B6", 1 },
        { "SPOSB3B7", 1 },
        { "SPOSB2B8", 1 },
    } },
    { 9, 1, 0, 1, "SPOS", {
        { "SPOSB1", 0 },
        { "SPOSB2B8", 0 },
        { "SPOSB3B7", 0 },
        { "SPOSB4B6", 0 },
        { "SPOSB5", 0 },
        { "SPOSB4B6", 1 },
        { "SPOSB3B7", 1 },
        { "SPOSB2B8", 1 },
    } },
    { 9, 2, 0, 1, "SPOS", {
        { "SPOSC1", 0 },
        { "SPOSC2C8", 0 },
        { "SPOSC3C7", 0 },
        { "SPOSC4C6", 0 },
        { "SPOSC5", 0 },
        { "SPOSC4C6", 1 },
        { "SPOSC3C7", 1 },
        { "SPOSC2C8", 1 },
    } },
    { 9, 2, 0, 1, "SPOS", {
        { "SPOSC1", 0 },
        { "SPOSC2C8", 0 },
        { "SPOSC3C7", 0 },
        { "SPOSC4C6", 0 },
        { "SPOSC5", 0 },
        { "SPOSC4C6", 1 },
        { "SPOSC3C7", 1 },
        { "SPOSC2C8", 1 },
    } },
    { 9, 3, 0, 1, "SPOS", {
        { "SPOSD1", 0 },
        { "SPOSD2D8", 0 },
        { "SPOSD3D7", 0 },
        { "SPOSD4D6", 0 },
        { "SPOSD5", 0 },
        { "SPOSD4D6", 1 },
        { "SPOSD3D7", 1 },
        { "SPOSD2D8", 1 },
    } },
    { 9, 3, 0, 1, "SPOS", {
        { "SPOSD1", 0 },
        { "SPOSD2D8", 0 },
        { "SPOSD3D7", 0 },
        { "SPOSD4D6", 0 },
        { "SPOSD5", 0 },
        { "SPOSD4D6", 1 },
        { "SPOSD3D7", 1 },
        { "SPOSD2D8", 1 },
    } },
    { 9, 4, 0, 1, "SPOS", {
        { "SPOSE1", 0 },
        { "SPOSE2E8", 0 },
        { "SPOSE3E7", 0 },
        { "SPOSE4E6", 0 },
        { "SPOSE5", 0 },
        { "SPOSE4E6", 1 },
        { "SPOSE3E7", 1 },
        { "SPOSE2E8", 1 },
    } },
    { 9, 5, 1, 1, "SPOS", {
        { "SPOSF1", 0 },
        { "SPOSF2F8", 0 },
        { "SPOSF3F7", 0 },
        { "SPOSF4F6", 0 },
        { "SPOSF5", 0 },
        { "SPOSF4F6", 1 },
        { "SPOSF3F7", 1 },
        { "SPOSF2F8", 1 },
    } },
    { 9, 4, 0, 1, "SPOS", {
        { "SPOSE1", 0 },
        { "SPOSE2E8", 0 },
        { "SPOSE3E7", 0 },
        { "SPOSE4E6", 0 },
        { "SPOSE5", 0 },
        { "SPOSE4E6", 1 },
        { "SPOSE3E7", 1 },
        { "SPOSE2E8", 1 },
    } },
    { 9, 6, 0, 1, "SPOS", {
        { "SPOSG1", 0 },
        { "SPOSG2G8", 0 },
        { "SPOSG3G7", 0 },
        { "SPOSG4G6", 0 },
        { "SPOSG5", 0 },
        { "SPOSG4G6", 1 },
        { "SPOSG3G7", 1 },
        { "SPOSG2G8", 1 },
    } },
    { 9, 6, 0, 1, "SPOS", {
        { "SPOSG1", 0 },
        { "SPOSG2G8", 0 },
        { "SPOSG3G7", 0 },
        { "SPOSG4G6", 0 },
        { "SPOSG5", 0 },
        { "SPOSG4G6", 1 },
        { "SPOSG3G7", 1 },
        { "SPOSG2G8", 1 },
    } },
    { 9, 7, 0, 0, "SPOS", {
        { "SPOSH0", 0 },
    } },
    { 9, 8, 0, 0, "SPOS", {
        { "SPOSI0", 0 },
    } },
    { 9, 9, 0, 0, "SPOS", {
        { "SPOSJ0", 0 },
    } },
    { 9, 10, 0, 0, "SPOS", {
        { "SPOSK0", 0 },
    } },
    { 9, 11, 0, 0, "SPOS", {
        { "SPOSL0", 0 },
    } },
    { 9, 12, 0, 0, "SPOS", {
        { "SPOSM0", 0 },
    } },
    { 9, 13, 0, 0, "SPOS", {
        { "SPOSN0", 0 },
    } },
    { 9, 14, 0, 0, "SPOS", {
        { "SPOSO0", 0 },
    } },
    { 9, 15, 0, 0, "SPOS", {
        { "SPOSP0", 0 },
    } },
    { 9, 16, 0, 0, "SPOS", {
        { "SPOSQ0", 0 },
    } },
    { 9, 17, 0, 0, "SPOS", {
        { "SPOSR0", 0 },
    } },
    { 9, 18, 0, 0, "SPOS", {
        { "SPOSS0", 0 },
    } },
    { 9, 19, 0, 0, "SPOS", {
        { "SPOST0", 0 },
    } },
    { 9, 20, 0, 0, "SPOS", {
        { "SPOSU0", 0 },
    } },
    { 9, 11, 0, 0, "SPOS", {
        { "SPOSL0", 0 },
    } },
    { 9, 10, 0, 0, "SPOS", {
        { "SPOSK0", 0 },
    } },
    { 9, 9, 0, 0, "SPOS", {
        { "SPOSJ0", 0 },
    } },
    { 9, 8, 0, 0, "SPOS", {
        { "SPOSI0", 0 },
    } },
    { 9, 7, 0, 0, "SPOS", {
        { "SPOSH0", 0 },
    } },
    { 10, 22, 0, 0, "PLAY", {
        { "PLAYW0", 0 },
    } },
    { 12, 22, 0, 0, "PLAY", {
        { "PLAYW0", 0 },
    } },
    { 15, 13, 0, 0, "PLAY", {
        { "PLAYN0", 0 },
    } },
    { 24, 0, 0, 0, "POL5", {
        { "POL5A0", 0 },
    } },
    { 35, 0, 1, 0, "CBRA", {
        { "CBRAA0", 0 },
    } },
    { 48, 0, 0, 0, "ELEC", {
        { "ELECA0", 0 },
    } },
    { 2001, 0, 0, 0, "SHOT", {
        { "SHOTA0", 0 },
    } },
    { 2002, 0, 0, 0, "MGUN", {
        { "MGUNA0", 0 },
    } },
    { 2003, 0, 0, 0, "LAUN", {
        { "LAUNA0", 0 },
    } },
    { 2007, 0, 0, 0, "CLIP", {
        { "CLIPA0", 0 },
    } },
    { 2008, 0, 0, 0, "SHEL", {
        { "SHELA0", 0 },
    } },
    { 2011, 0, 0, 0, "STIM", {
        { "STIMA0", 0 },
    } },
    { 2012, 0, 0, 0, "MEDI", {
        { "MEDIA0", 0 },
    } },
    { 2014, 0, 0, 0, "BON1", {
        { "BON1A0", 0 },
    } },
    { 2014, 1, 0, 0, "BON1", {
        { "BON1B0", 0 },
    } },
    { 2014, 2, 0, 0, "BON1", {
        { "BON1C0", 0 },
    } },
    { 2014, 3, 0, 0, "BON1", {
        { "BON1D0", 0 },
    } },
    { 2014, 2, 0, 0, "BON1", {
        { "BON1C0", 0 },
    } },
    { 2014, 1, 0, 0, "BON1", {
        { "BON1B0", 0 },
    } },
    { 2015, 0, 0, 0, "BON2", {
        { "BON2A0", 0 },
    } },
    { 2015, 1, 0, 0, "BON2", {
        { "BON2B0", 0 },
    } },
    { 2015, 2, 0, 0, "BON2", {
        { "BON2C0", 0 },
    } },
    { 2015, 3, 0, 0, "BON2", {
        { "BON2D0", 0 },
    } },
    { 2015, 2, 0, 0, "BON2", {
        { "BON2C0", 0 },
    } },
    { 2015, 1, 0, 0, "BON2", {
        { "BON2B0", 0 },
    } },
    { 2018, 0, 0, 0, "ARM1", {
        { "ARM1A0", 0 },
    } },
    { 2018, 1, 1, 0, "ARM1", {
        { "ARM1B0", 0 },
    } },
    { 2019, 0, 0, 0, "ARM2", {
        { "ARM2A0", 0 },
    } },
    { 2019, 1, 1, 0, "ARM2", {
        { "ARM2B0", 0 },
    } },
    { 2028, 0, 1, 0, "COLU", {
        { "COLUA0", 0 },
    } },
    { 2035, 0, 0, 0, "BAR1", {
        { "BAR1A0", 0 },
    } },
    { 2035, 1, 0, 0, "BAR1", {
        { "BAR1B0", 0 },
    } },
    { 2035, 0, 1, 0, "BEXP", {
        { "BEXPA0", 0 },
    } },
    { 2035, 1, 1, 0, "BEXP", {
        { "BEXPB0", 0 },
    } },
    { 2035, 2, 1, 0, "BEXP", {
        { "BEXPC0", 0 },
    } },
    { 2035, 3, 1, 0, "BEXP", {
        { "BEXPD0", 0 },
    } },
    { 2035, 4, 1, 0, "BEXP", {
        { "BEXPE0", 0 },
    } },
    { 2046, 0, 0, 0, "BROK", {
        { "BROKA0", 0 },
    } },
    { 2048, 0, 0, 0, "AMMO", {
        { "AMMOA0", 0 },
    } },
    { 2049, 0, 0, 0, "SBOX", {
        { "SBOXA0", 0 },
    } },
    { 3001, 0, 0, 1, "TROO", {
        { "TROOA1", 0 },
        { "TROOA2A8", 0 },
        { "TROOA3A7", 0 },
        { "TROOA4A6", 0 },
        { "TROOA5", 0 },
        { "TROOA4A6", 1 },
        { "TROOA3A7", 1 },
        { "TROOA2A8", 1 },
    } },
    { 3001, 1, 0, 1, "TROO", {
        { "TROOB1", 0 },
        { "TROOB2B8", 0 },
        { "TROOB3B7", 0 },
        { "TROOB4B6", 0 },
        { "TROOB5", 0 },
        { "TROOB4B6", 1 },
        { "TROOB3B7", 1 },
        { "TROOB2B8", 1 },
    } },
    { 3001, 0, 0, 1, "TROO", {
        { "TROOA1", 0 },
        { "TROOA2A8", 0 },
        { "TROOA3A7", 0 },
        { "TROOA4A6", 0 },
        { "TROOA5", 0 },
        { "TROOA4A6", 1 },
        { "TROOA3A7", 1 },
        { "TROOA2A8", 1 },
    } },
    { 3001, 0, 0, 1, "TROO", {
        { "TROOA1", 0 },
        { "TROOA2A8", 0 },
        { "TROOA3A7", 0 },
        { "TROOA4A6", 0 },
        { "TROOA5", 0 },
        { "TROOA4A6", 1 },
        { "TROOA3A7", 1 },
        { "TROOA2A8", 1 },
    } },
    { 3001, 1, 0, 1, "TROO", {
        { "TROOB1", 0 },
        { "TROOB2B8", 0 },
        { "TROOB3B7", 0 },
        { "TROOB4B6", 0 },
        { "TROOB5", 0 },
        { "TROOB4B6", 1 },
        { "TROOB3B7", 1 },
        { "TROOB2B8", 1 },
    } },
    { 3001, 1, 0, 1, "TROO", {
        { "TROOB1", 0 },
        { "TROOB2B8", 0 },
        { "TROOB3B7", 0 },
        { "TROOB4B6", 0 },
        { "TROOB5", 0 },
        { "TROOB4B6", 1 },
        { "TROOB3B7", 1 },
        { "TROOB2B8", 1 },
    } },
    { 3001, 2, 0, 1, "TROO", {
        { "TROOC1", 0 },
        { "TROOC2C8", 0 },
        { "TROOC3C7", 0 },
        { "TROOC4C6", 0 },
        { "TROOC5", 0 },
        { "TROOC4C6", 1 },
        { "TROOC3C7", 1 },
        { "TROOC2C8", 1 },
    } },
    { 3001, 2, 0, 1, "TROO", {
        { "TROOC1", 0 },
        { "TROOC2C8", 0 },
        { "TROOC3C7", 0 },
        { "TROOC4C6", 0 },
        { "TROOC5", 0 },
        { "TROOC4C6", 1 },
        { "TROOC3C7", 1 },
        { "TROOC2C8", 1 },
    } },
    { 3001, 3, 0, 1, "TROO", {
        { "TROOD1", 0 },
        { "TROOD2D8", 0 },
        { "TROOD3D7", 0 },
        { "TROOD4D6", 0 },
        { "TROOD5", 0 },
        { "TROOD4D6", 1 },
        { "TROOD3D7", 1 },
        { "TROOD2D8", 1 },
    } },
    { 3001, 3, 0, 1, "TROO", {
        { "TROOD1", 0 },
        { "TROOD2D8", 0 },
        { "TROOD3D7", 0 },
        { "TROOD4D6", 0 },
        { "TROOD5", 0 },
        { "TROOD4D6", 1 },
        { "TROOD3D7", 1 },
        { "TROOD2D8", 1 },
    } },
    { 3001, 4, 0, 1, "TROO", {
        { "TROOE1", 0 },
        { "TROOE2E8", 0 },
        { "TROOE3E7", 0 },
        { "TROOE4E6", 0 },
        { "TROOE5", 0 },
        { "TROOE4E6", 1 },
        { "TROOE3E7", 1 },
        { "TROOE2E8", 1 },
    } },
    { 3001, 5, 0, 1, "TROO", {
        { "TROOF1", 0 },
        { "TROOF2F8", 0 },
        { "TROOF3F7", 0 },
        { "TROOF4F6", 0 },
        { "TROOF5", 0 },
        { "TROOF4F6", 1 },
        { "TROOF3F7", 1 },
        { "TROOF2F8", 1 },
    } },
    { 3001, 6, 0, 1, "TROO", {
        { "TROOG1", 0 },
        { "TROOG2G8", 0 },
        { "TROOG3G7", 0 },
        { "TROOG4G6", 0 },
        { "TROOG5", 0 },
        { "TROOG4G6", 1 },
        { "TROOG3G7", 1 },
        { "TROOG2G8", 1 },
    } },
    { 3001, 7, 0, 1, "TROO", {
        { "TROOH1", 0 },
        { "TROOH2H8", 0 },
        { "TROOH3H7", 0 },
        { "TROOH4H6", 0 },
        { "TROOH5", 0 },
        { "TROOH4H6", 1 },
        { "TROOH3H7", 1 },
        { "TROOH2H8", 1 },
    } },
    { 3001, 7, 0, 1, "TROO", {
        { "TROOH1", 0 },
        { "TROOH2H8", 0 },
        { "TROOH3H7", 0 },
        { "TROOH4H6", 0 },
        { "TROOH5", 0 },
        { "TROOH4H6", 1 },
        { "TROOH3H7", 1 },
        { "TROOH2H8", 1 },
    } },
    { 3001, 8, 0, 0, "TROO", {
        { "TROOI0", 0 },
    } },
    { 3001, 9, 0, 0, "TROO", {
        { "TROOJ0", 0 },
    } },
    { 3001, 10, 0, 0, "TROO", {
        { "TROOK0", 0 },
    } },
    { 3001, 11, 0, 0, "TROO", {
        { "TROOL0", 0 },
    } },
    { 3001, 12, 0, 0, "TROO", {
        { "TROOM0", 0 },
    } },
    { 3001, 13, 0, 0, "TROO", {
        { "TROON0", 0 },
    } },
    { 3001, 14, 0, 0, "TROO", {
        { "TROOO0", 0 },
    } },
    { 3001, 15, 0, 0, "TROO", {
        { "TROOP0", 0 },
    } },
    { 3001, 16, 0, 0, "TROO", {
        { "TROOQ0", 0 },
    } },
    { 3001, 17, 0, 0, "TROO", {
        { "TROOR0", 0 },
    } },
    { 3001, 18, 0, 0, "TROO", {
        { "TROOS0", 0 },
    } },
    { 3001, 19, 0, 0, "TROO", {
        { "TROOT0", 0 },
    } },
    { 3001, 20, 0, 0, "TROO", {
        { "TROOU0", 0 },
    } },
    { 3001, 12, 0, 0, "TROO", {
        { "TROOM0", 0 },
    } },
    { 3001, 11, 0, 0, "TROO", {
        { "TROOL0", 0 },
    } },
    { 3001, 10, 0, 0, "TROO", {
        { "TROOK0", 0 },
    } },
    { 3001, 9, 0, 0, "TROO", {
        { "TROOJ0", 0 },
    } },
    { 3001, 8, 0, 0, "TROO", {
        { "TROOI0", 0 },
    } },
    { 3004, 0, 0, 1, "POSS", {
        { "POSSA1", 0 },
        { "POSSA2A8", 0 },
        { "POSSA3A7", 0 },
        { "POSSA4A6", 0 },
        { "POSSA5", 0 },
        { "POSSA4A6", 1 },
        { "POSSA3A7", 1 },
        { "POSSA2A8", 1 },
    } },
    { 3004, 1, 0, 1, "POSS", {
        { "POSSB1", 0 },
        { "POSSB2B8", 0 },
        { "POSSB3B7", 0 },
        { "POSSB4B6", 0 },
        { "POSSB5", 0 },
        { "POSSB4B6", 1 },
        { "POSSB3B7", 1 },
        { "POSSB2B8", 1 },
    } },
    { 3004, 0, 0, 1, "POSS", {
        { "POSSA1", 0 },
        { "POSSA2A8", 0 },
        { "POSSA3A7", 0 },
        { "POSSA4A6", 0 },
        { "POSSA5", 0 },
        { "POSSA4A6", 1 },
        { "POSSA3A7", 1 },
        { "POSSA2A8", 1 },
    } },
    { 3004, 0, 0, 1, "POSS", {
        { "POSSA1", 0 },
        { "POSSA2A8", 0 },
        { "POSSA3A7", 0 },
        { "POSSA4A6", 0 },
        { "POSSA5", 0 },
        { "POSSA4A6", 1 },
        { "POSSA3A7", 1 },
        { "POSSA2A8", 1 },
    } },
    { 3004, 1, 0, 1, "POSS", {
        { "POSSB1", 0 },
        { "POSSB2B8", 0 },
        { "POSSB3B7", 0 },
        { "POSSB4B6", 0 },
        { "POSSB5", 0 },
        { "POSSB4B6", 1 },
        { "POSSB3B7", 1 },
        { "POSSB2B8", 1 },
    } },
    { 3004, 1, 0, 1, "POSS", {
        { "POSSB1", 0 },
        { "POSSB2B8", 0 },
        { "POSSB3B7", 0 },
        { "POSSB4B6", 0 },
        { "POSSB5", 0 },
        { "POSSB4B6", 1 },
        { "POSSB3B7", 1 },
        { "POSSB2B8", 1 },
    } },
    { 3004, 2, 0, 1, "POSS", {
        { "POSSC1", 0 },
        { "POSSC2C8", 0 },
        { "POSSC3C7", 0 },
        { "POSSC4C6", 0 },
        { "POSSC5", 0 },
        { "POSSC4C6", 1 },
        { "POSSC3C7", 1 },
        { "POSSC2C8", 1 },
    } },
    { 3004, 2, 0, 1, "POSS", {
        { "POSSC1", 0 },
        { "POSSC2C8", 0 },
        { "POSSC3C7", 0 },
        { "POSSC4C6", 0 },
        { "POSSC5", 0 },
        { "POSSC4C6", 1 },
        { "POSSC3C7", 1 },
        { "POSSC2C8", 1 },
    } },
    { 3004, 3, 0, 1, "POSS", {
        { "POSSD1", 0 },
        { "POSSD2D8", 0 },
        { "POSSD3D7", 0 },
        { "POSSD4D6", 0 },
        { "POSSD5", 0 },
        { "POSSD4D6", 1 },
        { "POSSD3D7", 1 },
        { "POSSD2D8", 1 },
    } },
    { 3004, 3, 0, 1, "POSS", {
        { "POSSD1", 0 },
        { "POSSD2D8", 0 },
        { "POSSD3D7", 0 },
        { "POSSD4D6", 0 },
        { "POSSD5", 0 },
        { "POSSD4D6", 1 },
        { "POSSD3D7", 1 },
        { "POSSD2D8", 1 },
    } },
    { 3004, 4, 0, 1, "POSS", {
        { "POSSE1", 0 },
        { "POSSE2E8", 0 },
        { "POSSE3E7", 0 },
        { "POSSE4E6", 0 },
        { "POSSE5", 0 },
        { "POSSE4E6", 1 },
        { "POSSE3E7", 1 },
        { "POSSE2E8", 1 },
    } },
    { 3004, 5, 0, 1, "POSS", {
        { "POSSF1", 0 },
        { "POSSF2F8", 0 },
        { "POSSF3F7", 0 },
        { "POSSF4F6", 0 },
        { "POSSF5", 0 },
        { "POSSF4F6", 1 },
        { "POSSF3F7", 1 },
        { "POSSF2F8", 1 },
    } },
    { 3004, 4, 0, 1, "POSS", {
        { "POSSE1", 0 },
        { "POSSE2E8", 0 },
        { "POSSE3E7", 0 },
        { "POSSE4E6", 0 },
        { "POSSE5", 0 },
        { "POSSE4E6", 1 },
        { "POSSE3E7", 1 },
        { "POSSE2E8", 1 },
    } },
    { 3004, 6, 0, 1, "POSS", {
        { "POSSG1", 0 },
        { "POSSG2G8", 0 },
        { "POSSG3G7", 0 },
        { "POSSG4G6", 0 },
        { "POSSG5", 0 },
        { "POSSG4G6", 1 },
        { "POSSG3G7", 1 },
        { "POSSG2G8", 1 },
    } },
    { 3004, 6, 0, 1, "POSS", {
        { "POSSG1", 0 },
        { "POSSG2G8", 0 },
        { "POSSG3G7", 0 },
        { "POSSG4G6", 0 },
        { "POSSG5", 0 },
        { "POSSG4G6", 1 },
        { "POSSG3G7", 1 },
        { "POSSG2G8", 1 },
    } },
    { 3004, 7, 0, 0, "POSS", {
        { "POSSH0", 0 },
    } },
    { 3004, 8, 0, 0, "POSS", {
        { "POSSI0", 0 },
    } },
    { 3004, 9, 0, 0, "POSS", {
        { "POSSJ0", 0 },
    } },
    { 3004, 10, 0, 0, "POSS", {
        { "POSSK0", 0 },
    } },
    { 3004, 11, 0, 0, "POSS", {
        { "POSSL0", 0 },
    } },
    { 3004, 12, 0, 0, "POSS", {
        { "POSSM0", 0 },
    } },
    { 3004, 13, 0, 0, "POSS", {
        { "POSSN0", 0 },
    } },
    { 3004, 14, 0, 0, "POSS", {
        { "POSSO0", 0 },
    } },
    { 3004, 15, 0, 0, "POSS", {
        { "POSSP0", 0 },
    } },
    { 3004, 16, 0, 0, "POSS", {
        { "POSSQ0", 0 },
    } },
    { 3004, 17, 0, 0, "POSS", {
        { "POSSR0", 0 },
    } },
    { 3004, 18, 0, 0, "POSS", {
        { "POSSS0", 0 },
    } },
    { 3004, 19, 0, 0, "POSS", {
        { "POSST0", 0 },
    } },
    { 3004, 20, 0, 0, "POSS", {
        { "POSSU0", 0 },
    } },
    { 3004, 10, 0, 0, "POSS", {
        { "POSSK0", 0 },
    } },
    { 3004, 9, 0, 0, "POSS", {
        { "POSSJ0", 0 },
    } },
    { 3004, 8, 0, 0, "POSS", {
        { "POSSI0", 0 },
    } },
    { 3004, 7, 0, 0, "POSS", {
        { "POSSH0", 0 },
    } },
    { 37, 0, 1, 0, "PUFF", {
        { "PUFFA0", 0 },
    } },
    { 37, 1, 0, 0, "PUFF", {
        { "PUFFB0", 0 },
    } },
    { 37, 2, 0, 0, "PUFF", {
        { "PUFFC0", 0 },
    } },
    { 37, 3, 0, 0, "PUFF", {
        { "PUFFD0", 0 },
    } },
    { 38, 2, 0, 0, "BLUD", {
        { "BLUDC0", 0 },
    } },
    { 38, 1, 0, 0, "BLUD", {
        { "BLUDB0", 0 },
    } },
    { 38, 0, 0, 0, "BLUD", {
        { "BLUDA0", 0 },
    } },
    { 33, 0, 1, 1, "MISL", {
        { "MISLA1", 0 },
        { "MISLA8A2", 1 },
        { "MISLA7A3", 1 },
        { "MISLA6A4", 1 },
        { "MISLA5", 0 },
        { "MISLA6A4", 0 },
        { "MISLA7A3", 0 },
        { "MISLA8A2", 0 },
    } },
    { 33, 1, 1, 0, "MISL", {
        { "MISLB0", 0 },
    } },
    { 33, 2, 1, 0, "MISL", {
        { "MISLC0", 0 },
    } },
    { 33, 3, 1, 0, "MISL", {
        { "MISLD0", 0 },
    } },
};

#define E1M1_SPRITE_RULE_COUNT \
    ((int)(sizeof kE1M1Sprites / sizeof kE1M1Sprites[0]))

#endif /* DOOM_E1M1_SPRITES_INC */
