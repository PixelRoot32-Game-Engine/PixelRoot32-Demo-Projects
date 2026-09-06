#include "game/rules/AudioCues.h"

namespace top_down_city::audio_cues {

namespace {

/// One row per Cue, in milliseconds. Gunshot's 60 ms is a backstop, not a
/// throttle: the pistol's own fireRateSteps is 14 steps (224 ms) and the
/// shotgun's is 45 (720 ms), so this table can never swallow a legitimate
/// shot -- test_gunshot_cooldown_is_shorter_than_the_pistol_fire_interval
/// pins that against the weapon table itself rather than against a copy of
/// the number.
constexpr std::uint16_t kCooldownMs[static_cast<std::uint8_t>(Cue::Count)] = {
    /* Gunshot          */   60,
    /* ShotgunBlast     */  120,
    /* DryFire          */  200,
    /* WeaponPickup     */  250,
    /* WantedUp         */  400,
    /* VehicleEnter     */  300,
    /* VehicleExit      */  300,
    /* Busted           */ 1000,
    /* DistrictChange   */  500,
    /* Footstep         */  100,
    /* VehicleCrash     */  300,
    /* Doorway          */  400,
    /* PlayerHit        */  250,
    /* MissionDelivered */  800,
    /* MissionFailed    */  800,
    /* Roadkill         */  250,
    /* PoliceGunshot    */  120,
    /* TrafficPass      */ 1200,
    /* EngineRev        */  700,
    /* BrakeScreech     */  700,
    /* CarHorn          */ 1500,
    /* CrowdPanic       */  900,
};

}  // namespace

std::uint16_t cooldownMsFor(Cue cue) {
    const std::uint8_t index = static_cast<std::uint8_t>(cue);
    // Clamped rather than asserted, matching Wanted.h's penalty() lookup: a
    // cue nothing in the demo can produce still has to answer something, and
    // the mildest entry is a safer wrong answer than reading past the table.
    if (index >= static_cast<std::uint8_t>(Cue::Count)) {
        return kCooldownMs[0];
    }
    return kCooldownMs[index];
}

bool cueAllowed(std::uint16_t remainingMs) {
    return remainingMs == 0;
}

std::uint16_t ageCooldown(std::uint16_t remainingMs, unsigned long dtMs) {
    if (dtMs >= remainingMs) {
        return 0;
    }
    return static_cast<std::uint16_t>(remainingMs - dtMs);
}

namespace {

/// Row per vehicle colour (0..kRadioColorCount-1). Six civilian colours over
/// four stations cannot be even, so stations 1 and 2 carry two colours each
/// -- test_every_station_is_reachable_by_some_colour exists so no station is
/// ever dead data. Row kPoliceColor is the one exception a modulus cannot
/// express, which is why this is a table rather than `colour % 4`.
constexpr RadioTrackId kStationByColor[kRadioColorCount] = {
    RadioTrackId::One,    // 0
    RadioTrackId::Two,    // 1
    RadioTrackId::Three,  // 2
    RadioTrackId::Four,   // 3
    RadioTrackId::One,    // 4
    RadioTrackId::Two,    // 5
    RadioTrackId::None,   // 6 == kPoliceColor
};

}  // namespace

RadioTrackId radioFor(std::uint8_t vehicleColor) {
    if (vehicleColor >= kRadioColorCount) {
        return RadioTrackId::None;
    }
    return kStationByColor[vehicleColor];
}

MusicPlan musicPlanFor(bool driving, std::uint8_t vehicleColor, bool wanted,
                       bool busted) {
    if (busted) {
        return MusicPlan{RadioTrackId::None, false};
    }
    if (!driving) {
        return MusicPlan{RadioTrackId::None, wanted};
    }
    return MusicPlan{radioFor(vehicleColor), wanted};
}

bool planChanged(MusicPlan prev, MusicPlan next) {
    return prev.radio != next.radio || prev.siren != next.siren;
}

// --- Slice 4: footsteps, a crash worth hearing, and voice priority ------

std::uint8_t footstepStrideSteps(bool running) {
    return running ? kRunStrideSteps : kWalkStrideSteps;
}

bool footstepDue(std::uint8_t sinceLast, bool moving, bool running) {
    if (!moving) {
        return false;
    }
    return sinceLast >= footstepStrideSteps(running);
}

std::uint8_t ageFootstepClock(std::uint8_t sinceLast, bool fired) {
    if (fired) {
        return 0;
    }
    if (sinceLast == 0xFFu) {
        return sinceLast;   // saturate, do not wrap back to a "just fired" 0
    }
    return static_cast<std::uint8_t>(sinceLast + 1);
}

bool crashIsAudible(std::int32_t speedSubLost) {
    return speedSubLost >= kCrashAudibleSpeedSub;
}

namespace {

/// Row per Cue -- see the header for why Footstep alone is Ambient.
constexpr CuePriority kPriority[static_cast<std::uint8_t>(Cue::Count)] = {
    /* Gunshot          */ CuePriority::Essential,
    /* ShotgunBlast     */ CuePriority::Essential,
    /* DryFire          */ CuePriority::Normal,
    /* WeaponPickup     */ CuePriority::Normal,
    /* WantedUp         */ CuePriority::Essential,
    /* VehicleEnter     */ CuePriority::Normal,
    /* VehicleExit      */ CuePriority::Normal,
    /* Busted           */ CuePriority::Essential,
    /* DistrictChange   */ CuePriority::Normal,
    /* Footstep         */ CuePriority::Ambient,
    /* VehicleCrash     */ CuePriority::Normal,
    /* Doorway          */ CuePriority::Normal,
    /* PlayerHit        */ CuePriority::Essential,
    /* MissionDelivered */ CuePriority::Essential,
    /* MissionFailed    */ CuePriority::Essential,
    /* Roadkill         */ CuePriority::Essential,
    /* PoliceGunshot    */ CuePriority::Essential,
    /* TrafficPass      */ CuePriority::Ambient,
    /* EngineRev        */ CuePriority::Ambient,
    /* BrakeScreech     */ CuePriority::Ambient,
    /* CarHorn          */ CuePriority::Ambient,
    /* CrowdPanic       */ CuePriority::Ambient,
};

}  // namespace

CuePriority priorityOf(Cue cue) {
    const std::uint8_t index = static_cast<std::uint8_t>(cue);
    if (index >= static_cast<std::uint8_t>(Cue::Count)) {
        return CuePriority::Normal;
    }
    return kPriority[index];
}

std::uint8_t voiceCostFor(Cue cue) {
    return (cue == Cue::Gunshot || cue == Cue::ShotgunBlast) ? 2 : 1;
}

bool ambientCueAllowed(std::uint16_t gunfireQuietRemainingMs) {
    return gunfireQuietRemainingMs == 0;
}

// --- Slice 5a: ADR-14's admission gate -----------------------------------

CueVerdict admitCue(Cue cue, const SfxBudget& budget) {
    const CuePriority priority = priorityOf(cue);
    if (priority == CuePriority::Essential) {
        return CueVerdict::Play;
    }
    if (priority == CuePriority::Ambient) {
        if (budget.ambientQuietMs > 0) {
            return CueVerdict::RefusedQuiet;
        }
        if (budget.ambientBusyMs > 0) {
            return CueVerdict::RefusedBusy;
        }
    }
    // Normal, and an Ambient cue that has cleared both of its own gates: the
    // same voice test decides both -- an Ambient cue is a Normal cue with
    // two extra gates in front of it, not a different arithmetic.
    const std::uint16_t projected = static_cast<std::uint16_t>(
        budget.frameVoiceLoad + voiceCostFor(cue));
    if (projected > kSfxVoiceCount) {
        return CueVerdict::RefusedNoVoice;
    }
    return CueVerdict::Play;
}

SfxBudget chargeCue(SfxBudget budget, Cue cue) {
    budget.frameVoiceLoad = static_cast<std::uint8_t>(
        budget.frameVoiceLoad + voiceCostFor(cue));
    const CuePriority priority = priorityOf(cue);
    if (priority == CuePriority::Essential) {
        // Uniform, no exceptions: every Essential cue closes the same
        // window, not only the two weapons that used to set it alone.
        budget.ambientQuietMs = kEssentialQuietMs;
    } else if (priority == CuePriority::Ambient) {
        budget.ambientBusyMs = kAmbientHoldMs;
    }
    return budget;
}

SfxBudget ageBudget(SfxBudget budget, unsigned long dtMs) {
    // The step load describes only THIS step's demands -- it is cleared,
    // never aged, or a busy frame would still be "loaded" a step later with
    // nothing actually playing.
    budget.frameVoiceLoad = 0;
    budget.ambientQuietMs = ageCooldown(budget.ambientQuietMs, dtMs);
    budget.ambientBusyMs = ageCooldown(budget.ambientBusyMs, dtMs);
    return budget;
}

// --- Slice 5b: the traffic layer (ADR-15, ADR-18, ADR-19) ----------------

PedalEvent pedalEventFor(int throttle, int prevThrottle,
                         std::int32_t speedSub) {
    if (prevThrottle <= 0 && throttle > 0) {
        return PedalEvent::Accelerated;
    }
    if (prevThrottle >= 0 && throttle < 0
            && speedSub >= kBrakeAudibleSpeedSub) {
        return PedalEvent::Braked;
    }
    return PedalEvent::None;
}

std::uint8_t ageStallClock(std::uint8_t stalled, bool go) {
    if (go) {
        return 0;
    }
    if (stalled == 0xFFu) {
        return stalled;   // saturate, do not wrap back below the threshold
    }
    return static_cast<std::uint8_t>(stalled + 1);
}

bool hornDue(std::uint8_t stalledSteps) {
    return stalledSteps == kHornStallSteps;
}

}  // namespace top_down_city::audio_cues
