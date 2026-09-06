#pragma once

#include <platforms/EngineConfig.h>

#if PIXELROOT32_ENABLE_AUDIO

#include <pixelroot32/apu/AudioTypes.h>

#include "game/rules/AudioCues.h"   // kAmbientHoldMs -- engine-free, host-tested

/**
 * @brief Every one-shot sound in the city, hand-authored and in one place.
 *
 * All original, `constexpr`, typed in by hand: there is no generator and
 * nothing under `tools/`, because a generator is only required for GENERATED
 * assets and there are none here (ADR-4).
 *
 * No `InstrumentPreset` pointers: every event carries its own envelope through
 * duration and sweep, because presets are shared engine globals and using one
 * would make a one-shot's character depend on a table this demo does not own.
 *
 * @warning Field order is easy to get wrong SILENTLY: `preset` is field 7,
 *          not the last, and `duration` comes before `volume`. Every event
 *          needing a field past `duty` is written positionally in full,
 *          spelling out `noisePeriod` (0) and `preset` (nullptr) rather than
 *          skipping them -- a skipped middle field shifts everything after
 *          it.
 * @note NOISE frequency is the LFSR clock, not a pitch, so a NOISE "sweep" is
 *       the tail's texture dropping rather than a note bending.
 */
namespace top_down_city::city_sfx {

using pixelroot32::audio::AudioEvent;
using pixelroot32::audio::SfxBreakpoint;
using pixelroot32::audio::SweepCurve;
using pixelroot32::audio::WaveType;

// --- Gunshot (pistol). Two layers: the crack, then the body underneath it.
constexpr AudioEvent kPistolCrack{
    WaveType::NOISE, 1800.0f, 0.07f, 0.55f, 0.0f, 0, nullptr, 400.0f, 0.07f};
constexpr AudioEvent kPistolBody{
    WaveType::PULSE, 220.0f, 0.05f, 0.35f, 0.50f, 0, nullptr, 70.0f, 0.05f};

// --- Shotgun. The same two layers, lower and longer: the weapon table
//     already says this gun is slower and heavier.
constexpr AudioEvent kShotgunCrack{
    WaveType::NOISE, 1200.0f, 0.14f, 0.70f, 0.0f, 0, nullptr, 180.0f, 0.14f};
constexpr AudioEvent kShotgunBody{
    WaveType::PULSE, 150.0f, 0.09f, 0.45f, 0.50f, 0, nullptr, 50.0f, 0.09f};

// --- Weapon pickup. A three-point rising figure. The breakpoint table MUST
//     be static: the voice keeps pointer + count, so a stack temporary would
//     dangle. Two or more points supersede the single-segment sweep.
constexpr SfxBreakpoint kPickupPitch[] = {
    {0.00f, 523.0f}, {0.06f, 659.0f}, {0.12f, 880.0f}};
constexpr AudioEvent kWeaponPickup{
    WaveType::PULSE, 523.0f, 0.18f, 0.50f, 0.25f, 0, nullptr,
    0.0f, 0.0f, false, SweepCurve::Linear, nullptr, 0, kPickupPitch, 3};

// --- Dry-fire. A click, no sweep: a dry trigger is the absence of a shot.
constexpr AudioEvent kDryFire{WaveType::PULSE, 900.0f, 0.03f, 0.25f, 0.125f,
                              0, nullptr};

// --- Vehicle enter / exit. Low triangle thumps, distinguished only by pitch.
constexpr AudioEvent kVehicleEnter{
    WaveType::TRIANGLE, 180.0f, 0.10f, 0.45f, 0.0f, 0, nullptr};
constexpr AudioEvent kVehicleExit{
    WaveType::TRIANGLE, 140.0f, 0.10f, 0.45f, 0.0f, 0, nullptr};

// --- District change. A short, bright blip under the zone banner.
constexpr AudioEvent kDistrictChange{
    WaveType::PULSE, 660.0f, 0.06f, 0.30f, 0.5f, 0, nullptr};

// --- Busted. A NOISE fall, exponential so the tail dies away rather than
//     ramping down linearly.
constexpr AudioEvent kBusted{
    WaveType::NOISE, 900.0f, 0.35f, 0.60f, 0.0f, 0, nullptr,
    120.0f, 0.35f, false, SweepCurve::Exponential};

// --- Wanted-up. Base frequency per resulting star count; index 0 is unused,
//     because reportCrime only calls this on a real level-up. The event is
//     built at call time in AudioDirector::playWantedUp -- the frequency is
//     data-driven, so it cannot be a single constexpr AudioEvent.
constexpr float kWantedUpHz[5] = {440.0f, 494.0f, 554.0f, 622.0f, 740.0f};

/// An octave, so the cue reads as a rising two-tone alert rather than a
/// single pitch bending -- without adding a second event.
constexpr float kWantedUpSecondToneRatio = 2.0f;
constexpr float kWantedUpDurationSec = 0.12f;
constexpr float kWantedUpSweepDurationSec = 0.10f;
constexpr float kWantedUpVolume = 0.45f;
constexpr float kWantedUpDuty = 0.5f;

// --- Footstep. Two variants; the run brighter and shorter, so it reads as a
//     harder heel rather than a faster repeat of the same sound.
//     AudioDirector::playFootstep decides WHEN; this decides HOW.
constexpr AudioEvent kFootstepWalk{
    WaveType::NOISE, 1400.0f, 0.035f, 0.16f, 0.0f, 0, nullptr, 700.0f, 0.035f};
constexpr AudioEvent kFootstepRun{
    WaveType::NOISE, 1900.0f, 0.028f, 0.22f, 0.0f, 0, nullptr, 900.0f, 0.028f};

// --- Vehicle crash. Exponential like kBusted, but lower, longer, louder and
//     ONE voice rather than the gunshot's two: a crash is a single hit.
constexpr AudioEvent kVehicleCrash{
    WaveType::NOISE, 900.0f, 0.22f, 0.55f, 0.0f, 0, nullptr,
    120.0f, 0.22f, false, SweepCurve::Exponential};

// --- A doorway, any of them. A wooden thunk. The sweep is what keeps it from
//     being mistaken for kVehicleEnter/kVehicleExit, which are flat.
constexpr AudioEvent kDoorway{
    WaveType::TRIANGLE, 320.0f, 0.16f, 0.35f, 0.0f, 0, nullptr, 190.0f, 0.16f};

// --- Player hit. Tonal, not NOISE: a gunshot is NOISE-led, so a falling
//     pulse cannot be mistaken for one in the middle of a firefight.
constexpr AudioEvent kPlayerHit{
    WaveType::PULSE, 300.0f, 0.12f, 0.45f, 0.25f, 0, nullptr, 140.0f, 0.12f};

// --- Mission delivered. A four-point rise, longer and lower-starting than
//     kWeaponPickup's three-point one so the two are never confused.
constexpr SfxBreakpoint kMissionDeliveredPitch[] = {
    {0.00f, 392.0f}, {0.08f, 523.0f}, {0.16f, 659.0f}, {0.24f, 784.0f}};
constexpr AudioEvent kMissionDelivered{
    WaveType::PULSE, 392.0f, 0.30f, 0.45f, 0.5f, 0, nullptr,
    0.0f, 0.0f, false, SweepCurve::Linear, nullptr, 0,
    kMissionDeliveredPitch, 4};

// --- Mission failed. A falling PULSE at 25% duty: dying away like kBusted
//     and kVehicleCrash, but pitched and narrower than either.
constexpr AudioEvent kMissionFailed{
    WaveType::PULSE, 440.0f, 0.35f, 0.40f, 0.25f, 0, nullptr,
    165.0f, 0.35f, false, SweepCurve::Exponential};

// --- Roadkill (ADR-16). Deeper and shorter than kVehicleCrash -- 500->90 Hz
//     over 180 ms against its 900->120 over 220 -- and the only event here
//     with a direct LFSR period, which makes it a granular wet thud against
//     the crash's bright metal. One voice: a body is a single hit.
constexpr AudioEvent kRoadkill{
    WaveType::NOISE, 500.0f, 0.18f, 0.60f, 0.0f, 40, nullptr,
    90.0f, 0.18f, false, SweepCurve::Exponential};

// --- Police gunshot (ADR-17). One thin PULSE at 12.5% duty, the most nasal
//     timbre the APU makes. Pitched where the player's crack is NOISE, thin
//     where their body layer is a fat 50%, one voice where theirs is two:
//     unmistakably not the player's gun, even on the same step.
constexpr AudioEvent kPoliceGunshot{
    WaveType::PULSE, 1400.0f, 0.06f, 0.42f, 0.125f, 0, nullptr,
    300.0f, 0.06f, false, SweepCurve::Exponential};

// --- The traffic layer (ADR-15, ADR-18, ADR-19). Five Ambient events, all
//     one voice, all short -- the longest below is 220 ms.

// --- Traffic pass. TRIANGLE, the softest timbre the APU makes, for the one
//     sound the player hears constantly and never caused. Two variants a few
//     Hz apart, picked by the passing car's colour parity, so neither reads
//     as a mistuned copy of the other.
constexpr AudioEvent kTrafficPassLow{
    WaveType::TRIANGLE, 150.0f, 0.20f, 0.22f, 0.0f, 0, nullptr, 105.0f, 0.20f};
constexpr AudioEvent kTrafficPassHigh{
    WaveType::TRIANGLE, 190.0f, 0.20f, 0.22f, 0.0f, 0, nullptr, 130.0f, 0.20f};

// --- Engine rev. The only RISING figure at 25% duty in this file
//     (kMissionFailed and kPlayerHit both fall there), so it cannot be
//     mistaken for either mid-chase.
constexpr AudioEvent kEngineRev{
    WaveType::PULSE, 110.0f, 0.22f, 0.30f, 0.25f, 0, nullptr, 260.0f, 0.22f};

// --- Brake screech. Linear, not Exponential: a screech cuts off abruptly,
//     the opposite of kVehicleCrash's dying tail.
constexpr AudioEvent kBrakeScreech{
    WaveType::NOISE, 2600.0f, 0.16f, 0.28f, 0.0f, 0, nullptr,
    1100.0f, 0.16f, false, SweepCurve::Linear};

// --- Car horn. A down-then-up "beep-beep", deliberately the OPPOSITE
//     contour of playWantedUp's pure rise and quieter than it: the two
//     mitigations for sharing its 392-470 Hz band. Static table, as above.
constexpr SfxBreakpoint kCarHornPitch[] = {
    {0.00f, 392.0f}, {0.08f, 330.0f}, {0.16f, 392.0f}};
constexpr AudioEvent kCarHorn{
    WaveType::PULSE, 392.0f, 0.22f, 0.32f, 0.5f, 0, nullptr,
    0.0f, 0.0f, false, SweepCurve::Linear, nullptr, 0, kCarHornPitch, 3};

// --- Crowd panic. The only RISING noise sweep in the file; every other NOISE
//     cue here falls. A gasp that goes up against six sounds that go down is
//     free identity, no register trick required.
constexpr AudioEvent kCrowdPanic{
    WaveType::NOISE, 700.0f, 0.10f, 0.24f, 0.0f, 0, nullptr,
    2200.0f, 0.10f, false, SweepCurve::Linear};

// kAmbientHoldMs caps the whole Ambient tier at one live voice (ADR-14), which
// only holds if it outlives every Ambient event's own duration. The two longest
// are pinned here against the data itself rather than a copied number; the
// *1000.0f converts the event side into milliseconds so the compare does not
// narrow.
static_assert(
    static_cast<float>(top_down_city::audio_cues::kAmbientHoldMs)
            > kEngineRev.duration * 1000.0f
        && static_cast<float>(top_down_city::audio_cues::kAmbientHoldMs)
               > kCarHorn.duration * 1000.0f,
    "the ambient tier's shared hold must outlive its own longest event, or "
    "two ambient voices could overlap");

}  // namespace top_down_city::city_sfx

#endif  // PIXELROOT32_ENABLE_AUDIO
