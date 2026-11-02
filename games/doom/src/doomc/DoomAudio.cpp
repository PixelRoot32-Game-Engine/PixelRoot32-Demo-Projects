/*
 * The C <-> C++ sound seam: s_sound.c's I_* half, implemented over the APU.
 *
 * WHAT CROSSES
 *
 * s_sound.c owns DOOM's eight logical channels, its priority/link rules and its
 * distance arithmetic; this file owns the four APU SFX voices (slots 4..7, the
 * APU's SFX subpool) and the reduction between the two counts. Each
 * `doom_sound_start()` returns a handle that is the logical slot the C side
 * holds; the table below remembers which APU voice that slot was given. A cue
 * that cannot be placed is refused with -1 and the C channel frees itself on
 * the next S_UpdateSounds(), which is exactly what a DMX `I_StartSound()`
 * refusal did.
 *
 * THE 8 -> 4 RULE IS NOT HERE
 *
 * `sfxVoiceSelect()` (SfxBank.cpp) decides whether a newcomer takes an idle
 * voice, steals the least important sounding one, or is refused; this file
 * carries out that decision and counts it. A Reserved cue -- the player's
 * pain, the player's weapon, a boss -- is never returned as a victim, so
 * `steals` can never count one.
 *
 * LIFETIME WITHOUT A VOICE QUERY
 *
 * Upstream asks `I_SoundIsPlaying (handle)` every tic. The APU exposes no
 * cheap production query for that (its per-voice accessors are test hooks), so
 * the cue's own duration is kept as a 35 Hz tic budget and `doom_sound_tick()`
 * retires it. A one-shot's voice disables itself in the APU after the event's
 * duration; the budget only has to be long enough that the C layer frees the
 * channel after the sound ended, not before.
 *
 * NO ALLOCATION, NO APU IN THE INTERFACE
 *
 * The voice table is static and eight entries; `AudioEvent` lives on the stack
 * for the duration of the submit. Breakpoint tables are per-voice copies
 * because the pitch scale is applied at play time and the cue tables are
 * shared `const` data.
 *
 * `DOOM_AUDIO` gates the engine half. Without it this file defines the same
 * seven symbols as silent stubs, so lib/doom links in a build that has no
 * sound (DoomAudio.h says which environments those are).
 */
#include "doomc/DoomAudio.h"

// `s_sound.h` is a C header; its DOOM includes use upstream's `= {0}`
// initialisers, which C accepts and C++ -Wextra reports as a missing-field
// warning. Same suppression as test_input.cpp, for the same reason.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#include "s_sound.h"
#include "sounds.h"
#pragma GCC diagnostic pop

#include "doomc/DoomConsole.h"
#include "doomc/SfxBank.h"

#include <cstddef>
#include <cstdio>

#if defined(DOOM_AUDIO)

#include "doomc/SfxCueEvent.h"

#include <audio/AudioEngine.h>

namespace {

using doom::doomc::DoomAudioStats;
using doom::doomc::DoomAudioTelemetry;
using doom::doomc::DoomAudioTelemetrySource;
using doom::doomc::SfxClass;
using doom::doomc::SfxCue;
using doom::doomc::SfxVoiceScratch;
using doom::doomc::kSfxVoices;
using pixelroot32::audio::AudioCommand;
using pixelroot32::audio::AudioCommandType;
using pixelroot32::audio::AudioEngine;
using pixelroot32::audio::AudioEvent;

/// ApuCore::SFX_VOICE_BASE: the first slot a PLAY_EVENT may use. Written as a
/// literal because the APU's constants live in the synthesis library and this
/// file already depends on its event types; the host test pins the 4..7 range
/// through `doomAudioVoiceCount()`.
constexpr std::int32_t kApuSfxBase = 4;

/// DOOM's simulation rate: the cue's duration is kept as this many tics.
constexpr float kTicsPerSecond = 35.0f;

/// The docked SFX voices, one logical slot per S_NUMCHANNELS channel.
constexpr int kLogicalSlots = S_NUMCHANNELS;

/// How many successful plays print a `SHOW,audio,play` line in the diagnostic
/// build: the twelve-cue script's own count, so its whole script is visible
/// (the brief asked for ten; printing all twelve shows the last two too).
constexpr std::uint32_t kPlayLogLimit = 12u;

/// One logical slot: which APU voice it owns, and for how long.
struct Voice {
    std::int32_t handle;  ///< 0 when free; matches what the C side holds
    std::int32_t sfxId;
    std::int32_t slot;    ///< 0..3 in the SFX subpool, -1 when it owns none
    std::uint16_t ticsLeft;
    bool looping;
    SfxClass cls;
    float frequency;
    float sweepEnd;
    /// Per-play breakpoint copies: the pitch scale is applied here, and the
    /// event keeps pointers into this entry.
    SfxVoiceScratch scratch;
};

AudioEngine* gEngine = nullptr;
Voice gVoices[kLogicalSlots] = {};
std::int32_t gNextHandle = 1;
DoomAudioStats gStats = {0u, 0u, 0u, 0u, -1};
DoomAudioTelemetrySource gTelemetrySource = nullptr;
void* gTelemetryUser = nullptr;
std::uint32_t gPlayLogCount = 0u;

Voice* voiceForHandle(std::int32_t handle) {
    if (handle <= 0) {
        return nullptr;
    }
    for (int index = 0; index < kLogicalSlots; ++index) {
        if (gVoices[index].handle == handle) {
            return &gVoices[index];
        }
    }
    return nullptr;
}

Voice* freeVoice() {
    for (int index = 0; index < kLogicalSlots; ++index) {
        if (gVoices[index].handle == 0) {
            return &gVoices[index];
        }
    }
    return nullptr;
}

Voice* voiceOnSlot(std::int32_t slot) {
    for (int index = 0; index < kLogicalSlots; ++index) {
        if (gVoices[index].slot == slot && gVoices[index].handle != 0) {
            return &gVoices[index];
        }
    }
    return nullptr;
}

void submitStop(std::int32_t slot) {
    if (gEngine == nullptr || slot < 0) {
        return;
    }
    AudioCommand command{};
    command.type = AudioCommandType::STOP_CHANNEL;
    command.channelIndex = static_cast<std::uint8_t>(kApuSfxBase + slot);
    gEngine->submitCommand(command);
}

std::uint16_t ticsFor(const SfxCue& cue) {
    const float tics = cue.durationSec * kTicsPerSecond;
    const std::int32_t rounded = static_cast<std::int32_t>(tics + 0.5f);
    return static_cast<std::uint16_t>(rounded < 1 ? 1 : rounded);
}

#if defined(DOOM_AUDIO_SCRIPT)
void logPlay(std::int32_t sfxId, std::int32_t slot, std::int32_t volume,
             std::int32_t pitch, float durationSec) {
    if (gPlayLogCount >= kPlayLogLimit) {
        return;
    }
    ++gPlayLogCount;
    char line[160];
    std::snprintf(line, sizeof line,
                  "SHOW,audio,play,id=%d,name=%s,voice=%d,vol=%d,pitch=%d,dur_ms=%d\n",
                  static_cast<int>(sfxId), doom::doomc::sfxBankName(sfxId),
                  static_cast<int>(slot), static_cast<int>(volume),
                  static_cast<int>(pitch),
                  static_cast<int>(durationSec * 1000.0f + 0.5f));
    doom::doomc::writeLine(line);
}
#else
void logPlay(std::int32_t, std::int32_t, std::int32_t, std::int32_t, float) {}
#endif

}  // namespace

extern "C" int32_t doom_sound_start(int32_t sfx_id, int32_t volume, int32_t pitch,
                                    const r_mobj_t* origin) {
    // Identity only, and possibly an S_SectorOrigin() token: never dereference.
    (void)origin;

    const SfxCue* cue = doom::doomc::sfxBankCue(sfx_id);
    if (cue == nullptr) {
        ++gStats.unknown;
        return -1;
    }
    if (gEngine == nullptr) {
        ++gStats.refused;
        return -1;
    }

    Voice* entry = freeVoice();
    if (entry == nullptr) {
        ++gStats.refused;
        return -1;
    }

    SfxClass classes[kSfxVoices];
    for (int slot = 0; slot < kSfxVoices; ++slot) {
        classes[slot] = SfxClass::Idle;
    }
    for (int index = 0; index < kLogicalSlots; ++index) {
        const Voice& voice = gVoices[index];
        if (voice.handle != 0 && voice.slot >= 0 && voice.slot < kSfxVoices) {
            classes[voice.slot] = voice.cls;
        }
    }

    const std::int32_t slot = doom::doomc::sfxVoiceSelect(classes);
    if (slot < 0) {
        ++gStats.refused;
        return -1;
    }
    if (classes[slot] != SfxClass::Idle) {
        // A steal: stop the victim's voice. Its logical slot stays allocated
        // until the C layer notices on the next S_UpdateSounds(), exactly as a
        // DMX steal did.
        Voice* victim = voiceOnSlot(slot);
        submitStop(slot);
        if (victim != nullptr) {
            victim->slot = -1;
            victim->ticsLeft = 0u;
        }
        ++gStats.steals;
    }

    *entry = Voice{};
    const AudioEvent event =
        doom::doomc::sfxCueEvent(*cue, static_cast<int>(volume),
                                 static_cast<int>(pitch), entry->scratch);
    entry->handle = gNextHandle++;
    if (gNextHandle <= 0) {
        gNextHandle = 1;
    }
    entry->sfxId = sfx_id;
    entry->slot = slot;
    entry->ticsLeft = ticsFor(*cue);
    entry->looping = false;
    entry->cls = cue->priority;
    entry->frequency = event.frequency;
    entry->sweepEnd = event.sweepEndHz;

    gEngine->playEvent(event);

    ++gStats.plays;
    const std::int32_t absoluteSlot = kApuSfxBase + slot;
    if (absoluteSlot > gStats.highestVoice) {
        gStats.highestVoice = absoluteSlot;
    }
    logPlay(sfx_id, absoluteSlot, volume, pitch, cue->durationSec);
    return entry->handle;
}

extern "C" void doom_sound_stop(int32_t handle) {
    Voice* voice = voiceForHandle(handle);
    if (voice == nullptr) {
        return;
    }
    submitStop(voice->slot);
    *voice = Voice{};
}

extern "C" bool doom_sound_playing(int32_t handle) {
    const Voice* voice = voiceForHandle(handle);
    if (voice == nullptr) {
        return false;
    }
    return voice->slot >= 0 && voice->ticsLeft > 0u;
}

extern "C" void doom_sound_update(int32_t handle, int32_t volume, int32_t pitch) {
    Voice* voice = voiceForHandle(handle);
    if (voice == nullptr || gEngine == nullptr || !voice->looping) {
        // A one-shot's distance was applied when it started. The APU has no
        // command that retunes a sounding note without restarting it, and
        // restarting a crack every time the player turns would be worse than
        // the attenuation it buys. T45's bank has no looping cue; T47's music
        // is a separate transport.
        return;
    }

    const SfxCue* cue = doom::doomc::sfxBankCue(voice->sfxId);
    if (cue == nullptr) {
        return;
    }
    const AudioEvent event =
        doom::doomc::sfxCueEvent(*cue, static_cast<int>(volume),
                                 static_cast<int>(pitch), voice->scratch);
    gEngine->playEvent(event);
}

extern "C" void doom_sound_tick(void) {
    for (int index = 0; index < kLogicalSlots; ++index) {
        Voice& voice = gVoices[index];
        if (voice.handle == 0 || voice.slot < 0) {
            continue;
        }
        if (voice.ticsLeft > 0u) {
            --voice.ticsLeft;
        }
        if (voice.ticsLeft == 0u) {
            // The APU voice has ended by itself after the event's duration.
            // Keep the handle so the C channel can free it; only the voice is
            // gone.
            voice.slot = -1;
        }
    }
}

extern "C" bool doom_music_start(int32_t music_id, bool looping) {
    // T47 converts MUS into APU tracks and owns the transport. Until then a
    // request is answered honestly instead of pretending to play.
    (void)music_id;
    (void)looping;
    return false;
}

extern "C" void doom_music_stop(void) {}

extern "C" void doom_music_pause(bool paused) { (void)paused; }

namespace doom {
namespace doomc {

void doomAudioInit(pixelroot32::audio::AudioEngine& engine) {
    gEngine = &engine;
    for (int index = 0; index < kLogicalSlots; ++index) {
        gVoices[index] = Voice{};
    }
    gNextHandle = 1;
    gStats = DoomAudioStats{0u, 0u, 0u, 0u, -1};
    gPlayLogCount = 0u;
}

const DoomAudioStats& doomAudioStats() { return gStats; }

int doomAudioVoiceCount() { return kSfxVoices; }

int doomAudioActiveVoices() {
    int active = 0;
    for (int index = 0; index < kLogicalSlots; ++index) {
        if (gVoices[index].handle != 0 && gVoices[index].slot >= 0) {
            ++active;
        }
    }
    return active;
}

void doomAudioSetTelemetrySource(DoomAudioTelemetrySource source, void* user) {
    gTelemetrySource = source;
    gTelemetryUser = user;
}

bool doomAudioReadTelemetry(DoomAudioTelemetry* out) {
    if (gTelemetrySource == nullptr || out == nullptr) {
        return false;
    }
    return gTelemetrySource(gTelemetryUser, out);
}

}  // namespace doomc
}  // namespace doom

#else /* !DOOM_AUDIO */

/*
 * The silent half: the same seven symbols, so a build without DOOM_AUDIO links
 * lib/doom unchanged, and the counters the scene never asks for answer zero.
 * Nothing here reaches the engine or the APU.
 */

extern "C" int32_t doom_sound_start(int32_t sfx_id, int32_t volume, int32_t pitch,
                                    const r_mobj_t* origin) {
    (void)sfx_id;
    (void)volume;
    (void)pitch;
    (void)origin;
    return -1;
}

extern "C" void doom_sound_stop(int32_t handle) { (void)handle; }

extern "C" bool doom_sound_playing(int32_t handle) {
    (void)handle;
    return false;
}

extern "C" void doom_sound_update(int32_t handle, int32_t volume, int32_t pitch) {
    (void)handle;
    (void)volume;
    (void)pitch;
}

extern "C" void doom_sound_tick(void) {}

extern "C" bool doom_music_start(int32_t music_id, bool looping) {
    (void)music_id;
    (void)looping;
    return false;
}

extern "C" void doom_music_stop(void) {}

extern "C" void doom_music_pause(bool paused) { (void)paused; }

namespace doom {
namespace doomc {

void doomAudioInit(pixelroot32::audio::AudioEngine& engine) { (void)engine; }

const DoomAudioStats& doomAudioStats() {
    static const DoomAudioStats kZero = {0u, 0u, 0u, 0u, -1};
    return kZero;
}

int doomAudioVoiceCount() { return kSfxVoices; }

int doomAudioActiveVoices() { return 0; }

void doomAudioSetTelemetrySource(DoomAudioTelemetrySource source, void* user) {
    (void)source;
    (void)user;
}

bool doomAudioReadTelemetry(DoomAudioTelemetry* out) {
    (void)out;
    return false;
}

}  // namespace doomc
}  // namespace doom

#endif /* DOOM_AUDIO */
