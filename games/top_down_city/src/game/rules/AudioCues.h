#pragma once
#include <cstdint>

#include "game/rules/Wanted.h"   // kMaxStars -- engine-free, same directory

/**
 * What the city sounds like, expressed as tables rather than habits.
 *
 * Music is car-bound, not wanted-level-driven: no music on foot unless wanted
 * (the siren, alone), no chase track, and the station a car plays is a
 * deterministic function of its paint. `musicPlanFor` is the whole model as
 * one total function returning a value, which is why "forgetting to stop the
 * siren" cannot happen here -- `siren` is recomputed from scratch every frame,
 * so there is no transition to get wrong. The siren rides a music voice rather
 * than a looping SFX event because `AudioEngine::playEvent` returns `void`,
 * leaving a looping SFX no handle a director could ever stop.
 *
 * Every one-shot cue shares one throttle shape, a per-cue cooldown aged down
 * every logic step; one mechanism rather than nine bespoke ones is what makes
 * `test_every_cue_has_a_nonzero_cooldown` possible to write at all.
 *
 * Engine-free like the rest of `game/rules/` and host-tested -- the only
 * guarantee this kind of table ever gets on a device with no console to print
 * a wrong sound to.
 */
namespace top_down_city::audio_cues {

/// A copy of `top_down_city::kLogicStepMs`, so a cooldown can be checked
/// against a weapon's fire rate without this header learning what a scene is.
/// static_asserted in CityConstants.h, which sees both.
constexpr std::uint16_t kLogicStepMs = 16;

/// Every one-shot the city can make. One row per sound, not per call site.
enum class Cue : std::uint8_t {
    Gunshot = 0,
    ShotgunBlast,
    DryFire,
    WeaponPickup,
    WantedUp,
    VehicleEnter,
    VehicleExit,
    Busted,
    DistrictChange,
    Footstep,
    VehicleCrash,
    Doorway,           ///< any building the player can walk into
    PlayerHit,
    MissionDelivered,
    MissionFailed,
    Roadkill,          ///< ADR-16
    PoliceGunshot,     ///< ADR-17
    // The ambient/traffic layer (ADR-15, ADR-18, ADR-19). All five are
    // Ambient and share Footstep's one-voice tier cap.
    TrafficPass,
    EngineRev,
    BrakeScreech,
    CarHorn,
    CrowdPanic,
    Count
};

/// How long this cue must wait before it may be heard again, in milliseconds.
/// Never zero -- a zero row would not throttle anything, since `cueAllowed(0)`
/// is always true.
std::uint16_t cooldownMsFor(Cue cue);

/// @return `true` only when `remainingMs == 0`.
bool cueAllowed(std::uint16_t remainingMs);

/// `remainingMs - dtMs`, saturating at zero -- a step whose `dtMs` outlives
/// the remaining cooldown must land exactly at zero, not wrap past it on an
/// unsigned type.
std::uint16_t ageCooldown(std::uint16_t remainingMs, unsigned long dtMs);

// --- The radio plan -----------------------------------------------------

/// Copies of two vehicle constants, so this module can decide what a car
/// sounds like without learning what a sprite sheet is. Both tied down by
/// static_asserts in CityConstants.h.
constexpr std::uint8_t kRadioColorCount = 7;   // == kVehicleColorCount
constexpr std::uint8_t kPoliceColor     = 6;   // == kPoliceCarColor

/// The four stations. `None` is not a station: it is what a police car and a
/// pavement have in common.
enum class RadioTrackId : std::uint8_t { None = 0, One, Two, Three, Four,
                                          Count };

/// What the music transport should be doing right now. A value, not a state
/// machine.
struct MusicPlan {
    RadioTrackId radio = RadioTrackId::None;
    bool         siren = false;
};

/// Which station this car's paint plays. `kPoliceColor` is a police car and
/// has no radio; colours at or past `kRadioColorCount` return `None` --
/// unreachable today, but silence is the safe answer to a colour nobody has
/// authored for.
RadioTrackId radioFor(std::uint8_t vehicleColor);

/**
 * @brief The whole music model, in one total function.
 * @param driving `driving_ != nullptr` in the scene.
 * @param vehicleColor The driven car's colour; ignored when `!driving`.
 * @param wanted A BOOLEAN, never the star count (ADR-7): putting `stars` in
 *        the plan would restart the player's song on every crime.
 * @param busted Forces `{None, false}`, outranking both other arguments.
 */
MusicPlan musicPlanFor(bool driving, std::uint8_t vehicleColor, bool wanted,
                       bool busted);

bool planChanged(MusicPlan prev, MusicPlan next);

// --- Footsteps and voice priority ---------------------------------------

/// ADR-12: footsteps are distance-locked, not time-locked. A footfall costs
/// the same ~28 px of ground at either gait because the two stride lengths
/// are in the inverse ratio of the two gait speeds -- tied down by a
/// static_assert in CityConstants.h. The clock counts logic steps of ACTUAL
/// MOVEMENT, so it must only be advanced by a caller that has confirmed the
/// sprite moved this step.
constexpr std::uint8_t kWalkStrideSteps = 28;   // 448 ms, ~2.2 Hz
constexpr std::uint8_t kRunStrideSteps  = 16;   // 256 ms, ~3.9 Hz

std::uint8_t footstepStrideSteps(bool running);

/// Is a footfall due this step? `sinceLast` counts logic steps of MOVEMENT,
/// and a standing player never takes a step regardless of it.
bool footstepDue(std::uint8_t sinceLast, bool moving, bool running);

/// One step of ageing for the footstep clock: 0 when `fired`, otherwise
/// `sinceLast + 1` saturating at 255 rather than wrapping -- a wrap to 0 would
/// read as "just took a step" to the very next check.
std::uint8_t ageFootstepClock(std::uint8_t sinceLast, bool fired);

/// A car that stopped dead from below this speed nudged a kerb; at or above
/// it, it hit something worth hearing. Bracketed by static_asserts in
/// CityConstants.h against the manoeuvring and top speeds.
constexpr std::int32_t kCrashAudibleSpeedSub = 256;   // 1 px/step

/// @param speedSubLost `VehicleActor::consumeCrash()`'s reading -- 0 when the
///        car has not stopped dead against anything since the last read.
bool crashIsAudible(std::int32_t speedSubLost);

/// The SFX subpool (`ApuCore::SFX_VOICE_COUNT` in the engine), copied here as
/// a number this module can reason about.
constexpr std::uint8_t kSfxVoiceCount = 4;

/// One pistol fire interval (14 steps * 16 ms). ADR-14: every ESSENTIAL cue
/// sets this, not only the two weapon ones, so no Ambient cue is admitted for
/// this long after any of them.
constexpr std::uint16_t kEssentialQuietMs = 224;

/// Three tiers. Only `Ambient` cues may ever be REFUSED rather than merely
/// throttled by their own cooldown.
enum class CuePriority : std::uint8_t { Essential = 0, Normal, Ambient };

CuePriority priorityOf(Cue cue);

/// How many of the four SFX voices this cue costs -- 2 for the two weapon
/// cues (a crack layered on a body), 1 for everything else.
std::uint8_t voiceCostFor(Cue cue);

/// @return `true` only once the quiet clock has aged back to zero.
bool ambientCueAllowed(std::uint16_t gunfireQuietRemainingMs);

// --- ADR-14's admission gate --------------------------------------------
//
// A director-side model of the SFX subpool as ONE step-load counter and TWO
// ageing clocks -- deliberately NOT a model of the four physical voice slots,
// because the engine reports nothing back about which slot a cue landed on or
// when it was stolen, so a slot model would drift silently the first time it
// guessed wrong.

/// How long the whole Ambient tier holds its one voice after it plays,
/// deliberately longer than any Ambient event's own duration. That is what
/// caps the tier at exactly one live voice by construction, not by counting.
constexpr std::uint16_t kAmbientHoldMs = 260;

/// The SFX subpool as this module sees it for one logic step.
struct SfxBudget {
    /// Voices demanded so far THIS step, zeroed every step by ageBudget. It
    /// cannot see a gunshot that started three steps ago -- that gap is what
    /// ambientQuietMs covers, which is why Normal is the only tier this field
    /// alone can refuse.
    std::uint8_t  frameVoiceLoad = 0;
    /// Set to kEssentialQuietMs by every admitted Essential cue. No Ambient
    /// cue is admitted while this reads non-zero.
    std::uint16_t ambientQuietMs = 0;
    /// Set to kAmbientHoldMs by every admitted Ambient cue. The tier's voice
    /// cap: because the hold outlives the longest Ambient event, two Ambient
    /// voices can never be live at once.
    std::uint16_t ambientBusyMs = 0;
};

/// Kept distinct so a test (or a future log) can tell "no voice for it" apart
/// from "the street is meant to be quiet right now".
enum class CueVerdict : std::uint8_t {
    Play = 0,
    RefusedQuiet,
    RefusedBusy,
    RefusedNoVoice,
};

/**
 * @brief May this cue play, against the budget as it stands before this
 *        step's cues are charged?
 * @return `Essential` is always `Play`: at 4/4 a fifth Essential demand is
 *         still admitted and the engine's own blind steal picks the loser, an
 *         accepted case rather than one this function tries to prevent.
 *         `Normal` is `RefusedNoVoice` once admitting it would pass
 *         `kSfxVoiceCount`. `Ambient` is `RefusedQuiet` while
 *         `ambientQuietMs > 0`, then `RefusedBusy` while `ambientBusyMs > 0`,
 *         then subject to the same voice test.
 */
CueVerdict admitCue(Cue cue, const SfxBudget& budget);

/// Charge an admitted cue: `voiceCostFor(cue)` onto `frameVoiceLoad`, and the
/// matching tier clock re-armed. The caller must have checked `admitCue`; this
/// does not re-check.
SfxBudget chargeCue(SfxBudget budget, Cue cue);

SfxBudget ageBudget(SfxBudget budget, unsigned long dtMs);

// --- The traffic layer (ADR-15, ADR-18, ADR-19) -------------------------

/// Earshot, against `threat::within`'s own squared-distance circle. Inside
/// the 240 px viewport on purpose: with a mono mix (spec R8.1) the only
/// honest claim is "you can hear what you can see".
constexpr int kTrafficAudiblePx = 96;

/// Below this the car is manoeuvring, not driving, and a screech would be a
/// lie. Exactly `kTrafficCruiseSpeedSub`, pinned by a static_assert in
/// CityConstants.h.
constexpr std::int32_t kBrakeAudibleSpeedSub = 256;   // 1 px/step = 62.5 px/s

/// A pedal EDGE, never a hold: a throttle held down for many steps fires
/// `Accelerated` once, on the step it first goes positive.
enum class PedalEvent : std::uint8_t { None = 0, Accelerated, Braked };

/**
 * @brief Did the driver's foot just do something worth a cue?
 * @param prevThrottle The car's throttle the LAST step the scene checked.
 * @return `Accelerated` only on the step the pedal goes from `<= 0` to `> 0`.
 *         `Braked` only on the step it goes from `>= 0` to `< 0`, AND ONLY
 *         while `speedSub >= kBrakeAudibleSpeedSub` -- braking and reversing
 *         share one button, and reversing out of a standstill must stay
 *         silent because nothing is being braked.
 *
 * ADR-19: computed by the SCENE from two getters, never inside
 * `VehicleActor::step()`. The director runs per frame and the car per fixed
 * 16 ms step, so a director-side speed diff across its own clock would miss
 * short taps and invent events between two frames it never saw.
 */
PedalEvent pedalEventFor(int throttle, int prevThrottle,
                         std::int32_t speedSub);

/// Logic steps a car must be blocked before it leans on the horn. 720 ms is
/// long enough that pausing for somebody crossing is not an insult, short
/// enough that the car is still on screen when it complains.
constexpr std::uint8_t kHornStallSteps = 45;

/// One logic step of ageing for a traffic slot's stall clock: 0 the moment
/// `go` is true, otherwise `stalled + 1` saturating at 255 -- a wrap to a
/// small value would make an already-honked, permanently wedged car honk
/// again.
std::uint8_t ageStallClock(std::uint8_t stalled, bool go);

/// Is a car stalled this many steps due to honk right now? `true` on EXACTLY
/// `stalledSteps == kHornStallSteps`, never `>=` -- a `>=` test would honk
/// every step a car stayed wedged, sixty-two times a second, where the intent
/// is one honk per stall.
bool hornDue(std::uint8_t stalledSteps);

}  // namespace top_down_city::audio_cues
