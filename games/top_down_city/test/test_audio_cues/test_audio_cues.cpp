/**
 * @brief What the city sounds like: a radio plan per car, and when a
 *        one-shot cue may speak.
 *
 * GTA Advance revision: the fifteen cases that proved the wanted-driven
 * MusicState model (a three-state siren transition table and a chase-tempo
 * ladder) were deleted alongside the functions they tested, and that is a
 * coverage upgrade rather than a loss: a plan recomputed from scratch every
 * frame has no transition to get wrong, so the guarantee those fifteen bought
 * by exhaustive enumeration is now bought by construction.
 *
 * What covers the model that replaced it, in the order the banners below name
 * it: the three cooldown cases (which predate both revisions),
 * `radioFor`/`musicPlanFor`/`planChanged`, Slice 4's SFX pass, Slice 5a's
 * admission gate and Slice 5b's traffic layer.
 */
#include <unity.h>

#include <cstdint>

#include "game/rules/AudioCues.h"
#include "game/rules/Wanted.h"
#include "game/rules/Weapon.h"

namespace cues = top_down_city::audio_cues;
namespace wanted = top_down_city::wanted;
namespace weapons = top_down_city::weapons;

void setUp() {}
void tearDown() {}

// --- The cooldown clock every one-shot cue shares --------------------------

void test_every_cue_has_a_nonzero_cooldown() {
    // A cooldown of zero is not a throttle, it is a table entry that forgot
    // to throttle anything -- cueAllowed(0) is always true, so a zero row
    // would let that one cue retrigger every single logic step.
    //
    // Widened in place rather than by adding siblings: the loop bound is
    // Cue::Count itself, so rows are walked for free the moment the enum
    // grows, and the explicit count below is what proves this test still
    // exercises every row rather than a copy frozen at an earlier size.
    TEST_ASSERT_EQUAL_UINT8(22, static_cast<std::uint8_t>(cues::Cue::Count));
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(cues::Cue::Count);
         ++c) {
        const cues::Cue cue = static_cast<cues::Cue>(c);
        TEST_ASSERT_TRUE(cues::cooldownMsFor(cue) > 0);
    }
}

void test_gunshot_cooldown_is_shorter_than_the_pistol_fire_interval() {
    // The audio-side cooldown must be a backstop, never a second trigger
    // gate: it has to be strictly shorter than the weapon's own fire
    // interval, or a legitimate shot the weapon allowed could still be
    // swallowed by the audio layer.
    const std::uint16_t pistolIntervalMs = static_cast<std::uint16_t>(
        weapons::spec(weapons::WeaponId::Pistol).fireRateSteps
        * cues::kLogicStepMs);
    TEST_ASSERT_TRUE(cues::cooldownMsFor(cues::Cue::Gunshot)
                     < pistolIntervalMs);
}

void test_age_cooldown_saturates_at_zero() {
    // A step whose deltaTime outlives the remaining cooldown must land
    // exactly at zero, not wrap past it on an unsigned type.
    TEST_ASSERT_EQUAL_UINT16(0, cues::ageCooldown(100, 150));
    TEST_ASSERT_EQUAL_UINT16(0, cues::ageCooldown(0, 0));
    TEST_ASSERT_EQUAL_UINT16(400, cues::ageCooldown(500, 100));
    TEST_ASSERT_EQUAL_UINT16(0, cues::ageCooldown(16, 16));
}

// --- The GTA Advance revision: a radio plan, not a wanted-driven state
// machine (cases 1-13, design revision 2 numbering) ------------------------

void test_on_foot_and_clean_is_silence() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/false, /*vehicleColor=*/0, /*wanted=*/false,
        /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::None, plan.radio);
    TEST_ASSERT_FALSE(plan.siren);
}

void test_on_foot_and_wanted_is_the_siren_alone() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/false, /*vehicleColor=*/0, /*wanted=*/true,
        /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::None, plan.radio);
    TEST_ASSERT_TRUE(plan.siren);
}

void test_driving_a_civilian_car_plays_its_station() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/true, /*vehicleColor=*/0, /*wanted=*/false,
        /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::One, plan.radio);
    TEST_ASSERT_FALSE(plan.siren);
}

void test_driving_and_wanted_keeps_the_station_and_adds_the_siren() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/true, /*vehicleColor=*/0, /*wanted=*/true,
        /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::One, plan.radio);
    TEST_ASSERT_TRUE(plan.siren);
}

void test_a_police_car_has_no_radio() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/true, /*vehicleColor=*/cues::kPoliceColor,
        /*wanted=*/false, /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::None, plan.radio);
    TEST_ASSERT_FALSE(plan.siren);
}

void test_a_police_car_still_carries_the_siren_when_wanted() {
    const cues::MusicPlan plan = cues::musicPlanFor(
        /*driving=*/true, /*vehicleColor=*/cues::kPoliceColor,
        /*wanted=*/true, /*busted=*/false);
    TEST_ASSERT_EQUAL(cues::RadioTrackId::None, plan.radio);
    TEST_ASSERT_TRUE(plan.siren);
}

void test_busted_silences_everything() {
    // busted outranks driving, colour and wanted alike -- on foot or in any
    // of the 7 colours, wanted or not, the arrest is always {None, false}.
    for (std::uint8_t color = 0; color < cues::kRadioColorCount; ++color) {
        for (int drivingFlag = 0; drivingFlag < 2; ++drivingFlag) {
            for (int wantedFlag = 0; wantedFlag < 2; ++wantedFlag) {
                const cues::MusicPlan plan = cues::musicPlanFor(
                    drivingFlag != 0, color, wantedFlag != 0,
                    /*busted=*/true);
                TEST_ASSERT_EQUAL(cues::RadioTrackId::None, plan.radio);
                TEST_ASSERT_FALSE(plan.siren);
            }
        }
    }
}

void test_the_same_colour_always_picks_the_same_station() {
    for (std::uint8_t color = 0; color < cues::kRadioColorCount; ++color) {
        const cues::RadioTrackId first = cues::radioFor(color);
        for (int repeat = 0; repeat < 5; ++repeat) {
            TEST_ASSERT_EQUAL(first, cues::radioFor(color));
        }
    }
}

void test_every_civilian_colour_maps_to_a_real_station() {
    for (std::uint8_t color = 0; color < cues::kRadioColorCount; ++color) {
        if (color == cues::kPoliceColor) {
            continue;
        }
        TEST_ASSERT_NOT_EQUAL(cues::RadioTrackId::None, cues::radioFor(color));
    }
}

void test_every_station_is_reachable_by_some_colour() {
    // Wanted.h's own "a rung that changes nothing is a table entry nobody
    // notices broke" argument, applied to the colour table: every station
    // is somebody's car.
    const cues::RadioTrackId kStations[] = {
        cues::RadioTrackId::One, cues::RadioTrackId::Two,
        cues::RadioTrackId::Three, cues::RadioTrackId::Four,
    };
    for (cues::RadioTrackId station : kStations) {
        bool reached = false;
        for (std::uint8_t color = 0; color < cues::kRadioColorCount;
             ++color) {
            if (cues::radioFor(color) == station) {
                reached = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(reached, "a station no colour ever plays");
    }
}

void test_an_unknown_colour_has_no_radio() {
    TEST_ASSERT_EQUAL(cues::RadioTrackId::None,
                       cues::radioFor(cues::kRadioColorCount));
    TEST_ASSERT_EQUAL(
        cues::RadioTrackId::None,
        cues::radioFor(static_cast<std::uint8_t>(cues::kRadioColorCount + 5)));
}

void test_the_plan_is_unchanged_by_the_star_count() {
    // ADR-7's guard: the plan's `siren` field is the wanted BOOLEAN, never
    // the star count -- climbing 1 through 5 stars in the same car must not
    // change a single field of the plan, or the radio would restart on
    // every escalation instead of exactly twice per wanted episode.
    const cues::MusicPlan atOneStar = cues::musicPlanFor(
        /*driving=*/true, /*vehicleColor=*/1, /*wanted=*/true,
        /*busted=*/false);
    for (std::uint8_t stars = 2; stars <= wanted::kMaxStars; ++stars) {
        // musicPlanFor takes no star count at all: the loop documents the
        // guarantee rather than varying an argument that is not there.
        const cues::MusicPlan atThisStarCount = cues::musicPlanFor(
            /*driving=*/true, /*vehicleColor=*/1, /*wanted=*/true,
            /*busted=*/false);
        TEST_ASSERT_EQUAL(atOneStar.radio, atThisStarCount.radio);
        TEST_ASSERT_EQUAL(atOneStar.siren, atThisStarCount.siren);
    }
}

void test_plan_changed_is_true_only_when_something_audible_differs() {
    const cues::MusicPlan silence{cues::RadioTrackId::None, false};
    const cues::MusicPlan sirenOnly{cues::RadioTrackId::None, true};
    const cues::MusicPlan stationOnly{cues::RadioTrackId::One, false};
    const cues::MusicPlan stationAndSiren{cues::RadioTrackId::One, true};
    const cues::MusicPlan otherStation{cues::RadioTrackId::Two, false};

    TEST_ASSERT_FALSE(cues::planChanged(silence, silence));
    TEST_ASSERT_FALSE(cues::planChanged(stationOnly, stationOnly));
    TEST_ASSERT_TRUE(cues::planChanged(silence, sirenOnly));
    TEST_ASSERT_TRUE(cues::planChanged(silence, stationOnly));
    TEST_ASSERT_TRUE(cues::planChanged(stationOnly, stationAndSiren));
    TEST_ASSERT_TRUE(cues::planChanged(stationOnly, otherStation));
    TEST_ASSERT_TRUE(cues::planChanged(stationAndSiren, silence));
}

// --- Slice 4: footsteps, crash and voice priority (cases 14-29) -----------
//
// Footsteps are distance-locked, not time-locked (ADR-12): a footfall costs
// the same ~28 px of ground at either gait, because the two stride lengths are
// in the inverse ratio of the two gait speeds -- four walk strides to seven
// run strides, against a sprint that is 1.75x the walk. The stride clock
// counts logic steps of ACTUAL MOVEMENT since the last footfall (CityScene
// only advances it on a step where the sprite moved), so a standing player
// never accrues one no matter how long they wait.

void test_the_two_strides_are_the_inverse_of_the_two_gaits() {
    // 4:7 on the strides against 256:448 on the speeds. Written as the
    // cross-multiplication rather than a fraction so it holds in integers,
    // and mirrored by the static_assert in CityConstants.h, which is the one
    // header that can see this ratio AND the speeds it is the inverse of.
    TEST_ASSERT_EQUAL_UINT16(cues::kWalkStrideSteps * 4,
                             cues::kRunStrideSteps * 7);
    TEST_ASSERT_EQUAL_UINT8(cues::kWalkStrideSteps,
                            cues::footstepStrideSteps(/*running=*/false));
    TEST_ASSERT_EQUAL_UINT8(cues::kRunStrideSteps,
                            cues::footstepStrideSteps(/*running=*/true));
}

void test_a_footfall_is_due_only_after_a_full_stride() {
    // Walking: the stride is 28 steps -- one short is not yet due, exactly
    // that many is.
    TEST_ASSERT_FALSE(cues::footstepDue(cues::kWalkStrideSteps - 1,
                                        /*moving=*/true, /*running=*/false));
    TEST_ASSERT_TRUE(cues::footstepDue(cues::kWalkStrideSteps,
                                       /*moving=*/true, /*running=*/false));
    // Running: the stride is 16 steps -- same boundary, fewer steps.
    TEST_ASSERT_FALSE(cues::footstepDue(cues::kRunStrideSteps - 1,
                                        /*moving=*/true, /*running=*/true));
    TEST_ASSERT_TRUE(cues::footstepDue(cues::kRunStrideSteps,
                                       /*moving=*/true, /*running=*/true));
}

void test_a_standing_player_never_takes_a_step() {
    // moving == false refuses regardless of how long the clock has read --
    // the guard that keeps a stride reading from a previous walk from
    // firing the instant the player merely stands still long enough.
    TEST_ASSERT_FALSE(cues::footstepDue(255, /*moving=*/false,
                                        /*running=*/false));
    TEST_ASSERT_FALSE(cues::footstepDue(255, /*moving=*/false,
                                        /*running=*/true));
}

void test_the_footstep_clock_restarts_at_zero_after_a_footfall() {
    TEST_ASSERT_EQUAL_UINT8(0, cues::ageFootstepClock(50, /*fired=*/true));
    TEST_ASSERT_EQUAL_UINT8(0, cues::ageFootstepClock(0, /*fired=*/true));
}

void test_the_footstep_clock_saturates_rather_than_wrapping() {
    // A uint8_t that wrapped past 255 back to 0 would read as "just took a
    // step" to the very next check -- saturating keeps a long, uninterrupted
    // walk from ever producing that false reading.
    TEST_ASSERT_EQUAL_UINT8(255, cues::ageFootstepClock(255, /*fired=*/false));
    TEST_ASSERT_EQUAL_UINT8(11, cues::ageFootstepClock(10, /*fired=*/false));
}

void test_the_footstep_cooldown_cannot_swallow_a_run_stride() {
    // The same backstop argument test_gunshot_cooldown_is_shorter_than_the_
    // pistol_fire_interval makes for gunfire, applied to the faster gait: the
    // 100 ms cooldown row must be strictly shorter than a run stride's own
    // 256 ms (16 steps * 16 ms), or the cooldown -- meant only as a backstop
    // against a double-fire -- would silently eat every other footfall.
    const std::uint16_t runStrideMs = static_cast<std::uint16_t>(
        cues::kRunStrideSteps * cues::kLogicStepMs);
    TEST_ASSERT_TRUE(cues::cooldownMsFor(cues::Cue::Footstep) < runStrideMs);
}

void test_a_crash_below_the_threshold_is_silent() {
    TEST_ASSERT_FALSE(cues::crashIsAudible(cues::kCrashAudibleSpeedSub - 1));
}

void test_a_crash_at_the_threshold_is_heard() {
    TEST_ASSERT_TRUE(cues::crashIsAudible(cues::kCrashAudibleSpeedSub));
}

void test_a_car_stopping_from_rest_is_not_a_crash() {
    // VehicleActor::consumeCrash() reads 0 whenever the car has not stopped
    // dead against anything since the last read -- an ordinary stop, not a
    // crash. Zero is always below the threshold, for any car in this demo.
    TEST_ASSERT_FALSE(cues::crashIsAudible(0));
}

// test_footsteps_are_the_only_droppable_cue (Slice 4) is DELETED rather than
// widened: its assertion -- that Footstep is the ONLY Ambient cue -- stopped
// being true when Slice 5b's five traffic cues joined the tier.
// test_every_traffic_cue_is_droppable below supersedes it, pinning the FULL
// Ambient membership (all six rows), which is strictly stronger.

void test_every_essential_cue_is_never_refused() {
    // Refusal (ambientCueAllowed's gate) is only ever consulted for the one
    // Ambient row -- these seven are the things a player did or must notice,
    // and none of them may be silently dropped for a footfall's sake.
    const cues::Cue kEssential[] = {
        cues::Cue::Gunshot,     cues::Cue::ShotgunBlast,
        cues::Cue::WantedUp,    cues::Cue::PlayerHit,
        cues::Cue::Busted,      cues::Cue::MissionDelivered,
        cues::Cue::MissionFailed,
    };
    for (cues::Cue cue : kEssential) {
        TEST_ASSERT_EQUAL(cues::CuePriority::Essential, cues::priorityOf(cue));
    }
}

void test_gunfire_silences_footsteps_for_one_fire_interval() {
    TEST_ASSERT_FALSE(cues::ambientCueAllowed(cues::kEssentialQuietMs));
    TEST_ASSERT_FALSE(cues::ambientCueAllowed(1));
}

void test_footsteps_return_once_the_gunfire_clock_ages_out() {
    TEST_ASSERT_TRUE(cues::ambientCueAllowed(0));
    // The clock ages down through the same ageCooldown every other cue's
    // cooldown uses -- one fire interval of elapsed time empties it.
    const std::uint16_t agedOut =
        cues::ageCooldown(cues::kEssentialQuietMs, cues::kEssentialQuietMs);
    TEST_ASSERT_TRUE(cues::ambientCueAllowed(agedOut));
}

void test_the_weapon_cues_are_the_only_two_voice_cues() {
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(cues::Cue::Count);
         ++c) {
        const cues::Cue cue = static_cast<cues::Cue>(c);
        const bool isWeapon =
            cue == cues::Cue::Gunshot || cue == cues::Cue::ShotgunBlast;
        TEST_ASSERT_EQUAL_UINT8(isWeapon ? 2 : 1, cues::voiceCostFor(cue));
    }
}

void test_the_worst_admissible_burst_fits_the_four_sfx_voices() {
    // With footsteps refused for one fire interval after any weapon cue, the
    // worst admissible simultaneous burst is a shotgun blast, a hit landing,
    // and an arrest -- exactly the four-voice SFX subpool, with no room left
    // for a footfall to steal from it.
    const std::uint8_t worstBurst = static_cast<std::uint8_t>(
        cues::voiceCostFor(cues::Cue::ShotgunBlast)
        + cues::voiceCostFor(cues::Cue::PlayerHit)
        + cues::voiceCostFor(cues::Cue::Busted));
    TEST_ASSERT_EQUAL_UINT8(cues::kSfxVoiceCount, worstBurst);
}

// --- Slice 5a: ADR-14's admission gate (SfxBudget / admitCue / chargeCue /
// ageBudget) and the two Essential cues it exists to protect from ------------
//
// The pool is already saturated by ordinary play (design §15.3): one first
// shot from clean costs 3 of 4 SFX voices before anything else fires. These
// cases prove the director-side gate priorityOf/voiceCostFor were built for
// in slice 4 and never wired up -- this is their first real caller.

void test_the_ambient_layer_can_never_hold_two_voices_at_once() {
    // After one Ambient cue is admitted and charges the shared hold, a
    // second Ambient cue in the very same step is refused -- the whole tier
    // can never stack two voices, regardless of which two Ambient cues they
    // are (Footstep is the only one that exists yet, but the mechanism does
    // not know that).
    cues::SfxBudget budget{};
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::Footstep, budget));
    budget = cues::chargeCue(budget, cues::Cue::Footstep);
    TEST_ASSERT_EQUAL(cues::CueVerdict::RefusedBusy,
                      cues::admitCue(cues::Cue::Footstep, budget));
}

void test_an_admitted_ambient_cue_charges_the_shared_ambient_hold() {
    cues::SfxBudget budget{};
    budget = cues::chargeCue(budget, cues::Cue::Footstep);
    TEST_ASSERT_EQUAL_UINT16(cues::kAmbientHoldMs, budget.ambientBusyMs);
}

void test_every_essential_cue_closes_the_ambient_window() {
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(cues::Cue::Count);
         ++c) {
        const cues::Cue cue = static_cast<cues::Cue>(c);
        if (cues::priorityOf(cue) != cues::CuePriority::Essential) {
            continue;
        }
        cues::SfxBudget budget{};
        budget = cues::chargeCue(budget, cue);
        TEST_ASSERT_EQUAL_UINT16(cues::kEssentialQuietMs,
                                 budget.ambientQuietMs);
    }
}

void test_no_normal_or_ambient_cue_closes_the_ambient_window() {
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(cues::Cue::Count);
         ++c) {
        const cues::Cue cue = static_cast<cues::Cue>(c);
        if (cues::priorityOf(cue) == cues::CuePriority::Essential) {
            continue;
        }
        cues::SfxBudget budget{};
        budget = cues::chargeCue(budget, cue);
        TEST_ASSERT_EQUAL_UINT16(0, budget.ambientQuietMs);
    }
}

void test_an_essential_cue_is_admitted_with_the_pool_already_full() {
    cues::SfxBudget budget{};
    budget.frameVoiceLoad = cues::kSfxVoiceCount;
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::Gunshot, budget));
}

void test_a_normal_cue_is_refused_once_the_step_is_committed() {
    cues::SfxBudget budget{};
    budget.frameVoiceLoad = 3;
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::VehicleCrash, budget));
    budget.frameVoiceLoad = 4;
    TEST_ASSERT_EQUAL(cues::CueVerdict::RefusedNoVoice,
                      cues::admitCue(cues::Cue::VehicleCrash, budget));
}

void test_the_first_shot_burst_fits_the_four_sfx_voices() {
    // Design §15.3/§16.2's named worst case: Gunshot(2) + WantedUp(1) +
    // PlayerHit(1) == the whole pool, every one admitted, and a same-step
    // Ambient cue refused by the quiet window the burst itself just opened
    // -- not by running out of voices, which would have been true anyway.
    cues::SfxBudget budget{};
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::Gunshot, budget));
    budget = cues::chargeCue(budget, cues::Cue::Gunshot);
    TEST_ASSERT_EQUAL_UINT8(2, budget.frameVoiceLoad);

    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::WantedUp, budget));
    budget = cues::chargeCue(budget, cues::Cue::WantedUp);
    TEST_ASSERT_EQUAL_UINT8(3, budget.frameVoiceLoad);

    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::PlayerHit, budget));
    budget = cues::chargeCue(budget, cues::Cue::PlayerHit);
    TEST_ASSERT_EQUAL_UINT8(cues::kSfxVoiceCount, budget.frameVoiceLoad);

    TEST_ASSERT_EQUAL(cues::CueVerdict::RefusedQuiet,
                      cues::admitCue(cues::Cue::Footstep, budget));
}

void test_a_fifth_essential_demand_is_still_admitted_and_says_so() {
    // Named and accepted, not hidden: with the pool already at 4/4, a fifth
    // Essential demand is still Play -- the engine's own blind steal decides
    // which voice loses. admitCue does not pretend this cannot happen.
    cues::SfxBudget budget{};
    budget.frameVoiceLoad = cues::kSfxVoiceCount;
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::MissionDelivered, budget));
}

void test_ageing_the_budget_clears_the_step_load_but_not_the_clocks() {
    cues::SfxBudget budget{};
    budget.frameVoiceLoad = 3;
    budget.ambientQuietMs = 100;
    budget.ambientBusyMs = 50;
    const cues::SfxBudget aged = cues::ageBudget(budget, /*dtMs=*/10);
    TEST_ASSERT_EQUAL_UINT8(0, aged.frameVoiceLoad);
    TEST_ASSERT_EQUAL_UINT16(90, aged.ambientQuietMs);
    TEST_ASSERT_EQUAL_UINT16(40, aged.ambientBusyMs);
}

void test_ageing_the_budget_saturates_both_clocks_at_zero() {
    cues::SfxBudget budget{};
    budget.ambientQuietMs = 10;
    budget.ambientBusyMs = 5;
    const cues::SfxBudget aged = cues::ageBudget(budget, /*dtMs=*/1000);
    TEST_ASSERT_EQUAL_UINT16(0, aged.ambientQuietMs);
    TEST_ASSERT_EQUAL_UINT16(0, aged.ambientBusyMs);
}

void test_roadkill_and_police_gunfire_are_never_refused() {
    cues::SfxBudget budget{};
    budget.frameVoiceLoad = cues::kSfxVoiceCount;
    budget.ambientQuietMs = 999;
    budget.ambientBusyMs = 999;
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::Roadkill, budget));
    TEST_ASSERT_EQUAL(cues::CueVerdict::Play,
                      cues::admitCue(cues::Cue::PoliceGunshot, budget));
    TEST_ASSERT_EQUAL(cues::CuePriority::Essential,
                      cues::priorityOf(cues::Cue::Roadkill));
    TEST_ASSERT_EQUAL(cues::CuePriority::Essential,
                      cues::priorityOf(cues::Cue::PoliceGunshot));
}

void test_police_gunfire_costs_one_voice_not_two() {
    // Unlike the player's own two-layer shot (ADR-17): pitched-and-thin, one
    // voice, never the crack-over-body pair.
    TEST_ASSERT_EQUAL_UINT8(1, cues::voiceCostFor(cues::Cue::PoliceGunshot));
}

void test_police_gunshot_cooldown_is_shorter_than_the_police_fire_interval() {
    // Same backstop argument as the pistol's own case, applied to the
    // officers' slower sidearm: the audio-side cooldown must never be able
    // to swallow a shot the weapon table already allowed.
    const std::uint16_t policeIntervalMs = static_cast<std::uint16_t>(
        weapons::spec(weapons::WeaponId::PolicePistol).fireRateSteps
        * cues::kLogicStepMs);
    TEST_ASSERT_TRUE(cues::cooldownMsFor(cues::Cue::PoliceGunshot)
                     < policeIntervalMs);
}

// --- Slice 5b: the traffic layer -- pedalEventFor / ageStallClock / hornDue,
// and the ambient tier's five new droppable cues -----------------------------
//
// ADR-19: acceleration and braking are a pedal EDGE, computed by a pure rule
// from the car's own throttle history, never a speed delta the director
// samples across its own per-frame clock -- see the header comment on
// pedalEventFor for why a delta would miss short taps and invent phantom
// ones. The horn's `==` (not `>=`) is pinned directly, so a permanently
// wedged car honks once per stall rather than sixty-two times a second.

void test_a_stall_clock_resets_the_moment_the_car_moves() {
    TEST_ASSERT_EQUAL_UINT8(0, cues::ageStallClock(10, /*go=*/true));
}

void test_a_stall_clock_saturates_rather_than_wrapping() {
    TEST_ASSERT_EQUAL_UINT8(255, cues::ageStallClock(255, /*go=*/false));
}

void test_a_horn_sounds_once_per_stall_not_once_per_step() {
    // hornDue pins `==`, not `>=`: exactly on the step the clock reaches the
    // threshold, and never again while the car stays wedged there.
    TEST_ASSERT_FALSE(cues::hornDue(cues::kHornStallSteps - 1));
    TEST_ASSERT_TRUE(cues::hornDue(cues::kHornStallSteps));
    TEST_ASSERT_FALSE(cues::hornDue(cues::kHornStallSteps + 1));
}

void test_a_car_that_never_stalls_never_honks() {
    // A car that moves every step ages its clock straight back to zero, so
    // hornDue can never see the threshold no matter how many steps pass.
    std::uint8_t stalled = 0;
    for (int step = 0; step < 200; ++step) {
        stalled = cues::ageStallClock(stalled, /*go=*/true);
        TEST_ASSERT_FALSE(cues::hornDue(stalled));
    }
}

void test_a_pedal_event_is_the_edge_not_the_hold() {
    // The edge only, not the hold: a throttle held down for many steps fires
    // Accelerated once, on the step it first goes positive.
    TEST_ASSERT_EQUAL(cues::PedalEvent::Accelerated,
                      cues::pedalEventFor(/*throttle=*/1, /*prevThrottle=*/0,
                                          /*speedSub=*/0));
    TEST_ASSERT_EQUAL(cues::PedalEvent::None,
                      cues::pedalEventFor(1, /*prevThrottle=*/1, 0));
}

void test_braking_below_the_audible_speed_is_silent() {
    // Below kBrakeAudibleSpeedSub the car is manoeuvring, not driving, and a
    // screech would be a lie -- even on the first step the brake is applied.
    TEST_ASSERT_EQUAL(
        cues::PedalEvent::None,
        cues::pedalEventFor(/*throttle=*/-1, /*prevThrottle=*/1,
                            cues::kBrakeAudibleSpeedSub - 1));
    TEST_ASSERT_EQUAL(
        cues::PedalEvent::Braked,
        cues::pedalEventFor(-1, 1, cues::kBrakeAudibleSpeedSub));
}

void test_reversing_is_not_braking() {
    // Braking and reversing share one button. Starting from rest and holding
    // it down is reversing, not braking -- speedSub never crosses the audible
    // threshold, so no screech fires on the way out of the alley.
    std::int32_t speedSub = 0;
    for (int step = 0; step < 10; ++step) {
        const cues::PedalEvent event =
            cues::pedalEventFor(-1, step == 0 ? 0 : -1, speedSub);
        TEST_ASSERT_EQUAL(cues::PedalEvent::None, event);
    }
}

void test_releasing_the_throttle_is_neither_pedal_event() {
    // Letting go of the accelerator, or letting go of the brake, is silence --
    // neither Accelerated (throttle must go POSITIVE) nor Braked (throttle
    // must go NEGATIVE) is an edge into zero.
    TEST_ASSERT_EQUAL(cues::PedalEvent::None,
                      cues::pedalEventFor(/*throttle=*/0, /*prevThrottle=*/1,
                                          cues::kBrakeAudibleSpeedSub));
    TEST_ASSERT_EQUAL(cues::PedalEvent::None,
                      cues::pedalEventFor(0, /*prevThrottle=*/-1,
                                          cues::kBrakeAudibleSpeedSub));
}

void test_every_traffic_cue_is_droppable() {
    std::uint8_t ambientCount = 0;
    for (std::uint8_t c = 0; c < static_cast<std::uint8_t>(cues::Cue::Count);
         ++c) {
        const cues::Cue cue = static_cast<cues::Cue>(c);
        if (cues::priorityOf(cue) != cues::CuePriority::Ambient) {
            continue;
        }
        ++ambientCount;
        const bool isKnownAmbient =
            cue == cues::Cue::Footstep || cue == cues::Cue::TrafficPass
            || cue == cues::Cue::EngineRev || cue == cues::Cue::BrakeScreech
            || cue == cues::Cue::CarHorn || cue == cues::Cue::CrowdPanic;
        TEST_ASSERT_TRUE(isKnownAmbient);
    }
    TEST_ASSERT_EQUAL_UINT8(6, ambientCount);
}

void test_no_ambient_cue_can_outrun_the_shared_hold() {
    // Applies to the FIVE NEW traffic cues, deliberately not to Footstep,
    // whose 100 ms row is pinned SHORTER on purpose:
    // test_the_footstep_cooldown_cannot_swallow_a_run_stride requires it to
    // stay under a 256 ms run stride, just below kAmbientHoldMs's 260 ms. That
    // is not a gap in the tier's voice cap -- the shared busy clock still caps
    // footfalls at one every 260 ms, as it does every Ambient cue. The five
    // new rows have no such competing constraint, so each must clear the
    // shared floor itself: a row shorter than the hold it sits behind would
    // look ready when admitCue would still refuse it as RefusedBusy.
    const cues::Cue kNewTrafficCues[] = {
        cues::Cue::TrafficPass, cues::Cue::EngineRev,
        cues::Cue::BrakeScreech, cues::Cue::CarHorn, cues::Cue::CrowdPanic,
    };
    for (cues::Cue cue : kNewTrafficCues) {
        TEST_ASSERT_EQUAL(cues::CuePriority::Ambient, cues::priorityOf(cue));
        TEST_ASSERT_TRUE(cues::cooldownMsFor(cue) >= cues::kAmbientHoldMs);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_cue_has_a_nonzero_cooldown);
    RUN_TEST(test_gunshot_cooldown_is_shorter_than_the_pistol_fire_interval);
    RUN_TEST(test_age_cooldown_saturates_at_zero);
    RUN_TEST(test_on_foot_and_clean_is_silence);
    RUN_TEST(test_on_foot_and_wanted_is_the_siren_alone);
    RUN_TEST(test_driving_a_civilian_car_plays_its_station);
    RUN_TEST(test_driving_and_wanted_keeps_the_station_and_adds_the_siren);
    RUN_TEST(test_a_police_car_has_no_radio);
    RUN_TEST(test_a_police_car_still_carries_the_siren_when_wanted);
    RUN_TEST(test_busted_silences_everything);
    RUN_TEST(test_the_same_colour_always_picks_the_same_station);
    RUN_TEST(test_every_civilian_colour_maps_to_a_real_station);
    RUN_TEST(test_every_station_is_reachable_by_some_colour);
    RUN_TEST(test_an_unknown_colour_has_no_radio);
    RUN_TEST(test_the_plan_is_unchanged_by_the_star_count);
    RUN_TEST(test_plan_changed_is_true_only_when_something_audible_differs);
    RUN_TEST(test_the_two_strides_are_the_inverse_of_the_two_gaits);
    RUN_TEST(test_a_footfall_is_due_only_after_a_full_stride);
    RUN_TEST(test_a_standing_player_never_takes_a_step);
    RUN_TEST(test_the_footstep_clock_restarts_at_zero_after_a_footfall);
    RUN_TEST(test_the_footstep_clock_saturates_rather_than_wrapping);
    RUN_TEST(test_the_footstep_cooldown_cannot_swallow_a_run_stride);
    RUN_TEST(test_a_crash_below_the_threshold_is_silent);
    RUN_TEST(test_a_crash_at_the_threshold_is_heard);
    RUN_TEST(test_a_car_stopping_from_rest_is_not_a_crash);
    RUN_TEST(test_every_essential_cue_is_never_refused);
    RUN_TEST(test_gunfire_silences_footsteps_for_one_fire_interval);
    RUN_TEST(test_footsteps_return_once_the_gunfire_clock_ages_out);
    RUN_TEST(test_the_weapon_cues_are_the_only_two_voice_cues);
    RUN_TEST(test_the_worst_admissible_burst_fits_the_four_sfx_voices);
    RUN_TEST(test_the_ambient_layer_can_never_hold_two_voices_at_once);
    RUN_TEST(test_an_admitted_ambient_cue_charges_the_shared_ambient_hold);
    RUN_TEST(test_every_essential_cue_closes_the_ambient_window);
    RUN_TEST(test_no_normal_or_ambient_cue_closes_the_ambient_window);
    RUN_TEST(test_an_essential_cue_is_admitted_with_the_pool_already_full);
    RUN_TEST(test_a_normal_cue_is_refused_once_the_step_is_committed);
    RUN_TEST(test_the_first_shot_burst_fits_the_four_sfx_voices);
    RUN_TEST(test_a_fifth_essential_demand_is_still_admitted_and_says_so);
    RUN_TEST(test_ageing_the_budget_clears_the_step_load_but_not_the_clocks);
    RUN_TEST(test_ageing_the_budget_saturates_both_clocks_at_zero);
    RUN_TEST(test_roadkill_and_police_gunfire_are_never_refused);
    RUN_TEST(test_police_gunfire_costs_one_voice_not_two);
    RUN_TEST(test_police_gunshot_cooldown_is_shorter_than_the_police_fire_interval);
    RUN_TEST(test_a_stall_clock_resets_the_moment_the_car_moves);
    RUN_TEST(test_a_stall_clock_saturates_rather_than_wrapping);
    RUN_TEST(test_a_horn_sounds_once_per_stall_not_once_per_step);
    RUN_TEST(test_a_car_that_never_stalls_never_honks);
    RUN_TEST(test_a_pedal_event_is_the_edge_not_the_hold);
    RUN_TEST(test_braking_below_the_audible_speed_is_silent);
    RUN_TEST(test_reversing_is_not_braking);
    RUN_TEST(test_releasing_the_throttle_is_neither_pedal_event);
    RUN_TEST(test_every_traffic_cue_is_droppable);
    RUN_TEST(test_no_ambient_cue_can_outrun_the_shared_hold);
    return UNITY_END();
}
