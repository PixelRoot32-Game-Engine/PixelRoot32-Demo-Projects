/*
 * Host harness for demo playback (D4.4): the recorded stream, played through the
 * world's own tic, measured and hashed.
 *
 * WHAT THIS IS FOR
 *
 * The shareware WAD carries three recorded demos -- DEMO1, DEMO2 and DEMO3 -- and
 * a demo is the strongest available proof of simulation fidelity a port can
 * produce: it is 2,134 to 5,026 tics of upstream's own commands, and it stays
 * coherent only if every P_Random() draw, every state transition and every
 * thinker order is upstream's. This harness plays one demo from its header to
 * its terminator and reports four kinds of evidence:
 *
 *   1. THE STREAM. The header's fields, and the demo's own recorded tic count
 *      (the lump's length), so "the run consumed the whole stream" is a number
 *      and not a claim. The command digest is a SHA-256 of the stream as
 *      decoded, which tools/demo_check.py recomputes from the WAD with its own
 *      reader: two independent implementations must agree on every byte.
 *   2. THE END STATE. The player's position, facing, health, armor, ammo, the
 *      kill/item/secret totals and the live mobj count, plus a SHA-256 of that
 *      state and of every sector and mobj behind it.
 *   3. THE HASH CHAIN. The same state hash every CHECK_TICS tics and one
 *      SHA-256 over the chain, so a divergence is pinned to a window of tics
 *      instead of only to the end.
 *   4. COHERENCE. Facts about the run that do not need a reference port: the
 *      player's centre never crosses a one-sided linedef and never leaves the
 *      map, the recorded turn is applied exactly, forward commands produce
 *      forward motion, the demo opens doors, its USE presses trigger lines, it
 *      shoots and spends ammo, and its damage is bounded by its health.
 *
 * WHAT A PIN PROVES, AND WHAT IT DOES NOT
 *
 * There is no reference port on this machine and no frame-by-frame oracle for a
 * whole level's simulation, so this harness cannot prove that a run matches 1993
 * DOOM. The hashes are a REGRESSION PIN: they are the values this port produced
 * once, recorded so that any later change to the simulation that moves the state
 * fails the gate loudly. A pin says "the simulation did not change since the pin
 * was taken". It says nothing about whether it was right when the pin was taken,
 * and a commit that re-baselines it without an explanation is a commit that
 * hides a change.
 *
 * What a run *can* prove without a reference is falsification, and the demos
 * offer one check of that kind from their own data. Every shareware demo dies
 * and then carries input for thousands of tics more, including a USE hold just
 * after the death. In linuxdoom-1.10 a single-player death runs P_DeathThink to
 * PST_REBORN, and G_DoReborn answers it with `gameaction = ga_loadlevel` -- the
 * same level, reloaded, after which playback continues; G_CheckDemoStatus is
 * reached at the marker, never on a death. So the falsifiable statement is not
 * "the player never dies" (all three die) but "the corpse is never what the
 * stream is played on": a run whose player ends the stream not PST_LIVE, or
 * which never reloaded after its death, is not vanilla's. ALIVE reports whether
 * the player is live at the last command and how many reloads answered its
 * deaths, and VERDICT folds that together with the missing-action count
 * p_action.c keeps.
 *
 * THE PROTOCOL (tools/demo_check.py parses it; the order is part of the format)
 *
 *   DEMO name=DEMO1 version=109 skill=2 episode=1 map=5 deathmatch=0 respawn=0
 *       fast=0 nomonsters=0 consoleplayer=0 players=1,0,0,0 bytes=20118 tics=5026
 *   CMD count=5026 lastinput=5001 digest=<sha256>
 *   WORLD map=E1M5 spawned=216 kills=91 items=29 secrets=9 mobjs=216 rng=120
 *       specials=14 triggers=0
 *   ARENA load=<inUse> peak=<peak> free=<free> overhead=<overhead>
 *   CHECK n=100 hash=<sha256>            one line per CHECK_TICS commands, in order
 *   RELOAD n=<tic> x=<x> y=<y> health=<h> mobjs=<c> leveltime=0 arena=<inUse>
 *   FINAL n=5026 hash=<sha256>
 *   CHAIN count=51 hash=<sha256>
 *   STATE n=5026 x=-14680064 y=-40894464 z=0 angle=0 health=0 armor=0
 *       ammo=0,0,0,0 kills=7 items=4 secrets=0 mobjs=208
 *   RUN tics=5026 stop=marker leveltime=<lt> declared=5026
 *       `leveltime` is the world's clock when the stream ended, not the stream's
 *       position: a reload restarts it at P_SetupLevel's `leveltime = 0;`, so a
 *       run of 5,026 commands that reloaded ends with a smaller number.
 *   COHERE wallcross=0 outside=0 angleskip=<n> anglebad=0 anglepunch=<n>
 *       sectorchanges=<n> sectors=<n> doorpass=<n> doorsopen=<n> forward=<n>
 *       netfwd=<i64> netback=<i64> shots=<n> ammo=<n> attackticks=<n>
 *       useticks=<n> useedges=<n> usetriggers=<n> hpmin=<n> hpmax=<n>
 *       hploss=<n> hpgain=<n> deadtics=<n> mobjmax=<n>
 *   SPEC triggers=<n> unsupported=<n> deferred=<n> scrolls=<n>
 *       the line specials the playback reached, as run totals: the level's own
 *       counters are zeroed by p_spawn_specials() (which a reload calls), so the
 *       harness adds each tic's delta to its own totals.
 *   ANOM missing=<n> firsttic=<n> first=<action> last=<action>
 *   MISSING count=<n> overflow=<n> <id>@<tic>:<type> ...
 *   ALIVE alive=<0|1> lastinput=<n> deathtic=<n|-1> reborns=<n>
 *   VERDICT faithful=<0|1> reason=<none|action|death>
 *
 * `n` counts commands consumed, from 1, so `n=5026` is DEMO1's whole stream; a
 * RELOAD's `n` is the command whose tic set PST_REBORN, and a reload consumes no
 * command of its own. ARENA and the RELOAD `arena=` field are z_stats().inUse,
 * the payload capacity of the level's live blocks: a reload re-lays the same
 * arena, so every one of them must equal ARENA's `load`.
 *
 * `anglebad` counts commands whose recorded angleturn was not applied exactly;
 * only commands where P_MovePlayer ran are compared, and `angleskip` is the
 * rest, which are a dead player or a reaction time and are expected.
 * `anglepunch` is the third case: A_Punch's `player->mo->angle =
 * P_PointToAngle2(...)` turns the avatar onto the thing it hit, which is
 * upstream's own rule and is counted rather than excused. `netfwd` and
 * `netback` are the displacement projected on the facing in 16.16 map units:
 * one number for "the commands move the player forward" and one for the residual
 * a turn leaves in the momentum.
 *
 * USAGE
 *
 *   demo_check <path-to-WAD> <DEMO lump> [--perturb skip]
 *
 *   --perturb skip   discard the first command before playing, so the whole
 *                    stream runs one tic out of step with the world. The final
 *                    hash must change: the pin is a function of the commands.
 *
 * The WAD is copyrighted shareware data and is never committed; point this tool
 * at a file outside the repository. tools/demo_check.py builds it into a
 * temporary directory it removes before returning.
 *
 * This is a desktop tool: malloc, stdio and the WAD come from the host, none of
 * which the firmware uses.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doom_map.h"
#include "g_game.h"
#include "p_action.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"
#include "r_bsp.h"
#include "r_tables.h"
#include "w_wad.h"

/* One CHECK line every this many commands. 100 keeps DEMO1's chain at 51
 * entries: short enough to diff, dense enough to place a divergence inside 100
 * tics. */
#define CHECK_TICS 100u

/* The hash's layout version. Bumping it invalidates every pin on purpose: a
 * field added to the state cannot keep an old number meaningful. */
#define STATE_HASH_VERSION 1u

/* --- SHA-256 (FIPS 180-4) ------------------------------------------------ */

typedef struct sha256 {
    uint32_t state[8];
    uint64_t bits;
    uint8_t buffer[64];
    size_t used;
} sha256_t;

static const uint32_t kRound[64] = {
    0x428A2F98ul, 0x71374491ul, 0xB5C0FBCFul, 0xE9B5DBA5ul,
    0x3956C25Bul, 0x59F111F1ul, 0x923F82A4ul, 0xAB1C5ED5ul,
    0xD807AA98ul, 0x12835B01ul, 0x243185BEul, 0x550C7DC3ul,
    0x72BE5D74ul, 0x80DEB1FEul, 0x9BDC06A7ul, 0xC19BF174ul,
    0xE49B69C1ul, 0xEFBE4786ul, 0x0FC19DC6ul, 0x240CA1CCul,
    0x2DE92C6Ful, 0x4A7484AAul, 0x5CB0A9DCul, 0x76F988DAul,
    0x983E5152ul, 0xA831C66Dul, 0xB00327C8ul, 0xBF597FC7ul,
    0xC6E00BF3ul, 0xD5A79147ul, 0x06CA6351ul, 0x14292967ul,
    0x27B70A85ul, 0x2E1B2138ul, 0x4D2C6DFCul, 0x53380D13ul,
    0x650A7354ul, 0x766A0ABBul, 0x81C2C92Eul, 0x92722C85ul,
    0xA2BFE8A1ul, 0xA81A664Bul, 0xC24B8B70ul, 0xC76C51A3ul,
    0xD192E819ul, 0xD6990624ul, 0xF40E3585ul, 0x106AA070ul,
    0x19A4C116ul, 0x1E376C08ul, 0x2748774Cul, 0x34B0BCB5ul,
    0x391C0CB3ul, 0x4ED8AA4Aul, 0x5B9CCA4Ful, 0x682E6FF3ul,
    0x748F82EEul, 0x78A5636Ful, 0x84C87814ul, 0x8CC70208ul,
    0x90BEFFFAul, 0xA4506CEBul, 0xBEF9A3F7ul, 0xC67178F2ul
};

static uint32_t rotateRight(uint32_t value, unsigned int count) {
    return (value >> count) | (value << (32u - count));
}

static void sha256Block(sha256_t* context, const uint8_t* block) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    unsigned int index;

    for (index = 0; index < 16u; ++index) {
        words[index] = ((uint32_t)block[index * 4u] << 24)
                     | ((uint32_t)block[index * 4u + 1u] << 16)
                     | ((uint32_t)block[index * 4u + 2u] << 8)
                     | (uint32_t)block[index * 4u + 3u];
    }
    for (index = 16u; index < 64u; ++index) {
        const uint32_t s0 = rotateRight(words[index - 15u], 7u)
                          ^ rotateRight(words[index - 15u], 18u)
                          ^ (words[index - 15u] >> 3);
        const uint32_t s1 = rotateRight(words[index - 2u], 17u)
                          ^ rotateRight(words[index - 2u], 19u)
                          ^ (words[index - 2u] >> 10);
        words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (index = 0; index < 64u; ++index) {
        const uint32_t s1 = rotateRight(e, 6u) ^ rotateRight(e, 11u) ^ rotateRight(e, 25u);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + choose + kRound[index] + words[index];
        const uint32_t s0 = rotateRight(a, 2u) ^ rotateRight(a, 13u) ^ rotateRight(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256Init(sha256_t* context) {
    context->state[0] = 0x6A09E667ul;
    context->state[1] = 0xBB67AE85ul;
    context->state[2] = 0x3C6EF372ul;
    context->state[3] = 0xA54FF53Aul;
    context->state[4] = 0x510E527Ful;
    context->state[5] = 0x9B05688Cul;
    context->state[6] = 0x1F83D9ABul;
    context->state[7] = 0x5BE0CD19ul;
    context->bits = 0u;
    context->used = 0u;
}

static void sha256Update(sha256_t* context, const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t index = 0u;

    context->bits += (uint64_t)size * 8u;

    while (index < size) {
        const size_t room = 64u - context->used;
        const size_t take = (size - index) < room ? (size - index) : room;

        memcpy(context->buffer + context->used, bytes + index, take);
        context->used += take;
        index += take;

        if (context->used == 64u) {
            sha256Block(context, context->buffer);
            context->used = 0u;
        }
    }
}

static void sha256Final(sha256_t* context, uint8_t out[32]) {
    const uint64_t bits = context->bits;
    static const uint8_t pad = 0x80u;
    uint8_t zero = 0u;
    uint8_t length[8];
    unsigned int index;

    sha256Update(context, &pad, 1u);
    while (context->used != 56u) {
        sha256Update(context, &zero, 1u);
    }
    for (index = 0; index < 8u; ++index) {
        length[7u - index] = (uint8_t)((bits >> (index * 8u)) & 0xFFu);
    }
    sha256Update(context, length, sizeof length);

    for (index = 0; index < 8u; ++index) {
        out[index * 4u] = (uint8_t)((context->state[index] >> 24) & 0xFFu);
        out[index * 4u + 1u] = (uint8_t)((context->state[index] >> 16) & 0xFFu);
        out[index * 4u + 2u] = (uint8_t)((context->state[index] >> 8) & 0xFFu);
        out[index * 4u + 3u] = (uint8_t)(context->state[index] & 0xFFu);
    }
}

static void digestToHex(const uint8_t digest[32], char hex[65]) {
    static const char digits[] = "0123456789abcdef";
    unsigned int index;

    for (index = 0; index < 32u; ++index) {
        hex[index * 2u] = digits[(digest[index] >> 4) & 0x0Fu];
        hex[index * 2u + 1u] = digits[digest[index] & 0x0Fu];
    }
    hex[64] = '\0';
}

/* --- Little-endian writers into a hash ----------------------------------- */

/*
 * Every number reaches the hash as four little-endian bytes, so the pin does
 * not depend on the host's endianness or a struct's padding.
 */
static void hashU32(sha256_t* context, uint32_t value) {
    const uint8_t bytes[4] = {
        (uint8_t)(value & 0xFFu),
        (uint8_t)((value >> 8) & 0xFFu),
        (uint8_t)((value >> 16) & 0xFFu),
        (uint8_t)((value >> 24) & 0xFFu)
    };

    sha256Update(context, bytes, sizeof bytes);
}

static void hashI32(sha256_t* context, int32_t value) {
    hashU32(context, (uint32_t)value);
}

static void hashU8(sha256_t* context, uint8_t value) {
    sha256Update(context, &value, 1u);
}

/* --- Files and map ------------------------------------------------------- */

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

/* "E%dM%d", the name W_CheckNumForName wants. A version-109 header stores
 * episode and map as single bytes, so the largest name it can spell is "E9M9"
 * and an eight-byte buffer is enough. */
static void mapNameOf(const g_demo_header_t* header, char out[8]) {
    out[0] = 'E';
    out[1] = (char)('0' + header->episode);
    out[2] = 'M';
    out[3] = (char)('0' + header->map);
    out[4] = '\0';
}

static int openMap(const wad_state_t* wad, const char* name, doom_map_t* map,
                   doom_map_source_t* source) {
    const int marker = W_CheckNumForName(wad, name);
    int index;

    if (marker < 0 || marker + 1 + DOOM_MAP_LUMP_COUNT > wad->numLumps) {
        return 0;
    }
    for (index = 0; index < DOOM_MAP_LUMP_COUNT; ++index) {
        const int lump = marker + 1 + index;

        source->data[index] = W_LumpPointer(wad, lump);
        source->size[index] = (size_t)W_LumpLength(wad, lump);
    }
    return doom_map_open(map, source) == DOOM_MAP_OK;
}

/* --- Coherence geometry -------------------------------------------------- */

/*
 * A strict straddle test between segment (a,b) and segment (c,d). It is the
 * harness's own geometry, not a DOOM rule: a segment that crosses a one-sided
 * linedef has a wall between its endpoints whatever the collision code believes.
 * The products are 64-bit because a map coordinate is a 16.16 int32.
 */
static int segmentsCross(fixed_t ax, fixed_t ay, fixed_t bx, fixed_t by,
                         fixed_t cx, fixed_t cy, fixed_t dx, fixed_t dy) {
    const int64_t d1 = (int64_t)(bx - ax) * (int64_t)(cy - ay) -
                       (int64_t)(by - ay) * (int64_t)(cx - ax);
    const int64_t d2 = (int64_t)(bx - ax) * (int64_t)(dy - ay) -
                       (int64_t)(by - ay) * (int64_t)(dx - ax);
    const int64_t d3 = (int64_t)(dx - cx) * (int64_t)(ay - cy) -
                       (int64_t)(dy - cy) * (int64_t)(ax - cx);
    const int64_t d4 = (int64_t)(dx - cx) * (int64_t)(by - cy) -
                       (int64_t)(dy - cy) * (int64_t)(bx - cx);

    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

/* How many one-sided linedefs the player's step crossed. A step that crosses a
 * wall leaves the player on the far side of it: P_TryMove is the rule that
 * forbids it, and this is the rule's observable consequence. */
static uint32_t crossedWall(const doom_map_t* map, fixed_t ax, fixed_t ay,
                            fixed_t bx, fixed_t by) {
    uint32_t index;
    uint32_t crossed = 0u;

    for (index = 0; index < map->lumps[DOOM_MAP_LINEDEFS].count; ++index) {
        doom_line_t line;
        doom_vertex_t v1;
        doom_vertex_t v2;

        doom_map_get_line(map, (uint16_t)index, &line);
        if ((line.flags & DOOM_ML_TWOSIDED) != 0) {
            continue;
        }
        doom_map_get_vertex(map, line.v1, &v1);
        doom_map_get_vertex(map, line.v2, &v2);
        if (segmentsCross(ax, ay, bx, by, v1.x, v1.y, v2.x, v2.y) != 0) {
            crossed += 1u;
        }
    }
    return crossed;
}

/* The sector a point stands in, or 0xFFFF when the point is outside the map. */
static uint16_t sectorAt(const doom_map_t* map, fixed_t x, fixed_t y, int* inside) {
    uint16_t subsector = 0;
    doom_subsector_t record;

    *inside = r_bsp_point_in_subsector(map, x, y, &subsector) == R_BSP_OK;
    if (*inside == 0) {
        return 0xFFFFu;
    }
    doom_map_get_subsector(map, subsector, &record);
    return doom_map_subsector_sector(map, &record);
}

/* --- The state hash ------------------------------------------------------ */

/*
 * Hashes one moment of the world. The field list is deliberate and is the pin's
 * subject:
 *
 *   header     STATE_HASH_VERSION, the tic, the leveltime
 *   random     the P_Random() position the next tic's draws continue from
 *   player     position, momentum, facing, both healths (the mobj's and the
 *              record's), the mobj's state and tics, armor, the four ammo pools,
 *              their ceilings, both weapon slots, refire, the three counters,
 *              the view height and its bob, the death state, the light level
 *   sectors    every sector's floor, ceiling, light and special: doors, lifts,
 *              crushers and light flashes are state, not decoration
 *   mobjs      every thinker-tagged mobj in insertion order: type, position,
 *              momentum, health, flags, state, tics, facing, reaction time, the
 *              chase counters and whether it has a target
 *
 * In insertion order on purpose: the thinker list's order is something a demo
 * depends on, so a reordering must move the hash.
 */
static void hashState(sha256_t* context, const p_level_t* level, uint32_t tic) {
    const p_player_t* player = &level->playerState;
    const r_mobj_t* mobj;
    uint16_t sector;
    uint32_t count = 0u;
    int field;

    hashU32(context, STATE_HASH_VERSION);
    hashU32(context, tic);
    hashI32(context, level->leveltime);
    hashI32(context, level->random.index);

    if (player->mo != NULL) {
        hashI32(context, player->mo->x);
        hashI32(context, player->mo->y);
        hashI32(context, player->mo->z);
        hashU32(context, player->mo->angle);
        hashI32(context, player->mo->momx);
        hashI32(context, player->mo->momy);
        hashI32(context, player->mo->momz);
        hashI32(context, player->mo->health);
        hashI32(context, player->mo->state);
        hashI32(context, player->mo->tics);
        hashU32(context, player->mo->flags);
    } else {
        for (field = 0; field < 11; ++field) {
            hashI32(context, 0);
        }
    }
    hashI32(context, player->health);
    hashI32(context, player->armorpoints);
    hashI32(context, player->armortype);
    hashI32(context, player->ammo[P_AM_CLIP]);
    hashI32(context, player->ammo[P_AM_SHELL]);
    hashI32(context, player->ammo[P_AM_CELL]);
    hashI32(context, player->ammo[P_AM_MISL]);
    hashI32(context, player->maxammo[P_AM_CLIP]);
    hashI32(context, player->maxammo[P_AM_SHELL]);
    hashI32(context, player->maxammo[P_AM_CELL]);
    hashI32(context, player->maxammo[P_AM_MISL]);
    hashI32(context, player->readyweapon);
    hashI32(context, player->pendingweapon);
    hashI32(context, player->refire);
    hashI32(context, player->killcount);
    hashI32(context, player->itemcount);
    hashI32(context, player->secretcount);
    hashI32(context, player->viewz);
    hashI32(context, player->viewheight);
    hashI32(context, player->deltaviewheight);
    hashI32(context, player->bob);
    hashI32(context, (int32_t)player->playerstate);
    hashI32(context, player->extralight);

    hashU32(context, level->sectorCount);
    for (sector = 0; sector < level->sectorCount; ++sector) {
        hashI32(context, level->sectors[sector].floorHeight);
        hashI32(context, level->sectors[sector].ceilingHeight);
        hashI32(context, level->sectors[sector].lightLevel);
        hashI32(context, level->sectors[sector].special);
    }

    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        count += 1u;
    }
    hashU32(context, count);
    for (mobj = p_level_first_mobj(level); mobj != NULL;
         mobj = p_level_next_mobj(mobj)) {
        hashI32(context, (int32_t)mobj->type);
        hashI32(context, mobj->x);
        hashI32(context, mobj->y);
        hashI32(context, mobj->z);
        hashI32(context, mobj->momx);
        hashI32(context, mobj->momy);
        hashI32(context, mobj->momz);
        hashI32(context, mobj->health);
        hashU32(context, mobj->flags);
        hashI32(context, mobj->state);
        hashI32(context, mobj->tics);
        hashU32(context, mobj->angle);
        hashI32(context, mobj->reactiontime);
        hashI32(context, mobj->movecount);
        hashI32(context, mobj->threshold);
        hashU8(context, (uint8_t)(mobj->target != NULL ? 1u : 0u));
    }
}

/* --- The stream's own record --------------------------------------------- */

/*
 * The command digest: the four decoded fields re-encoded as the lump's own four
 * bytes, so tools/demo_check.py reproduces it from the raw stream without
 * knowing anything about this harness's types.
 */
static void digestCommand(sha256_t* context, const p_ticcmd_t* cmd) {
    hashU8(context, (uint8_t)cmd->forwardmove);
    hashU8(context, (uint8_t)cmd->sidemove);
    hashU8(context, (uint8_t)((uint16_t)cmd->angleturn >> 8));
    hashU8(context, cmd->buttons);
}

static int commandIsEmpty(const p_ticcmd_t* cmd) {
    return cmd->forwardmove == 0 && cmd->sidemove == 0 &&
           cmd->angleturn == 0 && cmd->buttons == 0;
}

/* --- The run ------------------------------------------------------------- */

typedef struct run_totals {
    uint32_t tics;
    uint32_t checkpoints;
    uint32_t sectorChanges;
    uint32_t sectorsVisited;
    uint32_t doorPass;
    uint32_t doorsOpen;
    uint32_t wallCross;
    uint32_t outside;
    uint32_t angleSkip;
    uint32_t angleBad;
    uint32_t anglePunch;
    uint32_t forwardTics;
    uint32_t shots;
    uint32_t attackTics;
    uint32_t useTics;
    uint32_t useEdges;
    uint32_t useTriggers;
    uint32_t deadTics;
    uint32_t mobjMax;
    uint32_t missing;
    uint32_t reloads;
    /* line specials the run's playback reached: triggers, unsupported,
     * deferred, scrolls (p_spec.h's p_spec_stats_t order). */
    uint32_t spec[4];
    uint32_t missingFirstTic;
    uint32_t missingFirstAction;
    uint32_t missingLastAction;
    int64_t netForward;
    int64_t netBackward;
    int64_t ammoSpent;
    int32_t hpMin;
    int32_t hpMax;
    int64_t hpLoss;
    int64_t hpGain;
    int32_t lastInputTic;
    int32_t deathTic;
    char finalHash[65];
    char chainHash[65];
} run_totals_t;

static void usage(void) {
    fprintf(stderr, "usage: demo_check <path-to-WAD> <DEMO lump> "
                    "[--perturb skip]\n");
}

int main(int argc, char** argv) {
    wad_state_t wad;
    doom_map_source_t source;
    doom_map_t map;
    g_demo_t demo;
    g_demo_t scan;
    g_demo_run_t run;
    p_level_t level;
    p_spawn_rules_t rules;
    g_demo_error_t error;
    uint8_t* bytes;
    uint8_t* arena;
    uint8_t* visited;
    fixed_t* initialCeiling;
    size_t size = 0;
    const char* wadPath;
    const char* demoName;
    char mapName[8];
    run_totals_t totals;
    p_action_trace_t missingTrace;
    p_spec_trace_t unsupportedTrace;
    sha256_t commandHash;
    sha256_t chainHash;
    sha256_t checkHash;
    char hex[65];
    uint8_t digest[32];
    uint32_t tic = 0;
    uint32_t missingBefore = 0;
    uint32_t specBefore[4] = {0u, 0u, 0u, 0u};
    uint32_t pressBefore = 0;
    uint32_t useLineHitsBefore = 0;
    fixed_t prevX = 0;
    fixed_t prevY = 0;
    angle_t prevAngle = 0;
    int16_t prevReaction = 0;
    int32_t prevReady = 0;
    int32_t prevHealth = 0;
    int32_t prevAmmo[P_NUMAMMO] = {0, 0, 0, 0};
    uint16_t prevSector = 0xFFFFu;
    uint8_t prevButtons = 0;
    int perturbSkip = 0;
    int argvIndex;
    int pool;
    uint16_t sectorCount;
    uint16_t index16;
    int inside = 0;

    if (argc < 3) {
        usage();
        return 2;
    }
    wadPath = argv[1];
    demoName = argv[2];
    for (argvIndex = 3; argvIndex < argc; ++argvIndex) {
        if (strcmp(argv[argvIndex], "--perturb") == 0 && argvIndex + 1 < argc &&
            strcmp(argv[argvIndex + 1], "skip") == 0) {
            perturbSkip = 1;
            argvIndex += 1;
        } else {
            fprintf(stderr, "demo_check: unknown argument %s\n", argv[argvIndex]);
            return 2;
        }
    }

    bytes = readFile(wadPath, &size);
    if (bytes == NULL) {
        fprintf(stderr, "demo_check: cannot read %s\n", wadPath);
        return 2;
    }
    if (wad_open(&wad, bytes, size) != WAD_ERR_OK) {
        fprintf(stderr, "demo_check: %s is not a WAD\n", wadPath);
        free(bytes);
        return 2;
    }

    {
        const int lump = W_CheckNumForName(&wad, demoName);

        if (lump < 0) {
            fprintf(stderr, "demo_check: %s carries no %s lump\n", wadPath,
                    demoName);
            free(bytes);
            return 2;
        }
        error = g_demo_open(&demo, (const uint8_t*)W_LumpPointer(&wad, lump),
                            (uint32_t)W_LumpLength(&wad, lump));
    }
    if (error != G_DEMO_OK) {
        fprintf(stderr, "demo_check: %s rejected with g_demo error %d\n",
                demoName, (int)error);
        free(bytes);
        return 2;
    }
    printf("DEMO name=%s version=%u skill=%u episode=%u map=%u deathmatch=%u "
           "respawn=%u fast=%u nomonsters=%u consoleplayer=%u "
           "players=%u,%u,%u,%u bytes=%u tics=%u\n",
           demoName, (unsigned)demo.header.version, (unsigned)demo.header.skill,
           (unsigned)demo.header.episode, (unsigned)demo.header.map,
           (unsigned)demo.header.deathmatch, (unsigned)demo.header.respawn,
           (unsigned)demo.header.fast, (unsigned)demo.header.nomonsters,
           (unsigned)demo.header.consoleplayer,
           (unsigned)demo.header.playerInGame[0],
           (unsigned)demo.header.playerInGame[1],
           (unsigned)demo.header.playerInGame[2],
           (unsigned)demo.header.playerInGame[3], demo.size,
           g_demo_tics(&demo));

    memset(&totals, 0, sizeof totals);
    totals.hpMin = 100;
    totals.hpMax = 100;
    totals.lastInputTic = -1;
    totals.deathTic = -1;

    /* The stream's own claims, read once before the world exists. The record is
     * a cursor over the caller's bytes, so a struct copy is a second reader over
     * the same lump -- the playback below starts from its own untouched copy. */
    scan = demo;
    sha256Init(&commandHash);
    {
        p_ticcmd_t cmd;
        uint32_t index = 0u;

        for (;;) {
            const g_demo_error_t read = g_demo_read(&scan, &cmd);

            if (read != G_DEMO_OK) {
                break;
            }
            digestCommand(&commandHash, &cmd);
            if (!commandIsEmpty(&cmd)) {
                totals.lastInputTic = (int32_t)index;
            }
            index += 1u;
        }
    }
    sha256Final(&commandHash, digest);
    digestToHex(digest, hex);
    printf("CMD count=%u lastinput=%ld digest=%s\n", g_demo_tics(&demo),
           (long)totals.lastInputTic, hex);

    /* The level the header names. A demo that says E1M5 is played on E1M5: the
     * map is the header's, never the harness's. */
    mapNameOf(&demo.header, mapName);
    if (!openMap(&wad, mapName, &map, &source)) {
        fprintf(stderr, "demo_check: %s is not a usable map in this WAD\n",
                mapName);
        free(bytes);
        return 2;
    }

    sectorCount = (uint16_t)map.lumps[DOOM_MAP_SECTORS].count;
    arena = (uint8_t*)malloc(1024u * 1024u);
    visited = (uint8_t*)malloc(sectorCount);
    initialCeiling = (fixed_t*)malloc((size_t)sectorCount * sizeof(fixed_t));
    if (arena == NULL || visited == NULL || initialCeiling == NULL) {
        fprintf(stderr, "demo_check: out of memory\n");
        free(initialCeiling);
        free(visited);
        free(arena);
        free(bytes);
        return 2;
    }
    memset(visited, 0, sectorCount);

    /* The header's game state, then the level: P_SetupLevel's order is the map,
     * the things, then the specials. p_spawn_specials() is separate because it
     * consumes the random sequence (light flashes and strobes draw) and a
     * THINGS-only load must not. */
    g_demo_rules(&demo, &rules);
    if (p_level_init(&level, &map, arena, 1024u * 1024u, &rules) != P_SETUP_OK) {
        fprintf(stderr, "demo_check: %s: level load failed with code %d\n",
                mapName, (int)level.error);
        free(initialCeiling);
        free(visited);
        free(arena);
        free(bytes);
        return 2;
    }
    /* The distinct missing-action set: a caller-owned table the dispatcher fills
     * in, one entry per action the run reaches without an implementation. It
     * exists because `missing=` plus `last=` cannot name a set: two different
     * actions in one tic collapse into one counter increment. Installed before
     * p_spawn_specials() so a missing action reached during level setup would
     * also be recorded (at tic 0); the spawn path itself runs none. */
    memset(&missingTrace, 0, sizeof missingTrace);
    level.context.actionTrace = &missingTrace;
    /* The distinct unsupported-line-special set, the same arrangement: a
     * caller-owned table the trigger dispatch fills in, installed before the
     * spawn scan so a special reached during setup would also be recorded. */
    memset(&unsupportedTrace, 0, sizeof unsupportedTrace);
    level.context.specTrace = &unsupportedTrace;
    if (p_spawn_specials(&level) != P_SPEC_OK) {
        fprintf(stderr, "demo_check: %s: p_spawn_specials refused the level\n",
                mapName);
        p_level_shutdown(&level);
        free(initialCeiling);
        free(visited);
        free(arena);
        free(bytes);
        return 2;
    }
    if (level.player == NULL) {
        fprintf(stderr, "demo_check: %s: the header's player did not spawn\n",
                mapName);
        p_level_shutdown(&level);
        free(initialCeiling);
        free(visited);
        free(arena);
        free(bytes);
        return 2;
    }
    for (tic = 0; tic < (uint32_t)sectorCount; ++tic) {
        initialCeiling[tic] = level.sectors[tic].ceilingHeight;
    }
    printf("WORLD map=%s spawned=%u kills=%u items=%u secrets=%u mobjs=%u "
           "rng=%d specials=%u triggers=%u\n",
           mapName, (unsigned)level.stats.spawned, (unsigned)level.totalKills,
           (unsigned)level.totalItems, (unsigned)level.totalSecrets,
           p_level_mobj_count(&level), level.random.index,
           (unsigned)level.specStats.spawned,
           (unsigned)level.specStats.triggers);
    {
        /* The level's arena, as the reload must leave it: `load` is what every
         * reload's `arena=` must equal, so a reload that allocated a second
         * level's worth of blocks fails here and not on the third death. */
        const zone_stats_t arena = z_stats(&level.zone);

        printf("ARENA load=%u peak=%u free=%u overhead=%u\n",
               (unsigned)arena.inUse, (unsigned)arena.peakInUse,
               (unsigned)arena.freeBytes, (unsigned)arena.overhead);
    }
    /* The load's own line-special counts are not the playback's: the run counts
     * from here, and a reload re-baselines at p_spawn_specials' reset. */
    specBefore[0] = level.specStats.triggers;
    specBefore[1] = level.specStats.unsupportedLineSpecial;
    specBefore[2] = level.specStats.deferredLineSpecial;
    specBefore[3] = level.specStats.lineScrolls;
    pressBefore = level.playerState.usePresses;
    useLineHitsBefore = level.specStats.useLineHits;

    /* One perturb, and it is the honest one: the stream is shifted by one tic
     * against the world, so every command lands on the wrong world state. If the
     * final hash does not move, the pin is not measuring the stream. */
    if (perturbSkip != 0) {
        p_ticcmd_t dropped;

        if (g_demo_read(&demo, &dropped) != G_DEMO_OK) {
            fprintf(stderr, "demo_check: --perturb skip found no command to "
                            "drop\n");
            p_level_shutdown(&level);
            free(initialCeiling);
            free(visited);
            free(arena);
            free(bytes);
            return 2;
        }
    }

    memset(&run, 0, sizeof run);
    sha256Init(&chainHash);

    prevX = level.player->x;
    prevY = level.player->y;
    prevAngle = level.player->angle;
    prevReaction = level.player->reactiontime;
    prevReady = level.playerState.readyweapon;
    prevHealth = level.playerState.health;
    prevSector = sectorAt(&map, prevX, prevY, &inside);

    for (tic = 0;; ++tic) {
        p_ticcmd_t cmd;
        const g_demo_error_t read = g_demo_read(&demo, &cmd);
        uint16_t sector = 0xFFFFu;
        int inMap = 0;
        int ranMove;

        if (read == G_DEMO_ERR_MARKER) {
            run.stop = (uint8_t)G_DEMO_STOP_MARKER;
            break;
        }
        if (read == G_DEMO_ERR_END) {
            run.stop = (uint8_t)G_DEMO_STOP_END;
            break;
        }
        if (read != G_DEMO_OK) {
            fprintf(stderr, "demo_check: the stream failed at tic %u with "
                            "g_demo error %d\n", tic, (int)read);
            p_level_shutdown(&level);
            free(initialCeiling);
            free(visited);
            free(arena);
            free(bytes);
            return 2;
        }

        if ((cmd.buttons & BT_ATTACK) != 0u) {
            totals.attackTics += 1u;
        }
        if ((cmd.buttons & BT_USE) != 0u) {
            totals.useTics += 1u;
            if ((prevButtons & BT_USE) == 0u) {
                totals.useEdges += 1u;
            }
        }
        prevButtons = cmd.buttons;

        /* P_MovePlayer's turn gate, decided before the tic runs: reaction time
         * is decremented instead of moving, and a dead player does not move. */
        ranMove = level.playerState.playerstate == P_PST_LIVE &&
                  prevReaction == 0;

        missingTrace.tic = tic;
        unsupportedTrace.tic = tic;
        if (p_level_tic(&level, &cmd, NULL) != P_TICK_OK) {
            fprintf(stderr, "demo_check: the world refused tic %u\n", tic);
            p_level_shutdown(&level);
            free(initialCeiling);
            free(visited);
            free(arena);
            free(bytes);
            return 2;
        }
        run.tics += 1u;
        totals.tics += 1u;

        /* D4.5: a line trigger set an exit. G_Ticker would enter the game state
         * machine's G_DoCompleted; this driver has no intermission and must not
         * follow a transition, so it stops and names it. A demo that reaches one
         * is a finding: the run is shorter than its lump and the gate says so. */
        if (level.exitRequest != (uint8_t)P_EXIT_NONE) {
            run.stop = (uint8_t)G_DEMO_STOP_EXIT;
            break;
        }

        /* --- coherence, one tic at a time --- */
        sector = sectorAt(&map, level.player->x, level.player->y, &inMap);
        if (inMap == 0 || sector == 0xFFFFu) {
            totals.outside += 1u;
        } else {
            if (visited[sector] == 0u) {
                visited[sector] = 1u;
                totals.sectorsVisited += 1u;
            }
            if (prevSector != 0xFFFFu && sector != prevSector) {
                totals.sectorChanges += 1u;
                if (level.sectors[sector].ceilingHeight !=
                    initialCeiling[sector]) {
                    totals.doorPass += 1u;
                }
            }
        }

        totals.wallCross += crossedWall(&map, prevX, prevY, level.player->x,
                                       level.player->y);

        /* `player->mo->angle += (cmd->angleturn<<16);` -- exact or counted.
         *
         * A_Punch is the one other writer in this port: when a punch finds a
         * target it turns the avatar onto it (`player->mo->angle =
         * P_PointToAngle2(...)`, p_pspr.c). That is upstream's own rule, so a
         * tic whose facing moved against its command *while the fist is the
         * ready weapon* is counted as a punch snap and not as a defect. */
        if (ranMove != 0) {
            const angle_t expected =
                prevAngle + (angle_t)((uint32_t)cmd.angleturn << 16);

            if (level.player->angle != expected) {
                if (prevReady == (int32_t)P_WP_FIST) {
                    totals.anglePunch += 1u;
                } else {
                    totals.angleBad += 1u;
                }
            }
        } else {
            totals.angleSkip += 1u;
        }

        /* The displacement projected on the facing the tic started with. A
         * forward command adds momentum along that facing, so the aggregate is
         * positive even though a turn leaves the previous momentum behind. */
        if (cmd.forwardmove != 0) {
            const fixed_t cos = r_fine_cosine(prevAngle);
            const fixed_t sin = r_fine_sine[prevAngle >> ANGLETOFINESHIFT];
            const int64_t forward =
                ((int64_t)(level.player->x - prevX) * cos +
                 (int64_t)(level.player->y - prevY) * sin) >> FRACBITS;

            if (cmd.forwardmove > 0) {
                totals.netForward += forward;
            } else {
                totals.netBackward += forward;
            }
            totals.forwardTics += 1u;
        }

        if (level.playerState.health != prevHealth) {
            if (level.playerState.health < prevHealth) {
                totals.hpLoss +=
                    (int64_t)(prevHealth - level.playerState.health);
            } else {
                totals.hpGain +=
                    (int64_t)(level.playerState.health - prevHealth);
            }
            prevHealth = level.playerState.health;
        }
        if (prevHealth < totals.hpMin) {
            totals.hpMin = prevHealth;
        }
        if (prevHealth > totals.hpMax) {
            totals.hpMax = prevHealth;
        }
        {
            int pool;

            for (pool = 0; pool < P_NUMAMMO; ++pool) {
                const int32_t ammo = level.playerState.ammo[pool];

                if (ammo < prevAmmo[pool]) {
                    totals.shots += 1u;
                    totals.ammoSpent += (int64_t)(prevAmmo[pool] - ammo);
                }
                prevAmmo[pool] = ammo;
            }
        }
        if (level.playerState.useTriggered != 0u) {
            totals.useTriggers += 1u;
        }
        if (level.playerState.playerstate != P_PST_LIVE) {
            totals.deadTics += 1u;
            if (totals.deathTic < 0) {
                totals.deathTic = (int32_t)tic;
            }
        }
        {
            const uint32_t mobjs = p_level_mobj_count(&level);

            if (mobjs > totals.mobjMax) {
                totals.mobjMax = mobjs;
            }
        }
        if (level.actions.notImplemented > missingBefore) {
            if (totals.missing == 0u) {
                totals.missingFirstTic = tic;
                totals.missingFirstAction = level.actions.lastNotImplemented;
            }
            totals.missing += level.actions.notImplemented - missingBefore;
            totals.missingLastAction = level.actions.lastNotImplemented;
            missingBefore = level.actions.notImplemented;
        }
        /* The special-line counters, accumulated the same way and for the same
         * reason: p_spawn_specials() zeroes them, and a reload calls it. */
        {
            const uint32_t now[4] = {
                level.specStats.triggers,
                level.specStats.unsupportedLineSpecial,
                level.specStats.deferredLineSpecial,
                level.specStats.lineScrolls
            };
            uint32_t field;

            for (field = 0u; field < 4u; ++field) {
                if (now[field] > specBefore[field]) {
                    totals.spec[field] += now[field] - specBefore[field];
                }
                specBefore[field] = now[field];
            }
        }

        prevX = level.player->x;
        prevY = level.player->y;
        prevAngle = level.player->angle;
        prevReaction = level.player->reactiontime;
        prevReady = level.playerState.readyweapon;
        prevSector = sector;

        /* --- the press's own evidence ---
         *
         * P_UseLines is upstream's PTR_UseTraverse: it walks the USERANGE
         * intercepts, stops at the first special line (dispatching it), and a
         * non-special line with no opening stops it too. `usePresses` says the
         * live player made a press this tic -- a death press never reaches
         * P_PlayerThink's body -- and `useLineHits` says the walk reached a
         * special line, whether or not its EV did anything. Tools/demo_check.py
         * re-walks the same 64-unit ray from the WAD's own geometry and these
         * two numbers are its independent comparison. */
        if (level.playerState.usePresses != pressBefore) {
            printf("PRESS n=%u x=%ld y=%ld angle=%lu reached=%u\n", tic,
                   (long)level.player->x, (long)level.player->y,
                   (unsigned long)level.player->angle,
                   level.specStats.useLineHits != useLineHitsBefore ? 1u : 0u);
            pressBefore = level.playerState.usePresses;
        }
        useLineHitsBefore = level.specStats.useLineHits;

        /* --- G_Ticker's reborn step ---
         *
         * `if (playeringame[i] && players[i].playerstate == PST_REBORN)
         *      G_DoReborn (i);` at the top of the *next* tic, which for a
         * single-player game is `gameaction = ga_loadlevel` and therefore a
         * reload of the same level before that tic's command is read. So the
         * reload happens here, after this command's tic and before the next
         * read, and it consumes no command. */
        if (level.playerState.playerstate == P_PST_REBORN) {
            const g_demo_error_t reborn = g_demo_reborn(&level);

            if (reborn != G_DEMO_OK) {
                fprintf(stderr, "demo_check: the reload at tic %u failed with "
                                "g_demo error %d\n", tic, (int)reborn);
                p_level_shutdown(&level);
                free(initialCeiling);
                free(visited);
                free(arena);
                free(bytes);
                return 2;
            }
            /* One line per answered death: where the avatar reappeared, what it
             * carries, and the arena's bytes in use. This is the evidence a
             * reload is a reload and not a teleport or a leak. */
            printf("RELOAD n=%u x=%ld y=%ld health=%ld mobjs=%u leveltime=%d "
                   "arena=%u\n",
                   tic, (long)level.player->x, (long)level.player->y,
                   (long)level.playerState.health,
                   (unsigned)p_level_mobj_count(&level), level.leveltime,
                   (unsigned)z_stats(&level.zone).inUse);
            totals.reloads += 1u;
            /* p_level_init() zeroes the level's action counters with the rest of
             * the state, so the running "how many are new" baseline restarts;
             * p_spawn_specials() does the same to the special-line counters. */
            missingBefore = 0u;
            specBefore[0] = level.specStats.triggers;
            specBefore[1] = level.specStats.unsupportedLineSpecial;
            specBefore[2] = level.specStats.deferredLineSpecial;
            specBefore[3] = level.specStats.lineScrolls;
            pressBefore = level.playerState.usePresses;
            useLineHitsBefore = level.specStats.useLineHits;
            /* The avatar is a different mobj at the map's start: the step this
             * tic did not make is the level change, not a teleport, and the next
             * tic compares against the respawned player. */
            prevX = level.player->x;
            prevY = level.player->y;
            prevAngle = level.player->angle;
            prevReaction = level.player->reactiontime;
            prevReady = level.playerState.readyweapon;
            prevHealth = level.playerState.health;
            for (pool = 0; pool < P_NUMAMMO; ++pool) {
                prevAmmo[pool] = level.playerState.ammo[pool];
            }
            prevSector = sectorAt(&map, prevX, prevY, &inside);
            if (prevSector != 0xFFFFu && visited[prevSector] == 0u) {
                visited[prevSector] = 1u;
                totals.sectorsVisited += 1u;
            }
        }

        /* --- the chain --- */
        if ((tic + 1u) % CHECK_TICS == 0u) {
            sha256Init(&checkHash);
            hashState(&checkHash, &level, tic);
            sha256Final(&checkHash, digest);
            digestToHex(digest, hex);
            printf("CHECK n=%u hash=%s\n", tic + 1u, hex);
            sha256Update(&chainHash, digest, sizeof digest);
            totals.checkpoints += 1u;
        }
    }
    run.leveltime = level.leveltime;

    /* The final state, whether or not it landed on a checkpoint. */
    sha256Init(&checkHash);
    hashState(&checkHash, &level, totals.tics > 0u ? totals.tics - 1u : 0u);
    sha256Final(&checkHash, digest);
    digestToHex(digest, totals.finalHash);
    printf("FINAL n=%u hash=%s\n", totals.tics, totals.finalHash);
    sha256Update(&chainHash, digest, sizeof digest);
    totals.checkpoints += 1u;
    sha256Final(&chainHash, digest);
    digestToHex(digest, totals.chainHash);
    printf("CHAIN count=%u hash=%s\n", totals.checkpoints, totals.chainHash);

    for (index16 = 0; index16 < sectorCount; ++index16) {
        if (level.sectors[index16].ceilingHeight != initialCeiling[index16]) {
            totals.doorsOpen += 1u;
        }
    }

    printf("STATE n=%u x=%ld y=%ld z=%ld angle=%lu health=%ld armor=%ld "
           "ammo=%ld,%ld,%ld,%ld kills=%ld items=%ld secrets=%ld mobjs=%u\n",
           totals.tics, (long)level.player->x, (long)level.player->y,
           (long)level.player->z, (unsigned long)level.player->angle,
           (long)level.playerState.health,
           (long)level.playerState.armorpoints,
           (long)level.playerState.ammo[P_AM_CLIP],
           (long)level.playerState.ammo[P_AM_SHELL],
           (long)level.playerState.ammo[P_AM_CELL],
           (long)level.playerState.ammo[P_AM_MISL],
           (long)level.playerState.killcount,
           (long)level.playerState.itemcount,
           (long)level.playerState.secretcount, p_level_mobj_count(&level));
    printf("RUN tics=%u stop=%s leveltime=%d declared=%u\n", run.tics,
           run.stop == (uint8_t)G_DEMO_STOP_MARKER
               ? "marker"
               : (run.stop == (uint8_t)G_DEMO_STOP_EXIT ? "exit" : "end"),
           run.leveltime, g_demo_tics(&demo));

    printf("COHERE wallcross=%u outside=%u angleskip=%u anglebad=%u "
           "anglepunch=%u sectorchanges=%u sectors=%u doorpass=%u doorsopen=%u "
           "forward=%u netfwd=%ld netback=%ld shots=%u ammo=%ld attackticks=%u "
           "useticks=%u useedges=%u usetriggers=%u hpmin=%ld hpmax=%ld "
           "hploss=%ld hpgain=%ld deadtics=%u mobjmax=%u\n",
           totals.wallCross, totals.outside, totals.angleSkip, totals.angleBad,
           totals.anglePunch, totals.sectorChanges, totals.sectorsVisited,
           totals.doorPass, totals.doorsOpen, totals.forwardTics,
           (long)totals.netForward, (long)totals.netBackward, totals.shots,
           (long)totals.ammoSpent, totals.attackTics, totals.useTics,
           totals.useEdges, totals.useTriggers, (long)totals.hpMin,
           (long)totals.hpMax, (long)totals.hpLoss, (long)totals.hpGain,
           totals.deadTics, totals.mobjMax);
    printf("SPEC triggers=%u unsupported=%u deferred=%u scrolls=%u\n",
           (unsigned)totals.spec[0], (unsigned)totals.spec[1],
           (unsigned)totals.spec[2], (unsigned)totals.spec[3]);
    printf("ANOM missing=%u firsttic=%u first=%u last=%u\n",
           totals.missing, totals.missingFirstTic, totals.missingFirstAction,
           totals.missingLastAction);
    {
        uint32_t entry;

        /* One token per distinct action: `<id>@<firstTic>:<type>`, type as the
         * mobj type that drew it (psprite for a weapon-sprite dispatch, which
         * has no mobj). */
        printf("MISSING count=%u overflow=%u", missingTrace.count,
               missingTrace.overflow);
        for (entry = 0u; entry < missingTrace.count; ++entry) {
            const p_action_missing_t* missing = &missingTrace.entries[entry];

            if (missing->type == P_ACTION_TYPE_NONE) {
                printf(" %u@%u:psprite", missing->action, missing->firstTic);
            } else {
                printf(" %u@%u:%u", missing->action, missing->firstTic,
                       missing->type);
            }
        }
        printf("\n");
    }
    {
        uint32_t entry;
        uint32_t unsupportedCount = 0u;
        uint32_t ignoredCount = 0u;

        /* Two sets, because the counter conflated them. `SPECUNSUP` is the
         * fidelity gap: upstream's trigger function names the number and this
         * port has no EV for it. `IGNORED` is the faithful no-op: upstream's
         * function does not name it either, so doing nothing is correct. One
         * token per distinct (special, kind): `<special>@<tic>:<line>:<kind>x<hits>`. */
        printf("SPECUNSUP count=");
        for (entry = 0u; entry < unsupportedTrace.count; ++entry) {
            if (unsupportedTrace.entries[entry].unsupported != 0u) {
                unsupportedCount += 1u;
            }
        }
        printf("%u overflow=%u", unsupportedCount, unsupportedTrace.overflow);
        for (entry = 0u; entry < unsupportedTrace.count; ++entry) {
            const p_spec_trace_entry_t* unsupported =
                &unsupportedTrace.entries[entry];
            const char* kind;

            if (unsupported->unsupported == 0u) {
                continue;
            }
            kind = unsupported->kind == (int16_t)P_TRIGGER_USE
                       ? "use"
                       : (unsupported->kind == (int16_t)P_TRIGGER_SHOOT ? "shoot"
                                                                        : "cross");
            printf(" %d@%u:%u:%sx%u", (int)unsupported->special,
                   unsupported->firstTic, (unsigned)unsupported->line, kind,
                   unsupported->hits);
        }
        printf("\n");

        printf("IGNORED count=");
        for (entry = 0u; entry < unsupportedTrace.count; ++entry) {
            if (unsupportedTrace.entries[entry].unsupported == 0u) {
                ignoredCount += 1u;
            }
        }
        printf("%u", ignoredCount);
        for (entry = 0u; entry < unsupportedTrace.count; ++entry) {
            const p_spec_trace_entry_t* ignored = &unsupportedTrace.entries[entry];
            const char* kind;

            if (ignored->unsupported != 0u) {
                continue;
            }
            kind = ignored->kind == (int16_t)P_TRIGGER_USE
                       ? "use"
                       : (ignored->kind == (int16_t)P_TRIGGER_SHOOT ? "shoot"
                                                                    : "cross");
            printf(" %d@%u:%u:%sx%u", (int)ignored->special, ignored->firstTic,
                   (unsigned)ignored->line, kind, ignored->hits);
        }
        printf("\n");
    }

    {
        /* "Faithful" in the only sense this harness can assert without a
         * reference: every action the run reached is implemented, no trigger
         * reached a line special with no EV handler (a run that does is no
         * longer upstream's world after the first one, whatever its coherence
         * counters say), and the stream is played on a live player -- which, for
         * a demo that dies, means every death was answered by a reload
         * (G_DoReborn -> ga_loadlevel) and the player is PST_LIVE at the last
         * command. A run that ends DEAD or PST_REBORN is a frozen corpse with
         * commands still arriving, and that is not what vanilla's
         * G_CheckDemoStatus-less death does. */
        const int alive = level.playerState.playerstate == P_PST_LIVE;
        const int unsupported = totals.spec[1] != 0u;

        printf("ALIVE alive=%d lastinput=%ld deathtic=%ld reborns=%u\n", alive,
               (long)totals.lastInputTic, (long)totals.deathTic,
               (unsigned)totals.reloads);
        printf("VERDICT faithful=%d reason=%s\n",
               totals.missing == 0u && alive != 0 && !unsupported ? 1 : 0,
               totals.missing != 0u ? "action"
                                    : (alive == 0 ? "death"
                                                  : (unsupported ? "special" : "none")));
    }

    p_level_shutdown(&level);
    free(initialCeiling);
    free(visited);
    free(arena);
    free(bytes);
    return 0;
}
