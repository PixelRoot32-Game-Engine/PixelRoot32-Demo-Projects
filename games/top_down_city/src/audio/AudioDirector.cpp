#include "audio/AudioDirector.h"

#if PIXELROOT32_ENABLE_AUDIO
#include "audio/CityRadio.h"
#include "audio/CitySfx.h"
#endif

namespace top_down_city {

namespace {
using audio_cues::Cue;
}  // namespace

AudioDirector& AudioDirector::instance() {
    static AudioDirector director;
    return director;
}

#if PIXELROOT32_ENABLE_AUDIO
void AudioDirector::bind(pixelroot32::audio::AudioEngine* engine,
                         pixelroot32::audio::MusicPlayer* music) {
    engine_ = engine;
    music_ = music;
    if (engine_ != nullptr) {
        // The SFX-against-music mix lives on two knobs that are NOT this one:
        // sfxVolume_ (0.65) attenuates every cue's own AudioEvent volume
        // before it reaches the engine (see playCue's `play` lambda), and
        // city_radio::kRadioMix (0.5) attenuates every station note's own --
        // the radio's level, and the siren's exemption from it, live there.
        // This call sets the GLOBAL master, once: MusicPlayer::setMasterVolume
        // forwards straight through to AudioEngine::setMasterVolume
        // (MusicPlayer.cpp:116-118), so going through the music handle is not
        // a "music bus", it is the same master every SFX voice mixes through
        // too. At the 0.55f it held since slice 1, that was quietening every
        // gunshot, pickup and siren-adjacent cue by 45%.
        engine_->setMasterVolume(0.85f);
    }
    if (music_ != nullptr) {
        // Nothing in the sequencer honours a track's "intended" tempo unless
        // something calls this. ADR-11: every station and the siren share this
        // one BPM, set once here, because a per-station tempo would make the
        // siren alternate at a different rate in every car.
        music_->setBPM(city_radio::kRadioBpm);
    }
}
#endif

void AudioDirector::playCue(audio_cues::Cue cue) {
#if PIXELROOT32_ENABLE_AUDIO
    if (!enabled_ || engine_ == nullptr) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(cue);
    if (index >= static_cast<std::size_t>(Cue::Count)) {
        return;
    }
    if (!audio_cues::cueAllowed(cooldownMs_[index])) {
        return;
    }
    // ADR-14: the shared voice-admission gate. Every cue in this switch is
    // Essential except VehicleCrash/Doorway (Normal), so this is where a
    // Normal cue can lose to an already-committed step -- something the old
    // per-cue cooldown alone could never express.
    if (audio_cues::admitCue(cue, budget_) != audio_cues::CueVerdict::Play) {
        return;
    }
    cooldownMs_[index] = audio_cues::cooldownMsFor(cue);
    budget_ = audio_cues::chargeCue(budget_, cue);

    // One layer for most cues, two stacked for the weapons: a crack on top
    // of a body is what makes a gunshot read as an impact rather than as a
    // single tone with a fast decay.
    auto play = [this](pixelroot32::audio::AudioEvent event) {
        event.volume *= sfxVolume_;
        engine_->playEvent(event);
    };

    switch (cue) {
        case Cue::Gunshot:
            play(city_sfx::kPistolCrack);
            play(city_sfx::kPistolBody);
            break;
        case Cue::ShotgunBlast:
            play(city_sfx::kShotgunCrack);
            play(city_sfx::kShotgunBody);
            break;
        case Cue::DryFire:
            play(city_sfx::kDryFire);
            break;
        case Cue::WeaponPickup:
            play(city_sfx::kWeaponPickup);
            break;
        case Cue::VehicleEnter:
            play(city_sfx::kVehicleEnter);
            break;
        case Cue::VehicleExit:
            play(city_sfx::kVehicleExit);
            break;
        case Cue::Busted:
            play(city_sfx::kBusted);
            break;
        case Cue::DistrictChange:
            play(city_sfx::kDistrictChange);
            break;
        case Cue::VehicleCrash:
            play(city_sfx::kVehicleCrash);
            break;
        case Cue::Doorway:
            play(city_sfx::kDoorway);
            break;
        case Cue::PlayerHit:
            play(city_sfx::kPlayerHit);
            break;
        case Cue::MissionDelivered:
            play(city_sfx::kMissionDelivered);
            break;
        case Cue::MissionFailed:
            play(city_sfx::kMissionFailed);
            break;
        case Cue::Roadkill:
            play(city_sfx::kRoadkill);
            break;
        case Cue::PoliceGunshot:
            play(city_sfx::kPoliceGunshot);
            break;
        case Cue::EngineRev:
            play(city_sfx::kEngineRev);
            break;
        case Cue::BrakeScreech:
            play(city_sfx::kBrakeScreech);
            break;
        case Cue::CarHorn:
            play(city_sfx::kCarHorn);
            break;
        case Cue::CrowdPanic:
            play(city_sfx::kCrowdPanic);
            break;
        case Cue::WantedUp:
            // Never reached through this path: WantedUp's event is a two-tone
            // sweep whose base frequency is data-driven by the resulting star
            // count, so it is played through playWantedUp below -- which
            // shares this same cooldown row and the same budget_.
            break;
        case Cue::Footstep:
            // Never reached through this path either: Footstep has its own
            // entry point, playFootstep, because it needs the stride clock
            // that no other cue consults.
            break;
        case Cue::TrafficPass:
            // Never reached through this path either: TrafficPass has its
            // own entry point, playTrafficPass, because which of its two
            // events to play is data the switch below cannot see.
            break;
        case Cue::Count:
            break;
    }
#else
    (void)cue;
#endif
}

void AudioDirector::playWantedUp(std::uint8_t stars) {
#if PIXELROOT32_ENABLE_AUDIO
    if (!enabled_ || engine_ == nullptr) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(Cue::WantedUp);
    if (!audio_cues::cueAllowed(cooldownMs_[index])) {
        return;
    }
    // WantedUp is Essential, so this is never a refusal in practice --
    // charging it is what matters: it is what closes the ambient window
    // uniformly, per ADR-14, even on a step with no gunfire in it at all.
    if (audio_cues::admitCue(Cue::WantedUp, budget_)
            != audio_cues::CueVerdict::Play) {
        return;
    }
    cooldownMs_[index] = audio_cues::cooldownMsFor(Cue::WantedUp);
    budget_ = audio_cues::chargeCue(budget_, Cue::WantedUp);

    // kWantedUpHz has one row per real star count (1..kMaxStars), not per
    // raw index -- stars == 0 never reaches here (reportCrime only calls
    // this on a real level-up), so the table needs no row for it.
    std::uint8_t clamped = stars;
    if (clamped < 1) {
        clamped = 1;
    }
    if (clamped > 5) {
        clamped = 5;
    }
    const float baseHz = city_sfx::kWantedUpHz[clamped - 1];

    pixelroot32::audio::AudioEvent event{};
    event.type = pixelroot32::audio::WaveType::PULSE;
    event.frequency = baseHz;
    event.duration = city_sfx::kWantedUpDurationSec;
    event.volume = city_sfx::kWantedUpVolume * sfxVolume_;
    event.duty = city_sfx::kWantedUpDuty;
    // The second tone: a sweep target above the base rather than a second
    // event, so a rising two-tone alert costs one voice, not two.
    event.sweepEndHz = baseHz * city_sfx::kWantedUpSecondToneRatio;
    event.sweepDurationSec = city_sfx::kWantedUpSweepDurationSec;
    engine_->playEvent(event);
#else
    (void)stars;
#endif
}

void AudioDirector::playFootstep(bool running) {
#if PIXELROOT32_ENABLE_AUDIO
    if (!enabled_ || engine_ == nullptr) {
        return;
    }
    // Called only when the caller has already confirmed the sprite moved
    // this step, so `moving` is always true here -- the pure rule function
    // still takes it, because test_a_standing_player_never_takes_a_step
    // exercises that guard directly, without going through a director.
    const bool fired =
        audio_cues::footstepDue(footstepClock_, /*moving=*/true, running);
    footstepClock_ = audio_cues::ageFootstepClock(footstepClock_, fired);
    if (!fired) {
        return;
    }
    // ADR-14: footsteps are the sole Ambient cue today, refused while the
    // shared quiet window is open (any Essential cue closed it -- not only
    // a weapon, per ADR-12's rule generalised) or while the tier's one
    // voice is already held by a still-cooling Ambient cue.
    if (audio_cues::admitCue(Cue::Footstep, budget_)
            != audio_cues::CueVerdict::Play) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(Cue::Footstep);
    if (!audio_cues::cueAllowed(cooldownMs_[index])) {
        return;
    }
    cooldownMs_[index] = audio_cues::cooldownMsFor(Cue::Footstep);
    budget_ = audio_cues::chargeCue(budget_, Cue::Footstep);

    pixelroot32::audio::AudioEvent event =
        running ? city_sfx::kFootstepRun : city_sfx::kFootstepWalk;
    event.volume *= sfxVolume_;
    engine_->playEvent(event);
#else
    (void)running;
#endif
}

void AudioDirector::playTrafficPass(bool high) {
#if PIXELROOT32_ENABLE_AUDIO
    if (!enabled_ || engine_ == nullptr) {
        return;
    }
    const std::size_t index = static_cast<std::size_t>(Cue::TrafficPass);
    if (!audio_cues::cueAllowed(cooldownMs_[index])) {
        return;
    }
    // Ambient, so this is where a passing car is refused while the shared
    // quiet window is open (any Essential cue closed it) or while the
    // tier's one voice is already held -- the same gate every other
    // Ambient cue goes through.
    if (audio_cues::admitCue(Cue::TrafficPass, budget_)
            != audio_cues::CueVerdict::Play) {
        return;
    }
    cooldownMs_[index] = audio_cues::cooldownMsFor(Cue::TrafficPass);
    budget_ = audio_cues::chargeCue(budget_, Cue::TrafficPass);

    pixelroot32::audio::AudioEvent event =
        high ? city_sfx::kTrafficPassHigh : city_sfx::kTrafficPassLow;
    event.volume *= sfxVolume_;
    engine_->playEvent(event);
#else
    (void)high;
#endif
}

void AudioDirector::setMusicPlan(audio_cues::MusicPlan plan) {
    if (!audio_cues::planChanged(plan_, plan)) {
        // Never re-issued while the plan has not moved: `play()` would
        // restart the track from note 0 (ADR-7), and a settled plan is the
        // common case every frame it holds.
        plan_ = plan;
        return;
    }
#if PIXELROOT32_ENABLE_AUDIO
    if (music_ != nullptr) {
        using audio_cues::RadioTrackId;
        if (plan.radio == RadioTrackId::None && !plan.siren) {
            music_->stop();
        } else if (plan.radio == RadioTrackId::None) {
            // Wanted, on foot or in a police car: the siren alone, as its
            // own track rather than a voice of one.
            music_->play(city_radio::SIREN_TRACK);
        } else {
            // ADR-1's double buffer: write the slot that is NOT the one a
            // previous play() may still be dereferencing on the audio
            // thread, then flip. Eight fields copied BY NAME rather than
            // `out = station;` -- ADR-8's enforcement -- and secondVoice is
            // SUBSTITUTED, never read from the station, so a station that
            // ever fills its own secondVoice loses that voice immediately,
            // in every state, rather than silently only while wanted.
            auto& out = composed_[composedSlot_ ^ 1];
            const auto& station = city_radio::stationTrack(plan.radio);
            out.notes = station.notes;
            out.count = station.count;
            out.loop = station.loop;
            out.channelType = station.channelType;
            out.duty = station.duty;
            out.secondVoice =
                plan.siren ? &city_radio::SIREN_TRACK : nullptr;
            out.thirdVoice = station.thirdVoice;
            out.percussion = station.percussion;
            composedSlot_ ^= 1;
            music_->play(out);
        }
    }
#endif
    plan_ = plan;
}

void AudioDirector::update(unsigned long dtMs) {
    for (auto& remaining : cooldownMs_) {
        remaining = audio_cues::ageCooldown(remaining, dtMs);
    }
    budget_ = audio_cues::ageBudget(budget_, dtMs);
}

void AudioDirector::stopMusic() {
#if PIXELROOT32_ENABLE_AUDIO
    if (music_ != nullptr) {
        music_->stop();
    }
#endif
    plan_ = audio_cues::MusicPlan{};
}

void AudioDirector::setSfxVolume(float volume) {
#if PIXELROOT32_ENABLE_AUDIO
    sfxVolume_ = volume;
#else
    (void)volume;
#endif
}

void AudioDirector::resetCooldowns() {
    for (auto& remaining : cooldownMs_) {
        remaining = 0;
    }
    footstepClock_ = 0;
    budget_ = audio_cues::SfxBudget{};
}

std::uint16_t AudioDirector::cooldownRemainingMs(audio_cues::Cue cue) const {
    const std::size_t index = static_cast<std::size_t>(cue);
    if (index >= static_cast<std::size_t>(Cue::Count)) {
        return 0;
    }
    return cooldownMs_[index];
}

}  // namespace top_down_city
