/*
 * Behaviour tests for DOOM's channel layer.
 *
 * Everything here goes through the public C API of lib/doom/s_sound.c. The
 * seven APU calls it makes are a test double at the bottom of this file, so the
 * channel table, its priority steal, the `link` arithmetic, the distance
 * culling, the pitch jitter and the music transport are exercised without an
 * engine, an APU, a board or an audio device.
 *
 * The rules asserted are the layer's contract, not the implementation's own
 * state:
 *
 *   - id 0 and ids past NUMSFX are silent and allocate nothing;
 *   - one sound per origin (a new sound from the same origin stops the old);
 *   - eight logical channels fill before the ninth steals;
 *   - a more important cue (lower S_sfx[] priority) steals a less important
 *     one, and a less important one is refused when every channel is more
 *     important;
 *   - sfx_chgun's link adds its pitch and volume;
 *   - a source past S_CLIPPING_DIST is culled and a near one is full volume;
 *     map 8 substitutes its own volume ladder;
 *   - the pitch jitter is M_Random's sequence, inside 0..255, and itemup/tink
 *     do not draw;
 *   - S_StopSound stops only that origin;
 *   - every id 1..108 has a cue inside the APU's legal ranges;
 *   - the 8 -> 4 selector never names a Reserved cue as a victim, for every
 *     class combination there is;
 *   - the music bank answers each of the nine level ids with its own committed
 *     theme (this environment defines DOOM_FULL_PROFILE) and nullptr elsewhere.
 */
#include <cstdint>
#include <cstring>

#include <unity.h>

// `s_sound.h` pulls p_mobj.h (the origin's type), whose C static inlines use
// upstream's `= {0}`, which C++ -Wextra reports as a missing-initializer
// warning. Same suppression as test_input.cpp, for the same reason.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "s_sound.h"
#include "sounds.h"
#pragma GCC diagnostic pop

#include "doomc/AudioTelemetry.h"
#include "doomc/SfxBank.h"
#include "doomc/SfxCueEvent.h"
#include "doomc/MusicBank.h"

#include "../../test_config.h"

using doom::doomc::SfxClass;
using doom::doomc::SfxCue;
using doom::doomc::sfxBankCue;
using doom::doomc::sfxBankName;
using doom::doomc::sfxBankSize;
using doom::doomc::sfxVoiceSelect;
using doom::doomc::kSfxMaxBreakpoints;
using doom::doomc::kSfxVoices;
using doom::doomc::sfxBankPreset;
using doom::doomc::SfxVoiceScratch;
using doom::doomc::sfxCueEvent;
using doom::doomc::musicBankTrack;
using doom::doomc::DoomAudioStats;
using doom::doomc::DoomAudioTelemetry;
using doom::doomc::kAudioReportLineBytes;
using doom::doomc::formatAudioReportLine;
using pixelroot32::audio::AudioEvent;
using pixelroot32::audio::InstrumentPreset;
using pixelroot32::audio::MusicTrack;
using pixelroot32::audio::Note;
using pixelroot32::audio::WaveType;
using pixelroot32::audio::WaveType;

namespace {

// --- The seam double -------------------------------------------------------
//
// One start is one handle, exactly as DoomAudio hands them out. The tests never
// look inside the channel table: they look at what the C layer asked the seam
// to do, which is the observable behaviour of the interface.

struct Seam {
    std::int32_t starts;
    std::int32_t stops;
    std::int32_t updates;
    std::int32_t ticks;
    std::int32_t musicStarts;
    std::int32_t musicStops;
    std::int32_t musicPauses;
    bool musicStartAnswer;
    std::int32_t lastStartId;
    std::int32_t lastStartVolume;
    std::int32_t lastStartPitch;
    std::int32_t lastStopHandle;
    std::int32_t lastMusicId;
    std::int32_t nextHandle;
    bool playing[64];
};

Seam seam;

void resetSeam() {
    std::memset(&seam, 0, sizeof seam);
    seam.musicStartAnswer = true;
    seam.nextHandle = 1;
    for (int index = 0; index < 64; ++index) {
        seam.playing[index] = true;
    }
}

/// A thing at a position and facing; nothing else is read by the sound layer.
r_mobj_t thingAt(fixed_t x, fixed_t y, angle_t angle) {
    r_mobj_t thing;
    std::memset(&thing, 0, sizeof thing);
    thing.x = x;
    thing.y = y;
    thing.angle = angle;
    return thing;
}

/// The channel a new sound from @p origin would take, without playing one.
///
/// S_getChannel() is upstream's public "which channel would this get", so the
/// tests can observe occupancy through the API instead of counting stops.
int probeChannel(const r_mobj_t* origin, int sfxId) {
    return S_getChannel(origin, &S_sfx[sfxId]);
}

/// Fills all eight channels with @p sfxId from eight distinct origins.
void fillChannels(r_mobj_t things[8], int sfxId) {
    for (int index = 0; index < 8; ++index) {
        things[index] = thingAt(0, 0, 0);
        S_StartSound(&things[index], sfxId);
    }
}

}  // namespace

// --- Unity hooks -----------------------------------------------------------

extern "C" {

int32_t doom_sound_start(int32_t sfx_id, int32_t volume, int32_t pitch,
                         const r_mobj_t* origin) {
    (void)origin;
    seam.starts += 1;
    seam.lastStartId = sfx_id;
    seam.lastStartVolume = volume;
    seam.lastStartPitch = pitch;
    const std::int32_t handle = seam.nextHandle++;
    if (handle > 0 && handle < 64) {
        seam.playing[handle] = true;
    }
    return handle;
}

void doom_sound_stop(int32_t handle) {
    seam.stops += 1;
    seam.lastStopHandle = handle;
    if (handle > 0 && handle < 64) {
        seam.playing[handle] = false;
    }
}

bool doom_sound_playing(int32_t handle) {
    if (handle <= 0 || handle >= 64) {
        return false;
    }
    return seam.playing[handle];
}

void doom_sound_update(int32_t handle, int32_t volume, int32_t pitch) {
    (void)handle;
    (void)volume;
    (void)pitch;
    seam.updates += 1;
}

void doom_sound_tick(void) { seam.ticks += 1; }

bool doom_music_start(int32_t music_id, bool looping) {
    (void)looping;
    seam.musicStarts += 1;
    seam.lastMusicId = music_id;
    return seam.musicStartAnswer;
}

void doom_music_stop(void) { seam.musicStops += 1; }

void doom_music_pause(bool paused) {
    (void)paused;
    seam.musicPauses += 1;
}

}  // extern "C"

void setUp(void) {
    test_setup();
    resetSeam();
    S_Init(S_MAX_VOLUME, S_MAX_VOLUME);
    S_SetGameLevel(1, 1);
    S_SetListener(nullptr);
}

void tearDown() { test_teardown(); }

// --- The table is upstream's ----------------------------------------------

void test_the_enums_numbers_are_upstreams(void) {
    // NUMSFX counts the dummy row: ids 1..108 are real.
    TEST_ASSERT_EQUAL_INT(109, (int)NUMSFX);
    TEST_ASSERT_EQUAL_INT(68, (int)NUMMUSIC);
    TEST_ASSERT_EQUAL_INT(1, (int)sfx_pistol);
    TEST_ASSERT_EQUAL_INT(20, (int)sfx_doropn);
    TEST_ASSERT_EQUAL_INT(22, (int)sfx_stnmov);
    TEST_ASSERT_EQUAL_INT(25, (int)sfx_plpain);
    TEST_ASSERT_EQUAL_INT(86, (int)sfx_chgun);
    TEST_ASSERT_EQUAL_INT(108, (int)sfx_radio);
    TEST_ASSERT_EQUAL_INT(S_NUMCHANNELS, numChannels);
    TEST_ASSERT_EQUAL_INT(8, numChannels);

    TEST_ASSERT_EQUAL_STRING("none", S_sfx[sfx_None].name);
    TEST_ASSERT_EQUAL_STRING("pistol", S_sfx[sfx_pistol].name);
    TEST_ASSERT_EQUAL_STRING("radio", S_sfx[sfx_radio].name);
    TEST_ASSERT_EQUAL_STRING("e1m1", S_music[mus_e1m1].name);

    // The one link, with upstream's pitch and volume.
    TEST_ASSERT_TRUE(S_sfx[sfx_chgun].link == &S_sfx[sfx_pistol]);
    TEST_ASSERT_EQUAL_INT(150, S_sfx[sfx_chgun].pitch);
    TEST_ASSERT_EQUAL_INT(0, S_sfx[sfx_chgun].volume);
    TEST_ASSERT_NULL(S_sfx[sfx_pistol].link);
}

// --- Bogus ids -------------------------------------------------------------

void test_id_zero_and_ids_past_numSFX_are_silent(void) {
    r_mobj_t origin = thingAt(0, 0, 0);

    S_StartSound(nullptr, 0);
    S_StartSound(nullptr, -(int)sfx_pistol);
    S_StartSound(&origin, (int)NUMSFX);
    S_StartSound(&origin, 999);

    // Nothing reached the seam, and no channel was claimed: the first channel
    // is still the next one a real sound would take.
    TEST_ASSERT_EQUAL_INT(0, seam.starts);
    TEST_ASSERT_EQUAL_INT(0, probeChannel(&origin, sfx_pistol));
    S_StopChannel(0);
}

// --- One sound per origin --------------------------------------------------

void test_one_sound_per_origin(void) {
    r_mobj_t first = thingAt(0, 0, 0);
    r_mobj_t second = thingAt(0, 0, 0);
    r_mobj_t third = thingAt(0, 0, 0);

    S_StartSound(&first, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(1, seam.starts);

    // The same origin's new sound stops the old one instead of stacking.
    S_StartSound(&first, sfx_shotgn);
    TEST_ASSERT_EQUAL_INT(2, seam.starts);
    TEST_ASSERT_EQUAL_INT(1, seam.stops);

    // Only one channel is in use for `first`, so the second origin takes
    // channel 1 and a third origin would take 2 -- not 3, which is what a
    // leaked channel from the replaced pistol would have produced.
    S_StartSound(&second, sfx_itemup);
    TEST_ASSERT_EQUAL_INT(3, seam.starts);
    TEST_ASSERT_EQUAL_INT(2, probeChannel(&third, sfx_itemup));
    S_StopChannel(2);
}

// --- The channel table -----------------------------------------------------

void test_eight_channels_fill_before_any_steal(void) {
    r_mobj_t things[9];

    for (int index = 0; index < 8; ++index) {
        things[index] = thingAt(index * 64 * FRACUNIT, 0, 0);
        S_StartSound(&things[index], sfx_pistol);
    }

    TEST_ASSERT_EQUAL_INT(8, seam.starts);
    TEST_ASSERT_EQUAL_INT(0, seam.stops);

    // The ninth has no free channel and must take one by priority.
    things[8] = thingAt(0, 64 * FRACUNIT, 0);
    S_StartSound(&things[8], sfx_pistol);
    TEST_ASSERT_EQUAL_INT(9, seam.starts);
    TEST_ASSERT_EQUAL_INT(1, seam.stops);
}

void test_a_more_important_cue_steals_a_less_important_one(void) {
    r_mobj_t things[9];
    fillChannels(things, sfx_sawidl);  // priority 118: the least important

    TEST_ASSERT_EQUAL_INT(8, seam.starts);
    TEST_ASSERT_EQUAL_INT(0, seam.stops);

    things[8] = thingAt(0, 0, 0);
    S_StartSound(&things[8], sfx_pistol);  // priority 64: more important
    TEST_ASSERT_EQUAL_INT(9, seam.starts);
    TEST_ASSERT_EQUAL_INT(1, seam.stops);
}

void test_a_less_important_cue_is_refused_when_every_channel_is_more_important(void) {
    r_mobj_t things[9];
    fillChannels(things, sfx_pldeth);  // priority 32: the most important class

    things[8] = thingAt(0, 0, 0);
    S_StartSound(&things[8], sfx_sawidl);  // priority 118: no victim
    TEST_ASSERT_EQUAL_INT(8, seam.starts);
    TEST_ASSERT_EQUAL_INT(0, seam.stops);

    // The table is full and the refused cue claimed nothing: a probe that
    // could take a channel still cannot, because all eight are occupied by
    // more important sounds.
    TEST_ASSERT_EQUAL_INT(-1, probeChannel(&things[8], sfx_itemup));
}

// --- The link rule ---------------------------------------------------------

void test_the_chgun_link_adds_its_pitch_and_volume(void) {
    r_mobj_t origin = thingAt(0, 0, 0);

    // A fresh S_Init() just reset M_Random, so the first draw is table[1] = 8:
    // pitch = 150 + 16 - (8 & 31) = 158, and the link's volume delta is +0.
    S_StartSound(&origin, sfx_chgun);
    TEST_ASSERT_EQUAL_INT(sfx_chgun, seam.lastStartId);
    TEST_ASSERT_EQUAL_INT(158, seam.lastStartPitch);
    TEST_ASSERT_EQUAL_INT(S_MAX_VOLUME, seam.lastStartVolume);

    // The same cue without the link would be 128 + 16 - 8 = 136; the link is
    // what made it 150-based.
    S_Init(S_MAX_VOLUME, S_MAX_VOLUME);
    S_StartSound(&origin, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(136, seam.lastStartPitch);
}

// --- Distance --------------------------------------------------------------

void test_distance_culling_and_full_volume(void) {
    r_mobj_t listener = thingAt(0, 0, 0);
    r_mobj_t near = thingAt(100 * FRACUNIT, 0, 0);
    r_mobj_t far = thingAt(2000 * FRACUNIT, 0, 0);

    S_SetListener(&listener);
    S_StartSound(&near, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(1, seam.starts);
    TEST_ASSERT_EQUAL_INT(S_MAX_VOLUME, seam.lastStartVolume);

    S_StartSound(&far, sfx_shotgn);
    TEST_ASSERT_EQUAL_INT(1, seam.starts);  // culled: no voice, no channel

    // Map 8 is upstream's exception: past the clipping distance it still
    // sounds, at the 15 floor of its own ladder.
    S_SetGameLevel(1, 8);
    S_StartSound(&far, sfx_shotgn);
    TEST_ASSERT_EQUAL_INT(2, seam.starts);
    TEST_ASSERT_EQUAL_INT(15, seam.lastStartVolume);
    S_SetGameLevel(1, 1);
}

void test_adjust_sound_params_is_upstreams_rule(void) {
    r_mobj_t listener = thingAt(0, 0, 0);
    r_mobj_t source = thingAt(600 * FRACUNIT, 0, 0);
    int volume = S_MAX_VOLUME;
    int sep = 0;
    int pitch = 128;

    // 600 units is inside the 160..1200 attenuation band: less than full.
    TEST_ASSERT_EQUAL_INT(1, S_AdjustSoundParams(&listener, &source, &volume,
                                                 &sep, &pitch));
    TEST_ASSERT_TRUE(volume > 15);
    TEST_ASSERT_TRUE(volume < S_MAX_VOLUME);
    TEST_ASSERT_TRUE(sep >= 0 && sep <= 256);
    TEST_ASSERT_EQUAL_INT(128, pitch);

    // Directly in front is the centre of the stereo field, up to upstream's
    // own `0xffffffff - angle` wrap: with listener and source angles both 0
    // the fine index is 8191, the last entry of the sine table, whose value
    // differs from 0 by one fine step.
    TEST_ASSERT_TRUE(sep >= 126 && sep <= 130);
}

// --- Pitch jitter ----------------------------------------------------------

void test_the_pitch_jitter_is_m_random_and_draws_where_upstream_draws(void) {
    r_mobj_t origin = thingAt(0, 0, 0);

    // First draw: 8. A saw sound (sfx_sawup..sfx_sawhit) uses 8 - (M & 15).
    S_StartSound(&origin, sfx_sawidl);
    TEST_ASSERT_EQUAL_INT(128 + 8 - (8 & 15), seam.lastStartPitch);

    // Second draw: 109, the general 16 - (M & 31) arm.
    S_StartSound(&origin, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(128 + 16 - (109 & 31), seam.lastStartPitch);

    // itemup and tink draw nothing, so the next general sound reads table[3].
    S_StartSound(&origin, sfx_itemup);
    TEST_ASSERT_EQUAL_INT(128, seam.lastStartPitch);
    S_StartSound(&origin, sfx_tink);
    TEST_ASSERT_EQUAL_INT(128, seam.lastStartPitch);
    S_StartSound(&origin, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(128 + 16 - (220 & 31), seam.lastStartPitch);

    // Every id in the game jitters inside upstream's clamp. A fresh sequence
    // makes the walk deterministic; the same origin keeps the channel count at
    // one, so the table never fills.
    S_Init(S_MAX_VOLUME, S_MAX_VOLUME);
    for (int id = 1; id < (int)NUMSFX; ++id) {
        S_StartSound(&origin, id);
        TEST_ASSERT_TRUE(seam.lastStartPitch >= 0);
        TEST_ASSERT_TRUE(seam.lastStartPitch <= 255);
        TEST_ASSERT_EQUAL_INT(id, seam.lastStartId);
    }
}

// --- Stopping --------------------------------------------------------------

void test_stop_sound_stops_only_that_origin(void) {
    r_mobj_t first = thingAt(0, 0, 0);
    r_mobj_t second = thingAt(0, 0, 0);

    S_StartSound(&first, sfx_pistol);
    const std::int32_t firstHandle = seam.nextHandle - 1;
    S_StartSound(&second, sfx_pistol);
    const std::int32_t secondHandle = seam.nextHandle - 1;
    TEST_ASSERT_EQUAL_INT(2, seam.starts);

    S_StopSound(&first);
    TEST_ASSERT_EQUAL_INT(1, seam.stops);
    TEST_ASSERT_EQUAL_INT(firstHandle, seam.lastStopHandle);
    TEST_ASSERT_TRUE(doom_sound_playing(secondHandle));
    TEST_ASSERT_FALSE(doom_sound_playing(firstHandle));

    // The first origin is free again, the second is not.
    TEST_ASSERT_EQUAL_INT(0, probeChannel(&first, sfx_itemup));
    S_StopChannel(0);
}

// --- Music -----------------------------------------------------------------

void test_start_kills_sounds_and_asks_for_the_levels_music(void) {
    r_mobj_t things[8];
    fillChannels(things, sfx_pistol);

    S_Start();
    TEST_ASSERT_EQUAL_INT(8, seam.stops);
    TEST_ASSERT_EQUAL_INT(1, seam.musicStarts);
    TEST_ASSERT_EQUAL_INT(mus_e1m1, seam.lastMusicId);
}

void test_pause_resume_and_stop_reach_the_music_transport(void) {
    S_StartMusic(mus_inter);
    TEST_ASSERT_EQUAL_INT(1, seam.musicStarts);
    TEST_ASSERT_EQUAL_INT(mus_inter, seam.lastMusicId);

    S_PauseSound();
    TEST_ASSERT_EQUAL_INT(1, seam.musicPauses);
    S_PauseSound();  // already paused: no second call
    TEST_ASSERT_EQUAL_INT(1, seam.musicPauses);

    S_ResumeSound();
    TEST_ASSERT_EQUAL_INT(2, seam.musicPauses);

    S_StopMusic();
    TEST_ASSERT_EQUAL_INT(1, seam.musicStops);

    // A second S_StartMusic with the transport refusing nothing: the id is
    // requested once and the level is quiet only when the seam says so.
    seam.musicStartAnswer = false;
    S_StartMusic(mus_e1m1);
    TEST_ASSERT_EQUAL_INT(2, seam.musicStarts);
}

// --- The bank --------------------------------------------------------------

void test_every_id_has_a_cue_inside_the_apus_ranges(void) {
    TEST_ASSERT_EQUAL_UINT32((std::uint32_t)NUMSFX - 1u,
                             (std::uint32_t)sfxBankSize());

    TEST_ASSERT_NULL(sfxBankCue(0));
    TEST_ASSERT_NULL(sfxBankCue(-1));
    TEST_ASSERT_NULL(sfxBankCue((std::int32_t)NUMSFX));
    TEST_ASSERT_NULL(sfxBankCue(999));
    // Id 0 is upstream's dummy row and has a name but no cue. A name outside
    // the table answers "", so the log never prints a row that does not exist.
    TEST_ASSERT_EQUAL_STRING("none", sfxBankName(0));
    TEST_ASSERT_EQUAL_STRING("", sfxBankName(-1));
    TEST_ASSERT_EQUAL_STRING("", sfxBankName((std::int32_t)NUMSFX));
    TEST_ASSERT_EQUAL_STRING("pistol", sfxBankName(sfx_pistol));

    for (std::int32_t id = 1; id < (std::int32_t)NUMSFX; ++id) {
        const SfxCue* cue = sfxBankCue(id);
        TEST_ASSERT_NOT_NULL(cue);
        TEST_ASSERT_EQUAL_STRING(S_sfx[id].name, sfxBankName(id));

        // The APU's legal ranges: a pulse needs a frequency to start, every
        // one-shot needs a duration, and volume/duty are 0..1.
        TEST_ASSERT_TRUE(cue->frequencyHz > 0.0f);
        TEST_ASSERT_TRUE(cue->durationSec > 0.0f);
        TEST_ASSERT_TRUE(cue->volume >= 0.0f && cue->volume <= 1.0f);
        TEST_ASSERT_TRUE(cue->duty >= 0.0f && cue->duty <= 1.0f);
        TEST_ASSERT_TRUE(cue->sweepEndHz >= 0.0f);
        TEST_ASSERT_TRUE(cue->attackSec >= 0.0f && cue->attackSec <= 10.0f);
        TEST_ASSERT_TRUE(cue->decaySec >= 0.0f && cue->decaySec <= 10.0f);
        TEST_ASSERT_TRUE(cue->sustain >= 0.0f && cue->sustain <= 1.0f);
        TEST_ASSERT_TRUE(cue->releaseSec >= 0.0f && cue->releaseSec <= 10.0f);
        // The 93-step LFSR is a NOISE property only.
        if (cue->noiseShort) {
            TEST_ASSERT_TRUE(cue->wave == doom::doomc::SfxWave::Noise);
        }

        TEST_ASSERT_TRUE(cue->pitchCount <= (std::uint8_t)kSfxMaxBreakpoints);
        TEST_ASSERT_TRUE(cue->dutyCount <= (std::uint8_t)kSfxMaxBreakpoints);
        for (std::uint8_t point = 0; point < cue->pitchCount; ++point) {
            TEST_ASSERT_TRUE(cue->pitch[point].timeSec >= 0.0f);
            TEST_ASSERT_TRUE(cue->pitch[point].value > 0.0f);
            if (point > 0) {
                TEST_ASSERT_TRUE(cue->pitch[point].timeSec
                                 >= cue->pitch[point - 1].timeSec);
            }
        }
        for (std::uint8_t point = 0; point < cue->dutyCount; ++point) {
            TEST_ASSERT_TRUE(cue->dutyPoints[point].timeSec >= 0.0f);
            TEST_ASSERT_TRUE(cue->dutyPoints[point].value >= 0.0f
                             && cue->dutyPoints[point].value <= 1.0f);
            if (point > 0) {
                TEST_ASSERT_TRUE(cue->dutyPoints[point].timeSec
                                 >= cue->dutyPoints[point - 1].timeSec);
            }
        }
    }
}

void test_every_cues_preset_is_the_envelope_it_was_fitted_to(void) {
    // `AudioEvent` takes ADSR through an InstrumentPreset and nowhere else;
    // if the two tables drifted, the cue would carry numbers the APU never
    // reads. SfxBank.cpp derives one from the other, and this proves it.
    TEST_ASSERT_NULL(sfxBankPreset(nullptr));
    TEST_ASSERT_NULL(sfxBankPreset(sfxBankCue(0)));
    TEST_ASSERT_NULL(sfxBankPreset(sfxBankCue((std::int32_t)NUMSFX)));

    for (std::int32_t id = 1; id < (std::int32_t)NUMSFX; ++id) {
        const SfxCue* cue = sfxBankCue(id);
        TEST_ASSERT_NOT_NULL(cue);
        const InstrumentPreset* preset = sfxBankPreset(cue);
        TEST_ASSERT_NOT_NULL(preset);

        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->volume, preset->baseVolume);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->durationSec, preset->defaultDuration);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->duty, preset->duty);
        TEST_ASSERT_EQUAL_UINT8(cue->noisePeriod, preset->noisePeriod);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->attackSec, preset->attackTime);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->decaySec, preset->decayTime);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->sustain, preset->sustainLevel);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->releaseSec, preset->releaseTime);
        TEST_ASSERT_EQUAL(cue->noiseShort, preset->noiseLfsrShort);
        TEST_ASSERT_EQUAL(cue->noiseShort, preset->noiseShortMode);
        TEST_ASSERT_TRUE(preset->lfoTarget == pixelroot32::audio::LfoTarget::NONE);
    }
}

void test_the_event_translation_is_the_one_the_gate_renders(void) {
    // `SfxCueEvent.h` is shared by DoomAudio.cpp and tools/sfx_render.cpp;
    // this asserts the mapping itself, so a gate that renders the bank is
    // rendering the firmware's event.
    const SfxCue* cue = sfxBankCue(sfx_pistol);
    TEST_ASSERT_NOT_NULL(cue);
    SfxVoiceScratch scratch{};
    const AudioEvent event = sfxCueEvent(*cue, 127, 128, scratch);

    TEST_ASSERT_TRUE(event.type == WaveType::NOISE || event.type == WaveType::PULSE);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, cue->frequencyHz, event.frequency);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->durationSec, event.duration);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->volume, event.volume);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->duty, event.duty);
    TEST_ASSERT_EQUAL_UINT8(cue->noisePeriod, event.noisePeriod);
    TEST_ASSERT_TRUE(event.preset == sfxBankPreset(cue));
    TEST_ASSERT_FALSE(event.loop);
    if (cue->pitchCount >= 2u) {
        TEST_ASSERT_TRUE(event.pitchEnvelope == scratch.pitch);
        TEST_ASSERT_EQUAL_UINT8(cue->pitchCount, event.pitchEnvelopeCount);
        for (std::uint8_t point = 0; point < cue->pitchCount; ++point) {
            TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, cue->pitch[point].timeSec,
                                     scratch.pitch[point].timeSec);
            TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, cue->pitch[point].value,
                                     scratch.pitch[point].value);
        }
    }
    if (cue->dutyCount > 0u) {
        TEST_ASSERT_TRUE(event.dutySteps == scratch.duty);
        TEST_ASSERT_EQUAL_UINT8(cue->dutyCount, event.dutyStepCount);
    }

    // DOOM's pitch is 1/128ths: half pitch is half frequency on the tone and
    // on every pitch-table value the event carries.
    const AudioEvent lower = sfxCueEvent(*cue, 64, 64, scratch);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, cue->frequencyHz * 0.5f, lower.frequency);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, cue->volume * (64.0f / 127.0f), lower.volume);
    if (cue->pitchCount >= 2u) {
        for (std::uint8_t point = 0; point < cue->pitchCount; ++point) {
            TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, cue->pitch[point].value * 0.5f,
                                     scratch.pitch[point].value);
        }
    }
}

// --- The 8 -> 4 reduction --------------------------------------------------

void test_voice_selection_prefers_idle_then_the_least_important(void) {
    const SfxClass allIdle[kSfxVoices] = {SfxClass::Idle, SfxClass::Idle,
                                          SfxClass::Idle, SfxClass::Idle};
    TEST_ASSERT_EQUAL_INT(0, sfxVoiceSelect(allIdle));

    const SfxClass secondIdle[kSfxVoices] = {SfxClass::Normal, SfxClass::Idle,
                                             SfxClass::High, SfxClass::Idle};
    TEST_ASSERT_EQUAL_INT(1, sfxVoiceSelect(secondIdle));

    const SfxClass full[kSfxVoices] = {SfxClass::Reserved, SfxClass::Normal,
                                       SfxClass::High, SfxClass::Reserved};
    TEST_ASSERT_EQUAL_INT(1, sfxVoiceSelect(full));  // Normal before High

    const SfxClass reservedHigh[kSfxVoices] = {SfxClass::Reserved, SfxClass::High,
                                               SfxClass::Reserved, SfxClass::Reserved};
    TEST_ASSERT_EQUAL_INT(1, sfxVoiceSelect(reservedHigh));

    const SfxClass allReserved[kSfxVoices] = {
        SfxClass::Reserved, SfxClass::Reserved, SfxClass::Reserved,
        SfxClass::Reserved};
    TEST_ASSERT_EQUAL_INT(-1, sfxVoiceSelect(allReserved));
}

void test_no_class_combination_steals_a_reserved_voice(void) {
    // Exhaustive over the three cue classes: the selector must either answer
    // "refuse" or name a voice that is not Reserved. This is the "no
    // high-value cue is stolen" rule, asserted rather than trusted.
    const SfxClass classes[3] = {SfxClass::Normal, SfxClass::High,
                                 SfxClass::Reserved};
    for (int a = 0; a < 3; ++a) {
        for (int b = 0; b < 3; ++b) {
            for (int c = 0; c < 3; ++c) {
                for (int d = 0; d < 3; ++d) {
                    const SfxClass slots[kSfxVoices] = {classes[a], classes[b],
                                                        classes[c], classes[d]};
                    const std::int32_t victim = sfxVoiceSelect(slots);
                    if (victim >= 0) {
                        TEST_ASSERT_TRUE(slots[victim] != SfxClass::Reserved);
                    }
                }
            }
        }
    }
}

// --- S_UpdateSounds --------------------------------------------------------

void test_update_sounds_ticks_and_frees_a_finished_channel(void) {
    r_mobj_t listener = thingAt(0, 0, 0);
    r_mobj_t origin = thingAt(100 * FRACUNIT, 0, 0);

    S_SetListener(&listener);
    S_StartSound(&origin, sfx_pistol);
    TEST_ASSERT_EQUAL_INT(1, seam.starts);

    // The seam says the cue ended; the next tick frees the channel and the
    // handle is stopped once.
    seam.playing[1] = false;
    S_UpdateSounds(&listener);
    TEST_ASSERT_EQUAL_INT(1, seam.ticks);
    TEST_ASSERT_EQUAL_INT(1, seam.stops);

    // The channel is free again.
    TEST_ASSERT_EQUAL_INT(0, probeChannel(&origin, sfx_itemup));
    S_StopChannel(0);
}

// --- The music bank --------------------------------------------------------
//
// This environment defines DOOM_FULL_PROFILE, so the bank under test is the
// one the native build links: one committed asset per level theme. The 4 MB
// lookup (every level id -> E1M1) is not compiled here by design; the ESP32
// image and the offline gate are what pin it.

namespace {

/// One level theme as the bank must expose it.
///
/// The counts and the loop length are the generated asset's own numbers: the
/// `MusicTrack` initialisers `tools/mus2tracks.py` emits, and the score grid
/// its header records as `MusicNote`s and stored beats. A theme whose
/// conversion changes moves them, so this table is the host-side record of
/// what each committed `.inc` holds.
struct ThemeExpectation {
    int musicId;
    const char* name;
    std::size_t melody;
    std::size_t second;  // the third melodic voice; 0 when the score has none
    std::size_t bass;
    std::size_t percussion;
    float beats;  // the score's grid, which all four tracks must close on
    bool hasSecond;
};

const ThemeExpectation kThemes[] = {
    {(int)mus_e1m1, "e1m1", 657, 712, 263, 513, 480.0f, true},
    {(int)mus_e1m2, "e1m2", 21, 21, 388, 535, 776.75f, true},
    {(int)mus_e1m3, "e1m3", 166, 0, 2176, 1159, 1360.0f, false},
    {(int)mus_e1m4, "e1m4", 446, 99, 541, 820, 853.25f, true},
    {(int)mus_e1m5, "e1m5", 657, 237, 361, 6, 820.0f, true},
    {(int)mus_e1m6, "e1m6", 385, 65, 122, 452, 420.0f, true},
    {(int)mus_e1m7, "e1m7", 164, 33, 482, 222, 754.25f, true},
    {(int)mus_e1m8, "e1m8", 48, 38, 79, 324, 760.0f, true},
    {(int)mus_e1m9, "e1m9", 536, 293, 293, 882, 686.75f, true},
};

constexpr std::size_t kThemeCount = sizeof kThemes / sizeof kThemes[0];

/// The stored beats a track advances; the offline gate closes every track on
/// the same value, which is what keeps the four loops aligned.
float trackBeats(const MusicTrack& track) {
    float total = 0.0f;
    for (std::size_t index = 0; index < track.count; ++index) {
        total += track.notes[index].duration;
    }
    return total;
}

/// The stored-beat start of a track's first sound: a melodic note or a drum
/// (Rest + preset), skipping plain rests.
float firstSoundBeat(const MusicTrack& track) {
    float start = 0.0f;
    for (std::size_t index = 0; index < track.count; ++index) {
        const pixelroot32::audio::MusicNote& entry = track.notes[index];
        if (entry.note != Note::Rest || entry.preset != nullptr) {
            return start;
        }
        start += entry.duration;
    }
    return start;
}

}  // namespace

void test_the_music_bank_plays_the_nine_level_themes(void) {
    // Nothing outside the nine level ids resolves, so S_ChangeMusic leaves the
    // intermission, the title and the levels past NUMMUSIC quiet instead of
    // holding a request that can never play.
    TEST_ASSERT_NULL(musicBankTrack(mus_None));
    TEST_ASSERT_NULL(musicBankTrack(mus_inter));
    TEST_ASSERT_NULL(musicBankTrack(mus_intro));
    TEST_ASSERT_NULL(musicBankTrack((int)NUMMUSIC));
    TEST_ASSERT_NULL(musicBankTrack(-1));

    const MusicTrack* answered[kThemeCount] = {};

    for (std::size_t index = 0; index < kThemeCount; ++index) {
        const ThemeExpectation& theme = kThemes[index];
        const MusicTrack* track = musicBankTrack(theme.musicId);
        TEST_ASSERT_NOT_NULL(track);
        answered[index] = track;

        // The melody is the id's own asset: its note count and the score grid
        // it closes on identify the lump, not just "some track".
        TEST_ASSERT_EQUAL_UINT32((unsigned)theme.melody, (unsigned)track->count);
        TEST_ASSERT_TRUE(track->loop);
        TEST_ASSERT_EQUAL((int)WaveType::PULSE, (int)track->channelType);
        TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.5f, track->duty);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, theme.beats, trackBeats(*track));

        // The four-voice shape: melody on the main track, the second voice and
        // the bass on the APU's parallel sub-tracks, one percussion track.
        // E1M3's score carries two melodic channels, so its second voice is
        // absent and the bass rides `thirdVoice` alone -- a real conversion
        // shape this table records rather than papers over.
        if (theme.hasSecond) {
            TEST_ASSERT_NOT_NULL(track->secondVoice);
            TEST_ASSERT_EQUAL_UINT32((unsigned)theme.second,
                                     (unsigned)track->secondVoice->count);
            TEST_ASSERT_EQUAL((int)WaveType::PULSE,
                              (int)track->secondVoice->channelType);
            TEST_ASSERT_FLOAT_WITHIN(1.0e-6f, 0.125f, track->secondVoice->duty);
            TEST_ASSERT_TRUE(track->secondVoice->loop);
            TEST_ASSERT_FLOAT_WITHIN(0.01f, theme.beats, trackBeats(*track->secondVoice));
        } else {
            TEST_ASSERT_NULL(track->secondVoice);
        }

        TEST_ASSERT_NOT_NULL(track->thirdVoice);
        TEST_ASSERT_EQUAL_UINT32((unsigned)theme.bass,
                                 (unsigned)track->thirdVoice->count);
        TEST_ASSERT_EQUAL((int)WaveType::TRIANGLE,
                          (int)track->thirdVoice->channelType);
        TEST_ASSERT_TRUE(track->thirdVoice->loop);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, theme.beats, trackBeats(*track->thirdVoice));

        TEST_ASSERT_NOT_NULL(track->percussion);
        TEST_ASSERT_EQUAL_UINT32((unsigned)theme.percussion,
                                 (unsigned)track->percussion->count);
        TEST_ASSERT_EQUAL((int)WaveType::NOISE,
                          (int)track->percussion->channelType);
        TEST_ASSERT_TRUE(track->percussion->loop);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, theme.beats, trackBeats(*track->percussion));

        // A drum is Rest + a duty-0 preset; ApuCore rejects any other shape,
        // and a preset-less Rest is silence on the noise track.
        int drums = 0;
        for (std::size_t note = 0; note < track->percussion->count; ++note) {
            const pixelroot32::audio::MusicNote& entry = track->percussion->notes[note];
            TEST_ASSERT_EQUAL((int)Note::Rest, (int)entry.note);
            if (entry.preset != nullptr) {
                ++drums;
                TEST_ASSERT_EQUAL_FLOAT(0.0f, entry.preset->duty);
            }
        }
        TEST_ASSERT_GREATER_THAN(0, drums);

        // S1: every theme opens within 5 stored beats (1.0 s at 300 BPM). The
        // generic "three busiest" ranking left E1M2 silent for 70.5 beats
        // (14.1 s) and E1M6/E1M7 for 60/68.25 beats, so the arrangement table
        // must keep a channel with material at the score start.
        float firstSound = firstSoundBeat(*track);
        if (track->secondVoice != nullptr) {
            const float second = firstSoundBeat(*track->secondVoice);
            if (second < firstSound) {
                firstSound = second;
            }
        }
        const float bass = firstSoundBeat(*track->thirdVoice);
        if (bass < firstSound) {
            firstSound = bass;
        }
        const float percussion = firstSoundBeat(*track->percussion);
        if (percussion < firstSound) {
            firstSound = percussion;
        }
        TEST_ASSERT_TRUE(firstSound <= 5.0f);
    }

    // One asset per id: a level change must start different music, not restart
    // the same track. Equal pointers would make the nine-theme promise a lie
    // even while every count above still matched.
    for (std::size_t left = 0; left < kThemeCount; ++left) {
        for (std::size_t right = left + 1; right < kThemeCount; ++right) {
            TEST_ASSERT_TRUE(answered[left] != answered[right]);
        }
    }
}

void test_the_music_bank_keeps_the_e1m1_asset(void) {
    // E1M1 is the one asset every build ships, gate or not. Its identity is
    // unchanged from the 4 MB build: 2,145 emitted notes (657 melody) that
    // close on 480.000 stored beats = the original 96 s at 300 BPM.
    const MusicTrack* track = musicBankTrack(mus_e1m1);
    TEST_ASSERT_NOT_NULL(track);
    TEST_ASSERT_NOT_NULL(track->notes);
    TEST_ASSERT_EQUAL_UINT32(657u, (unsigned)track->count);
    TEST_ASSERT_TRUE(track->loop);
    TEST_ASSERT_EQUAL((int)WaveType::PULSE, (int)track->channelType);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, track->duty);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 480.0f, trackBeats(*track));

    TEST_ASSERT_NOT_NULL(track->secondVoice);
    TEST_ASSERT_NOT_NULL(track->thirdVoice);
    TEST_ASSERT_NOT_NULL(track->percussion);
    TEST_ASSERT_EQUAL_UINT32(712u, (unsigned)track->secondVoice->count);
    TEST_ASSERT_EQUAL_UINT32(263u, (unsigned)track->thirdVoice->count);
    TEST_ASSERT_EQUAL_UINT32(513u, (unsigned)track->percussion->count);

    // The one id outside the nine-theme range that used to answer a track
    // still answers nothing: only the level ids changed.
    TEST_ASSERT_NULL(musicBankTrack((int)mus_e1m1 - 1));
    TEST_ASSERT_NULL(musicBankTrack((int)mus_e1m9 + 1));
}

// --- The APU telemetry seam ------------------------------------------------
//
// The two numbers the soak exists to prove are read through a source the
// platform installs. The seam is engine-free, so all of its behaviour is
// observable here: no source answers false and the line keeps its dashes, and
// an installed one puts its numbers on the line and is called once per read.

namespace {

struct TelemetrySource {
    std::uint32_t underruns;
    std::uint32_t worstUs;
    bool answer;
    int calls;
};

bool readTelemetry(void* user, DoomAudioTelemetry* out) {
    TelemetrySource* source = static_cast<TelemetrySource*>(user);
    ++source->calls;
    if (!source->answer) {
        return false;
    }
    out->underruns = source->underruns;
    out->worstUs = source->worstUs;
    return true;
}

/// The counters the line is formatted from; the values are distinct so a
/// swapped field shows up in the expected string.
DoomAudioStats sampleStats() { return DoomAudioStats{12u, 3u, 4u, 5u, 7}; }

}  // namespace

void test_no_telemetry_source_prints_dashes(void) {
    doom::doomc::doomAudioSetTelemetrySource(nullptr, nullptr);
    DoomAudioTelemetry telemetry = {0u, 0u};
    TEST_ASSERT_FALSE(doom::doomc::doomAudioReadTelemetry(&telemetry));

    char line[kAudioReportLineBytes];
    const std::size_t length =
        formatAudioReportLine(line, sizeof line, sampleStats(), 4, 2, false, telemetry);
    TEST_ASSERT_GREATER_THAN(0, (int)length);
    TEST_ASSERT_EQUAL_STRING(
        "SHOW,audio,plays=12,steals=3,refused=4,unknown=5,voices=4,active=2,"
        "underruns=-,worst_us=-\n",
        line);
}

void test_an_installed_source_reaches_the_line(void) {
    TelemetrySource source = {7u, 1234u, true, 0};
    doom::doomc::doomAudioSetTelemetrySource(readTelemetry, &source);

    DoomAudioTelemetry telemetry = {0u, 0u};
    TEST_ASSERT_TRUE(doom::doomc::doomAudioReadTelemetry(&telemetry));
    TEST_ASSERT_EQUAL_UINT32(7u, telemetry.underruns);
    TEST_ASSERT_EQUAL_UINT32(1234u, telemetry.worstUs);
    // Read on request, never polled: one read is one call, so nothing in the
    // seam can spin on the counters.
    TEST_ASSERT_EQUAL_INT(1, source.calls);

    char line[kAudioReportLineBytes];
    TEST_ASSERT_GREATER_THAN(
        0, (int)formatAudioReportLine(line, sizeof line, sampleStats(), 4, 2, true, telemetry));
    TEST_ASSERT_EQUAL_STRING(
        "SHOW,audio,plays=12,steals=3,refused=4,unknown=5,voices=4,active=2,"
        "underruns=7,worst_us=1234\n",
        line);
}

void test_a_source_that_answers_false_keeps_the_dashes(void) {
    TelemetrySource source = {9u, 99u, false, 0};
    doom::doomc::doomAudioSetTelemetrySource(readTelemetry, &source);

    DoomAudioTelemetry telemetry = {0u, 0u};
    TEST_ASSERT_FALSE(doom::doomc::doomAudioReadTelemetry(&telemetry));
    TEST_ASSERT_EQUAL_INT(1, source.calls);

    // A source may answer "not available" -- a backend that lost its counters,
    // say. The line must then keep `-` rather than print the untouched zeros.
    char line[kAudioReportLineBytes];
    TEST_ASSERT_GREATER_THAN(
        0, (int)formatAudioReportLine(line, sizeof line, sampleStats(), 4, 2, false, telemetry));
    TEST_ASSERT_EQUAL_STRING(
        "SHOW,audio,plays=12,steals=3,refused=4,unknown=5,voices=4,active=2,"
        "underruns=-,worst_us=-\n",
        line);

    // Clearing the source unmounts it: the same values are no longer asked for.
    doom::doomc::doomAudioSetTelemetrySource(nullptr, nullptr);
    TEST_ASSERT_FALSE(doom::doomc::doomAudioReadTelemetry(&telemetry));
    TEST_ASSERT_EQUAL_INT(1, source.calls);
}

void test_a_line_that_does_not_fit_is_not_half_written(void) {
    DoomAudioTelemetry telemetry = {1u, 2u};
    char line[8];
    std::memset(line, 'x', sizeof line);
    TEST_ASSERT_EQUAL_UINT32(
        0u, (unsigned)formatAudioReportLine(line, sizeof line, sampleStats(), 4, 2, true, telemetry));
    TEST_ASSERT_EQUAL_CHAR('\0', line[0]);
}

// --- The runner ------------------------------------------------------------
//
// Unity's runner is explicit here, as in the other suites in this project: a
// missing main() links as a Windows GUI subsystem program and fails on
// WinMain, which is how this file first failed.

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();

    RUN_TEST(test_the_enums_numbers_are_upstreams);
    RUN_TEST(test_id_zero_and_ids_past_numSFX_are_silent);
    RUN_TEST(test_one_sound_per_origin);
    RUN_TEST(test_eight_channels_fill_before_any_steal);
    RUN_TEST(test_a_more_important_cue_steals_a_less_important_one);
    RUN_TEST(test_a_less_important_cue_is_refused_when_every_channel_is_more_important);
    RUN_TEST(test_the_chgun_link_adds_its_pitch_and_volume);
    RUN_TEST(test_distance_culling_and_full_volume);
    RUN_TEST(test_adjust_sound_params_is_upstreams_rule);
    RUN_TEST(test_the_pitch_jitter_is_m_random_and_draws_where_upstream_draws);
    RUN_TEST(test_stop_sound_stops_only_that_origin);
    RUN_TEST(test_start_kills_sounds_and_asks_for_the_levels_music);
    RUN_TEST(test_pause_resume_and_stop_reach_the_music_transport);
    RUN_TEST(test_every_id_has_a_cue_inside_the_apus_ranges);
    RUN_TEST(test_every_cues_preset_is_the_envelope_it_was_fitted_to);
    RUN_TEST(test_the_event_translation_is_the_one_the_gate_renders);
    RUN_TEST(test_voice_selection_prefers_idle_then_the_least_important);
    RUN_TEST(test_no_class_combination_steals_a_reserved_voice);
    RUN_TEST(test_update_sounds_ticks_and_frees_a_finished_channel);
    RUN_TEST(test_the_music_bank_plays_the_nine_level_themes);
    RUN_TEST(test_the_music_bank_keeps_the_e1m1_asset);
    RUN_TEST(test_no_telemetry_source_prints_dashes);
    RUN_TEST(test_an_installed_source_reaches_the_line);
    RUN_TEST(test_a_source_that_answers_false_keeps_the_dashes);
    RUN_TEST(test_a_line_that_does_not_fit_is_not_half_written);

    return UNITY_END();
}
