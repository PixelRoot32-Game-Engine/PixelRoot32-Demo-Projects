#pragma once

#include <cstdint>

#include <audio/AudioMusicTypes.h>
#include <audio/AudioTypes.h>

/**
 * @file PoolSfx.h
 * @brief NES-vocabulary sound effects for the Pool demo.
 *
 * SfxBank-style static API (layers at t=0, sequenced steps with delays), so
 * the director stays a thin cooldown + delay scheduler. Original designs,
 * not transcriptions: pulse blips and noise ticks in the spirit of the
 * 8-bit era. All events are `constexpr`-built from `static` data — the
 * voice holds the pointers, so nothing here may live on the stack.
 */

namespace pool {

namespace a = pixelroot32::audio;

enum class PoolSfx : uint8_t {
    CueStrike,   ///< Cue hits the ball: noise tick + low thock.
    BallClick,   ///< Ball-ball impact: short bright tick (cooldown-guarded).
    CushionThud, ///< Cushion bounce: low thud (cooldown-guarded).
    PocketDrop,  ///< Target falls: descending blip + rattle.
    Scratch,     ///< Cue pocketed: wah down into a buzz.
    Foul,        ///< Order foul without a scratch: short dissonant buzz.
    StageClear,  ///< Rising 6-note arpeggio, sequenced.
    AimTick,     ///< Aim-step tick, in the spirit of the original cursor chirp.
    UiConfirm,   ///< Menu / retry confirm blip.
    PauseToggle, ///< Pause on/off blip.
    Count
};

struct PoolSfxBank {
    struct SequenceStep {
        float delaySec;
        a::AudioEvent event;
    };

    static constexpr unsigned long cooldownMs(PoolSfx id) {
        switch (id) {
            case PoolSfx::BallClick:
                return 70;
            case PoolSfx::CushionThud:
                return 110;
            case PoolSfx::Scratch:
            case PoolSfx::Foul:
                return 250;
            case PoolSfx::AimTick:
                return 120;
            case PoolSfx::UiConfirm:
                return 80;
            case PoolSfx::PauseToggle:
                return 150;
            default:
                return 0;
        }
    }

    static constexpr uint8_t layerCount(PoolSfx id) {
        switch (id) {
            case PoolSfx::CueStrike:
            case PoolSfx::PocketDrop:
            case PoolSfx::Scratch:
            case PoolSfx::Foul:
                return 2;
            case PoolSfx::BallClick:
            case PoolSfx::CushionThud:
            case PoolSfx::AimTick:
            case PoolSfx::UiConfirm:
            case PoolSfx::PauseToggle:
                return 1;
            default:
                return 0;
        }
    }

    static constexpr uint8_t sequenceStepCount(PoolSfx id) {
        switch (id) {
            case PoolSfx::StageClear:
                return 6;
            default:
                return 0;
        }
    }

    static a::AudioEvent layerEvent(PoolSfx id, uint8_t layerIndex) {
        using namespace pixelroot32::audio;
        switch (id) {
            case PoolSfx::CueStrike:
                if (layerIndex == 0) {
                    return {WaveType::NOISE, 2400.0f, 0.04f, 0.50f, 0.0f};
                }
                if (layerIndex == 1) {
                    AudioEvent thock{WaveType::PULSE, 200.0f, 0.08f, 0.55f, 0.5f};
                    thock.sweepEndHz = 110.0f;
                    thock.sweepDurationSec = 0.08f;
                    return thock;
                }
                break;
            case PoolSfx::BallClick:
                if (layerIndex == 0) {
                    return {WaveType::PULSE, 2300.0f, 0.03f, 0.32f, 0.5f};
                }
                break;
            case PoolSfx::CushionThud:
                if (layerIndex == 0) {
                    AudioEvent thud{WaveType::PULSE, 150.0f, 0.09f, 0.50f, 0.5f};
                    thud.sweepEndHz = 85.0f;
                    thud.sweepDurationSec = 0.09f;
                    return thud;
                }
                break;
            case PoolSfx::PocketDrop:
                if (layerIndex == 0) {
                    AudioEvent drop{WaveType::PULSE, 660.0f, 0.12f, 0.50f, 0.5f};
                    drop.sweepEndHz = 180.0f;
                    drop.sweepDurationSec = 0.12f;
                    return drop;
                }
                if (layerIndex == 1) {
                    return {WaveType::NOISE, 1000.0f, 0.07f, 0.35f, 0.0f};
                }
                break;
            case PoolSfx::Scratch:
                if (layerIndex == 0) {
                    AudioEvent wah{WaveType::SAW, 300.0f, 0.35f, 0.50f, 0.5f};
                    wah.sweepEndHz = 90.0f;
                    wah.sweepDurationSec = 0.35f;
                    return wah;
                }
                if (layerIndex == 1) {
                    return {WaveType::PULSE, 150.0f, 0.30f, 0.40f, 0.25f};
                }
                break;
            case PoolSfx::Foul:
                if (layerIndex == 0) {
                    return {WaveType::PULSE, 140.0f, 0.22f, 0.50f, 0.25f};
                }
                if (layerIndex == 1) {
                    return {WaveType::PULSE, 105.0f, 0.22f, 0.50f, 0.25f};
                }
                break;
            case PoolSfx::UiConfirm:
                if (layerIndex == 0) {
                    return {WaveType::PULSE, 880.0f, 0.06f, 0.40f, 0.25f};
                }
                break;
            case PoolSfx::AimTick:
                if (layerIndex == 0) {
                    return {WaveType::PULSE, 1500.0f, 0.03f, 0.25f, 0.5f};
                }
                break;
            case PoolSfx::PauseToggle:
                if (layerIndex == 0) {
                    return {WaveType::PULSE, 520.0f, 0.07f, 0.40f, 0.5f};
                }
                break;
            default:
                break;
        }
        return {WaveType::PULSE, 0.0f, 0.0f, 0.0f, 0.5f};
    }

    static SequenceStep sequenceStep(PoolSfx id, uint8_t stepIndex) {
        using namespace pixelroot32::audio;
        static constexpr float kArp[6] = {
            523.25f, 659.25f, 783.99f, 1046.5f, 1318.5f, 1568.0f,
        };
        if (id == PoolSfx::StageClear && stepIndex < 6) {
            AudioEvent note{WaveType::PULSE, kArp[stepIndex], 0.09f, 0.45f, 0.25f};
            note.preset = &INSTR_PULSE_LEAD;
            return {static_cast<float>(stepIndex) * 0.08f, note};
        }
        return {0.0f, {WaveType::PULSE, 0.0f, 0.0f, 0.0f, 0.5f}};
    }
};

}  // namespace pool
