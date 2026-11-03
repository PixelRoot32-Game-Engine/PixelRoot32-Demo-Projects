/*
 * The save record: writer, header reader and restore. See SaveGame.h for what
 * the record contains and why each field is or is not in it.
 *
 * This is engine-free C++17 with no allocation, no floating point and no stdio.
 * The only memory it touches is the caller's payload buffer and the caller's
 * level.
 */
#include "doomc/SaveGame.h"

#include <cstring>

#include "info_tables.h"
#include "p_ceilng.h"
#include "p_doors.h"
#include "p_floor.h"
#include "p_lights.h"
#include "p_plats.h"
#include "p_pspr.h"
#include "p_tick.h"

namespace doom {
namespace doomc {
namespace {

/* --- Format constants ----------------------------------------------------- */

/* "DSAV" little-endian. A record that does not start with this is not ours. */
constexpr std::uint32_t kMagic = 0x56415344u;
constexpr std::uint16_t kVersion = 1u;
constexpr std::uint16_t kSchema = 1u;

/* magic 4, version 2, schema 2, level 1, skill 1, reserved 2, leveltime 4,
 * kills 4, fingerprint 4, label 16, bodySize 4, checksum 4. */
constexpr std::size_t kHeaderSize = 48u;

/* Thinker kind byte. The low seven bits are the kind; the two flags ride in it
 * so a thinker costs one byte of type rather than three. */
constexpr std::uint8_t kKindMobj = 0x01u;
constexpr std::uint8_t kKindDoor = 0x02u;
constexpr std::uint8_t kKindCeiling = 0x03u;
constexpr std::uint8_t kKindFloor = 0x04u;
constexpr std::uint8_t kKindPlat = 0x05u;
constexpr std::uint8_t kKindLight = 0x06u;
constexpr std::uint8_t kKindMask = 0x3Fu;
constexpr std::uint8_t kFlagInert = 0x40u;   /* thinker.function == NULL */
constexpr std::uint8_t kFlagRemoved = 0x80u; /* marked, freed by the next walk */

constexpr std::uint16_t kNoRef = 0xFFFFu;

/* `tics` is -1 (permanent) or a positive countdown; a state transition always
 * copies the state table's tics. 0x7FFF therefore cannot occur and is free to
 * mean "this mobj's countdown is exactly states[state].tics", which is true of
 * every thing that has not been ticked since its last transition -- almost all
 * of E1M1's 116. Saving that as a sentinel is 2 bytes per idle mobj, which is
 * the difference between a self-checking budget and a lucky one. */
constexpr std::int16_t kTicsDefault = 0x7FFF;

/* Mobj field mask: the bit is set when the field equals what a fresh
 * p_spawn_mobj() of the same type produces, so it can be omitted. The reader
 * derives the omitted value from the same table, which is why the two sides can
 * never disagree. */
constexpr std::uint8_t kMobjZDefault = 0x01u;
constexpr std::uint8_t kMobjAngleCompass = 0x02u;
constexpr std::uint8_t kMobjMomZero = 0x04u;
constexpr std::uint8_t kMobjFloorCeilDefault = 0x08u;
constexpr std::uint8_t kMobjHealthDefault = 0x10u;
constexpr std::uint8_t kMobjFlagsDefault = 0x20u;
constexpr std::uint8_t kMobjRefsNone = 0x40u;
constexpr std::uint8_t kMobjAiDefault = 0x80u;

/* Sector delta field mask. */
constexpr std::uint8_t kSectorFloorHeight = 0x01u;
constexpr std::uint8_t kSectorCeilingHeight = 0x02u;
constexpr std::uint8_t kSectorFloorPic = 0x04u;
constexpr std::uint8_t kSectorCeilingPic = 0x08u;
constexpr std::uint8_t kSectorLight = 0x10u;
constexpr std::uint8_t kSectorSpecial = 0x20u;
constexpr std::uint8_t kSectorTag = 0x40u;

/* --- Byte I/O ------------------------------------------------------------- */

struct Writer {
    std::uint8_t* out;
    std::size_t cap;
    std::size_t at = 0u;
    bool overflow = false;

    void u8(std::uint8_t value) {
        if (overflow || at + 1u > cap) {
            overflow = true;
            return;
        }
        out[at++] = value;
    }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
    }
    void i16(std::int16_t value) { u16(static_cast<std::uint16_t>(value)); }
    void u32(std::uint32_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
        u8(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
    }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
        u32(static_cast<std::uint32_t>(value >> 32));
    }
    void block(const void* data, std::size_t size) {
        if (overflow || at + size > cap) {
            overflow = true;
            return;
        }
        if (size != 0u) {
            std::memcpy(out + at, data, size);
        }
        at += size;
    }
};

struct Reader {
    const std::uint8_t* in;
    std::size_t size;
    std::size_t at = 0u;
    bool bad = false;

    bool need(std::size_t count) {
        if (bad || at + count > size) {
            bad = true;
            return false;
        }
        return true;
    }
    std::uint8_t u8() {
        if (!need(1u)) {
            return 0u;
        }
        return in[at++];
    }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const std::uint32_t b0 = u8();
        const std::uint32_t b1 = u8();
        const std::uint32_t b2 = u8();
        const std::uint32_t b3 = u8();
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        const std::uint64_t hi = u32();
        return lo | (hi << 32);
    }
    bool seek(std::size_t offset) {
        if (offset > size) {
            bad = true;
            return false;
        }
        at = offset;
        return true;
    }
};

/* FNV-1a. A save is small, so a hash is cheaper than a table and needs no
 * library; it exists so one flipped byte is a BadField instead of a world that
 * looks plausible until a monster walks through a wall. The engine's
 * RecordCodec adds its own CRC-32 envelope; this is the save layer's guard, so
 * the host tests can prove the rejection without the engine. */
constexpr std::uint32_t kFnvBasis = 2166136261u;
constexpr std::uint32_t kFnvPrime = 16777619u;

std::uint32_t fnvBytes(std::uint32_t hash, const void* data, std::size_t size) {
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(data);

    for (std::size_t i = 0u; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint32_t fnvU32(std::uint32_t hash, std::uint32_t value) {
    std::uint8_t bytes[4];
    bytes[0] = static_cast<std::uint8_t>(value & 0xFFu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    bytes[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    bytes[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
    return fnvBytes(hash, bytes, sizeof bytes);
}

/*
 * What "this map" means.
 *
 * A p_level_t does not name its map and a doom_map_t is a set of lump views, so
 * the record identifies the map by its THINGS bytes plus the line and sector
 * counts. Two episodes' maps cannot collide on the same WAD, and a save from
 * E1M1 opened into E1M2 is a WrongLevel even when the caller's levelNumber was
 * equally wrong.
 */
std::uint32_t mapFingerprint(const doom_map_t* map) {
    std::uint32_t hash = kFnvBasis;

    hash = fnvBytes(hash, map->lumps[DOOM_MAP_THINGS].data,
                    map->lumps[DOOM_MAP_THINGS].size);
    hash = fnvU32(hash, map->lumps[DOOM_MAP_LINEDEFS].count);
    hash = fnvU32(hash, map->lumps[DOOM_MAP_SECTORS].count);
    hash = fnvU32(hash, map->lumps[DOOM_MAP_VERTEXES].count);
    return hash;
}

/* --- Labels --------------------------------------------------------------- */

const char* skillTag(std::uint8_t skill) {
    switch (skill) {
        case P_SKILL_BABY: return "BABY";
        case P_SKILL_EASY: return "EASY";
        case P_SKILL_MEDIUM: return "MED";
        case P_SKILL_HARD: return "HURT";
        case P_SKILL_NIGHTMARE: return "NGT";
        default: return "???";
    }
}

/*
 * "E1M1 HURT 03:12": episode 1 map level, a four-letter skill tag and the clock.
 * Fifteen characters plus the terminator is exactly the field, which is why the
 * tags are short; "NIGHT" would not fit beside the clock. A time past 99:59 is
 * clamped rather than wrapped, because a label that reads 00:10 after two hours
 * is worse than one that reads 99:59.
 */
void makeLabel(char out[16], std::uint8_t level, std::uint8_t skill,
               std::int32_t leveltime) {
    std::int32_t seconds = leveltime / P_TICRATE;

    if (seconds < 0) {
        seconds = 0;
    }
    if (seconds > 99 * 60 + 59) {
        seconds = 99 * 60 + 59;
    }
    const std::int32_t minutes = seconds / 60;
    const std::int32_t rest = seconds % 60;
    const char* tag = skillTag(skill);

    std::size_t at = 0u;
    out[at++] = 'E';
    out[at++] = '1';
    out[at++] = 'M';
    out[at++] = static_cast<char>('0' + level);
    out[at++] = ' ';
    for (std::size_t i = 0u; tag[i] != '\0' && at < 15u; ++i) {
        out[at++] = tag[i];
    }
    out[at++] = ' ';
    out[at++] = static_cast<char>('0' + (minutes / 10) % 10);
    out[at++] = static_cast<char>('0' + minutes % 10);
    out[at++] = ':';
    out[at++] = static_cast<char>('0' + (rest / 10) % 10);
    out[at++] = static_cast<char>('0' + rest % 10);
    out[at] = '\0';
}

/* --- Shared shapes -------------------------------------------------------- */

struct PlayerData {
    std::uint16_t mo;
    std::uint8_t playerstate;
    std::int8_t forwardmove;
    std::int8_t sidemove;
    std::int16_t angleturn;
    std::uint8_t buttons;
    std::int32_t viewz;
    std::int32_t viewheight;
    std::int32_t deltaviewheight;
    std::int32_t bob;
    std::int32_t health;
    std::int32_t armorpoints;
    std::int32_t armortype;
    std::int32_t ammo[P_NUMAMMO];
    std::int32_t maxammo[P_NUMAMMO];
    std::int32_t powers[P_NUMPOWERS];
    std::uint8_t pendingweapon;
    std::uint8_t readyweapon;
    std::int16_t pspriteState[P_NUMPSPRITES];
    std::int16_t pspriteTics[P_NUMPSPRITES];
    std::int32_t pspriteSx[P_NUMPSPRITES];
    std::int32_t pspriteSy[P_NUMPSPRITES];
    std::uint8_t weaponowned[P_NUMWEAPONS];
    std::uint8_t cards[P_NUMCARDS];
    std::uint8_t backpack;
    std::uint8_t attackdown;
    std::uint8_t usedown;
    std::int16_t refire;
    std::int32_t killcount;
    std::int32_t itemcount;
    std::int32_t secretcount;
    std::int32_t damagecount;
    std::int32_t bonuscount;
    std::int32_t extralight;
    std::int32_t fixedcolormap;
    std::uint16_t attacker;
    std::uint8_t didsecret;
    std::uint8_t onground;
    std::uint8_t useTriggered;
    std::uint8_t pspritesStarted;
};

struct MobjData {
    std::uint8_t type;
    std::uint8_t mask;
    std::int32_t x;
    std::int32_t y;
    std::int32_t z;
    std::uint32_t angle;
    std::int32_t momx;
    std::int32_t momy;
    std::int32_t momz;
    std::int32_t floorz;
    std::int32_t ceilingz;
    std::uint16_t state;
    std::int16_t tics;
    std::int32_t health;
    std::uint32_t flags;
    std::uint8_t lastlook;
    std::int16_t reactiontime;
    std::int16_t movedir;
    std::int16_t movecount;
    std::int16_t threshold;
    std::uint16_t target;
    std::uint16_t tracer;
    bool removed;
};

struct MoverData {
    std::uint8_t kind;
    bool inert;
    bool removed;
    std::uint16_t sector;
    std::int16_t type;
    std::int16_t direction;
    std::int16_t status;
    std::int16_t oldStatus;
    std::int16_t crush;
    std::int16_t tag;
    std::int16_t oldDirection;
    std::int16_t newspecial;
    std::uint64_t texture;
    std::int32_t speed;
    std::int32_t topHeight;
    std::int32_t bottomHeight;
    std::int32_t destHeight;
    std::int32_t low;
    std::int32_t high;
    std::int32_t wait;
    std::int32_t count;
    std::int32_t topWait;
    std::int32_t topCountdown;
    std::int32_t maxLight;
    std::int32_t minLight;
    std::int32_t maxTime;
    std::int32_t minTime;
    std::int32_t darkTime;
    std::int32_t brightTime;
};

/* --- The mobj walk -------------------------------------------------------- */

/*
 * The index of @p needle in the level's mobj order, or -1.
 *
 * The order is p_level_first_mobj()'s: thinker insertion order without the
 * sector thinkers. The writer and the reader both define an index that way, so
 * a reference means the same thing on both sides. A pointer that is not in the
 * list (a dangling target, a mobj removed and freed since) answers -1 and the
 * reference is stored as kNoRef; that is the same information the original had,
 * because a dangling pointer cannot be followed either.
 */
int mobjIndex(const p_level_t& level, const r_mobj_t* needle) {
    const r_mobj_t* mobj;
    int index = 0;

    if (needle == nullptr) {
        return -1;
    }
    for (mobj = p_level_first_mobj(&level); mobj != nullptr;
         mobj = p_level_next_mobj(mobj), ++index) {
        if (mobj == needle) {
            return index;
        }
    }
    return -1;
}

r_mobj_t* mobjAt(const p_level_t& level, std::uint16_t index) {
    const r_mobj_t* mobj = p_level_first_mobj(&level);

    while (mobj != nullptr && index != 0u) {
        mobj = p_level_next_mobj(mobj);
        --index;
    }
    return const_cast<r_mobj_t*>(mobj);
}

std::uint16_t reference(const p_level_t& level, const r_mobj_t* needle) {
    const int index = mobjIndex(level, needle);

    return index < 0 ? kNoRef : static_cast<std::uint16_t>(index);
}

/* Narrow writers with a range check.
 *
 * The player's counters are small and a fixed 32-bit word for each of them is
 * bytes the mobj list cannot spare. Narrowing is safe only if a value that does
 * not fit is a refusal rather than a silently truncated field, so each helper
 * marks the writer overflowed and saveWrite answers TooLarge. */
void putU16Range(Writer& w, std::int32_t value) {
    if (value < 0 || value > 0xFFFF) {
        w.overflow = true;
        return;
    }
    w.u16(static_cast<std::uint16_t>(value));
}

void putI16Range(Writer& w, std::int32_t value) {
    if (value < -32768 || value > 32767) {
        w.overflow = true;
        return;
    }
    w.i16(static_cast<std::int16_t>(value));
}

void putU8Range(Writer& w, std::int32_t value) {
    if (value < 0 || value > 255) {
        w.overflow = true;
        return;
    }
    w.u8(static_cast<std::uint8_t>(value));
}

/* --- Writing the player --------------------------------------------------- */

void writePlayer(Writer& w, const p_level_t& level, const p_player_t& p) {
    w.u16(reference(level, p.mo));
    w.u8(static_cast<std::uint8_t>(p.playerstate));
    w.u8(static_cast<std::uint8_t>(p.cmd.forwardmove));
    w.u8(static_cast<std::uint8_t>(p.cmd.sidemove));
    w.i16(p.cmd.angleturn);
    w.u8(p.cmd.buttons);
    w.i32(p.viewz);
    w.i32(p.viewheight);
    w.i32(p.deltaviewheight);
    w.i32(p.bob);
    putI16Range(w, p.health);
    putI16Range(w, p.armorpoints);
    putI16Range(w, p.armortype);
    for (int i = 0; i < P_NUMAMMO; ++i) {
        putU16Range(w, p.ammo[i]);
    }
    for (int i = 0; i < P_NUMAMMO; ++i) {
        putU16Range(w, p.maxammo[i]);
    }
    for (int i = 0; i < P_NUMPOWERS; ++i) {
        w.i32(p.powers[i]);
    }
    w.u8(static_cast<std::uint8_t>(p.pendingweapon));
    w.u8(static_cast<std::uint8_t>(p.readyweapon));
    for (int i = 0; i < P_NUMPSPRITES; ++i) {
        w.i16(p.psprites[i].state);
        w.i16(static_cast<std::int16_t>(p.psprites[i].tics));
        w.i32(p.psprites[i].sx);
        w.i32(p.psprites[i].sy);
    }
    for (int i = 0; i < P_NUMWEAPONS; ++i) {
        w.u8(p.weaponowned[i]);
    }
    for (int i = 0; i < P_NUMCARDS; ++i) {
        w.u8(p.cards[i]);
    }
    w.u8(p.backpack);
    w.u8(static_cast<std::uint8_t>(p.attackdown != 0));
    w.u8(static_cast<std::uint8_t>(p.usedown != 0));
    putU16Range(w, p.refire);
    putU16Range(w, p.killcount);
    putU16Range(w, p.itemcount);
    putU16Range(w, p.secretcount);
    putU16Range(w, p.damagecount);
    putU16Range(w, p.bonuscount);
    putU8Range(w, p.extralight);
    putU8Range(w, p.fixedcolormap);
    w.u16(reference(level, p.attacker));
    w.u8(p.didsecret);
    w.u8(p.onground);
    w.u8(p.useTriggered);
    w.u8(p.pspritesStarted);
}

/* --- Writing the world ---------------------------------------------------- */

/*
 * The sector record as the *map* holds it.
 *
 * doom_map_get_sector() answers through the renderer's live seam when the game
 * has bound it (p_level_bind_render(), which the frame session does). A save
 * taken during play would then compare the live array against itself and store
 * no deltas at all -- a record that restores every door to its closed height.
 * This decodes the immutable SECTORS lump directly, with the same 26-byte
 * layout and the same r_pack_name() the accessor uses. The seam is deliberately
 * not consulted: the baseline of a delta is the WAD, not the world.
 */
bool immutableSector(const doom_map_t* map, std::uint16_t index,
                     doom_sector_t& out) {
    const doom_map_lump_view_t& lump = map->lumps[DOOM_MAP_SECTORS];
    const std::uint8_t* at;

    if (lump.data == nullptr || static_cast<std::uint32_t>(index) >= lump.count) {
        return false;
    }
    at = lump.data + static_cast<std::size_t>(index) * DOOM_SECTOR_SIZE;
    out.floorHeight = static_cast<fixed_t>(
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(
            at[0] | (static_cast<std::uint16_t>(at[1]) << 8))) << 16);
    out.ceilingHeight = static_cast<fixed_t>(
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(
            at[2] | (static_cast<std::uint16_t>(at[3]) << 8))) << 16);
    out.floorPic = r_pack_name(at + 4);
    out.ceilingPic = r_pack_name(at + 12);
    out.lightLevel = static_cast<std::int16_t>(static_cast<std::uint16_t>(
        at[20] | (static_cast<std::uint16_t>(at[21]) << 8)));
    out.special = static_cast<std::int16_t>(static_cast<std::uint16_t>(
        at[22] | (static_cast<std::uint16_t>(at[23]) << 8)));
    out.tag = static_cast<std::int16_t>(static_cast<std::uint16_t>(
        at[24] | (static_cast<std::uint16_t>(at[25]) << 8)));
    return true;
}

std::int16_t defaultReactionTime(const p_level_t& level, std::int16_t type) {
    if (level.rules.skill == P_SKILL_NIGHTMARE) {
        return 0;
    }
    return static_cast<std::int16_t>(mobjinfo[type].reactiontime);
}

void writeMobj(Writer& w, const p_level_t& level, const p_thinker_t* node) {
    const r_mobj_t* m = p_mobj_of_const(node);
    std::uint8_t mask = 0u;

    if (m == nullptr || m->type < 0 || m->type >= NUM_MOBJ_TYPES ||
        m->state < 0 || m->state >= NUM_STATES) {
        /* A corrupt world is not a record this layer can describe. Refusing
         * here is why saveWrite answers TooLarge instead of writing a body whose
         * indices the reader would have to reject. */
        w.overflow = true;
        return;
    }
    if (m->z == m->floorz) {
        mask |= kMobjZDefault;
    }
    if ((m->angle % ANG45) == 0u) {
        mask |= kMobjAngleCompass;
    }
    if (m->momx == 0 && m->momy == 0 && m->momz == 0) {
        mask |= kMobjMomZero;
    }
    if (m->sector < level.sectorCount &&
        level.sectors[m->sector].floorHeight == m->floorz &&
        level.sectors[m->sector].ceilingHeight == m->ceilingz) {
        mask |= kMobjFloorCeilDefault;
    }
    if (m->health == mobjinfo[m->type].spawnhealth) {
        mask |= kMobjHealthDefault;
    }
    if (m->flags == mobjinfo[m->type].flags) {
        mask |= kMobjFlagsDefault;
    }
    if (m->target == nullptr && m->tracer == nullptr) {
        mask |= kMobjRefsNone;
    }
    if (m->reactiontime == defaultReactionTime(level, m->type) &&
        m->movedir == 0 && m->movecount == 0 && m->threshold == 0) {
        mask |= kMobjAiDefault;
    }

    w.u8(static_cast<std::uint8_t>(kKindMobj |
                                   (node->removed != 0u ? kFlagRemoved : 0u)));
    w.u8(static_cast<std::uint8_t>(m->type));
    w.u8(mask);
    w.i32(m->x);
    w.i32(m->y);
    w.u16(static_cast<std::uint16_t>(m->state));
    if (m->tics == states[m->state].tics) {
        w.i16(kTicsDefault);
    } else {
        w.i16(m->tics);
    }
    w.u8(static_cast<std::uint8_t>(m->lastlook));
    if ((mask & kMobjZDefault) == 0u) {
        w.i32(m->z);
    }
    if ((mask & kMobjAngleCompass) != 0u) {
        w.u8(static_cast<std::uint8_t>(m->angle / ANG45));
    } else {
        w.u32(m->angle);
    }
    if ((mask & kMobjMomZero) == 0u) {
        w.i32(m->momx);
        w.i32(m->momy);
        w.i32(m->momz);
    }
    if ((mask & kMobjFloorCeilDefault) == 0u) {
        w.i32(m->floorz);
        w.i32(m->ceilingz);
    }
    if ((mask & kMobjHealthDefault) == 0u) {
        w.i32(m->health);
    }
    if ((mask & kMobjFlagsDefault) == 0u) {
        w.u32(m->flags);
    }
    if ((mask & kMobjRefsNone) == 0u) {
        w.u16(reference(level, m->target));
        w.u16(reference(level, m->tracer));
    }
    if ((mask & kMobjAiDefault) == 0u) {
        w.i16(m->reactiontime);
        w.i16(m->movedir);
        w.i16(m->movecount);
        w.i16(m->threshold);
    }
}

void writeMover(Writer& w, const p_thinker_t* node) {
    std::uint8_t kind = 0u;

    switch (node->kind) {
        case P_THINKER_DOOR: kind = kKindDoor; break;
        case P_THINKER_CEILING: kind = kKindCeiling; break;
        case P_THINKER_FLOOR: kind = kKindFloor; break;
        case P_THINKER_PLAT: kind = kKindPlat; break;
        case P_THINKER_LIGHTFLASH: kind = kKindLight; break;
        default: kind = 0u; break;
    }
    if (kind == 0u) {
        /* A thinker kind this record does not know. The writer marks the body
         * overflow so saveWrite answers TooLarge rather than dropping it: a
         * save that silently loses a door is worse than a save that refuses. */
        w.overflow = true;
        return;
    }
    kind |= (node->removed != 0u ? kFlagRemoved : 0u);
    kind |= (node->function == nullptr ? kFlagInert : 0u);
    w.u8(kind);

    switch (node->kind) {
        case P_THINKER_DOOR: {
            const p_door_thinker_t* d =
                reinterpret_cast<const p_door_thinker_t*>(node);
            w.u16(d->sector);
            w.i16(d->type);
            w.i16(d->direction);
            w.i32(d->topHeight);
            w.i32(d->speed);
            w.i32(d->topWait);
            w.i32(d->topCountdown);
            break;
        }
        case P_THINKER_CEILING: {
            const p_ceiling_thinker_t* c =
                reinterpret_cast<const p_ceiling_thinker_t*>(node);
            w.u16(c->sector);
            w.i16(c->type);
            w.i16(c->direction);
            w.i16(c->crush);
            w.i16(c->tag);
            w.i16(c->oldDirection);
            w.i32(c->bottomHeight);
            w.i32(c->topHeight);
            w.i32(c->speed);
            break;
        }
        case P_THINKER_FLOOR: {
            const p_floor_thinker_t* f =
                reinterpret_cast<const p_floor_thinker_t*>(node);
            w.u16(f->sector);
            w.i16(f->type);
            w.i16(f->direction);
            w.i16(f->crush);
            w.i16(f->newspecial);
            w.u64(f->texture);
            w.i32(f->destHeight);
            w.i32(f->speed);
            break;
        }
        case P_THINKER_PLAT: {
            const p_plat_thinker_t* p =
                reinterpret_cast<const p_plat_thinker_t*>(node);
            w.u16(p->sector);
            w.i16(p->type);
            w.i16(p->status);
            w.i16(p->oldStatus);
            w.i16(p->crush);
            w.i16(p->tag);
            w.i32(p->speed);
            w.i32(p->low);
            w.i32(p->high);
            w.i32(p->wait);
            w.i32(p->count);
            break;
        }
        case P_THINKER_LIGHTFLASH: {
            const p_light_thinker_t* l =
                reinterpret_cast<const p_light_thinker_t*>(node);
            w.u16(l->sector);
            w.i16(l->type);
            w.i16(l->direction);
            w.i32(l->count);
            w.i32(l->maxLight);
            w.i32(l->minLight);
            w.i32(l->maxTime);
            w.i32(l->minTime);
            w.i32(l->darkTime);
            w.i32(l->brightTime);
            break;
        }
        default: break;
    }
}

/* How many sectors and lines differ from the immutable map record. */
void writeSectors(Writer& w, const p_level_t& level) {
    std::uint16_t count = 0u;

    for (std::uint16_t i = 0u; i < level.sectorCount; ++i) {
        doom_sector_t base;
        const p_sector_t& s = level.sectors[i];
        std::uint8_t mask = 0u;

        (void)immutableSector(level.map, i, base);
        if (s.floorHeight != base.floorHeight) mask |= kSectorFloorHeight;
        if (s.ceilingHeight != base.ceilingHeight) mask |= kSectorCeilingHeight;
        if (s.floorPic != base.floorPic) mask |= kSectorFloorPic;
        if (s.ceilingPic != base.ceilingPic) mask |= kSectorCeilingPic;
        if (s.lightLevel != base.lightLevel) mask |= kSectorLight;
        /* A nonzero WAD special may have been cleared by P_SpawnSpecials(), so
         * its current value has to be pinned even when it still equals the WAD
         * byte (special 4 keeps its number, and 9/5/7/11/16 keep theirs). */
        if (s.special != base.special || base.special != 0) mask |= kSectorSpecial;
        if (s.tag != base.tag) mask |= kSectorTag;
        if (mask != 0u) {
            ++count;
        }
    }
    w.u16(count);
    for (std::uint16_t i = 0u; i < level.sectorCount; ++i) {
        doom_sector_t base;
        const p_sector_t& s = level.sectors[i];
        std::uint8_t mask = 0u;

        (void)immutableSector(level.map, i, base);
        if (s.floorHeight != base.floorHeight) mask |= kSectorFloorHeight;
        if (s.ceilingHeight != base.ceilingHeight) mask |= kSectorCeilingHeight;
        if (s.floorPic != base.floorPic) mask |= kSectorFloorPic;
        if (s.ceilingPic != base.ceilingPic) mask |= kSectorCeilingPic;
        if (s.lightLevel != base.lightLevel) mask |= kSectorLight;
        if (s.special != base.special || base.special != 0) mask |= kSectorSpecial;
        if (s.tag != base.tag) mask |= kSectorTag;
        if (mask == 0u) {
            continue;
        }
        w.u16(i);
        w.u8(mask);
        if ((mask & kSectorFloorHeight) != 0u) w.i32(s.floorHeight);
        if ((mask & kSectorCeilingHeight) != 0u) w.i32(s.ceilingHeight);
        if ((mask & kSectorFloorPic) != 0u) w.u64(s.floorPic);
        if ((mask & kSectorCeilingPic) != 0u) w.u64(s.ceilingPic);
        if ((mask & kSectorLight) != 0u) w.i16(s.lightLevel);
        if ((mask & kSectorSpecial) != 0u) w.i16(s.special);
        if ((mask & kSectorTag) != 0u) w.i16(s.tag);
    }
}

void writeLines(Writer& w, const p_level_t& level) {
    std::uint16_t count = 0u;

    for (std::uint16_t i = 0u; i < level.lineCount; ++i) {
        if (level.lineSpecial[i] != p_line_special(level.map, i)) {
            ++count;
        }
    }
    w.u16(count);
    for (std::uint16_t i = 0u; i < level.lineCount; ++i) {
        if (level.lineSpecial[i] == p_line_special(level.map, i)) {
            continue;
        }
        w.u16(i);
        w.i16(level.lineSpecial[i]);
    }
}

void writeThinkers(Writer& w, const p_level_t& level) {
    std::uint16_t thinkers = 0u;

    for (const p_thinker_t* node = p_thinker_first(&level.thinkers);
         node != nullptr; node = p_thinker_next(node)) {
        if (thinkers == 0xFFFFu) {
            w.overflow = true;
            return;
        }
        ++thinkers;
    }
    w.u16(thinkers);
    for (const p_thinker_t* node = p_thinker_first(&level.thinkers);
         node != nullptr; node = p_thinker_next(node)) {
        if (node->kind == P_THINKER_MOBJ) {
            writeMobj(w, level, node);
        } else {
            writeMover(w, node);
        }
    }
}

void writeSound(Writer& w, const p_level_t& level) {
    std::uint16_t count = 0u;

    if (level.context.sectorSound != nullptr) {
        for (std::uint16_t i = 0u; i < level.context.sectorSoundCount; ++i) {
            const r_mobj_t* target = level.context.sectorSound[i].soundTarget;

            if (target != nullptr && mobjIndex(level, target) >= 0) {
                ++count;
            }
        }
    }
    w.u16(count);
    if (level.context.sectorSound == nullptr) {
        return;
    }
    for (std::uint16_t i = 0u; i < level.context.sectorSoundCount; ++i) {
        const r_mobj_t* target = level.context.sectorSound[i].soundTarget;

        if (target == nullptr) {
            continue;
        }
        const int index = mobjIndex(level, target);

        if (index < 0) {
            /* A dangling target (the mobj was removed and freed). There is
             * nothing to index, and the original's pointer cannot be followed
             * either; leaving the sector out is the same state. */
            continue;
        }
        w.u16(i);
        w.u16(static_cast<std::uint16_t>(index));
    }
}

void writeScrolls(Writer& w, const p_level_t& level) {
    w.u16(level.scrollCount);
    for (std::uint16_t i = 0u; i < level.scrollCount; ++i) {
        /* Only `dx` is archived. This upstream's P_UpdateSpecials case 48 has no
         * vertical term (`dy` is written nowhere but the spawn's zero), so a
         * stored `dy` would be 32 bytes of a constant. If a later special gains a
         * vertical scroll, this is the line that has to grow. */
        w.i32(level.scrolls[i].dx);
    }
}

/* --- Writing the whole body ---------------------------------------------- */

void writeBody(Writer& w, const p_level_t& level, const p_player_t& player) {
    /* First, because the reader validates the player's two references and the
     * sound targets against it while it is still parsing forward. */
    const std::uint32_t mobjs = p_level_mobj_count(&level);

    if (mobjs > 0xFFFFu) {
        w.overflow = true;
        return;
    }
    w.u16(static_cast<std::uint16_t>(mobjs));
    w.u8(player.mo != nullptr ? 1u : 0u);
    if (player.mo != nullptr) {
        writePlayer(w, level, player);
    }

    w.i32(level.totalKills);
    w.i32(level.totalItems);
    w.i32(level.totalSecrets);
    w.u8(level.exitRequest);
    w.i32(level.animTime);
    w.u32(level.random.index);

    writeSectors(w, level);
    writeLines(w, level);
    writeThinkers(w, level);
    writeSound(w, level);
    writeScrolls(w, level);
}

void writeHeader(std::uint8_t* out, std::uint8_t levelNumber, std::uint8_t skill,
                 std::int32_t leveltime, std::int32_t kills,
                 std::uint32_t fingerprint, const char label[16],
                 std::uint32_t bodySize, std::uint32_t checksum) {
    Writer w{out, kHeaderSize};

    w.u32(kMagic);
    w.u16(kVersion);
    w.u16(kSchema);
    w.u8(levelNumber);
    w.u8(skill);
    w.u8(0u);
    w.u8(0u);
    w.i32(leveltime);
    w.i32(kills);
    w.u32(fingerprint);
    w.block(label, 16u);
    w.u32(bodySize);
    w.u32(checksum);
}

/* --- Reading -------------------------------------------------------------- */

bool readPlayer(Reader& r, PlayerData& p) {
    p.mo = r.u16();
    p.playerstate = r.u8();
    p.forwardmove = static_cast<std::int8_t>(r.u8());
    p.sidemove = static_cast<std::int8_t>(r.u8());
    p.angleturn = r.i16();
    p.buttons = r.u8();
    p.viewz = r.i32();
    p.viewheight = r.i32();
    p.deltaviewheight = r.i32();
    p.bob = r.i32();
    p.health = r.i16();
    p.armorpoints = r.i16();
    p.armortype = r.i16();
    for (int i = 0; i < P_NUMAMMO; ++i) {
        p.ammo[i] = r.u16();
    }
    for (int i = 0; i < P_NUMAMMO; ++i) {
        p.maxammo[i] = r.u16();
    }
    for (int i = 0; i < P_NUMPOWERS; ++i) {
        p.powers[i] = r.i32();
    }
    p.pendingweapon = r.u8();
    p.readyweapon = r.u8();
    for (int i = 0; i < P_NUMPSPRITES; ++i) {
        p.pspriteState[i] = r.i16();
        p.pspriteTics[i] = r.i16();
        p.pspriteSx[i] = r.i32();
        p.pspriteSy[i] = r.i32();
    }
    for (int i = 0; i < P_NUMWEAPONS; ++i) {
        p.weaponowned[i] = r.u8();
    }
    for (int i = 0; i < P_NUMCARDS; ++i) {
        p.cards[i] = r.u8();
    }
    p.backpack = r.u8();
    p.attackdown = r.u8();
    p.usedown = r.u8();
    p.refire = r.u16();
    p.killcount = r.u16();
    p.itemcount = r.u16();
    p.secretcount = r.u16();
    p.damagecount = r.u16();
    p.bonuscount = r.u16();
    p.extralight = r.u8();
    p.fixedcolormap = r.u8();
    p.attacker = r.u16();
    p.didsecret = r.u8();
    p.onground = r.u8();
    p.useTriggered = r.u8();
    p.pspritesStarted = r.u8();
    return !r.bad;
}

bool readMobj(Reader& r, MobjData& m) {
    m.type = r.u8();
    m.mask = r.u8();
    m.x = r.i32();
    m.y = r.i32();
    m.state = r.u16();
    m.tics = r.i16();
    m.lastlook = r.u8();
    if (m.state >= NUM_STATES) {
        return false;
    }
    if (m.tics == kTicsDefault) {
        m.tics = states[m.state].tics;
    }
    m.z = 0;
    m.angle = 0u;
    m.momx = 0;
    m.momy = 0;
    m.momz = 0;
    m.floorz = 0;
    m.ceilingz = 0;
    m.health = 0;
    m.flags = 0u;
    m.target = kNoRef;
    m.tracer = kNoRef;
    m.reactiontime = 0;
    m.movedir = 0;
    m.movecount = 0;
    m.threshold = 0;
    if ((m.mask & kMobjZDefault) == 0u) {
        m.z = r.i32();
    }
    if ((m.mask & kMobjAngleCompass) != 0u) {
        m.angle = static_cast<std::uint32_t>(r.u8()) * ANG45;
    } else {
        m.angle = r.u32();
    }
    if ((m.mask & kMobjMomZero) == 0u) {
        m.momx = r.i32();
        m.momy = r.i32();
        m.momz = r.i32();
    }
    if ((m.mask & kMobjFloorCeilDefault) == 0u) {
        m.floorz = r.i32();
        m.ceilingz = r.i32();
    }
    if ((m.mask & kMobjHealthDefault) == 0u) {
        m.health = r.i32();
    }
    if ((m.mask & kMobjFlagsDefault) == 0u) {
        m.flags = r.u32();
    }
    if ((m.mask & kMobjRefsNone) == 0u) {
        m.target = r.u16();
        m.tracer = r.u16();
    }
    if ((m.mask & kMobjAiDefault) == 0u) {
        m.reactiontime = r.i16();
        m.movedir = r.i16();
        m.movecount = r.i16();
        m.threshold = r.i16();
    }
    return !r.bad;
}

bool readMover(Reader& r, MoverData& m) {
    m.sector = 0u;
    m.type = 0;
    m.direction = 0;
    m.status = 0;
    m.oldStatus = 0;
    m.crush = 0;
    m.tag = 0;
    m.oldDirection = 0;
    m.newspecial = 0;
    m.texture = 0;
    m.speed = 0;
    m.topHeight = 0;
    m.bottomHeight = 0;
    m.destHeight = 0;
    m.low = 0;
    m.high = 0;
    m.wait = 0;
    m.count = 0;
    m.topWait = 0;
    m.topCountdown = 0;
    m.maxLight = 0;
    m.minLight = 0;
    m.maxTime = 0;
    m.minTime = 0;
    m.darkTime = 0;
    m.brightTime = 0;

    switch (m.kind) {
        case kKindDoor:
            m.sector = r.u16();
            m.type = r.i16();
            m.direction = r.i16();
            m.topHeight = r.i32();
            m.speed = r.i32();
            m.topWait = r.i32();
            m.topCountdown = r.i32();
            break;
        case kKindCeiling:
            m.sector = r.u16();
            m.type = r.i16();
            m.direction = r.i16();
            m.crush = r.i16();
            m.tag = r.i16();
            m.oldDirection = r.i16();
            m.bottomHeight = r.i32();
            m.topHeight = r.i32();
            m.speed = r.i32();
            break;
        case kKindFloor:
            m.sector = r.u16();
            m.type = r.i16();
            m.direction = r.i16();
            m.crush = r.i16();
            m.newspecial = r.i16();
            m.texture = r.u64();
            m.destHeight = r.i32();
            m.speed = r.i32();
            break;
        case kKindPlat:
            m.sector = r.u16();
            m.type = r.i16();
            m.status = r.i16();
            m.oldStatus = r.i16();
            m.crush = r.i16();
            m.tag = r.i16();
            m.speed = r.i32();
            m.low = r.i32();
            m.high = r.i32();
            m.wait = r.i32();
            m.count = r.i32();
            break;
        case kKindLight:
            m.sector = r.u16();
            m.type = r.i16();
            m.direction = r.i16();
            m.count = r.i32();
            m.maxLight = r.i32();
            m.minLight = r.i32();
            m.maxTime = r.i32();
            m.minTime = r.i32();
            m.darkTime = r.i32();
            m.brightTime = r.i32();
            break;
        default:
            return false;
    }
    return !r.bad;
}

} /* namespace */

/* --- Public: header ------------------------------------------------------- */

SaveStatus saveReadHeader(const std::uint8_t* in, std::size_t size,
                          SaveHeader& out) {
    if (in == nullptr || size < kHeaderSize) {
        return SaveStatus::Truncated;
    }
    Reader r{in, size};

    if (r.u32() != kMagic) {
        return SaveStatus::BadMagic;
    }
    const std::uint16_t version = r.u16();
    const std::uint16_t schema = r.u16();

    if (version != kVersion || schema != kSchema) {
        return SaveStatus::BadVersion;
    }
    out.level = r.u8();
    out.skill = r.u8();
    (void)r.u8();
    (void)r.u8();
    out.leveltime = r.i32();
    out.kills = r.i32();
    (void)r.u32(); /* fingerprint */
    if (!r.need(16u)) {
        return SaveStatus::Truncated;
    }
    std::memcpy(out.label, in + r.at, 16u);
    out.label[15] = '\0';
    r.at += 16u;
    const std::uint32_t bodySize = r.u32();
    (void)r.u32(); /* checksum */

    if (r.bad) {
        return SaveStatus::Truncated;
    }
    if (out.level < 1u || out.level > 9u ||
        out.skill > static_cast<std::uint8_t>(P_SKILL_NIGHTMARE)) {
        return SaveStatus::BadField;
    }
    if (bodySize > size - kHeaderSize) {
        return SaveStatus::Truncated;
    }
    return SaveStatus::Ok;
}

/* --- Public: write -------------------------------------------------------- */

SaveStatus saveWrite(const p_level_t& level, const p_player_t& player,
                     std::uint8_t levelNumber, std::uint8_t* out,
                     std::size_t capacity, std::size_t& written) {
    if (levelNumber < 1u || levelNumber > 9u) {
        return SaveStatus::WrongLevel;
    }
    if (out == nullptr || !level.open || level.map == nullptr ||
        level.sectors == nullptr) {
        return SaveStatus::BadField;
    }
    const std::uint8_t skill = static_cast<std::uint8_t>(level.rules.skill);

    if (skill > static_cast<std::uint8_t>(P_SKILL_NIGHTMARE)) {
        return SaveStatus::BadField;
    }
    std::size_t bodyCap = capacity < kSavePayloadMax ? capacity : kSavePayloadMax;

    if (bodyCap <= kHeaderSize) {
        return SaveStatus::TooLarge;
    }
    bodyCap -= kHeaderSize;

    Writer w{out, bodyCap};

    writeBody(w, level, player);
    if (w.overflow) {
        return SaveStatus::TooLarge;
    }
    const std::size_t bodySize = w.at;

    if (bodySize + kHeaderSize > kSavePayloadMax ||
        bodySize + kHeaderSize > capacity) {
        return SaveStatus::TooLarge;
    }
    /* The body was written at offset 0 to keep the writer simple; move it up
     * behind the header, which is written last because it carries the body's
     * length and checksum. */
    std::memmove(out + kHeaderSize, out, bodySize);
    const std::uint32_t checksum =
        fnvBytes(kFnvBasis, out + kHeaderSize, bodySize);

    char label[16];
    makeLabel(label, levelNumber, skill, level.leveltime);
    writeHeader(out, levelNumber, skill, level.leveltime, player.killcount,
                mapFingerprint(level.map), label,
                static_cast<std::uint32_t>(bodySize), checksum);
    written = kHeaderSize + bodySize;
    return SaveStatus::Ok;
}

/* --- Public: apply -------------------------------------------------------- */

namespace {

/*
 * Frees every thinker the fresh load made.
 *
 * P_RemoveThinker() only marks; the walk is what unlinks and releases. Marking
 * the whole list and then running one walk is therefore the one safe way to
 * empty it through the public API -- and it uses p_level_release_thinker(), so
 * the arena's accounting is exactly what a level change would leave.
 */
void clearThinkers(p_level_t& level) {
    const p_thinker_t* node = p_thinker_first(&level.thinkers);

    while (node != nullptr) {
        const p_thinker_t* next = p_thinker_next(node);
        (void)p_thinker_remove(&level.thinkers,
                               const_cast<p_thinker_t*>(node));
        node = next;
    }
    (void)p_run_thinkers(&level.thinkers, p_level_release_thinker,
                         &level.context, nullptr);
    level.player = nullptr;
    for (std::uint16_t i = 0u; i < level.sectorCount; ++i) {
        level.sectors[i].specialdata = nullptr;
    }
}

bool sectorFromMover(const MoverData& mover, std::uint16_t sectorCount) {
    return mover.sector < sectorCount;
}

bool validMover(const MoverData& m) {
    switch (m.kind) {
        case kKindDoor:
            return m.type >= 0 && m.type <= 7 && m.direction >= -1 &&
                   m.direction <= 2;
        case kKindCeiling:
            return m.type >= 0 && m.type <= 5;
        case kKindFloor:
            return m.type >= 0 && m.type <= 12;
        case kKindPlat:
            return m.type >= 0 && m.type <= 4 && m.status >= 0 && m.status <= 3 &&
                   m.oldStatus >= 0 && m.oldStatus <= 3;
        case kKindLight:
            return m.type >= 0 && m.type <= 3;
        default:
            return false;
    }
}

SaveStatus createMobj(p_level_t& level, const MobjData& d) {
    if (d.type >= NUM_MOBJ_TYPES || d.state >= NUM_STATES) {
        return SaveStatus::BadField;
    }
    p_mobj_error_t error = P_MOBJ_OK;
    r_mobj_t* mobj = p_spawn_mobj(&level.context, d.x, d.y, ONFLOORZ,
                                  static_cast<mobjtype_t>(d.type), &error);

    if (mobj == nullptr) {
        return SaveStatus::ApplyFailed;
    }
    if ((d.mask & kMobjFloorCeilDefault) == 0u) {
        mobj->floorz = d.floorz;
        mobj->ceilingz = d.ceilingz;
    }
    if ((d.mask & kMobjZDefault) != 0u) {
        mobj->z = mobj->floorz;
    } else {
        mobj->z = d.z;
    }
    mobj->angle = d.angle;
    if ((d.mask & kMobjMomZero) == 0u) {
        mobj->momx = d.momx;
        mobj->momy = d.momy;
        mobj->momz = d.momz;
    }
    mobj->state = static_cast<std::int16_t>(d.state);
    mobj->tics = d.tics;
    mobj->sprite = states[d.state].sprite;
    mobj->frame = states[d.state].frame;
    if ((d.mask & kMobjHealthDefault) == 0u) {
        mobj->health = d.health;
    }
    if ((d.mask & kMobjFlagsDefault) == 0u) {
        mobj->flags = d.flags;
    }
    mobj->lastlook = static_cast<std::int16_t>(d.lastlook);
    if ((d.mask & kMobjAiDefault) == 0u) {
        mobj->reactiontime = d.reactiontime;
        mobj->movedir = d.movedir;
        mobj->movecount = d.movecount;
        mobj->threshold = d.threshold;
    }
    /* p_spawn_player() is the only writer of this flag, and the only mobj type
     * it can spawn is MT_PLAYER, so the type is the whole identity. */
    mobj->player = d.type == MT_PLAYER ? 1u : 0u;
    if (d.removed) {
        (void)p_thinker_remove(&level.thinkers, &mobj->thinker);
    }
    return SaveStatus::Ok;
}

SaveStatus createMover(p_level_t& level, const MoverData& d) {
    if (!sectorFromMover(d, level.sectorCount) || !validMover(d)) {
        return SaveStatus::BadField;
    }
    p_thinker_fn fn = nullptr;
    p_thinker_kind_t kind = P_THINKER_LIGHTFLASH;
    std::size_t size = 0u;

    switch (d.kind) {
        case kKindDoor:
            fn = p_door_thinker;
            kind = P_THINKER_DOOR;
            size = sizeof(p_door_thinker_t);
            break;
        case kKindCeiling:
            fn = p_ceiling_thinker;
            kind = P_THINKER_CEILING;
            size = sizeof(p_ceiling_thinker_t);
            break;
        case kKindFloor:
            fn = p_floor_thinker;
            kind = P_THINKER_FLOOR;
            size = sizeof(p_floor_thinker_t);
            break;
        case kKindPlat:
            fn = p_plat_thinker;
            kind = P_THINKER_PLAT;
            size = sizeof(p_plat_thinker_t);
            break;
        case kKindLight:
            fn = p_light_thinker;
            kind = P_THINKER_LIGHTFLASH;
            size = sizeof(p_light_thinker_t);
            break;
        default:
            return SaveStatus::BadField;
    }
    void* raw = p_spec_alloc_thinker(&level.context, size, fn, kind);

    if (raw == nullptr) {
        return SaveStatus::ApplyFailed;
    }
    switch (d.kind) {
        case kKindDoor: {
            p_door_thinker_t* t = static_cast<p_door_thinker_t*>(raw);
            t->sector = d.sector;
            t->type = d.type;
            t->direction = d.direction;
            t->topHeight = d.topHeight;
            t->speed = d.speed;
            t->topWait = d.topWait;
            t->topCountdown = d.topCountdown;
            break;
        }
        case kKindCeiling: {
            p_ceiling_thinker_t* t = static_cast<p_ceiling_thinker_t*>(raw);
            t->sector = d.sector;
            t->type = d.type;
            t->direction = d.direction;
            t->crush = d.crush;
            t->tag = d.tag;
            t->oldDirection = d.oldDirection;
            t->bottomHeight = d.bottomHeight;
            t->topHeight = d.topHeight;
            t->speed = d.speed;
            break;
        }
        case kKindFloor: {
            p_floor_thinker_t* t = static_cast<p_floor_thinker_t*>(raw);
            t->sector = d.sector;
            t->type = d.type;
            t->direction = d.direction;
            t->crush = d.crush;
            t->newspecial = d.newspecial;
            t->texture = d.texture;
            t->destHeight = d.destHeight;
            t->speed = d.speed;
            break;
        }
        case kKindPlat: {
            p_plat_thinker_t* t = static_cast<p_plat_thinker_t*>(raw);
            t->sector = d.sector;
            t->type = d.type;
            t->status = d.status;
            t->oldStatus = d.oldStatus;
            t->crush = d.crush;
            t->tag = d.tag;
            t->speed = d.speed;
            t->low = d.low;
            t->high = d.high;
            t->wait = d.wait;
            t->count = d.count;
            break;
        }
        case kKindLight: {
            p_light_thinker_t* t = static_cast<p_light_thinker_t*>(raw);
            t->sector = d.sector;
            t->type = d.type;
            t->direction = d.direction;
            t->count = d.count;
            t->maxLight = d.maxLight;
            t->minLight = d.minLight;
            t->maxTime = d.maxTime;
            t->minTime = d.minTime;
            t->darkTime = d.darkTime;
            t->brightTime = d.brightTime;
            break;
        }
        default:
            break;
    }
    if (d.inert) {
        reinterpret_cast<p_thinker_t*>(raw)->function = nullptr;
    }
    level.sectors[d.sector].specialdata = reinterpret_cast<p_thinker_t*>(raw);
    if (d.removed) {
        (void)p_thinker_remove(&level.thinkers,
                              reinterpret_cast<p_thinker_t*>(raw));
    }
    return SaveStatus::Ok;
}

} /* namespace */

SaveStatus saveApply(const std::uint8_t* in, std::size_t size,
                     std::uint8_t levelNumber, p_level_t& level,
                     p_player_t& player) {
    if (in == nullptr || size < kHeaderSize) {
        return SaveStatus::Truncated;
    }
    {   /* Header validation: every rejection here leaves the world untouched. */
        Reader h{in, size};

        if (h.u32() != kMagic) {
            return SaveStatus::BadMagic;
        }
        const std::uint16_t version = h.u16();
        const std::uint16_t schema = h.u16();

        if (version != kVersion || schema != kSchema) {
            return SaveStatus::BadVersion;
        }
        const std::uint8_t savedLevel = h.u8();
        const std::uint8_t savedSkill = h.u8();
        (void)h.u8();
        (void)h.u8();
        const std::int32_t savedLeveltime = h.i32();
        (void)h.i32(); /* kills: metadata */
        const std::uint32_t savedFingerprint = h.u32();
        h.at += 16u; /* label: metadata */
        const std::uint32_t bodySize = h.u32();
        const std::uint32_t checksum = h.u32();

        if (h.bad) {
            return SaveStatus::Truncated;
        }
        if (savedLevel < 1u || savedLevel > 9u || savedLevel != levelNumber) {
            return SaveStatus::WrongLevel;
        }
        if (savedSkill != static_cast<std::uint8_t>(level.rules.skill)) {
            return SaveStatus::WrongSkill;
        }
        if (!level.open || level.map == nullptr || level.sectors == nullptr) {
            return SaveStatus::BadField;
        }
        if (savedFingerprint != mapFingerprint(level.map)) {
            return SaveStatus::WrongLevel;
        }
        if (bodySize != size - kHeaderSize) {
            return SaveStatus::Truncated;
        }
        if (fnvBytes(kFnvBasis, in + kHeaderSize, bodySize) != checksum) {
            return SaveStatus::BadField;
        }
        level.leveltime = savedLeveltime;
    }

    Reader r{in + kHeaderSize, size - kHeaderSize};
    const std::uint16_t mobjCount = r.u16();
    const bool playerPresent = r.u8() != 0u;
    PlayerData savedPlayer{};
    bool hasPlayer = false;

    if (playerPresent) {
        if (!readPlayer(r, savedPlayer)) {
            return SaveStatus::Truncated;
        }
        if ((savedPlayer.mo != kNoRef && savedPlayer.mo >= mobjCount) ||
            (savedPlayer.attacker != kNoRef &&
             savedPlayer.attacker >= mobjCount) ||
            savedPlayer.playerstate > P_PST_REBORN ||
            savedPlayer.readyweapon >= P_NUMWEAPONS ||
            savedPlayer.pendingweapon > P_WP_NOCHANGE ||
            savedPlayer.pspriteState[0] < 0 ||
            savedPlayer.pspriteState[0] >= NUM_STATES ||
            savedPlayer.pspriteState[1] < 0 ||
            savedPlayer.pspriteState[1] >= NUM_STATES) {
            return SaveStatus::BadField;
        }
        hasPlayer = true;
    }

    const std::int32_t totalKills = r.i32();
    const std::int32_t totalItems = r.i32();
    const std::int32_t totalSecrets = r.i32();
    const std::uint8_t exitRequest = r.u8();
    const std::int32_t animTime = r.i32();
    const std::uint32_t randomIndex = r.u32();

    if (exitRequest > P_EXIT_SECRET) {
        return SaveStatus::BadField;
    }

    /* Sectors. */
    const std::uint16_t sectorCount = r.u16();
    for (std::uint16_t i = 0u; i < sectorCount; ++i) {
        const std::uint16_t index = r.u16();
        const std::uint8_t mask = r.u8();
        std::int32_t floorHeight = 0;
        std::int32_t ceilingHeight = 0;
        std::uint64_t floorPic = 0u;
        std::uint64_t ceilingPic = 0u;
        std::int16_t light = 0;
        std::int16_t special = 0;
        std::int16_t tag = 0;

        if (index >= level.sectorCount || (mask & ~(kSectorFloorHeight |
                                                   kSectorCeilingHeight |
                                                   kSectorFloorPic |
                                                   kSectorCeilingPic |
                                                   kSectorLight |
                                                   kSectorSpecial |
                                                   kSectorTag)) != 0u) {
            return SaveStatus::BadField;
        }
        if ((mask & kSectorFloorHeight) != 0u) floorHeight = r.i32();
        if ((mask & kSectorCeilingHeight) != 0u) ceilingHeight = r.i32();
        if ((mask & kSectorFloorPic) != 0u) floorPic = r.u64();
        if ((mask & kSectorCeilingPic) != 0u) ceilingPic = r.u64();
        if ((mask & kSectorLight) != 0u) light = r.i16();
        if ((mask & kSectorSpecial) != 0u) special = r.i16();
        if ((mask & kSectorTag) != 0u) tag = r.i16();
        if (r.bad) {
            return SaveStatus::Truncated;
        }
        p_sector_t& s = level.sectors[index];

        if ((mask & kSectorFloorHeight) != 0u) s.floorHeight = floorHeight;
        if ((mask & kSectorCeilingHeight) != 0u) s.ceilingHeight = ceilingHeight;
        if ((mask & kSectorFloorPic) != 0u) s.floorPic = floorPic;
        if ((mask & kSectorCeilingPic) != 0u) s.ceilingPic = ceilingPic;
        if ((mask & kSectorLight) != 0u) s.lightLevel = light;
        if ((mask & kSectorSpecial) != 0u) s.special = special;
        if ((mask & kSectorTag) != 0u) s.tag = tag;
    }

    /* Linedefs. */
    const std::uint16_t lineCount = r.u16();
    for (std::uint16_t i = 0u; i < lineCount; ++i) {
        const std::uint16_t index = r.u16();
        const std::int16_t value = r.i16();

        if (index >= level.lineCount) {
            return SaveStatus::BadField;
        }
        level.lineSpecial[index] = value;
    }

    /* Thinkers. */
    const std::size_t thinkerOffset = r.at;
    const std::uint16_t thinkerCount = r.u16();
    std::uint16_t seenMobjs = 0u;

    if (!r.bad) {
        clearThinkers(level);
    }
    for (std::uint16_t i = 0u; i < thinkerCount; ++i) {
        const std::uint8_t rawKind = r.u8();
        const std::uint8_t kind = rawKind & kKindMask;

        if (kind == kKindMobj) {
            MobjData d{};
            d.removed = (rawKind & kFlagRemoved) != 0u;
            if (!readMobj(r, d)) {
                return SaveStatus::Truncated;
            }
            const SaveStatus created = createMobj(level, d);
            if (created != SaveStatus::Ok) {
                return created;
            }
            ++seenMobjs;
        } else {
            MoverData d{};
            d.kind = kind;
            d.inert = (rawKind & kFlagInert) != 0u;
            d.removed = (rawKind & kFlagRemoved) != 0u;
            if (!readMover(r, d)) {
                return SaveStatus::BadField;
            }
            const SaveStatus created = createMover(level, d);
            if (created != SaveStatus::Ok) {
                return created;
            }
        }
    }
    if (seenMobjs != mobjCount) {
        return SaveStatus::BadField;
    }

    /* Sound targets: the observable half of the flood state, by mobj index. */
    const std::uint16_t soundCount = r.u16();
    if (level.context.sectorSound == nullptr) {
        return SaveStatus::BadField;
    }
    for (std::uint16_t i = 0u; i < soundCount; ++i) {
        const std::uint16_t sector = r.u16();
        const std::uint16_t target = r.u16();

        if (sector >= level.context.sectorSoundCount || target >= mobjCount) {
            return SaveStatus::BadField;
        }
        level.context.sectorSound[sector].soundTarget = mobjAt(level, target);
        level.context.sectorSound[sector].soundTraversed = 0;
        level.context.sectorSound[sector].validCount = 0u;
    }
    /* The epoch is not state: the next P_NoiseAlert() starts a fresh flood and
     * visits every sector whose validCount is not the new epoch. Starting at 0
     * with every entry at 0 is exactly a fresh flood's precondition. */
    level.context.soundValidCount = 0u;

    /* Scroll offsets, matched by index against the fresh scroll table. */
    const std::uint16_t scrollCount = r.u16();
    if (scrollCount != level.scrollCount ||
        (scrollCount != 0u && level.scrolls == nullptr)) {
        return SaveStatus::BadField;
    }
    for (std::uint16_t i = 0u; i < scrollCount; ++i) {
        level.scrolls[i].dx = r.i32();
    }
    if (r.bad || r.at != r.size) {
        return SaveStatus::Truncated;
    }

    /* Mobjs' references, now that every mobj exists. Re-reading the thinker
     * section costs one more parse and avoids a pointer table on the stack,
     * which the board cannot spare. */
    {
        Reader refs = r;
        (void)refs.seek(thinkerOffset);
        (void)refs.u16(); /* thinkerCount */
        const r_mobj_t* walker = p_level_first_mobj(&level);

        for (std::uint16_t i = 0u; i < thinkerCount; ++i) {
            const std::uint8_t rawKind = refs.u8();
            const std::uint8_t kind = rawKind & kKindMask;

            if (kind == kKindMobj) {
                MobjData d{};
                d.removed = (rawKind & kFlagRemoved) != 0u;
                if (!readMobj(refs, d) || walker == nullptr) {
                    return SaveStatus::BadField;
                }
                r_mobj_t* mobj = const_cast<r_mobj_t*>(walker);
                walker = p_level_next_mobj(walker);
                mobj->target = d.target == kNoRef ? nullptr : mobjAt(level, d.target);
                mobj->tracer = d.tracer == kNoRef ? nullptr : mobjAt(level, d.tracer);
            } else {
                MoverData d{};
                d.kind = kind;
                if (!readMover(refs, d)) {
                    return SaveStatus::BadField;
                }
            }
        }
    }

    /* The player record, last: its two references need every mobj. */
    if (hasPlayer) {
        p_player_t& p = player;
        p.mo = savedPlayer.mo == kNoRef ? nullptr : mobjAt(level, savedPlayer.mo);
        p.playerstate = static_cast<p_playerstate_t>(savedPlayer.playerstate);
        p.cmd.forwardmove = savedPlayer.forwardmove;
        p.cmd.sidemove = savedPlayer.sidemove;
        p.cmd.angleturn = savedPlayer.angleturn;
        p.cmd.buttons = savedPlayer.buttons;
        p.viewz = savedPlayer.viewz;
        p.viewheight = savedPlayer.viewheight;
        p.deltaviewheight = savedPlayer.deltaviewheight;
        p.bob = savedPlayer.bob;
        p.health = savedPlayer.health;
        p.armorpoints = savedPlayer.armorpoints;
        p.armortype = savedPlayer.armortype;
        for (int i = 0; i < P_NUMAMMO; ++i) {
            p.ammo[i] = savedPlayer.ammo[i];
            p.maxammo[i] = savedPlayer.maxammo[i];
        }
        for (int i = 0; i < P_NUMPOWERS; ++i) {
            p.powers[i] = savedPlayer.powers[i];
        }
        p.pendingweapon = savedPlayer.pendingweapon;
        p.readyweapon = savedPlayer.readyweapon;
        for (int i = 0; i < P_NUMPSPRITES; ++i) {
            p.psprites[i].state = savedPlayer.pspriteState[i];
            p.psprites[i].tics = savedPlayer.pspriteTics[i];
            p.psprites[i].sx = savedPlayer.pspriteSx[i];
            p.psprites[i].sy = savedPlayer.pspriteSy[i];
        }
        for (int i = 0; i < P_NUMWEAPONS; ++i) {
            p.weaponowned[i] = savedPlayer.weaponowned[i];
        }
        for (int i = 0; i < P_NUMCARDS; ++i) {
            p.cards[i] = savedPlayer.cards[i];
        }
        p.backpack = savedPlayer.backpack;
        p.attackdown = savedPlayer.attackdown;
        p.usedown = savedPlayer.usedown;
        p.refire = savedPlayer.refire;
        p.killcount = savedPlayer.killcount;
        p.itemcount = savedPlayer.itemcount;
        p.secretcount = savedPlayer.secretcount;
        p.damagecount = savedPlayer.damagecount;
        p.bonuscount = savedPlayer.bonuscount;
        p.extralight = savedPlayer.extralight;
        p.fixedcolormap = savedPlayer.fixedcolormap;
        p.attacker = savedPlayer.attacker == kNoRef
                         ? nullptr
                         : mobjAt(level, savedPlayer.attacker);
        p.didsecret = savedPlayer.didsecret;
        p.onground = savedPlayer.onground;
        p.useTriggered = savedPlayer.useTriggered;
        p.pspritesStarted = savedPlayer.pspritesStarted;
        level.player = p.mo;
    } else {
        level.player = nullptr;
    }

    level.totalKills = totalKills;
    level.totalItems = totalItems;
    level.totalSecrets = totalSecrets;
    level.exitRequest = exitRequest;
    level.animTime = animTime;
    level.random.index = randomIndex;
    return SaveStatus::Ok;
}

} /* namespace doomc */
} /* namespace doom */
