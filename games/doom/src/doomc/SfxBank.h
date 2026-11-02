#pragma once

/*
 * The SFX bank: one POD description per sfxenum_t id, and the 8 -> 4 voice
 * reduction rule.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT
 *
 * This is the seam between DOOM's sound *identity* (`sounds.h`'s ids and
 * names) and the APU's one-shot event. It is deliberately engine-free and
 * APU-free: no `pixelroot32` header is included here, so the bank can be read,
 * tested and diffed without a board, and the mapping into `AudioEvent` lives
 * in DoomAudio.cpp alone.
 *
 * THE NUMBERS IN T45 ARE PLACEHOLDERS, AND THEY SAY SO
 *
 * T45's job is the layer, not the timbre: every id 1..NUMSFX-1 resolves to a
 * cue with the right shape and the right *character* (a weapon crack is a
 * pulse or a noise burst; a monster's sight is a low pulse; a door is a low
 * creak), and one per id so D6.1's "every shareware id resolves" is true
 * everywhere. T46 replaces the numbers with values derived from the WAD's DS*
 * lumps through tools/sfx_design.py and gates them with tools/sfx_check.py,
 * and it must be able to do that without touching this API: the fields below
 * are what the analysis produces.
 *
 * THE 8 -> 4 RULE IS A PURE FUNCTION
 *
 * DOOM keeps eight logical channels (s_sound.c's channel table); the APU has
 * four SFX voices (slots 4..7). `sfxVoiceSelect()` is the reduction on its
 * own, so the host test can prove the rule without an engine: an idle voice
 * first, then the least important sounding cue, and a refusal when every voice
 * holds a Reserved cue. A Reserved cue -- the player's pain, the player's
 * weapon, a boss -- is never a victim. That is S16's "under a scripted
 * fire-fight no high-value cue is stolen", expressed as code that cannot be
 * forgotten.
 */

#include <cstddef>
#include <cstdint>

namespace doom {
namespace doomc {

/// The wave families the APU's one-shot event offers. Spelled here so this
/// header never includes the APU's AudioTypes.h.
enum class SfxWave : std::uint8_t {
    Pulse = 0,
    Triangle,
    Noise,
    Sine,
    Saw,
};

/// The APU's SFX subpool size: slots 4..7 (ApuCore::SFX_VOICE_COUNT).
constexpr int kSfxVoices = 4;

/// The APU accepts at most four breakpoints in each table
/// (kMaxSfxDutySteps / kMaxSfxPitchPoints).
constexpr std::size_t kSfxMaxBreakpoints = 4;

/// One `SfxBreakpoint`: a time from the cue's start and the value held from
/// it. Times must be non-decreasing; T46's generator enforces what it emits
/// and the unit test enforces the bank.
struct SfxPoint {
    float timeSec;
    float value;
};

/**
 * @brief How important a cue is when the four voices are full.
 *
 * Ordered, and the order is the rule: `sfxVoiceSelect()` takes the lowest
 * class and refuses when the lowest is `Reserved`. `Idle` is not a property of
 * a cue; it is how a free voice is recorded in the selector's input.
 */
enum class SfxClass : std::uint8_t {
    Idle = 0,
    Normal = 1,
    High = 2,
    Reserved = 3,
};

/**
 * @brief One cue: everything DoomAudio needs to build an AudioEvent.
 *
 * `frequencyHz` is the start frequency; `sweepEndHz > 0` is a sweep to that
 * frequency over `durationSec`. For `Noise`, `frequencyHz` is the LFSR clock
 * and `noisePeriod` (0 = derive from the clock) overrides it. `duty` is the
 * pulse duty cycle. `attackSec`/`releaseSec` are the envelope times T46's
 * analysis produces; the Legacy profile applies its own envelope, so T45
 * carries them for the design gate and does not synthesise an ADSR from them.
 *
 * `pitch`/`dutyPoints` are copied into the APU event only when their count is
 * >= 2 (pitch) or >= 1 (duty); a pitch envelope supersedes `sweepEndHz`, which
 * is the APU's own precedence.
 */
struct SfxCue {
    SfxWave wave;
    float frequencyHz;
    float sweepEndHz;
    float durationSec;
    float volume;
    float duty;
    std::uint8_t noisePeriod;
    float attackSec;
    float releaseSec;
    SfxClass priority;
    SfxPoint pitch[kSfxMaxBreakpoints];
    std::uint8_t pitchCount;
    SfxPoint dutyPoints[kSfxMaxBreakpoints];
    std::uint8_t dutyCount;
};

/**
 * @brief The cue for an sfxenum_t id.
 *
 * @return NULL for id 0 (upstream's dummy row) and for anything outside
 *         1..NUMSFX-1. Callers treat NULL as a silent no-op (D6.1), and
 *         DoomAudio counts it as `unknown` rather than inventing a sound.
 */
const SfxCue* sfxBankCue(std::int32_t sfxId);

/// The number of ids the bank describes: NUMSFX - 1 (id 0 is the silent row).
std::size_t sfxBankSize();

/// The S_sfx[] name for an id, or "" when the id names no row. For the log.
const char* sfxBankName(std::int32_t sfxId);

/**
 * @brief The 8 -> 4 reduction: which voice the newcomer takes, or -1.
 *
 * @param slots The class of the cue on each of the four SFX voices; `Idle`
 *              for a free one.
 * @return An idle voice's index; else the least important sounding voice's
 *         index; else -1, meaning "hold the cue". `Reserved` is never
 *         returned: the player's pain, the player's weapon and a boss cue are
 *         not stolen while anything else sounds.
 */
std::int32_t sfxVoiceSelect(const SfxClass slots[kSfxVoices]);

}  // namespace doomc
}  // namespace doom
