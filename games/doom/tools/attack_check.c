/*
 * Host harness for the weapons and the attack path (D4.2d2).
 *
 * It loads E1M1 through lib/doom/p_setup.c, keeps only the avatar that
 * P_LoadThings() already spawned, and then drives the real psprite machine and
 * the real attack path from a fixed script: raise the pistol, fire it and hold
 * the trigger through A_ReFire, change to the shotgun, to the chaingun, dry the
 * chaingun out so P_CheckAmmo picks a replacement, change to the fist and punch a
 * fresh target.
 *
 * tools/attack_check.py compiles this file, runs it next to
 * tools/doom_ref.py --attack, and compares the two outputs line for line.
 *
 * THE PROTOCOL
 *
 *   ATTACK <map> script=<name>
 *   SETUP rng=<n> mobjs=<n> sector=<n> floor=<i32> ceiling=<i32> player=<slot>
 *   STEP <n> op=<name> rng=<n>
 *   PSP <0|1> state=<s> tics=<t> sx=<i32> sy=<i32>
 *   MOBJ <slot> type=<t> x=<i32> y=<i32> z=<i32> health=<i32> flags=0x<8x>
 *       state=<s> tics=<t> tgt=<slot|-1> momx=<i32> momy=<i32>
 *   PLAYER hp=<i32> ammo=<4> w=<mask> pend=<n> ready=<n> refire=<n> atk=<n>
 *       lite=<n> snd=<n> noise=<n>
 *   SUMMARY steps=<n> rng=<n> puffs=<n> bloods=<n> targetHp=<i32>
 *       target2Hp=<i32> mobjs=<n> ready=<n> snd=<n> noise=<n> shots=<n>
 *
 * Every PSP line is one overlay layer: its statenum_t, its remaining tics and the
 * two screen coordinates. `sx`/`sy` are printed because A_WeaponReady's bob,
 * A_Lower's and A_Raise's steps and the flash's follow-the-gun copy are the only
 * observable form of the layer's geometry (nothing draws it yet: the weapon widget
 * is D5.1). Nothing rendered is compared, because nothing is rendered.
 *
 * Every MOBJ line is the *whole* thinker list in insertion order, so a puff, a
 * blood spray or a dropped clip appears as a new slot and a removal closes the
 * gap. The puff and blood positions are what the shot's geometry decides, so a
 * transposed intercept or a wrong slope shows up as a coordinate.
 *
 * `snd` is p_player_t::silentSounds -- every S_* call the weapon actions make --
 * and `noise` is P_FireWeapon's P_NoiseAlert. Neither sound nor alert exists in
 * this port (S2 has no audio layer, T15 owns P_NoiseAlert), so the two counters
 * are the evidence that the branches were reached the right number of times.
 *
 * WHAT IT DOES NOT DO
 *
 * No p_spawn_specials(): sector specials are tools/specials_check.py's subject,
 * and leaving them out keeps the tic clock and the RNG identical on both sides.
 * No sector damage, no monster AI, no P_DeathThink: the avatar never dies here.
 *
 * THE PERTURBATION HOOK
 *
 * P_GUNSHOT_DAMAGE, P_SHOT_SPREAD_SHIFT, P_WEAPONTOP, P_LOWERSPEED and
 * P_MISSILERANGE are #ifndefs in p_pspr.h/p_map.h, so the whole C build can be
 * perturbed with one -D. tools/attack_check.py --perturb does that, which is how
 * the gate is proved able to fail.
 *
 * Build: see tools/attack_check.py. This is a desktop tool: malloc, stdio, the WAD
 * and a wall clock come from the host, none of which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "p_inter.h"
#include "p_map.h"
#include "p_mobj.h"
#include "p_pspr.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_tables.h"
#include "w_wad.h"

#define ATTACK_ARENA_BYTES (256u * 1024u)
#define ATTACK_SCRIPT "weapons-wall-and-target"

/* The one offset from the avatar that both sides use: 40 map units along its
 * spawn facing, which is inside E1M1's start room and closer than MELEERANGE so
 * the fist can reach it too. */
#define ATTACK_TARGET_REACH 40

/* The tic counts of each phase. They are the script: the oracle runs the same
 * numbers, and a change here must change tools/doom_ref.py too. */
#define ATTACK_RAISE_TICS 15
#define ATTACK_HOLD_PISTOL 14
#define ATTACK_LOWER_TICS 15
#define ATTACK_RAISE_WEAPON_TICS 16
#define ATTACK_HOLD_SHOTGUN 20
#define ATTACK_HOLD_CHAINGUN 10
#define ATTACK_DRY_TICS 40
#define ATTACK_HOLD_FIST 6

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

static void print_psprites(const p_player_t* player) {
    int position;

    for (position = 0; position < P_NUMPSPRITES; ++position) {
        const p_pspdef_t* psp = &player->psprites[position];

        printf("PSP %d state=%d tics=%ld sx=%ld sy=%ld\n", position,
               (int)psp->state, (long)psp->tics, (long)psp->sx, (long)psp->sy);
    }
}

static void print_player(const p_player_t* player) {
    unsigned weapons = 0u;
    int i;

    for (i = 0; i < P_NUMWEAPONS; ++i) {
        if (player->weaponowned[i]) {
            weapons |= 1u << i;
        }
    }

    printf("PLAYER hp=%ld ammo=%ld,%ld,%ld,%ld w=%u pend=%ld ready=%ld "
           "refire=%ld atk=%ld lite=%ld snd=%lu noise=%lu\n",
           (long)player->health, (long)player->ammo[P_AM_CLIP],
           (long)player->ammo[P_AM_SHELL], (long)player->ammo[P_AM_CELL],
           (long)player->ammo[P_AM_MISL], weapons, (long)player->pendingweapon,
           (long)player->readyweapon, (long)player->refire,
           (long)player->attackdown, (long)player->extralight,
           (unsigned long)player->silentSounds,
           (unsigned long)player->noiseAlerts);
}

static void emit(p_level_t* level, int* step, const char* op) {
    const r_mobj_t* mobj;
    int slot = 0;

    printf("STEP %d op=%s rng=%u\n", *step, op, (unsigned)level->random.index);
    print_psprites(&level->playerState);
    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        print_mobj(level, slot, mobj);
        slot += 1;
    }
    print_player(&level->playerState);
    *step += 1;
}

/* --- The scripted operations --------------------------------------------- */

/* One game tic: P_Ticker's order (the player think, the thinker walk, the clock).
 * `p_level_tic` is exactly that, which is why the script is one call per step. */
static void tic(p_level_t* level, uint8_t buttons, int* step, const char* op) {
    p_ticcmd_t cmd;

    memset(&cmd, 0, sizeof cmd);
    cmd.buttons = buttons;
    (void)p_level_tic(level, &cmd, NULL);
    emit(level, step, op);
}

static r_mobj_t* spawn_target(p_level_t* level, fixed_t x, fixed_t y) {
    return p_spawn_mobj(&level->context, x, y, ONFLOORZ, MT_POSSESSED, NULL);
}

/* "Aim at it exactly": the shot's direction must not depend on the THING record's
 * angle, so the avatar is turned to face the target the moment it is spawned. */
static void aim_at(r_mobj_t* avatar, const r_mobj_t* target) {
    avatar->angle = p_point_to_angle(target->x - avatar->x,
                                     target->y - avatar->y);
}

static int run_attack(p_level_t* level, const char* mapName) {
    r_mobj_t* avatar = level->player;
    r_mobj_t* target;
    r_mobj_t* target2;
    p_player_t* player = &level->playerState;
    fixed_t targetX;
    fixed_t targetY;
    uint32_t fine;
    uint16_t sector;
    int step = 0;
    int i;
    int puffs;
    int bloods;
    const r_mobj_t* mobj;

    sector = avatar->sector;
    printf("ATTACK %s script=%s\n", mapName, ATTACK_SCRIPT);
    printf("SETUP rng=%u mobjs=%u sector=%u floor=%ld ceiling=%ld player=%d\n",
           (unsigned)level->random.index, (unsigned)p_level_mobj_count(level),
           (unsigned)sector, (long)level->sectors[sector].floorHeight,
           (long)level->sectors[sector].ceilingHeight, slot_of(level, avatar));

    /* The target's spot, computed once from the spawn facing and reused for the
     * second target so both stand in the same provably open place. */
    fine = avatar->angle >> ANGLETOFINESHIFT;
    targetX = avatar->x +
              (int32_t)ATTACK_TARGET_REACH * r_fine_sine[fine + FINEANGLES / 4];
    targetY = avatar->y + (int32_t)ATTACK_TARGET_REACH * r_fine_sine[fine];

    target = spawn_target(level, targetX, targetY);
    if (target == NULL) {
        fprintf(stderr, "attack_check: the zone refused the first target\n");
        return 2;
    }
    aim_at(avatar, target);

    /* The weapons P_GiveWeapon would hand over, then the ammo forced to the
     * script's numbers so a change in the give rules cannot move the baseline. */
    (void)p_give_weapon(&level->context, player, P_WP_SHOTGUN, 0);
    (void)p_give_weapon(&level->context, player, P_WP_CHAINGUN, 0);
    player->ammo[P_AM_CLIP] = 50;
    player->ammo[P_AM_SHELL] = 20;
    player->ammo[P_AM_CELL] = 0;
    player->ammo[P_AM_MISL] = 0;

    emit(level, &step, "setup-weapons");
    p_setup_psprites(&level->context, player);
    emit(level, &step, "setup-psprites");

    for (i = 0; i < ATTACK_RAISE_TICS; ++i) {
        tic(level, 0, &step, "raise-pistol");
    }
    tic(level, BT_ATTACK, &step, "fire-pistol");
    for (i = 0; i < ATTACK_HOLD_PISTOL; ++i) {
        tic(level, BT_ATTACK, &step, "pistol-hold");
    }
    tic(level, 0, &step, "pistol-release");

    tic(level, (uint8_t)(BT_CHANGE | (2u << BT_WEAPONSHIFT)), &step,
        "change-shotgun");
    for (i = 0; i < ATTACK_LOWER_TICS; ++i) {
        tic(level, 0, &step, "lower-for-shotgun");
    }
    for (i = 0; i < ATTACK_RAISE_WEAPON_TICS; ++i) {
        tic(level, 0, &step, "raise-shotgun");
    }
    tic(level, BT_ATTACK, &step, "fire-shotgun");
    for (i = 0; i < ATTACK_HOLD_SHOTGUN; ++i) {
        tic(level, BT_ATTACK, &step, "shotgun-hold");
    }
    tic(level, 0, &step, "shotgun-release");

    tic(level, (uint8_t)(BT_CHANGE | (3u << BT_WEAPONSHIFT)), &step,
        "change-chaingun");
    for (i = 0; i < ATTACK_LOWER_TICS; ++i) {
        tic(level, 0, &step, "lower-for-chaingun");
    }
    for (i = 0; i < ATTACK_RAISE_WEAPON_TICS; ++i) {
        tic(level, 0, &step, "raise-chaingun");
    }
    tic(level, BT_ATTACK, &step, "fire-chaingun");
    for (i = 0; i < ATTACK_HOLD_CHAINGUN; ++i) {
        tic(level, BT_ATTACK, &step, "chaingun-hold");
    }

    /* One clip left, then the trigger keeps going: the gun clicks empty,
     * A_ReFire asks P_CheckAmmo, and the preference order picks the shotgun. */
    player->ammo[P_AM_CLIP] = 1;
    emit(level, &step, "clips-one");
    for (i = 0; i < ATTACK_DRY_TICS; ++i) {
        tic(level, BT_ATTACK, &step, "chaingun-dry");
    }
    tic(level, 0, &step, "dry-release");

    tic(level, (uint8_t)(BT_CHANGE | (0u << BT_WEAPONSHIFT)), &step,
        "change-fist");
    for (i = 0; i < ATTACK_LOWER_TICS; ++i) {
        tic(level, 0, &step, "lower-for-fist");
    }
    for (i = 0; i < ATTACK_RAISE_WEAPON_TICS; ++i) {
        tic(level, 0, &step, "raise-fist");
    }

    /* A fresh target where the first stood: the fist's trace passes the corpse
     * without a blood spray (it is not MF_SHOOTABLE any more), so the punch needs
     * something alive to hit. */
    target2 = spawn_target(level, targetX, targetY);
    if (target2 == NULL) {
        fprintf(stderr, "attack_check: the zone refused the second target\n");
        return 2;
    }
    aim_at(avatar, target2);
    emit(level, &step, "target-two");
    tic(level, BT_ATTACK, &step, "fire-fist");
    for (i = 0; i < ATTACK_HOLD_FIST; ++i) {
        tic(level, BT_ATTACK, &step, "fist-hold");
    }
    tic(level, 0, &step, "fist-release");

    puffs = 0;
    bloods = 0;
    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        if (mobj->type == (int16_t)MT_PUFF) {
            puffs += 1;
        }
        if (mobj->type == (int16_t)MT_BLOOD) {
            bloods += 1;
        }
    }

    printf("SUMMARY steps=%d rng=%u puffs=%d bloods=%d targetHp=%ld "
           "target2Hp=%ld mobjs=%u ready=%ld snd=%lu noise=%lu shots=%u\n",
           step, (unsigned)level->random.index, puffs, bloods,
           (long)target->health, (long)target2->health,
           (unsigned)p_level_mobj_count(level), (long)player->readyweapon,
           (unsigned long)player->silentSounds,
           (unsigned long)player->noiseAlerts,
           (unsigned)level->specStats.triggers);

    /* The self-check: the script must have done what it documents, or the gate
     * compares two silent no-ops. */
    printf("INFO scene mobjs=%u puffs=%d bloods=%d targetHp=%ld target2Hp=%ld "
           "ammo=%ld,%ld ready=%ld snd=%lu noise=%lu\n",
           (unsigned)p_level_mobj_count(level), puffs, bloods,
           (long)target->health, (long)target2->health,
           (long)player->ammo[P_AM_CLIP], (long)player->ammo[P_AM_SHELL],
           (long)player->readyweapon, (unsigned long)player->silentSounds,
           (unsigned long)player->noiseAlerts);

    if (step != 213 || target->health >= 20 || target2->health >= 20 ||
        bloods == 0 || player->ammo[P_AM_CLIP] != 0 ||
        player->readyweapon != (int32_t)P_WP_FIST) {
        fprintf(stderr, "attack_check: the script did not reach its documented "
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
        fprintf(stderr, "usage: attack_check <path-to-WAD> [map]\n");
        return 2;
    }
    if (argc == 3) {
        mapName = argv[2];
    }

    bytes = readFile(argv[1], &size);
    if (bytes == NULL) {
        fprintf(stderr, "attack_check: cannot read %s\n", argv[1]);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "attack_check: not a WAD\n");
        free(bytes);
        return 2;
    }
    marker = W_CheckNumForName(&wad, mapName);
    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad.numLumps) {
        fprintf(stderr, "attack_check: %s is not in this WAD\n", mapName);
        free(bytes);
        return 2;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source.data[index] = W_LumpPointer(&wad, lump);
        source.size[index] = (size_t)W_LumpLength(&wad, lump);
    }
    if (doom_map_open(&map, &source) != DOOM_MAP_OK) {
        fprintf(stderr, "attack_check: map rejected with doom_map error %d\n",
                (int)map.error);
        free(bytes);
        return 2;
    }

    arena = (uint8_t*)malloc(ATTACK_ARENA_BYTES);
    if (arena == NULL) {
        fprintf(stderr, "attack_check: no arena\n");
        free(bytes);
        return 2;
    }

    rules = p_spawn_rules_default();
    if (p_level_init(&level, &map, arena, ATTACK_ARENA_BYTES, &rules) !=
        P_SETUP_OK) {
        fprintf(stderr, "attack_check: level load failed with code %d\n",
                (int)level.error);
        free(arena);
        free(bytes);
        return 2;
    }
    if (level.player == NULL) {
        fprintf(stderr, "attack_check: %s has no single-player start\n", mapName);
        p_level_shutdown(&level);
        free(arena);
        free(bytes);
        return 2;
    }

    /*
     * P_LoadThings() already ran P_SpawnPlayer() for the first type-1 record.
     * Keep that mobj and remove every other thing, so the scene is the script's
     * and E1M1's population cannot block a shot or absorb it. The removal is
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

    result = run_attack(&level, mapName);

    p_level_shutdown(&level);
    free(arena);
    free(bytes);
    return result;
}
