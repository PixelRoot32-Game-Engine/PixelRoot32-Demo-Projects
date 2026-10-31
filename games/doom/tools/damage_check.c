/*
 * Host harness for damage, death, pickups and the crush (D4.2d1).
 *
 * It loads E1M1 through lib/doom/p_setup.c, keeps only the avatar that
 * P_LoadThings() already spawned, and then drives the real p_inter.c rules from
 * a fixed script: three pickups through a real p_try_move (so PIT_CheckThing's
 * special branch is what hands the item over), an armor and damage table on the
 * player, three monster deaths (a normal one, a gib, and one whose fall-forward
 * coin is drawn), a non-countkill death, the two sprays, a crush in a rising
 * sector and the death animation that follows it.
 *
 * tools/damage_check.py compiles this file, runs it next to
 * tools/doom_ref.py --damage, and compares the two outputs line for line.
 *
 * THE PROTOCOL
 *
 *   DAMAGE <map> script=<name> steps=<N>
 *   SETUP rng=<n> mobjs=<n> sector=<n> floor=<i32> ceiling=<i32> player=<slot>
 *   STEP <n> op=<name> rng=<n> crushed=<n>
 *   MOBJ <slot> type=<t> x=<i32> y=<i32> z=<i32> health=<i32> flags=0x<8x>
 *       state=<s> tics=<t> tgt=<slot|-1> momx=<i32> momy=<i32>
 *   PLAYER hp=<i32> ar=<i32> at=<i32> item=<i32> kill=<i32> att=<slot|-1>
 *       ps=<n> dmg=<i32> bonus=<i32> ammo=<4> maxammo=<4> w=<mask>
 *       cards=<mask> pack=<0|1> pend=<n> pow=<6>
 *   SUMMARY steps=<n> rng=<n> crushed=<n> kill=<n> item=<n> health=<i32>
 *       spawns=<n>
 *
 * Every MOBJ line is the *whole* thinker list in insertion order, so a spray or
 * a dropped item appears as a new slot and a removal closes the gap. `tgt` is
 * the target resolved to a slot, or -1 when it is gone. `rng` on the STEP line
 * is the P_Random() position, which is how a missed or extra draw shows up
 * before any state does.
 *
 * THE SCRIPT (identical in tools/doom_ref.py's damage_script())
 *
 *   setup       remove every THING but the avatar; spawn MG0/MG1/MG2 (possessed,
 *               MF_COUNTKILL), MB0 (barrel, not a kill) and MH0 (a puff held
 *               96 units above MG2, the inflictor of the fall-forward hit)
 *   step 0..2   spawn a health bonus, an armor bonus and a clip 20 units ahead
 *               of the player and step 8 units forward onto each: P_TouchSpecial
 *               Thing runs from PIT_CheckThing, through p_try_move
 *   step 3      one thinker walk, which releases the three picked-up mobjs
 *   step 4      P_GiveArmor (1): 100 points, green
 *   step 5      damage the player 40 (no inflictor, no source)
 *   step 6      damage the player 30 from MG0 (thrust, attacker, target)
 *   step 7      damage the player 1 (the `saved = damage/3 == 0` case)
 *   step 8      P_GiveArmor (2): 200 points, blue
 *   step 9      damage the player 60 (blue absorbs 30)
 *   step 10     damage the player 5
 *   step 11     touch a green armor while blue armor is held: refused, and the
 *               item stays in the world ("already have it")
 *   step 12     damage MG0 15 (survives at 5, pain state)
 *   step 13     tick
 *   step 14     damage MG0 25: normal death, clip drop, killcount 1
 *   step 15     tick
 *   step 16     damage MG2 25 from MH0: `damage < 40 && damage > health &&
 *               z - inflictor->z > 64` draws the fall-forward coin; killcount 2
 *   step 17     tick
 *   step 18     damage MB0 60: barrel death, no killcount, no drop
 *   step 19     tick
 *   step 20     damage MG1 60: health < -spawnhealth, so the xdeath chain
 *   step 21     tick
 *   step 22..25 P_SpawnBlood 15 / 10 / 5 and P_SpawnPuff: the three blood
 *               branches and the puff
 *   step 26..28 three ticks, which walk those sprays and release the ones that
 *               reach S_NULL
 *   step 29..   crush: the player's own sector's floor rises 8 units a tic for
 *               32 tics with `leveltime` counting, so `!(leveltime&3)` lands on
 *               every fourth tic; the player takes 10 a time, dies, and is
 *               gibbed by the next crush that finds it dead
 *   then        DAMAGE_TICKS_AFTER_DEATH thinker walks, which carry the player's
 *               death chain through A_PlayerScream and A_Fall
 *
 * WHAT IT DOES NOT DO
 *
 * No p_spawn_specials(): sector specials are tools/specials_check.py's subject,
 * and leaving them out keeps the tic clock and the RNG identical on both sides.
 * No P_PlayerThink: the avatar's input is tools/movement_check.py's.
 *
 * THE PERTURBATION HOOK
 *
 * P_ARMOR_GREEN_DIVISOR, P_ARMOR_BLUE_DIVISOR, P_CRUSH_DAMAGE and
 * P_BLOOD_Z_SHIFT are #ifndefs in p_inter.h/p_mobj.h, so the whole C build can
 * be perturbed with one -D. tools/damage_check.py --perturb does that, which is
 * how the gate is proved able to fail.
 *
 * Build: see tools/damage_check.py. This is a desktop tool: malloc, stdio, the
 * WAD and a wall clock come from the host, none of which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p_inter.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "w_wad.h"

#define DAMAGE_ARENA_BYTES (256u * 1024u)
#define DAMAGE_SCRIPT "damage-death-crush-pickup"
#define DAMAGE_CRUSH_STEPS 32
#define DAMAGE_CRUSH_STEP (8 * FRACUNIT)
#define DAMAGE_TICKS_AFTER_DEATH 24
/* Every emit() of the script, counted: the DAMAGE header states it and the
 * self-check refuses a run that did not emit exactly this many steps. */
#define DAMAGE_STEPS 84

/* Map-unit offsets from the player start, so the scene is deterministic and the
 * same numbers appear in tools/doom_ref.py. */
#define DAMAGE_MG0_DX 64
#define DAMAGE_MG1_DY 64
#define DAMAGE_MG2_DX (-64)
#define DAMAGE_MB0_DX (-96)
#define DAMAGE_REACH (20 * FRACUNIT)
#define DAMAGE_STEP (8 * FRACUNIT)
#define DAMAGE_DROP_HEIGHT (96 * FRACUNIT)
#define DAMAGE_SPRAY_Z (28 * FRACUNIT)

/* --- Reading and printing ------------------------------------------------- */

static uint8_t* readFile(const char* path, size_t* size) {
    FILE* handle = fopen(path, "rb");
    uint8_t* data;
    long length;

    if (handle == NULL) {
        return NULL;
    }
    if (fseek(handle, 0, SEEK_END) != 0) {
        fclose(handle);
        return NULL;
    }
    length = ftell(handle);
    if (length < 0 || fseek(handle, 0, SEEK_SET) != 0) {
        fclose(handle);
        return NULL;
    }
    data = (uint8_t*)malloc((size_t)length);
    if (data == NULL) {
        fclose(handle);
        return NULL;
    }
    if (fread(data, 1, (size_t)length, handle) != (size_t)length) {
        free(data);
        fclose(handle);
        return NULL;
    }
    fclose(handle);
    *size = (size_t)length;
    return data;
}

/* The slot a mobj occupies in the thinker list, or -1 when it is no longer in
 * it. Both sides resolve a target the same way, so an identity is compared as a
 * position rather than as a pointer. */
static int slot_of(const p_level_t* level, const r_mobj_t* mobj) {
    const r_mobj_t* each;
    int slot = 0;

    if (mobj == NULL) {
        return -1;
    }
    for (each = p_level_first_mobj(level); each != NULL;
         each = p_level_next_mobj(each)) {
        if (each == mobj) {
            return slot;
        }
        slot += 1;
    }
    return -1;
}

static void print_mobj(const p_level_t* level, int slot, const r_mobj_t* mobj) {
    printf("MOBJ %d type=%d x=%ld y=%ld z=%ld health=%ld flags=0x%08lx "
           "state=%d tics=%d tgt=%d momx=%ld momy=%ld\n",
           slot, (int)mobj->type, (long)mobj->x, (long)mobj->y, (long)mobj->z,
           (long)mobj->health, (unsigned long)mobj->flags, (int)mobj->state,
           (int)mobj->tics, slot_of(level, mobj->target), (long)mobj->momx,
           (long)mobj->momy);
}

static void print_player(const p_level_t* level) {
    const p_player_t* player = &level->playerState;
    unsigned weapons = 0u;
    unsigned cards = 0u;
    int i;

    for (i = 0; i < P_NUMWEAPONS; ++i) {
        if (player->weaponowned[i]) {
            weapons |= 1u << i;
        }
    }
    for (i = 0; i < P_NUMCARDS; ++i) {
        if (player->cards[i]) {
            cards |= 1u << i;
        }
    }

    printf("PLAYER hp=%ld ar=%ld at=%ld item=%ld kill=%ld att=%d ps=%d "
           "dmg=%ld bonus=%ld ammo=%ld,%ld,%ld,%ld maxammo=%ld,%ld,%ld,%ld "
           "w=%u cards=%u pack=%d pend=%ld pow=%ld,%ld,%ld,%ld,%ld,%ld\n",
           (long)player->health, (long)player->armorpoints,
           (long)player->armortype, (long)player->itemcount,
           (long)player->killcount, slot_of(level, player->attacker),
           (int)player->playerstate, (long)player->damagecount,
           (long)player->bonuscount, (long)player->ammo[P_AM_CLIP],
           (long)player->ammo[P_AM_SHELL], (long)player->ammo[P_AM_CELL],
           (long)player->ammo[P_AM_MISL], (long)player->maxammo[P_AM_CLIP],
           (long)player->maxammo[P_AM_SHELL], (long)player->maxammo[P_AM_CELL],
           (long)player->maxammo[P_AM_MISL], weapons, cards,
           player->backpack != 0u ? 1 : 0, (long)player->pendingweapon,
           (long)player->powers[0], (long)player->powers[1],
           (long)player->powers[2], (long)player->powers[3],
           (long)player->powers[4], (long)player->powers[5]);
}

static void emit(p_level_t* level, int* step, const char* op) {
    const r_mobj_t* mobj;
    int slot = 0;

    printf("STEP %d op=%s rng=%u crushed=%u\n", *step, op,
           (unsigned)level->random.index,
           (unsigned)level->specStats.crushedThings);
    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        print_mobj(level, slot, mobj);
        slot += 1;
    }
    print_player(level);
    *step += 1;
}

/* --- The scripted operations --------------------------------------------- */

static r_mobj_t* spawn_at(p_level_t* level, mobjtype_t type, fixed_t x,
                          fixed_t y, fixed_t z) {
    return p_spawn_mobj(&level->context, x, y, z, type, NULL);
}

/* One item, twenty units ahead, then one eight-unit step onto it. The touch runs
 * inside p_check_position's thing loop, which is the real path a pickup takes. */
static void pickup(p_level_t* level, mobjtype_t type, int* step,
                   const char* op) {
    r_mobj_t* player = level->player;

    (void)spawn_at(level, type, player->x, player->y + DAMAGE_REACH, ONFLOORZ);
    (void)p_try_move(&level->context, player, player->x, player->y + DAMAGE_STEP);
    emit(level, step, op);
}

static void give_armor(p_level_t* level, int armortype, int* step,
                       const char* op) {
    (void)p_give_armor(&level->playerState, armortype);
    emit(level, step, op);
}

static void damage(p_level_t* level, r_mobj_t* target, r_mobj_t* inflictor,
                   r_mobj_t* source, int amount, int* step, const char* op) {
    p_damage_mobj(&level->context, target, inflictor, source, amount);
    emit(level, step, op);
}

static void tick(p_level_t* level, int* step, const char* op) {
    p_thinker_run_t run;

    (void)p_run_thinkers(&level->thinkers, &p_level_release_thinker,
                         &level->context, &run);
    level->leveltime += 1;
    emit(level, step, op);
}

static int run_damage(p_level_t* level, const char* mapName) {
    r_mobj_t* player = level->player;
    r_mobj_t* mg0;
    r_mobj_t* mg1;
    r_mobj_t* mg2;
    r_mobj_t* mb0;
    r_mobj_t* mh0;
    uint16_t sector;
    int step = 0;
    int i;

    /* SETUP: the avatar's own sector and the scene's mobjs. */
    sector = player->sector;
    printf("DAMAGE %s script=%s steps=%d\n", mapName, DAMAGE_SCRIPT,
           DAMAGE_STEPS);
    printf("SETUP rng=%u mobjs=%u sector=%u floor=%ld ceiling=%ld player=%d\n",
           (unsigned)level->random.index, (unsigned)p_level_mobj_count(level),
           (unsigned)sector, (long)level->sectors[sector].floorHeight,
           (long)level->sectors[sector].ceilingHeight,
           slot_of(level, player));

    mg0 = spawn_at(level, MT_POSSESSED, player->x + DAMAGE_MG0_DX * FRACUNIT,
                   player->y, ONFLOORZ);
    mg1 = spawn_at(level, MT_POSSESSED, player->x,
                   player->y + DAMAGE_MG1_DY * FRACUNIT, ONFLOORZ);
    mb0 = spawn_at(level, MT_BARREL, player->x + DAMAGE_MB0_DX * FRACUNIT,
                   player->y, ONFLOORZ);
    /* The fall-forward hit needs the target more than 64 units *above* its
     * inflictor, so MG2 is spawned 96 units up and MH0 exactly under it. Both
     * heights are explicit and the hit happens before any tic, which is the only
     * moment the difference is still there: gravity would drop MG2 on the first
     * walk. */
    mg2 = spawn_at(level, MT_POSSESSED, player->x + DAMAGE_MG2_DX * FRACUNIT,
                   player->y, player->z + DAMAGE_DROP_HEIGHT);
    mh0 = spawn_at(level, MT_PUFF, mg2->x, mg2->y, player->z);
    if (mg0 == NULL || mg1 == NULL || mg2 == NULL || mb0 == NULL || mh0 == NULL) {
        fprintf(stderr, "damage_check: the zone refused the scene\n");
        return 2;
    }

    /* The fall-forward coin: `damage < 40 && damage > health &&
     * z - inflictor->z > 64` and then `P_Random()&1`. MG2 dies either way. */
    damage(level, mg2, mh0, player, 25, &step, "damage-mg2-fallforward");

    /* The three pickups. */
    pickup(level, MT_MISC2, &step, "pickup-health");
    pickup(level, MT_MISC3, &step, "pickup-armor");
    pickup(level, MT_CLIP, &step, "pickup-ammo");
    tick(level, &step, "tick-release-items");

    /* The armor and damage table on the player. */
    give_armor(level, 1, &step, "give-armor-green");
    damage(level, player, NULL, NULL, 40, &step, "damage-player-40");
    damage(level, player, mg0, mg0, 30, &step, "damage-player-30-from-mg0");
    damage(level, player, NULL, NULL, 1, &step, "damage-player-1");
    give_armor(level, 2, &step, "give-armor-blue");
    damage(level, player, NULL, NULL, 60, &step, "damage-player-60");
    damage(level, player, NULL, NULL, 5, &step, "damage-player-5");

    /* "Already have it": green armor is refused while blue armor is held, so the
     * item stays in the world and itemcount does not move. */
    {
        r_mobj_t* item = spawn_at(level, MT_MISC0, player->x,
                                  player->y + DAMAGE_REACH, ONFLOORZ);

        p_touch_special_thing(&level->context, item, player);
        emit(level, &step, "touch-armor-refused");
    }

    /* Pain, a normal death with a clip drop, a barrel death that is not a kill,
     * and a gib. */
    damage(level, mg0, player, player, 15, &step, "damage-mg0-pain");
    tick(level, &step, "tick-mg0-pain");
    damage(level, mg0, player, player, 25, &step, "damage-mg0-kill");
    tick(level, &step, "tick-mg0-death");
    damage(level, mb0, player, player, 60, &step, "damage-barrel");
    tick(level, &step, "tick-barrel-death");
    damage(level, mg1, player, player, 60, &step, "damage-mg1-gib");
    tick(level, &step, "tick-mg1-gib");

    /* The two sprays, with the damage bands that choose their state. */
    {
        const fixed_t x = player->x + 16 * FRACUNIT;
        const fixed_t z = player->z + DAMAGE_SPRAY_Z;

        (void)p_spawn_blood(&level->context, x, player->y, z, 15);
        emit(level, &step, "spawn-blood-15");
        (void)p_spawn_blood(&level->context, x, player->y, z, 10);
        emit(level, &step, "spawn-blood-10");
        (void)p_spawn_blood(&level->context, x, player->y, z, 5);
        emit(level, &step, "spawn-blood-5");
        (void)p_spawn_puff(&level->context, x, player->y, z, P_MISSILERANGE);
        emit(level, &step, "spawn-puff");
    }
    for (i = 0; i < 3; ++i) {
        tick(level, &step, "tick-sprays");
    }

    /* The crush: the player's own sector rises, and the flag says the move may
     * crush. `leveltime` runs with the step so `!(leveltime&3)` lands every
     * fourth tic. */
    for (i = 0; i < DAMAGE_CRUSH_STEPS; ++i) {
        level->leveltime = i;
        level->sectors[sector].floorHeight += DAMAGE_CRUSH_STEP;
        (void)p_change_sector(&level->context, sector, 1);
        emit(level, &step, "crush");
    }

    /* The death animation: enough thinker walks to reach A_PlayerScream (10 tics
     * into the chain) and A_Fall (20). */
    for (i = 0; i < DAMAGE_TICKS_AFTER_DEATH; ++i) {
        tick(level, &step, "tick-death");
    }

    printf("SUMMARY steps=%d rng=%u crushed=%u kill=%ld item=%ld health=%ld "
           "spawns=%u\n",
           step, (unsigned)level->random.index,
           (unsigned)level->specStats.crushedThings,
           (long)level->playerState.killcount, (long)level->playerState.itemcount,
           (long)level->playerState.health, (unsigned)p_level_mobj_count(level));

    /* The self-check: the script must have done what it documents, or the gate
     * would compare two silent no-ops. */
    printf("INFO scene mobjs=%u pickupItems=%ld killcount=%ld crushed=%u "
           "playerHealth=%ld playerState=%d\n",
           (unsigned)p_level_mobj_count(level),
           (long)level->playerState.itemcount,
           (long)level->playerState.killcount,
           (unsigned)level->specStats.crushedThings,
           (long)level->playerState.health, (int)level->playerState.playerstate);

    if (step != DAMAGE_STEPS || level->playerState.killcount != 3 ||
        level->playerState.itemcount != 2 ||
        level->specStats.crushedThings == 0u || level->playerState.health != 0 ||
        level->playerState.playerstate != P_PST_DEAD) {
        fprintf(stderr, "damage_check: the script did not reach its documented "
                        "outcome (%d steps)\n",
                step);
        return 3;
    }
    return 0;
}

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    p_level_t level;
    p_spawn_rules_t rules;
    uint8_t* bytes;
    uint8_t* arena;
    size_t size = 0;
    const char* mapName = "E1M1";
    int marker;
    int index;
    int result;
    r_mobj_t* mobj;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: damage_check <path-to-WAD> [map]\n");
        return 2;
    }
    if (argc == 3) {
        mapName = argv[2];
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "damage_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "damage_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "damage_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "damage_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(DAMAGE_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "damage_check: no arena\n");
        free(bytes);
        return 2;
    }

    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, DAMAGE_ARENA_BYTES, &rules) !=
        P_SETUP_OK) {
        fprintf(stderr, "damage_check: level load failed with code %d\n",
                (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }
    if (level.player == NULL) {
        fprintf(stderr, "damage_check: %s has no single-player start\n", mapName);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    /*
     * P_LoadThings() already ran P_SpawnPlayer() for the first type-1 record.
     * Keep that mobj and remove every other thing, so the scene is the script's
     * and E1M1's population cannot block a move or absorb a hit. The removal is
     * deferred, so one thinker walk finishes it.
     */
    for (mobj = (r_mobj_t*)p_level_first_mobj(&level); mobj != NULL;
         mobj = (r_mobj_t*)p_level_next_mobj(mobj)) {
        if (mobj != level.player) {
            (void)p_remove_mobj(&level.context, mobj);
        }
    }
    (void)p_run_thinkers(&level.thinkers, &p_level_release_thinker,
                         &level.context, NULL);

    result = run_damage(&level, mapName);

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return result;
}
