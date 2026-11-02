/*
 * The SFX bank: one placeholder cue per sfxenum_t id, plus the 8 -> 4 voice
 * rule. See SfxBank.h for what this is and why it carries no engine type.
 *
 * WHY THE NUMBERS LOOK LIKE THIS
 *
 * Every row below is a *character* match, not a measurement: a pistol is a
 * short falling pulse, a shotgun a noise burst, a door a long low pulse whose
 * duty wanders, a sight a low triangle. They exist so D6.1 is true today --
 * every shareware id resolves to an event, unknown ids are silent -- and so
 * T46 can replace the numbers id by id without touching the API, the
 * reduction rule or the host test. Nothing here is claimed to sound like
 * DOOM's DS* lump; S16 makes that T46's measurement, not this comment's.
 *
 * A cue's class is the steal rule, not a volume: Reserved is the player's
 * pain, the player's weapon and the boss family (S16 names them); High is an
 * explosion, a door, a lift, a switch and a teleport; Normal is everything
 * else. `sfxVoiceSelect()` explains what Reserved means to a full voice pool.
 *
 * This file is engine-free, APU-free and allocation-free. All the data is
 * `const`, so it lives in flash.
 */
#include "doomc/SfxBank.h"

#include "sounds.h"

namespace doom {
namespace doomc {
namespace {

constexpr std::size_t kCueCount = static_cast<std::size_t>(NUMSFX) - 1u;

static_assert(NUMSFX > 1, "the bank is indexed by the ids above the dummy row");

// --- Cue construction ------------------------------------------------------
//
// The APU's event has no envelope-time field (the Legacy profile applies its
// own), so attack/release are carried for T46's design gate and default here
// to a short transient.

constexpr SfxCue makeCue(SfxWave wave, float frequencyHz, float sweepEndHz,
                         float durationSec, float volume, float duty,
                         std::uint8_t noisePeriod, SfxClass priority) {
    SfxCue cue{};
    cue.wave = wave;
    cue.frequencyHz = frequencyHz;
    cue.sweepEndHz = sweepEndHz;
    cue.durationSec = durationSec;
    cue.volume = volume;
    cue.duty = duty;
    cue.noisePeriod = noisePeriod;
    cue.attackSec = 0.004f;
    cue.releaseSec = 0.04f;
    cue.priority = priority;
    cue.pitchCount = 0u;
    cue.dutyCount = 0u;
    return cue;
}

/// A pulse with a linear sweep from @p f0 to @p f1 (f1 == f0: no sweep).
constexpr SfxCue pulse(float f0, float f1, float dur, float vol, float duty,
                       SfxClass cls) {
    return makeCue(SfxWave::Pulse, f0, f1, dur, vol, duty, 0u, cls);
}

/// A noise burst; @p f0 is the LFSR clock and @p f1 the swept clock.
constexpr SfxCue noise(float f0, float f1, float dur, float vol, SfxClass cls) {
    return makeCue(SfxWave::Noise, f0, f1, dur, vol, 0.5f, 0u, cls);
}

/// A triangle/sine/saw tone.
constexpr SfxCue tone(SfxWave wave, float f0, float f1, float dur, float vol,
                      SfxClass cls) {
    return makeCue(wave, f0, f1, dur, vol, 0.5f, 0u, cls);
}

/// Overrides the cue's pitch track with four breakpoints (the APU's maximum).
/// A pitch envelope supersedes the single sweep, so sweepEndHz is cleared.
constexpr SfxCue withPitch4(SfxCue cue, float t0, float v0, float t1, float v1,
                            float t2, float v2, float t3, float v3) {
    cue.pitch[0] = SfxPoint{t0, v0};
    cue.pitch[1] = SfxPoint{t1, v1};
    cue.pitch[2] = SfxPoint{t2, v2};
    cue.pitch[3] = SfxPoint{t3, v3};
    cue.pitchCount = 4u;
    cue.sweepEndHz = 0.0f;
    return cue;
}

/// Overrides the cue's duty track with three breakpoints (hold semantics, as
/// the APU's dutySteps). For the doors' creak.
constexpr SfxCue withDuty3(SfxCue cue, float t0, float v0, float t1, float v1,
                           float t2, float v2) {
    cue.dutyPoints[0] = SfxPoint{t0, v0};
    cue.dutyPoints[1] = SfxPoint{t1, v1};
    cue.dutyPoints[2] = SfxPoint{t2, v2};
    cue.dutyCount = 3u;
    return cue;
}

// --- The bank --------------------------------------------------------------
//
// One row per sfxenum_t value 1..NUMSFX-1, in enum order. The comment on each
// row is the id and the S_sfx[] name, so the table can be diffed against
// sounds.c line by line.

constexpr SfxCue kCues[kCueCount] = {
    /*   1 pistol */ pulse(1200.0f, 700.0f, 0.09f, 0.85f, 0.25f, SfxClass::Reserved),
    /*   2 shotgn */ noise(900.0f, 300.0f, 0.30f, 0.95f, SfxClass::Reserved),
    /*   3 sgcock */ pulse(520.0f, 430.0f, 0.08f, 0.60f, 0.50f, SfxClass::Reserved),
    /*   4 dshtgn */ noise(700.0f, 220.0f, 0.34f, 0.95f, SfxClass::Reserved),
    /*   5 dbopn  */ pulse(300.0f, 260.0f, 0.12f, 0.55f, 0.50f, SfxClass::Reserved),
    /*   6 dbcls  */ pulse(260.0f, 200.0f, 0.10f, 0.55f, 0.50f, SfxClass::Reserved),
    /*   7 dbload */ pulse(220.0f, 180.0f, 0.10f, 0.50f, 0.50f, SfxClass::Reserved),
    /*   8 plasma */ pulse(900.0f, 1500.0f, 0.10f, 0.70f, 0.25f, SfxClass::Reserved),
    /*   9 bfg    */ withPitch4(tone(SfxWave::Triangle, 160.0f, 0.0f, 0.55f, 0.90f,
                                    SfxClass::Reserved),
                               0.0f, 180.0f, 0.12f, 130.0f, 0.30f, 80.0f, 0.50f, 55.0f),
    /*  10 sawup  */ pulse(180.0f, 320.0f, 0.25f, 0.65f, 0.50f, SfxClass::Reserved),
    /*  11 sawidl */ tone(SfxWave::Triangle, 85.0f, 0.0f, 0.30f, 0.50f, SfxClass::Reserved),
    /*  12 sawful */ pulse(320.0f, 250.0f, 0.20f, 0.65f, 0.50f, SfxClass::Reserved),
    /*  13 sawhit */ noise(650.0f, 200.0f, 0.10f, 0.75f, SfxClass::Reserved),
    /*  14 rlaunc */ noise(600.0f, 240.0f, 0.25f, 0.85f, SfxClass::Reserved),
    /*  15 rxplod */ withPitch4(noise(300.0f, 0.0f, 0.45f, 0.90f, SfxClass::High),
                               0.0f, 300.0f, 0.08f, 190.0f, 0.22f, 110.0f, 0.45f, 60.0f),
    /*  16 firsht */ noise(500.0f, 300.0f, 0.25f, 0.65f, SfxClass::Normal),
    /*  17 firxpl */ noise(400.0f, 120.0f, 0.28f, 0.75f, SfxClass::High),
    /*  18 pstart */ pulse(140.0f, 180.0f, 0.20f, 0.50f, 0.50f, SfxClass::High),
    /*  19 pstop  */ pulse(180.0f, 130.0f, 0.20f, 0.50f, 0.50f, SfxClass::High),
    /*  20 doropn */ withDuty3(pulse(95.0f, 150.0f, 0.65f, 0.55f, 0.50f, SfxClass::High),
                               0.0f, 0.50f, 0.20f, 0.25f, 0.45f, 0.50f),
    /*  21 dorcls */ withDuty3(pulse(150.0f, 85.0f, 0.55f, 0.55f, 0.50f, SfxClass::High),
                               0.0f, 0.50f, 0.20f, 0.20f, 0.45f, 0.50f),
    /*  22 stnmov */ noise(140.0f, 140.0f, 0.35f, 0.40f, SfxClass::High),
    /*  23 swtchn */ pulse(700.0f, 500.0f, 0.06f, 0.65f, 0.50f, SfxClass::High),
    /*  24 swtchx */ pulse(500.0f, 340.0f, 0.08f, 0.65f, 0.50f, SfxClass::High),
    /*  25 plpain */ pulse(420.0f, 240.0f, 0.28f, 0.90f, 0.50f, SfxClass::Reserved),
    /*  26 dmpain */ pulse(300.0f, 180.0f, 0.25f, 0.75f, 0.50f, SfxClass::Normal),
    /*  27 popain */ pulse(330.0f, 200.0f, 0.22f, 0.75f, 0.50f, SfxClass::Normal),
    /*  28 vipain */ pulse(360.0f, 210.0f, 0.22f, 0.75f, 0.50f, SfxClass::Normal),
    /*  29 mnpain */ pulse(280.0f, 170.0f, 0.22f, 0.75f, 0.50f, SfxClass::Normal),
    /*  30 pepain */ pulse(380.0f, 220.0f, 0.22f, 0.75f, 0.50f, SfxClass::Normal),
    /*  31 slop   */ noise(350.0f, 110.0f, 0.30f, 0.75f, SfxClass::Normal),
    /*  32 itemup */ pulse(800.0f, 1250.0f, 0.12f, 0.55f, 0.50f, SfxClass::Normal),
    /*  33 wpnup  */ pulse(600.0f, 950.0f, 0.15f, 0.55f, 0.50f, SfxClass::Normal),
    /*  34 oof    */ pulse(260.0f, 150.0f, 0.20f, 0.85f, 0.50f, SfxClass::Normal),
    /*  35 telept */ withPitch4(tone(SfxWave::Sine, 420.0f, 0.0f, 0.35f, 0.65f,
                                    SfxClass::Normal),
                               0.0f, 420.0f, 0.10f, 700.0f, 0.22f, 1100.0f, 0.35f, 1600.0f),
    /*  36 posit1 */ pulse(240.0f, 160.0f, 0.30f, 0.65f, 0.50f, SfxClass::Normal),
    /*  37 posit2 */ pulse(250.0f, 170.0f, 0.30f, 0.65f, 0.50f, SfxClass::Normal),
    /*  38 posit3 */ pulse(260.0f, 175.0f, 0.30f, 0.65f, 0.50f, SfxClass::Normal),
    /*  39 bgsit1 */ tone(SfxWave::Triangle, 190.0f, 130.0f, 0.40f, 0.75f, SfxClass::Normal),
    /*  40 bgsit2 */ tone(SfxWave::Triangle, 175.0f, 120.0f, 0.42f, 0.75f, SfxClass::Normal),
    /*  41 sgtsit */ pulse(300.0f, 200.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  42 cacsit */ noise(220.0f, 140.0f, 0.35f, 0.70f, SfxClass::Normal),
    /*  43 brssit */ tone(SfxWave::Triangle, 150.0f, 95.0f, 0.45f, 0.80f, SfxClass::Normal),
    /*  44 cybsit */ pulse(120.0f, 90.0f, 0.50f, 0.70f, 0.50f, SfxClass::Normal),
    /*  45 spisit */ noise(180.0f, 120.0f, 0.40f, 0.65f, SfxClass::Normal),
    /*  46 bspsit */ pulse(200.0f, 140.0f, 0.40f, 0.65f, 0.50f, SfxClass::Normal),
    /*  47 kntsit */ pulse(210.0f, 145.0f, 0.40f, 0.65f, 0.50f, SfxClass::Normal),
    /*  48 vilsit */ noise(240.0f, 150.0f, 0.35f, 0.65f, SfxClass::Normal),
    /*  49 mansit */ pulse(130.0f, 100.0f, 0.50f, 0.75f, 0.50f, SfxClass::Normal),
    /*  50 pesit  */ pulse(240.0f, 160.0f, 0.35f, 0.65f, 0.50f, SfxClass::Normal),
    /*  51 sklatk */ pulse(600.0f, 380.0f, 0.12f, 0.65f, 0.25f, SfxClass::Normal),
    /*  52 sgtatk */ pulse(900.0f, 600.0f, 0.07f, 0.75f, 0.25f, SfxClass::Normal),
    /*  53 skepch */ noise(450.0f, 250.0f, 0.25f, 0.65f, SfxClass::Normal),
    /*  54 vilatk */ noise(500.0f, 300.0f, 0.22f, 0.65f, SfxClass::Normal),
    /*  55 claw   */ noise(260.0f, 140.0f, 0.15f, 0.70f, SfxClass::Normal),
    /*  56 skeswg */ noise(320.0f, 180.0f, 0.15f, 0.65f, SfxClass::Normal),
    /*  57 pldeth */ pulse(380.0f, 130.0f, 0.70f, 0.95f, 0.50f, SfxClass::Reserved),
    /*  58 pdiehi */ pulse(520.0f, 160.0f, 0.60f, 0.95f, 0.50f, SfxClass::Reserved),
    /*  59 podth1 */ pulse(260.0f, 140.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  60 podth2 */ pulse(270.0f, 150.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  61 podth3 */ pulse(280.0f, 160.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  62 bgdth1 */ tone(SfxWave::Triangle, 170.0f, 100.0f, 0.45f, 0.80f, SfxClass::Normal),
    /*  63 bgdth2 */ tone(SfxWave::Triangle, 160.0f, 95.0f, 0.45f, 0.80f, SfxClass::Normal),
    /*  64 sgtdth */ pulse(240.0f, 140.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  65 cacdth */ noise(220.0f, 120.0f, 0.40f, 0.75f, SfxClass::Normal),
    /*  66 skldth */ pulse(300.0f, 180.0f, 0.30f, 0.70f, 0.50f, SfxClass::Normal),
    /*  67 brsdth */ tone(SfxWave::Triangle, 140.0f, 85.0f, 0.50f, 0.80f, SfxClass::Normal),
    /*  68 cybdth */ noise(120.0f, 60.0f, 0.60f, 0.85f, SfxClass::Normal),
    /*  69 spidth */ noise(200.0f, 100.0f, 0.45f, 0.70f, SfxClass::Normal),
    /*  70 bspdth */ pulse(190.0f, 120.0f, 0.45f, 0.70f, 0.50f, SfxClass::Normal),
    /*  71 vildth */ noise(230.0f, 120.0f, 0.40f, 0.70f, SfxClass::Normal),
    /*  72 kntdth */ pulse(200.0f, 130.0f, 0.40f, 0.70f, 0.50f, SfxClass::Normal),
    /*  73 pedth  */ pulse(230.0f, 140.0f, 0.40f, 0.70f, 0.50f, SfxClass::Normal),
    /*  74 skedth */ pulse(250.0f, 160.0f, 0.35f, 0.70f, 0.50f, SfxClass::Normal),
    /*  75 posact */ pulse(220.0f, 190.0f, 0.18f, 0.55f, 0.50f, SfxClass::Normal),
    /*  76 bgact  */ tone(SfxWave::Triangle, 170.0f, 120.0f, 0.30f, 0.65f, SfxClass::Normal),
    /*  77 dmact  */ pulse(200.0f, 160.0f, 0.25f, 0.60f, 0.50f, SfxClass::Normal),
    /*  78 bspact */ pulse(210.0f, 170.0f, 0.25f, 0.60f, 0.50f, SfxClass::Normal),
    /*  79 bspwlk */ noise(160.0f, 160.0f, 0.18f, 0.45f, SfxClass::Normal),
    /*  80 vilact */ noise(210.0f, 150.0f, 0.22f, 0.55f, SfxClass::Normal),
    /*  81 noway  */ pulse(300.0f, 250.0f, 0.10f, 0.60f, 0.50f, SfxClass::Normal),
    /*  82 barexp */ withPitch4(noise(260.0f, 0.0f, 0.50f, 0.95f, SfxClass::High),
                               0.0f, 260.0f, 0.08f, 170.0f, 0.24f, 95.0f, 0.50f, 55.0f),
    /*  83 punch  */ noise(200.0f, 110.0f, 0.08f, 0.80f, SfxClass::Reserved),
    /*  84 hoof   */ noise(140.0f, 90.0f, 0.10f, 0.55f, SfxClass::Normal),
    /*  85 metal  */ pulse(900.0f, 700.0f, 0.05f, 0.55f, 0.25f, SfxClass::Normal),
    /*  86 chgun  */ pulse(1100.0f, 650.0f, 0.07f, 0.80f, 0.25f, SfxClass::Reserved),
    /*  87 tink   */ tone(SfxWave::Sine, 1800.0f, 0.0f, 0.05f, 0.50f, SfxClass::Normal),
    /*  88 bdopn  */ pulse(120.0f, 100.0f, 0.50f, 0.55f, 0.50f, SfxClass::High),
    /*  89 bdcls  */ pulse(105.0f, 75.0f, 0.50f, 0.55f, 0.50f, SfxClass::High),
    /*  90 itmbk  */ pulse(900.0f, 1250.0f, 0.10f, 0.55f, 0.50f, SfxClass::Normal),
    /*  91 flame  */ noise(320.0f, 220.0f, 0.18f, 0.60f, SfxClass::Normal),
    /*  92 flamst */ noise(260.0f, 360.0f, 0.30f, 0.60f, SfxClass::Normal),
    /*  93 getpow */ pulse(600.0f, 1400.0f, 0.40f, 0.80f, 0.50f, SfxClass::Normal),
    /*  94 bospit */ pulse(120.0f, 80.0f, 0.50f, 0.85f, 0.50f, SfxClass::Reserved),
    /*  95 boscub */ noise(150.0f, 90.0f, 0.60f, 0.85f, SfxClass::Reserved),
    /*  96 bossit */ tone(SfxWave::Triangle, 110.0f, 65.0f, 0.60f, 0.90f, SfxClass::Reserved),
    /*  97 bospn  */ pulse(140.0f, 90.0f, 0.35f, 0.85f, 0.50f, SfxClass::Reserved),
    /*  98 bosdth */ noise(110.0f, 50.0f, 0.80f, 0.90f, SfxClass::Reserved),
    /*  99 manatk */ noise(280.0f, 160.0f, 0.20f, 0.70f, SfxClass::Normal),
    /* 100 mandth */ pulse(180.0f, 100.0f, 0.50f, 0.75f, 0.50f, SfxClass::Normal),
    /* 101 sssit  */ pulse(500.0f, 320.0f, 0.10f, 0.70f, 0.25f, SfxClass::Normal),
    /* 102 ssdth  */ noise(200.0f, 120.0f, 0.35f, 0.70f, SfxClass::Normal),
    /* 103 keenpn */ pulse(600.0f, 400.0f, 0.10f, 0.70f, 0.50f, SfxClass::Normal),
    /* 104 keendt */ pulse(300.0f, 180.0f, 0.30f, 0.70f, 0.50f, SfxClass::Normal),
    /* 105 skeact */ pulse(340.0f, 240.0f, 0.12f, 0.60f, 0.50f, SfxClass::Normal),
    /* 106 skesit */ pulse(450.0f, 300.0f, 0.15f, 0.65f, 0.50f, SfxClass::Normal),
    /* 107 skeatk */ pulse(700.0f, 480.0f, 0.08f, 0.70f, 0.25f, SfxClass::Normal),
    /* 108 radio  */ noise(700.0f, 700.0f, 0.05f, 0.35f, SfxClass::Normal),
};

static_assert(sizeof(kCues) / sizeof(kCues[0]) == kCueCount,
              "one cue per sfxenum_t id above the dummy row");

}  // namespace

const SfxCue* sfxBankCue(std::int32_t sfxId) {
    if (sfxId < 1 || sfxId >= static_cast<std::int32_t>(NUMSFX)) {
        return nullptr;
    }
    return &kCues[static_cast<std::size_t>(sfxId) - 1u];
}

std::size_t sfxBankSize() { return kCueCount; }

const char* sfxBankName(std::int32_t sfxId) {
    if (sfxId < 0 || sfxId >= static_cast<std::int32_t>(NUMSFX)
        || S_sfx[sfxId].name == nullptr) {
        return "";
    }
    return S_sfx[sfxId].name;
}

std::int32_t sfxVoiceSelect(const SfxClass slots[kSfxVoices]) {
    std::int32_t lowest = -1;

    // An idle voice costs nothing: take the first one.
    for (std::int32_t slot = 0; slot < kSfxVoices; ++slot) {
        if (slots[slot] == SfxClass::Idle) {
            return slot;
        }
    }

    // Every voice is sounding. The least important cue is the victim, and a
    // Reserved one never is: S16's "no high-value cue is stolen" is this
    // `continue`, not a comment in a caller.
    for (std::int32_t slot = 0; slot < kSfxVoices; ++slot) {
        if (slots[slot] == SfxClass::Reserved) {
            continue;
        }
        if (lowest < 0 || slots[slot] < slots[lowest]) {
            lowest = slot;
        }
    }
    return lowest;
}

}  // namespace doomc
}  // namespace doom
