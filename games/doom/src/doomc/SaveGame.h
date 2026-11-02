/*
 * The port's own save record for one level (D7.1, task T52).
 *
 * WHAT THIS IS, AND WHY IT IS NOT VANILLA'S .dsg
 *
 * linuxdoom-1.10's p_saveg.c archives the level as a memory image: a flat copy
 * of `player_t`, every `mobj_t` and every sector thinker, with pointers stored
 * as byte offsets that only mean anything to a 32-bit DOS build of the same
 * struct layout. That record cannot be loaded here for three reasons:
 *
 *   - this port's thinkers live in a caller-owned arena at addresses that differ
 *     between two loads, so a stored pointer is not an index;
 *   - `doom_map` is a zero-copy view over (on the board) a mapped flash page, so
 *     there is no writable `sector_t sectors[]` in the map to copy;
 *   - the engine's slot store (S18) hands this layer a byte buffer, so the
 *     record has to be self-describing and index-based.
 *
 * So the record is the honest translation of the *idea*: the player record
 * without pointers, the level's counters, the mutable map state as deltas
 * against a fresh p_level_init(), and every mobj with its state as a table index
 * and its references as indices into the mobj list.
 *
 * A SAVE IS RESTORED INTO A WORLD BUILT THE SAME WAY
 *
 * saveApply() assumes @p level was just produced by the same
 * `p_level_init()` + `p_spawn_specials()` sequence as the world that was saved,
 * for the same map and skill. It then frees every thinker that fresh load made
 * (through p_level_release_thinker(), so the arena's accounting stays the
 * arena's) and rebuilds them from the record. The fresh load is not redundant:
 * it is what gives the arena, the scroll table, the animation names, the
 * player-start records and the sector base array. Only the *mutable* part is
 * overwritten.
 *
 * WHAT IS STORED AS A DELTA, AND WHY
 *
 * A full sector dump would be 85 sectors x 26 B for E1M1 alone (2,210 B) and a
 * full mobj dump is ~90 B per thing (119 of them at nightmare on E1M1, ~10.7 KB);
 * neither fits the 3,072 B slot budget S18/L55 derives from the 32 KB `saves`
 * partition. The record therefore stores:
 *
 *   - sectors: only those whose floor/ceiling height, floor/ceiling picture,
 *     light level, special or tag differs from the immutable SECTORS record, as
 *     (index, field mask, values). A sector whose WAD special is nonzero is
 *     always written, because P_SpawnSpecials() rewrites `special` for most of
 *     them and the record must pin the result, not the WAD byte.
 *   - linedefs: only the one-shot `special` clears, as (index, value).
 *   - mobjs: fixed fields that are always live, plus a per-field "same as a
 *     fresh spawn" mask. A monster still in its spawn state, on the floor, with
 *     no momentum and its spawn health costs 16 B; one mid-chase costs ~64 B.
 *     The comparison baselines are exactly what p_spawn_mobj() and
 *     mobjinfo[]/states[] produce, so a restored mobj gets the same values
 *     whether the writer omitted them or wrote them.
 *   - thinkers: one ordered list, so insertion order (which decides the order
 *     RNG draws and collision walks happen in) survives the round trip. Each
 *     entry is a kind byte followed by that kind's record.
 *
 * FIELDS DELIBERATELY NOT STORED, AND WHY
 *
 *   p_player_t::message        a static HUD string, re-selected by the HUD; not
 *                              read by the simulation.
 *   p_player_t::useTriggers, usePresses, silentSounds, noiseAlerts
 *                              diagnostic counters only; nothing reads them to
 *                              decide a tic.
 *   p_level_t::stats, actions, specStats
 *                              load/observation counters; zero on a fresh load
 *                              and not inputs to any rule.
 *   p_level_t::animFrames, animNames, and p_sector_t::floorFrame/ceilingFrame
 *                              pure functions of `leveltime` and the flat names;
 *                              P_UpdateSpecials() rewrites them on the first tic
 *                              after a restore, which every caller runs before
 *                              it draws. `animTime` itself IS stored.
 *   p_level_t::context.sectorSound[i].validCount/soundTraversed and
 *   context.soundValidCount
 *                              the flood epoch is only an intra-flood guard. The
 *                              observable is `soundTarget`, which is stored by
 *                              mobj index; the epoch is reset to 0 on restore so
 *                              the next P_NoiseAlert() visits every sector.
 *   p_level_t::playerStarts, playerStartSeen, deathmatchStarts,
 *   deathmatchStartCount, rules, scrolls[].line/side
 *                              reproduced by the fresh p_level_init() /
 *                              p_spawn_specials() the restore is applied to.
 *   p_level_t::scrolls[].dy    structurally zero: this upstream's case 48 adds
 *                              P_SCROLL_STEP to the front side's horizontal
 *                              offset and has no vertical term, so only `dx` is
 *                              archived. A future vertical scroller is the one
 *                              change that has to grow this record.
 *   r_mobj_t::spawnpoint       written by P_SpawnMapThing() and read by nothing
 *                              in this port (there is no item/nightmare respawn
 *                              path yet). A mobj therefore restores with a zero
 *                              spawnpoint; the moment a respawn path reads it,
 *                              this record must grow and this sentence must go.
 *   r_mobj_t::subsector, sector
 *                              recomputed from x/y by the same BSP lookup
 *                              P_SpawnMobj() uses.
 *   r_mobj_t::radius, height   copied from mobjinfo[type], never modified.
 *   r_mobj_t::sprite, frame    exactly states[state].sprite/frame; the only two
 *                              writers (P_SpawnMobj, P_SetMobjState) copy them
 *                              from the state table.
 *
 * WHAT THE HEADER IS FOR
 *
 * The slot menu (T53) shows eight rows without decoding a body: the level, the
 * skill, the clock, the kill count and a 15-character label. saveReadHeader()
 * reads only those bytes. Correctness does not rest on them: the body carries a
 * map fingerprint (a hash of THINGS plus the line/sector counts) and a
 * 32-bit checksum over the body, so a record from another map, or a record with
 * one flipped byte, is rejected rather than loaded. The engine's RecordCodec
 * owns a CRC-32 envelope in T53; this checksum is the save layer's own guard so
 * the host gate can prove the rejection now.
 *
 * This file is engine-free and APU-free: it includes lib/doom headers and
 * nothing from PixelRoot32. There is no allocation, no floating point and no
 * stdio; every buffer belongs to the caller.
 */
#ifndef DOOM_DOOMC_SAVEGAME_H
#define DOOM_DOOMC_SAVEGAME_H

#include <cstddef>
#include <cstdint>

#include "m_random.h"
#include "p_mobj.h"
#include "p_setup.h"
#include "p_spec.h"
#include "p_user.h"

namespace doom {
namespace doomc {

/*
 * One record's payload budget.
 *
 * S18/L55: the `saves` partition is 32 KB of NVS, eight slots, and an NVS page
 * is 4,096 B with 32 B of page/entry overhead. A slot can therefore carry at
 * most one page's worth; 3,072 B leaves room for the engine's 16-byte
 * RecordCodec envelope and for NVS's own chunk headers with margin. The value is
 * a contract with T53, so it is stated here once and the budget test measures
 * against it.
 */
constexpr std::size_t kSavePayloadMax = 3072;

/* Status codes. Every rejection names its own cause, because a menu that says
 * "could not load" for a wrong-skill record is a menu nobody can debug. */
enum class SaveStatus : std::uint8_t {
    Ok = 0,
    TooLarge,   /* the record does not fit the caller's buffer or kSavePayloadMax */
    Truncated,  /* the buffer is shorter than the record it claims to be */
    BadMagic,   /* not a save record at all */
    BadVersion, /* an unknown record version or schema */
    BadField,   /* a value out of range, an index past the end, or a bad checksum */
    WrongLevel, /* the record is for a different map than the level passed in */
    WrongSkill, /* the record is for a different skill than the level was loaded with */
    ApplyFailed /* the arena refused a thinker the record needs */
};

/* What the slot menu can show without decoding the body. */
struct SaveHeader {
    std::uint8_t level;      /* 1..9 (E1M1..E1M9) */
    std::uint8_t skill;      /* 0..4, p_skill_t */
    std::int32_t leveltime;  /* tics since the level loaded */
    std::int32_t kills;      /* the player's killcount */
    char label[16];          /* "E1M1 HURT 03:12", ASCII, zero-terminated */
};

/*
 * Writes one record for @p level and @p player into @p out.
 *
 * @p levelNumber is the map the world is (1..9). A p_level_t does not carry it --
 * the map view is a set of lumps and the WAD name is the caller's -- so the game
 * layer, which owns g_level_state_t, passes it. The skill is read from
 * `level.rules.skill`.
 *
 * The caller owns @p out. Nothing is written past @p capacity, and a record that
 * would exceed either @p capacity or kSavePayloadMax answers TooLarge with
 * @p written untouched. The bytes are a pure function of the level's state, so
 * two calls on the same world produce identical payloads.
 *
 * @param level       The open level; must have been built by p_level_init() and
 *                    p_spawn_specials().
 * @param player      The player record to archive (normally level.playerState).
 * @param levelNumber The map number, 1..9.
 * @param out         Destination buffer.
 * @param capacity    Bytes available at @p out.
 * @param written     Out: bytes written on Ok; untouched on failure.
 * @return Ok, TooLarge, BadField, or WrongLevel (levelNumber outside 1..9).
 */
SaveStatus saveWrite(const p_level_t& level, const p_player_t& player,
                     std::uint8_t levelNumber, std::uint8_t* out,
                     std::size_t capacity, std::size_t& written);

/**
 * Reads the fixed header without decoding the body.
 *
 * Validates the magic and the format version/schema, and that @p size can hold
 * the header and the body length the header claims. Every other field is copied
 * as-is; a header is metadata, and the body is validated by saveApply().
 *
 * @param in   The record bytes.
 * @param size Bytes available at @p in.
 * @param out  Filled on Ok.
 * @return Ok, Truncated, BadMagic or BadVersion.
 */
SaveStatus saveReadHeader(const std::uint8_t* in, std::size_t size,
                          SaveHeader& out);

/**
 * Restores @p level and @p player from a record.
 *
 * @p level must be an open level built by the same p_level_init() +
 * p_spawn_specials() sequence, for the same map and skill, that produced the
 * record. The header, the skill, the level number, the map fingerprint and the
 * body checksum are all validated before the first write to the world, so a
 * truncated record, a flipped byte, an unknown version, a different map and a
 * different skill each return their own status and leave the world untouched.
 * A body value that only fails while it is being consumed (an index past the
 * end, a state the table rejects, an arena refusal) is a BadField/ApplyFailed
 * and the level may be partially rebuilt; callers treat any non-Ok as "rebuild
 * the world from p_level_init()", because a half-applied save is not a world.
 *
 * @param in          The record bytes.
 * @param size        Bytes available at @p in.
 * @param levelNumber The map the caller believes @p level is, 1..9.
 * @param level       The freshly built world to overwrite.
 * @param player      The player record to overwrite (normally level.playerState).
 * @return Ok or one of the SaveStatus codes.
 */
SaveStatus saveApply(const std::uint8_t* in, std::size_t size,
                     std::uint8_t levelNumber, p_level_t& level,
                     p_player_t& player);

} /* namespace doomc */
} /* namespace doom */

#endif /* DOOM_DOOMC_SAVEGAME_H */
