/*
 * Behaviour tests for the save record (D7.1, task T52).
 *
 * WHAT THESE CASES PROVE, AND WHY A ROUND TRIP IS NOT ENOUGH
 *
 * A writer that is compared against its own reader proves the codec: every byte
 * it wrote came back. It does not prove the *save*. A field the writer forgot to
 * archive is absent from both sides and the round trip is still perfectly equal.
 *
 * So the central case is a differential: build world A from E1M1, run it with a
 * fixed ticcmd script for 20 tics, save; run A another 35 tics and snapshot
 * every observable; build world B the same way, restore the payload at tic 20,
 * run B the same 35 tics with the same script, and require every observable to
 * match. A field left out of the record shows up here as a divergence, and the
 * failure message names it and gives both values.
 *
 * The same differential is run with a door mid-motion, because the movers are
 * the state most likely to be left out, and once more with the RNG deliberately
 * not restored: that last case must *fail* to match. A gate that cannot fail
 * proves only that it runs.
 *
 * The world is a real E1M1 from tools/DOOM1.WAD, opened through the same
 * w_wad/doom_map/p_level_init/p_spawn_specials path the frame driver uses. The
 * WAD is copyrighted shareware data and is not committed; this suite needs it at
 * tools/DOOM1.WAD (or in DOOM_WAD). Its absence is a failure, not a skip: a
 * differential against a synthetic map would not exercise the 92-116 mobjs that
 * make the budget and the index encoding interesting.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <unity.h>

#include "doomc/SaveGame.h"

extern "C" {
#include "doom_map.h"
#include "p_doors.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_tick.h"
#include "p_user.h"
#include "w_wad.h"
}

#include "../../test_config.h"

using doom::doomc::kSavePayloadMax;
using doom::doomc::SaveHeader;
using doom::doomc::SaveStatus;
using doom::doomc::saveApply;
using doom::doomc::saveReadHeader;
using doom::doomc::saveWrite;

void setUp(void) {
    test_setup();
}

void tearDown() {
    test_teardown();
}

namespace {

/* --- The world ------------------------------------------------------------ */

constexpr std::size_t kArenaBytes = 96u * 1024u;
constexpr std::size_t kMaxMobjs = 256u;
constexpr std::size_t kMaxSectors = 256u;
constexpr std::size_t kMaxScrolls = 32u;
constexpr std::size_t kMaxLines = 512u;

struct World {
    alignas(8) std::uint8_t arena[kArenaBytes];
    doom_map_t map;
    p_level_t level;
    bool built;
};

World gA;
World gB;
World gC;

std::vector<std::uint8_t> gWadBytes;
wad_state_t gWad;
bool gWadOpen = false;

const char* wadPath() {
    const char* env = std::getenv("DOOM_WAD");

    if (env != nullptr) {
        return env;
    }
    static const char* const candidates[] = {
        "tools/DOOM1.WAD",
        "../tools/DOOM1.WAD",
        "../../tools/DOOM1.WAD",
        "DOOM1.WAD",
    };
    for (std::size_t i = 0u; i < sizeof candidates / sizeof candidates[0]; ++i) {
        std::FILE* file = std::fopen(candidates[i], "rb");

        if (file != nullptr) {
            std::fclose(file);
            return candidates[i];
        }
    }
    return nullptr;
}

bool loadWad() {
    if (gWadOpen) {
        return true;
    }
    const char* path = wadPath();

    if (path == nullptr) {
        return false;
    }
    std::FILE* file = std::fopen(path, "rb");

    if (file == nullptr) {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long length = std::ftell(file);

    if (length <= 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }
    gWadBytes.resize(static_cast<std::size_t>(length));
    if (std::fread(gWadBytes.data(), 1u, gWadBytes.size(), file) !=
        gWadBytes.size()) {
        std::fclose(file);
        gWadBytes.clear();
        return false;
    }
    std::fclose(file);
    if (wad_open(&gWad, gWadBytes.data(), gWadBytes.size()) != WAD_ERR_OK) {
        return false;
    }
    gWadOpen = true;
    return true;
}

bool buildWorld(World& world, const char* mapName, p_skill_t skill) {
    doom_map_source_t source{};
    const int marker = W_CheckNumForName(&gWad, mapName);

    if (marker < 0 ||
        marker + 1 + static_cast<int>(DOOM_MAP_LUMP_COUNT) > gWad.numLumps) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(DOOM_MAP_LUMP_COUNT); ++i) {
        const int lump = marker + 1 + i;

        source.data[i] = W_LumpPointer(&gWad, lump);
        source.size[i] = static_cast<std::size_t>(W_LumpLength(&gWad, lump));
    }
    std::memset(&world.map, 0, sizeof world.map);
    if (doom_map_open(&world.map, &source) != DOOM_MAP_OK) {
        return false;
    }
    p_spawn_rules_t rules = p_spawn_rules_default();
    rules.skill = skill;
    if (p_level_init(&world.level, &world.map, world.arena, sizeof world.arena,
                     &rules) != P_SETUP_OK) {
        return false;
    }
    if (p_spawn_specials(&world.level) != P_SPEC_OK) {
        return false;
    }
    world.level.rules.skill = skill;
    world.built = true;
    return true;
}

void destroyWorld(World& world) {
    if (world.built) {
        p_level_shutdown(&world.level);
        world.built = false;
    }
}

/* --- The script ----------------------------------------------------------- */

/*
 * One fixed ticcmd script, keyed on the world's own clock so the same function
 * drives both sides of the differential.
 *
 *   0..7     turn 180 degrees in place (4096 << 16 per tic is ANG22.5), which
 *            brings the imp cluster behind the E1M1 start into view;
 *   8..15    fire, so a weapon action and a noise flood both happen;
 *   16..     walk forward and keep firing, with a slow sweep.
 *
 * Holding BT_ATTACK is upstream's refire path; the pistol has 50 rounds, which
 * is more than 35 tics of it.
 */
void commandForTic(std::int32_t tic, p_ticcmd_t* cmd) {
    std::memset(cmd, 0, sizeof *cmd);
    if (tic < 8) {
        cmd->angleturn = 4096;
    } else if (tic < 16) {
        cmd->buttons = BT_ATTACK;
    } else if (tic < 24) {
        cmd->forwardmove = 0x19;
        cmd->buttons = BT_ATTACK;
    } else {
        cmd->forwardmove = 0x19;
        cmd->buttons = BT_ATTACK;
        cmd->angleturn = ((tic / 6) % 2) == 0 ? 640 : -640;
    }
}

void runTics(World& world, int count) {
    for (int i = 0; i < count; ++i) {
        p_ticcmd_t cmd;

        commandForTic(world.level.leveltime, &cmd);
        (void)p_level_tic(&world.level, &cmd, nullptr);
    }
}

/* --- Snapshots ------------------------------------------------------------ */

struct MobjSnap {
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::int32_t momx;
    std::int32_t momy;
    std::int32_t momz;
    std::int32_t floorz;
    std::int32_t ceilingz;
    std::uint32_t angle;
    std::int32_t health;
    std::uint32_t flags;
    std::int16_t state;
    std::int16_t tics;
    std::int16_t type;
    std::int16_t reactiontime;
    std::int16_t movedir;
    std::int16_t movecount;
    std::int16_t threshold;
    std::int16_t lastlook;
    std::uint16_t target;
    std::uint16_t tracer;
    std::uint16_t sector;
};

struct PlayerSnap {
    std::uint16_t mo;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::uint32_t angle;
    std::int32_t health;
    std::int32_t armorpoints;
    std::int32_t armortype;
    std::int32_t ammo[P_NUMAMMO];
    std::int32_t maxammo[P_NUMAMMO];
    std::int32_t powers[P_NUMPOWERS];
    std::int32_t readyweapon;
    std::int32_t pendingweapon;
    std::int16_t pspriteState[P_NUMPSPRITES];
    std::int16_t pspriteTics[P_NUMPSPRITES];
    std::int32_t pspriteSx[P_NUMPSPRITES];
    std::int32_t pspriteSy[P_NUMPSPRITES];
    std::uint8_t weaponowned[P_NUMWEAPONS];
    std::uint8_t cards[P_NUMCARDS];
    std::int32_t backpack;
    std::int32_t attackdown;
    std::int32_t usedown;
    std::int32_t refire;
    std::int32_t killcount;
    std::int32_t itemcount;
    std::int32_t secretcount;
    std::int32_t damagecount;
    std::int32_t bonuscount;
    std::int32_t extralight;
    std::int32_t fixedcolormap;
    std::uint16_t attacker;
    std::int32_t didsecret;
    std::int32_t useTriggered;
    std::int32_t pspritesStarted;
    std::int32_t forwardmove;
    std::int32_t sidemove;
    std::int32_t angleturn;
    std::int32_t buttons;
    std::int32_t viewheight;
    std::int32_t deltaviewheight;
    std::int32_t bob;
    std::int32_t viewz;
    std::int32_t playerstate;
    std::int32_t onground;
};

struct SectorSnap {
    std::int32_t floorHeight;
    std::int32_t ceilingHeight;
    std::uint64_t floorPic;
    std::uint64_t ceilingPic;
    std::int16_t lightLevel;
    std::int16_t special;
    std::int16_t tag;
};

struct WorldSnap {
    std::uint32_t mobjCount;
    MobjSnap mobjs[kMaxMobjs];
    PlayerSnap player;
    std::uint16_t sectorCount;
    SectorSnap sectors[kMaxSectors];
    std::uint16_t lineCount;
    std::int16_t lines[kMaxLines];
    std::uint16_t soundCount;
    std::uint16_t soundSector[kMaxSectors];
    std::uint16_t soundTarget[kMaxSectors];
    std::int32_t leveltime;
    std::int32_t totalKills;
    std::int32_t totalItems;
    std::int32_t totalSecrets;
    std::int32_t exitRequest;
    std::int32_t animTime;
    std::uint32_t randomIndex;
    std::uint16_t scrollCount;
    std::int32_t scrollDx[kMaxScrolls];
    std::int32_t scrollDy[kMaxScrolls];
};

std::uint16_t indexOf(const r_mobj_t* const* mobjs, std::uint32_t count,
                      const r_mobj_t* needle) {
    if (needle == nullptr) {
        return 0xFFFFu;
    }
    for (std::uint32_t i = 0u; i < count; ++i) {
        if (mobjs[i] == needle) {
            return static_cast<std::uint16_t>(i);
        }
    }
    return 0xFFFFu;
}

void snapshot(const World& world, WorldSnap& out) {
    const r_mobj_t* mobjs[kMaxMobjs];
    std::uint32_t count = 0u;

    std::memset(&out, 0, sizeof out);
    for (const r_mobj_t* m = p_level_first_mobj(&world.level); m != nullptr;
         m = p_level_next_mobj(m)) {
        if (count < kMaxMobjs) {
            mobjs[count++] = m;
        }
    }
    out.mobjCount = count;
    for (std::uint32_t i = 0u; i < count; ++i) {
        const r_mobj_t* m = mobjs[i];

        out.mobjs[i].x = m->x;
        out.mobjs[i].y = m->y;
        out.mobjs[i].z = m->z;
        out.mobjs[i].momx = m->momx;
        out.mobjs[i].momy = m->momy;
        out.mobjs[i].momz = m->momz;
        out.mobjs[i].floorz = m->floorz;
        out.mobjs[i].ceilingz = m->ceilingz;
        out.mobjs[i].angle = m->angle;
        out.mobjs[i].health = m->health;
        out.mobjs[i].flags = m->flags;
        out.mobjs[i].state = m->state;
        out.mobjs[i].tics = m->tics;
        out.mobjs[i].type = m->type;
        out.mobjs[i].reactiontime = m->reactiontime;
        out.mobjs[i].movedir = m->movedir;
        out.mobjs[i].movecount = m->movecount;
        out.mobjs[i].threshold = m->threshold;
        out.mobjs[i].lastlook = static_cast<std::int16_t>(m->lastlook);
        out.mobjs[i].target = indexOf(mobjs, count, m->target);
        out.mobjs[i].tracer = indexOf(mobjs, count, m->tracer);
        out.mobjs[i].sector = m->sector;
    }

    const p_player_t& p = world.level.playerState;

    out.player.mo = indexOf(mobjs, count, p.mo);
    out.player.x = p.mo != nullptr ? p.mo->x : 0;
    out.player.y = p.mo != nullptr ? p.mo->y : 0;
    out.player.z = p.mo != nullptr ? p.mo->z : 0;
    out.player.angle = p.mo != nullptr ? p.mo->angle : 0u;
    out.player.health = p.health;
    out.player.armorpoints = p.armorpoints;
    out.player.armortype = p.armortype;
    for (int i = 0; i < P_NUMAMMO; ++i) {
        out.player.ammo[i] = p.ammo[i];
        out.player.maxammo[i] = p.maxammo[i];
    }
    for (int i = 0; i < P_NUMPOWERS; ++i) {
        out.player.powers[i] = p.powers[i];
    }
    out.player.readyweapon = p.readyweapon;
    out.player.pendingweapon = p.pendingweapon;
    for (int i = 0; i < P_NUMPSPRITES; ++i) {
        out.player.pspriteState[i] = p.psprites[i].state;
        out.player.pspriteTics[i] = static_cast<std::int16_t>(p.psprites[i].tics);
        out.player.pspriteSx[i] = p.psprites[i].sx;
        out.player.pspriteSy[i] = p.psprites[i].sy;
    }
    for (int i = 0; i < P_NUMWEAPONS; ++i) {
        out.player.weaponowned[i] = p.weaponowned[i];
    }
    for (int i = 0; i < P_NUMCARDS; ++i) {
        out.player.cards[i] = p.cards[i];
    }
    out.player.backpack = p.backpack;
    out.player.attackdown = p.attackdown;
    out.player.usedown = p.usedown;
    out.player.refire = p.refire;
    out.player.killcount = p.killcount;
    out.player.itemcount = p.itemcount;
    out.player.secretcount = p.secretcount;
    out.player.damagecount = p.damagecount;
    out.player.bonuscount = p.bonuscount;
    out.player.extralight = p.extralight;
    out.player.fixedcolormap = p.fixedcolormap;
    out.player.attacker = indexOf(mobjs, count, p.attacker);
    out.player.didsecret = p.didsecret;
    out.player.useTriggered = p.useTriggered;
    out.player.pspritesStarted = p.pspritesStarted;
    out.player.forwardmove = p.cmd.forwardmove;
    out.player.sidemove = p.cmd.sidemove;
    out.player.angleturn = p.cmd.angleturn;
    out.player.buttons = p.cmd.buttons;
    out.player.viewheight = p.viewheight;
    out.player.deltaviewheight = p.deltaviewheight;
    out.player.bob = p.bob;
    out.player.viewz = p.viewz;
    out.player.playerstate = static_cast<std::int32_t>(p.playerstate);
    out.player.onground = p.onground;

    out.sectorCount = world.level.sectorCount;
    for (std::uint16_t i = 0u; i < world.level.sectorCount && i < kMaxSectors; ++i) {
        const p_sector_t& s = world.level.sectors[i];

        out.sectors[i].floorHeight = s.floorHeight;
        out.sectors[i].ceilingHeight = s.ceilingHeight;
        out.sectors[i].floorPic = s.floorPic;
        out.sectors[i].ceilingPic = s.ceilingPic;
        out.sectors[i].lightLevel = s.lightLevel;
        out.sectors[i].special = s.special;
        out.sectors[i].tag = s.tag;
    }
    out.lineCount = world.level.lineCount;
    for (std::uint16_t i = 0u; i < world.level.lineCount && i < kMaxLines; ++i) {
        out.lines[i] = world.level.lineSpecial[i];
    }
    out.soundCount = 0u;
    if (world.level.context.sectorSound != nullptr) {
        for (std::uint16_t i = 0u;
             i < world.level.context.sectorSoundCount && i < kMaxSectors; ++i) {
            const r_mobj_t* target = world.level.context.sectorSound[i].soundTarget;

            if (target == nullptr) {
                continue;
            }
            out.soundSector[out.soundCount] = i;
            out.soundTarget[out.soundCount] = indexOf(mobjs, count, target);
            ++out.soundCount;
        }
    }
    out.leveltime = world.level.leveltime;
    out.totalKills = world.level.totalKills;
    out.totalItems = world.level.totalItems;
    out.totalSecrets = world.level.totalSecrets;
    out.exitRequest = world.level.exitRequest;
    out.animTime = world.level.animTime;
    out.randomIndex = world.level.random.index;
    out.scrollCount = world.level.scrollCount;
    for (std::uint16_t i = 0u; i < world.level.scrollCount && i < kMaxScrolls; ++i) {
        out.scrollDx[i] = world.level.scrolls[i].dx;
        out.scrollDy[i] = world.level.scrolls[i].dy;
    }
}

/* --- Comparison ----------------------------------------------------------- */

bool checkI64(const char* what, int index, long long a, long long b, char* msg,
              std::size_t cap) {
    if (a == b) {
        return true;
    }
    std::snprintf(msg, cap, "%s[%d]: A=%lld B=%lld", what, index, a, b);
    return false;
}

bool checkU64(const char* what, int index, unsigned long long a,
              unsigned long long b, char* msg, std::size_t cap) {
    if (a == b) {
        return true;
    }
    std::snprintf(msg, cap, "%s[%d]: A=%llu B=%llu", what, index, a, b);
    return false;
}

#define CHECK_I32(what, index, av, bv)                                        \
    do {                                                                      \
        if (!checkI64(what, index, static_cast<long long>(av),                \
                      static_cast<long long>(bv), msg, cap)) {                \
            return false;                                                     \
        }                                                                     \
    } while (0)

#define CHECK_U32(what, index, av, bv)                                        \
    do {                                                                      \
        if (!checkU64(what, index, static_cast<unsigned long long>(av),       \
                      static_cast<unsigned long long>(bv), msg, cap)) {       \
            return false;                                                     \
        }                                                                     \
    } while (0)

bool compareSnapshots(const WorldSnap& a, const WorldSnap& b, char* msg,
                      std::size_t cap) {
    CHECK_I32("leveltime", 0, a.leveltime, b.leveltime);
    CHECK_I32("totalKills", 0, a.totalKills, b.totalKills);
    CHECK_I32("totalItems", 0, a.totalItems, b.totalItems);
    CHECK_I32("totalSecrets", 0, a.totalSecrets, b.totalSecrets);
    CHECK_I32("exitRequest", 0, a.exitRequest, b.exitRequest);
    CHECK_I32("animTime", 0, a.animTime, b.animTime);
    CHECK_U32("randomIndex", 0, a.randomIndex, b.randomIndex);
    CHECK_U32("mobjCount", 0, a.mobjCount, b.mobjCount);
    CHECK_U32("sectorCount", 0, a.sectorCount, b.sectorCount);
    CHECK_U32("scrollCount", 0, a.scrollCount, b.scrollCount);

    for (std::uint32_t i = 0u; i < a.scrollCount && i < kMaxScrolls; ++i) {
        CHECK_I32("scrollDx", static_cast<int>(i), a.scrollDx[i], b.scrollDx[i]);
        CHECK_I32("scrollDy", static_cast<int>(i), a.scrollDy[i], b.scrollDy[i]);
    }
    for (std::uint32_t i = 0u; i < a.sectorCount && i < kMaxSectors; ++i) {
        CHECK_I32("sectorFloor", static_cast<int>(i), a.sectors[i].floorHeight,
                  b.sectors[i].floorHeight);
        CHECK_I32("sectorCeiling", static_cast<int>(i),
                  a.sectors[i].ceilingHeight, b.sectors[i].ceilingHeight);
        CHECK_U32("sectorFloorPic", static_cast<int>(i), a.sectors[i].floorPic,
                  b.sectors[i].floorPic);
        CHECK_U32("sectorCeilingPic", static_cast<int>(i),
                  a.sectors[i].ceilingPic, b.sectors[i].ceilingPic);
        CHECK_I32("sectorLight", static_cast<int>(i), a.sectors[i].lightLevel,
                  b.sectors[i].lightLevel);
        CHECK_I32("sectorSpecial", static_cast<int>(i), a.sectors[i].special,
                  b.sectors[i].special);
        CHECK_I32("sectorTag", static_cast<int>(i), a.sectors[i].tag,
                  b.sectors[i].tag);
    }
    CHECK_U32("player.mo", 0, a.player.mo, b.player.mo);
    CHECK_I32("player.x", 0, a.player.x, b.player.x);
    CHECK_I32("player.y", 0, a.player.y, b.player.y);
    CHECK_I32("player.z", 0, a.player.z, b.player.z);
    CHECK_U32("player.angle", 0, a.player.angle, b.player.angle);
    CHECK_I32("player.health", 0, a.player.health, b.player.health);
    CHECK_I32("player.armorpoints", 0, a.player.armorpoints, b.player.armorpoints);
    CHECK_I32("player.armortype", 0, a.player.armortype, b.player.armortype);
    for (int i = 0; i < P_NUMAMMO; ++i) {
        CHECK_I32("player.ammo", i, a.player.ammo[i], b.player.ammo[i]);
    }
    CHECK_I32("player.readyweapon", 0, a.player.readyweapon, b.player.readyweapon);
    CHECK_I32("player.pendingweapon", 0, a.player.pendingweapon,
              b.player.pendingweapon);
    for (int i = 0; i < P_NUMPSPRITES; ++i) {
        CHECK_I32("player.pspriteState", i, a.player.pspriteState[i],
                  b.player.pspriteState[i]);
        CHECK_I32("player.pspriteTics", i, a.player.pspriteTics[i],
                  b.player.pspriteTics[i]);
    }
    CHECK_I32("player.killcount", 0, a.player.killcount, b.player.killcount);
    CHECK_I32("player.itemcount", 0, a.player.itemcount, b.player.itemcount);
    CHECK_I32("player.secretcount", 0, a.player.secretcount, b.player.secretcount);
    CHECK_I32("player.damagecount", 0, a.player.damagecount, b.player.damagecount);
    CHECK_I32("player.bonuscount", 0, a.player.bonuscount, b.player.bonuscount);
    CHECK_I32("player.extralight", 0, a.player.extralight, b.player.extralight);
    CHECK_I32("player.viewz", 0, a.player.viewz, b.player.viewz);
    CHECK_I32("player.playerstate", 0, a.player.playerstate, b.player.playerstate);
    CHECK_I32("player.onground", 0, a.player.onground, b.player.onground);
    CHECK_I32("player.viewheight", 0, a.player.viewheight, b.player.viewheight);
    CHECK_I32("player.deltaviewheight", 0, a.player.deltaviewheight,
              b.player.deltaviewheight);
    CHECK_I32("player.bob", 0, a.player.bob, b.player.bob);
    CHECK_I32("player.forwardmove", 0, a.player.forwardmove, b.player.forwardmove);
    CHECK_I32("player.sidemove", 0, a.player.sidemove, b.player.sidemove);
    CHECK_I32("player.angleturn", 0, a.player.angleturn, b.player.angleturn);
    CHECK_I32("player.buttons", 0, a.player.buttons, b.player.buttons);
    CHECK_I32("player.backpack", 0, a.player.backpack, b.player.backpack);
    CHECK_I32("player.attackdown", 0, a.player.attackdown, b.player.attackdown);
    CHECK_I32("player.usedown", 0, a.player.usedown, b.player.usedown);
    CHECK_I32("player.refire", 0, a.player.refire, b.player.refire);
    CHECK_I32("player.fixedcolormap", 0, a.player.fixedcolormap,
              b.player.fixedcolormap);
    CHECK_U32("player.attacker", 0, a.player.attacker, b.player.attacker);
    CHECK_I32("player.didsecret", 0, a.player.didsecret, b.player.didsecret);
    CHECK_I32("player.useTriggered", 0, a.player.useTriggered,
              b.player.useTriggered);
    CHECK_I32("player.pspritesStarted", 0, a.player.pspritesStarted,
              b.player.pspritesStarted);
    for (int i = 0; i < P_NUMAMMO; ++i) {
        CHECK_I32("player.maxammo", i, a.player.maxammo[i], b.player.maxammo[i]);
    }
    for (int i = 0; i < P_NUMPOWERS; ++i) {
        CHECK_I32("player.powers", i, a.player.powers[i], b.player.powers[i]);
    }
    for (int i = 0; i < P_NUMPSPRITES; ++i) {
        CHECK_I32("player.pspriteSx", i, a.player.pspriteSx[i],
                  b.player.pspriteSx[i]);
        CHECK_I32("player.pspriteSy", i, a.player.pspriteSy[i],
                  b.player.pspriteSy[i]);
    }
    for (int i = 0; i < P_NUMWEAPONS; ++i) {
        CHECK_I32("player.weaponowned", i, a.player.weaponowned[i],
                  b.player.weaponowned[i]);
    }
    for (int i = 0; i < P_NUMCARDS; ++i) {
        CHECK_I32("player.cards", i, a.player.cards[i], b.player.cards[i]);
    }
    CHECK_U32("lineCount", 0, a.lineCount, b.lineCount);
    for (std::uint32_t i = 0u; i < a.lineCount && i < kMaxLines; ++i) {
        CHECK_I32("lineSpecial", static_cast<int>(i), a.lines[i], b.lines[i]);
    }
    CHECK_U32("soundCount", 0, a.soundCount, b.soundCount);
    for (std::uint32_t i = 0u; i < a.soundCount && i < kMaxSectors; ++i) {
        CHECK_U32("soundSector", static_cast<int>(i), a.soundSector[i],
                  b.soundSector[i]);
        CHECK_U32("soundTarget", static_cast<int>(i), a.soundTarget[i],
                  b.soundTarget[i]);
    }

    for (std::uint32_t i = 0u; i < a.mobjCount && i < kMaxMobjs; ++i) {
        CHECK_I32("mobj.x", static_cast<int>(i), a.mobjs[i].x, b.mobjs[i].x);
        CHECK_I32("mobj.y", static_cast<int>(i), a.mobjs[i].y, b.mobjs[i].y);
        CHECK_I32("mobj.z", static_cast<int>(i), a.mobjs[i].z, b.mobjs[i].z);
        CHECK_I32("mobj.momx", static_cast<int>(i), a.mobjs[i].momx,
                  b.mobjs[i].momx);
        CHECK_I32("mobj.momy", static_cast<int>(i), a.mobjs[i].momy,
                  b.mobjs[i].momy);
        CHECK_I32("mobj.momz", static_cast<int>(i), a.mobjs[i].momz,
                  b.mobjs[i].momz);
        CHECK_I32("mobj.floorz", static_cast<int>(i), a.mobjs[i].floorz,
                  b.mobjs[i].floorz);
        CHECK_I32("mobj.ceilingz", static_cast<int>(i), a.mobjs[i].ceilingz,
                  b.mobjs[i].ceilingz);
        CHECK_U32("mobj.angle", static_cast<int>(i), a.mobjs[i].angle,
                  b.mobjs[i].angle);
        CHECK_I32("mobj.health", static_cast<int>(i), a.mobjs[i].health,
                  b.mobjs[i].health);
        CHECK_U32("mobj.flags", static_cast<int>(i), a.mobjs[i].flags,
                  b.mobjs[i].flags);
        CHECK_I32("mobj.state", static_cast<int>(i), a.mobjs[i].state,
                  b.mobjs[i].state);
        CHECK_I32("mobj.tics", static_cast<int>(i), a.mobjs[i].tics,
                  b.mobjs[i].tics);
        CHECK_I32("mobj.type", static_cast<int>(i), a.mobjs[i].type,
                  b.mobjs[i].type);
        CHECK_I32("mobj.reactiontime", static_cast<int>(i),
                  a.mobjs[i].reactiontime, b.mobjs[i].reactiontime);
        CHECK_I32("mobj.movedir", static_cast<int>(i), a.mobjs[i].movedir,
                  b.mobjs[i].movedir);
        CHECK_I32("mobj.movecount", static_cast<int>(i), a.mobjs[i].movecount,
                  b.mobjs[i].movecount);
        CHECK_I32("mobj.threshold", static_cast<int>(i), a.mobjs[i].threshold,
                  b.mobjs[i].threshold);
        CHECK_I32("mobj.lastlook", static_cast<int>(i), a.mobjs[i].lastlook,
                  b.mobjs[i].lastlook);
        CHECK_U32("mobj.target", static_cast<int>(i), a.mobjs[i].target,
                  b.mobjs[i].target);
        CHECK_U32("mobj.tracer", static_cast<int>(i), a.mobjs[i].tracer,
                  b.mobjs[i].tracer);
        CHECK_U32("mobj.sector", static_cast<int>(i), a.mobjs[i].sector,
                  b.mobjs[i].sector);
    }
    return true;
}

void assertSnapshotsEqual(const WorldSnap& a, const WorldSnap& b,
                          const char* context) {
    char message[256];

    message[0] = '\0';
    if (!compareSnapshots(a, b, message, sizeof message)) {
        char full[320];

        std::snprintf(full, sizeof full, "%s: %s", context, message);
        TEST_FAIL_MESSAGE(full);
    }
}

/* --- Fixtures ------------------------------------------------------------- */

bool beginWorld(World& world, const char* mapName, p_skill_t skill) {
    if (!loadWad()) {
        TEST_FAIL_MESSAGE(
            "tools/DOOM1.WAD is required by this suite (set DOOM_WAD to point "
            "at it): the differential needs a real E1M1");
        return false;
    }
    world.built = false;
    return buildWorld(world, mapName, skill);
}

/* A line whose manual-door special opens the sector behind it. */
int findManualDoorLine(const p_level_t& level) {
    static const std::int16_t kDoorSpecials[] = {1, 26, 27, 28, 31, 32, 33, 34,
                                                 117, 118};

    for (std::uint16_t i = 0u; i < level.lineCount; ++i) {
        doom_line_t line;
        std::uint16_t back = 0u;

        doom_map_get_line(level.map, i, &line);
        if (!p_line_back_sector(level.map, &line, &back)) {
            continue;
        }
        if (back >= level.sectorCount) {
            continue;
        }
        const p_sector_t& sector = level.sectors[back];

        if (sector.ceilingHeight != sector.floorHeight) {
            continue;
        }
        for (std::size_t k = 0u; k < sizeof kDoorSpecials / sizeof kDoorSpecials[0];
             ++k) {
            if (line.special == kDoorSpecials[k]) {
                return static_cast<int>(i);
            }
        }
    }
    return -1;
}

/* --- Tests ---------------------------------------------------------------- */

void test_header_round_trip_matches_the_archived_state(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    runTics(gA, 10);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));
    TEST_ASSERT_TRUE(written > 48u);
    TEST_ASSERT_TRUE(written <= kSavePayloadMax);

    SaveHeader header{};
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveReadHeader(payload, written, header)));
    TEST_ASSERT_EQUAL_UINT8(1u, header.level);
    TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(P_SKILL_MEDIUM), header.skill);
    TEST_ASSERT_EQUAL_INT32(gA.level.leveltime, header.leveltime);
    TEST_ASSERT_EQUAL_INT32(gA.level.playerState.killcount, header.kills);
    TEST_ASSERT_EQUAL_STRING("E1M1 MED 00:00", header.label);

    TEST_ASSERT_TRUE(beginWorld(gB, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveApply(payload, written, 1, gB.level,
                                                     gB.level.playerState)));

    WorldSnap a;
    WorldSnap b;

    snapshot(gA, a);
    snapshot(gB, b);
    assertSnapshotsEqual(a, b, "round trip");
    destroyWorld(gA);
    destroyWorld(gB);
}

void test_differential_after_55_tics(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;
    constexpr int kSaveAt = 20;
    constexpr int kRun = 35;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    runTics(gA, kSaveAt);
    WorldSnap atSave;

    snapshot(gA, atSave);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));
    runTics(gA, kRun);

    TEST_ASSERT_TRUE(beginWorld(gB, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveApply(payload, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_EQUAL_INT32(kSaveAt, gB.level.leveltime);
    runTics(gB, kRun);
    TEST_ASSERT_EQUAL_INT32(kSaveAt + kRun, gB.level.leveltime);

    WorldSnap a;
    WorldSnap b;

    snapshot(gA, a);
    snapshot(gB, b);
    TEST_ASSERT_TRUE_MESSAGE(a.mobjCount == b.mobjCount,
                             "the two worlds hold a different number of mobjs");

    /* The differential is only a proof if something happened. Count the things
     * that moved or changed state between the save and the end, and require a
     * shot to have been fired: a script whose every command was ignored would
     * compare two identical idle worlds and pass. */
    std::uint32_t changed = 0u;

    for (std::uint32_t i = 0u; i < atSave.mobjCount && i < a.mobjCount; ++i) {
        if (atSave.mobjs[i].x != a.mobjs[i].x ||
            atSave.mobjs[i].y != a.mobjs[i].y ||
            atSave.mobjs[i].z != a.mobjs[i].z ||
            atSave.mobjs[i].state != a.mobjs[i].state ||
            atSave.mobjs[i].health != a.mobjs[i].health) {
            ++changed;
        }
    }
    std::printf("SAVE_DIFFERENTIAL mobjs=%u changed=%u ammo=%d health=%d\n",
                static_cast<unsigned>(a.mobjCount),
                static_cast<unsigned>(changed),
                static_cast<int>(gA.level.playerState.ammo[P_AM_CLIP]),
                static_cast<int>(gA.level.playerState.health));
    TEST_ASSERT_TRUE_MESSAGE(changed >= 2u,
                             "the script changed fewer than two mobjs: the "
                             "differential is not exercising the world");
    TEST_ASSERT_TRUE_MESSAGE(gA.level.playerState.ammo[P_AM_CLIP] < 50,
                             "the weapon never fired: BT_ATTACK did nothing");
    assertSnapshotsEqual(a, b, "differential");
    destroyWorld(gA);
    destroyWorld(gB);
}

void test_differential_with_a_door_mid_motion(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;
    constexpr int kRun = 120;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    const int doorLine = findManualDoorLine(gA.level);

    TEST_ASSERT_TRUE_MESSAGE(doorLine >= 0, "E1M1 has no manual door line");
    TEST_ASSERT_TRUE(p_vertical_door(&gA.level.context,
                                     static_cast<std::uint16_t>(doorLine), 1) != 0);
    /* Run until the leaf is off the floor, so the record is taken mid-motion. */
    doom_line_t record;
    std::uint16_t doorSector = 0u;

    doom_map_get_line(gA.level.map, static_cast<std::uint16_t>(doorLine), &record);
    TEST_ASSERT_TRUE(p_line_back_sector(gA.level.map, &record, &doorSector));
    const fixed_t closed = gA.level.sectors[doorSector].floorHeight;
    int tics = 0;

    while (gA.level.sectors[doorSector].ceilingHeight <= closed && tics < 60) {
        runTics(gA, 1);
        ++tics;
    }
    TEST_ASSERT_TRUE_MESSAGE(
        gA.level.sectors[doorSector].ceilingHeight > closed,
        "the door never left the floor");
    const fixed_t mid = gA.level.sectors[doorSector].ceilingHeight;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));
    runTics(gA, kRun);

    TEST_ASSERT_TRUE(beginWorld(gB, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveApply(payload, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_EQUAL_INT32(mid, gB.level.sectors[doorSector].ceilingHeight);
    runTics(gB, kRun);
    TEST_ASSERT_TRUE_MESSAGE(
        gA.level.sectors[doorSector].ceilingHeight != mid,
        "the door did not finish moving after the save");
    TEST_ASSERT_EQUAL_INT32(gA.level.sectors[doorSector].ceilingHeight,
                            gB.level.sectors[doorSector].ceilingHeight);

    WorldSnap a;
    WorldSnap b;

    snapshot(gA, a);
    snapshot(gB, b);
    assertSnapshotsEqual(a, b, "door differential");
    destroyWorld(gA);
    destroyWorld(gB);
}

void test_missing_rng_must_diverge(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;
    constexpr int kSaveAt = 20;
    constexpr int kRun = 35;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    runTics(gA, kSaveAt);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));
    runTics(gA, kRun);

    TEST_ASSERT_TRUE(beginWorld(gC, "E1M1", P_SKILL_MEDIUM));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveApply(payload, written, 1, gC.level,
                                                     gC.level.playerState)));
    /* Exactly what a restore that did not archive the RNG would leave: the
     * sequence at its start while the world is 20 tics old. */
    gC.level.random.index = 0u;
    runTics(gC, kRun);

    WorldSnap a;
    WorldSnap c;
    char message[256];

    snapshot(gA, a);
    snapshot(gC, c);
    /* The RNG *index* is not an observable: two worlds may stand in the same
     * place with the sequence at different positions. Zero it on both sides so
     * the comparison is about what a player or a monster would see. */
    a.randomIndex = 0u;
    c.randomIndex = 0u;
    message[0] = '\0';
    TEST_ASSERT_FALSE_MESSAGE(compareSnapshots(a, c, message, sizeof message),
                              "a restore that skipped the RNG produced the same "
                              "world: the differential cannot see the RNG");
    destroyWorld(gA);
    destroyWorld(gC);
}

void test_rejections_leave_the_world_untouched(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    runTics(gA, 12);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));

    /* A second world that every rejection is applied to. */
    TEST_ASSERT_TRUE(beginWorld(gB, "E1M1", P_SKILL_MEDIUM));
    runTics(gB, 5);
    WorldSnap before;
    WorldSnap after;

    snapshot(gB, before);

    auto unchanged = [&]() -> bool {
        snapshot(gB, after);
        char message[256];

        message[0] = '\0';
        return compareSnapshots(before, after, message, sizeof message);
    };

    std::uint8_t copy[kSavePayloadMax + 64];

    std::memcpy(copy, payload, written);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Truncated),
                          static_cast<int>(saveApply(copy, written - 5u, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "a truncated record changed the world");

    std::memcpy(copy, payload, written);
    copy[written / 2u] ^= 0x40u;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::BadField),
                          static_cast<int>(saveApply(copy, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "a flipped byte changed the world");

    std::memcpy(copy, payload, written);
    copy[4] = 99u; /* version */
    copy[5] = 0u;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::BadVersion),
                          static_cast<int>(saveApply(copy, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "an unknown version changed the world");

    std::memcpy(copy, payload, written);
    copy[0] = 'X';
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::BadMagic),
                          static_cast<int>(saveApply(copy, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "a bad magic changed the world");

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::WrongLevel),
                          static_cast<int>(saveApply(payload, written, 2, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "a wrong level number changed the world");

    /* Same number, different map: the fingerprint has to catch it. */
    destroyWorld(gB);
    TEST_ASSERT_TRUE(beginWorld(gB, "E1M2", P_SKILL_MEDIUM));
    snapshot(gB, before);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::WrongLevel),
                          static_cast<int>(saveApply(payload, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "an E1M1 record changed an E1M2 world");

    destroyWorld(gB);
    TEST_ASSERT_TRUE(beginWorld(gB, "E1M1", P_SKILL_EASY));
    snapshot(gB, before);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::WrongSkill),
                          static_cast<int>(saveApply(payload, written, 1, gB.level,
                                                     gB.level.playerState)));
    TEST_ASSERT_TRUE_MESSAGE(unchanged(), "a wrong skill changed the world");
    destroyWorld(gA);
    destroyWorld(gB);
}

void test_budget_worst_case_fits(void) {
    std::uint8_t payload[kSavePayloadMax + 64];
    std::size_t written = 0u;

    /* Nightmare spawns the most things E1M1 can: 116 mobjs (hard does the same,
     * and the two share the 4 option bit). Measure at tic 0, when the count is
     * highest, which is the case this budget is about. */
    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_NIGHTMARE));
    const std::uint32_t mobjs = p_level_mobj_count(&gA.level);

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     written)));
    std::printf("SAVE_BUDGET mobjs=%u bytes=%u max=%u\n",
                static_cast<unsigned>(mobjs), static_cast<unsigned>(written),
                static_cast<unsigned>(kSavePayloadMax));
    {
        char line[96];

        std::snprintf(line, sizeof line, "SAVE_BUDGET mobjs=%u bytes=%u max=%u",
                      static_cast<unsigned>(mobjs),
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(kSavePayloadMax));
        TEST_MESSAGE(line);
    }
    TEST_ASSERT_EQUAL_UINT32(116u, mobjs);
    TEST_ASSERT_TRUE(written <= kSavePayloadMax);

    /* A save taken after the script has run is the other end of the range: the
     * monsters that woke are larger records, the rest are still small. Printing
     * it keeps the claim honest rather than only measuring the easy state. */
    runTics(gA, 55);
    std::size_t after = 0u;

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, payload, sizeof payload,
                                                     after)));
    std::printf("SAVE_BUDGET_AFTER_55_TICS mobjs=%u bytes=%u max=%u\n",
                static_cast<unsigned>(p_level_mobj_count(&gA.level)),
                static_cast<unsigned>(after),
                static_cast<unsigned>(kSavePayloadMax));
    {
        char line[96];

        std::snprintf(line, sizeof line,
                      "SAVE_BUDGET_AFTER_55_TICS mobjs=%u bytes=%u max=%u",
                      static_cast<unsigned>(p_level_mobj_count(&gA.level)),
                      static_cast<unsigned>(after),
                      static_cast<unsigned>(kSavePayloadMax));
        TEST_MESSAGE(line);
    }
    TEST_ASSERT_TRUE(after <= kSavePayloadMax);
    destroyWorld(gA);
}

void test_writing_twice_is_byte_identical(void) {
    std::uint8_t first[kSavePayloadMax + 64];
    std::uint8_t second[kSavePayloadMax + 64];
    std::size_t firstLength = 0u;
    std::size_t secondLength = 0u;

    TEST_ASSERT_TRUE(beginWorld(gA, "E1M1", P_SKILL_MEDIUM));
    runTics(gA, 17);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, first, sizeof first,
                                                     firstLength)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SaveStatus::Ok),
                          static_cast<int>(saveWrite(gA.level, gA.level.playerState,
                                                     1, second, sizeof second,
                                                     secondLength)));
    TEST_ASSERT_EQUAL_UINT32(static_cast<std::uint32_t>(firstLength),
                             static_cast<std::uint32_t>(secondLength));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(first, second, firstLength);
    destroyWorld(gA);
}

} /* namespace */

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_header_round_trip_matches_the_archived_state);
    RUN_TEST(test_differential_after_55_tics);
    RUN_TEST(test_differential_with_a_door_mid_motion);
    RUN_TEST(test_missing_rng_must_diverge);
    RUN_TEST(test_rejections_leave_the_world_untouched);
    RUN_TEST(test_budget_worst_case_fits);
    RUN_TEST(test_writing_twice_is_byte_identical);

    return UNITY_END();
}
