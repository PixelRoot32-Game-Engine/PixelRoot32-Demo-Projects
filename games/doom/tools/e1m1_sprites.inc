/*
 * The E1M1 sprite fixture: DoomEd number -> sprite lump.
 *
 * GENERATED FILE -- do not edit. Regenerate with:
 *     python tools/gen_e1m1_sprites.py --wad <path-to-DOOM1.WAD>
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
    { 2015, 0, 0, 0, "BON2", {
        { "BON2A0", 0 },
    } },
    { 2018, 0, 0, 0, "ARM1", {
        { "ARM1A0", 0 },
    } },
    { 2019, 0, 0, 0, "ARM2", {
        { "ARM2A0", 0 },
    } },
    { 2028, 0, 1, 0, "COLU", {
        { "COLUA0", 0 },
    } },
    { 2035, 0, 0, 0, "BAR1", {
        { "BAR1A0", 0 },
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
};

#define E1M1_SPRITE_RULE_COUNT \
    ((int)(sizeof kE1M1Sprites / sizeof kE1M1Sprites[0]))

#endif /* DOOM_E1M1_SPRITES_INC */
