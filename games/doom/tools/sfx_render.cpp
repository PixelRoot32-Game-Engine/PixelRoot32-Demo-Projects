/*
 * Host renderer for the SFX bank gate.
 *
 * Renders every cue through the real ApuCore at the board's 22050 Hz and
 * prints the same 16-segment analysis tools/sfx_design.py takes of the
 * original: RMS envelope, ZCR/2 centroid contour, and the autocorrelation
 * pitch track. tools/sfx_check.py compares the two.
 *
 * THE POINT OF INCLUDING SfxCueEvent.h
 *
 * The event built here is the event the firmware builds: the header is the
 * same translation `DoomAudio.cpp` calls. A harness that rebuilt the event its
 * own way would prove nothing about the bank.
 *
 * --perturb and --stub EXIST TO PROVE THE GATE CAN FAIL
 *
 *   --perturb scales every cue's frequency (and every pitch-table value) by
 *   1.5. The rendered pitch then leaves the 1.5-semitone window on every tonal
 *   cue and the centroid leaves the factor-of-2 window on the rest, so the
 *   check must exit non-zero.
 *   --stub ignores the bank and renders a fixed 440 Hz square for every id.
 *   Duration, class, envelope and centroid all miss, so the check must exit
 *   non-zero.
 *
 * Run it by hand only for debugging; tools/sfx_check.py compiles and invokes
 * it with the flags it needs.
 */
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <pixelroot32/apu/ApuCore.h>
#include <pixelroot32/apu/AudioTypes.h>

#include "doomc/SfxBank.h"
#include "doomc/SfxCueEvent.h"

namespace {

using doom::doomc::SfxCue;
using doom::doomc::sfxBankCue;
using doom::doomc::sfxBankSize;
using doom::doomc::sfxCueEvent;
using doom::doomc::sfxCueEventWithPreset;
using doom::doomc::SfxVoiceScratch;
using pixelroot32::audio::AudioCommand;
using pixelroot32::audio::AudioCommandType;
using pixelroot32::audio::AudioEvent;
using pixelroot32::audio::ApuCore;
using pixelroot32::audio::InstrumentPreset;
using pixelroot32::audio::WaveType;

constexpr int kRate = 22050;
constexpr int kSegments = 16;

double rms(const std::vector<double>& values) {
    if (values.empty()) return 0.0;
    double sum = 0.0;
    for (double value : values) sum += value * value;
    return std::sqrt(sum / static_cast<double>(values.size()));
}

std::vector<double> loudestWindow(const std::vector<double>& values, int window) {
    if (static_cast<int>(values.size()) <= window) return values;
    double energy = 0.0;
    for (int index = 0; index < window; ++index) energy += values[index] * values[index];
    double best = energy;
    int start = 0;
    for (int index = 1; index + window <= static_cast<int>(values.size()); ++index) {
        energy += values[index + window - 1] * values[index + window - 1]
                - values[index - 1] * values[index - 1];
        if (energy > best) {
            best = energy;
            start = index;
        }
    }
    return std::vector<double>(values.begin() + start, values.begin() + start + window);
}

void pitchOf(const std::vector<double>& segment, double* hz, double* confidence) {
    *hz = 0.0;
    *confidence = 0.0;
    const int window = std::max(2, static_cast<int>(0.025 * kRate));
    std::vector<double> sub = loudestWindow(segment, window);
    double mean = 0.0;
    for (double value : sub) mean += value;
    mean /= static_cast<double>(sub.size());
    for (double& value : sub) value -= mean;
    const int lo = std::max(1, static_cast<int>(kRate / 2000.0));
    const int hi = std::min({static_cast<int>(sub.size()) - 3,
                             static_cast<int>(kRate / 40.0),
                             static_cast<int>(sub.size()) / 2});
    if (hi <= lo) return;

    std::vector<double> corr(static_cast<std::size_t>(hi) + 1, 0.0);
    for (int lag = 1; lag <= hi; ++lag) {
        double numerator = 0.0;
        double left = 0.0;
        double right = 0.0;
        for (int index = 0; index + lag < static_cast<int>(sub.size()); ++index) {
            numerator += sub[index] * sub[index + lag];
            left += sub[index] * sub[index];
            right += sub[index + lag] * sub[index + lag];
        }
        const double den = std::sqrt(left * right);
        corr[lag] = den > 0.0 ? numerator / den : 0.0;
    }
    int zero = -1;
    for (int lag = 2; lag <= hi; ++lag) {
        if (corr[lag] < 0.0) {
            zero = lag;
            break;
        }
    }
    if (zero < 0) return;
    int start = std::max(lo, zero + 1);
    double best = 0.0;
    int bestLag = 0;
    for (int lag = start; lag <= hi; ++lag) {
        if (corr[lag] > best) {
            best = corr[lag];
            bestLag = lag;
        }
    }
    if (best < 0.5 || bestLag == 0) return;
    *hz = static_cast<double>(kRate) / bestLag;
    *confidence = best;
}

struct Metrics {
    double duration = 0.0;
    double envelope[kSegments] = {0.0};
    double centroid[kSegments] = {0.0};
    double duty[kSegments] = {0.5};
    double pitch[kSegments] = {0.0};
    double pitchFraction = 0.0;
    double pitchConfidence = 0.0;
    double pitchMean = 0.0;
    double pitchStd = 0.0;
    double centroidMean = 0.0;
    double centroidCv = 0.0;
    double flatness = 0.0;
};

Metrics analyse(const std::vector<int16_t>& pcm) {
    Metrics metrics;
    const int count = static_cast<int>(pcm.size());
    metrics.duration = static_cast<double>(count) / kRate;
    double raw[kSegments] = {0.0};
    for (int seg = 0; seg < kSegments; ++seg) {
        const int start = seg * count / kSegments;
        const int end = (seg + 1) * count / kSegments;
        std::vector<double> part;
        part.reserve(static_cast<std::size_t>(end - start));
        for (int index = start; index < end; ++index) {
            part.push_back(static_cast<double>(pcm[index]) / 32768.0);
        }
        raw[seg] = rms(part);
        int crossings = 0;
        double mean = 0.0;
        for (double value : part) mean += value;
        mean /= part.empty() ? 1.0 : static_cast<double>(part.size());
        int above = 0;
        for (int index = 0; index < static_cast<int>(part.size()); ++index) {
            if (index > 0 && ((part[index - 1] < 0.0) != (part[index] < 0.0))) ++crossings;
            if (part[index] > mean) ++above;
        }
        metrics.centroid[seg] = crossings * kRate
            / (2.0 * (part.empty() ? 1 : static_cast<int>(part.size())));
        metrics.duty[seg] = part.empty() ? 0.5
            : static_cast<double>(above) / static_cast<double>(part.size());
    }
    // Pitch is a separate pass: it is the expensive one and a segment's
    // confidence has to be kept beside its Hz.
    double confidenceSum = 0.0;
    int confidenceCount = 0;
    int accepted = 0;
    for (int seg = 0; seg < kSegments; ++seg) {
        const int start = seg * count / kSegments;
        const int end = (seg + 1) * count / kSegments;
        std::vector<double> part;
        for (int index = start; index < end; ++index) {
            part.push_back(static_cast<double>(pcm[index]) / 32768.0);
        }
        double hz = 0.0;
        double confidence = 0.0;
        pitchOf(part, &hz, &confidence);
        metrics.pitch[seg] = hz;
        if (hz > 0.0) {
            ++accepted;
            confidenceSum += confidence;
            ++confidenceCount;
        }
    }
    double peak = 0.0;
    for (double value : raw) peak = std::max(peak, value);
    if (peak <= 0.0) peak = 1.0;
    double weight = 0.0;
    double centroidMean = 0.0;
    for (int seg = 0; seg < kSegments; ++seg) {
        metrics.envelope[seg] = raw[seg] / peak;
        weight += metrics.envelope[seg];
        centroidMean += metrics.centroid[seg] * metrics.envelope[seg];
    }
    if (weight <= 0.0) weight = 1.0;
    metrics.centroidMean = centroidMean / weight;
    double variance = 0.0;
    for (int seg = 0; seg < kSegments; ++seg) {
        const double delta = metrics.centroid[seg] - metrics.centroidMean;
        variance += metrics.envelope[seg] * delta * delta;
    }
    variance /= weight;
    metrics.centroidCv = metrics.centroidMean > 0.0
        ? std::sqrt(variance) / metrics.centroidMean : 0.0;
    double flat = 0.0;
    double pitchSum = 0.0;
    for (int seg = 0; seg < kSegments; ++seg) {
        flat += metrics.envelope[seg];
        if (metrics.pitch[seg] > 0.0) pitchSum += metrics.pitch[seg];
    }
    metrics.flatness = flat / kSegments;
    metrics.pitchFraction = static_cast<double>(accepted) / kSegments;
    metrics.pitchConfidence = confidenceCount > 0
        ? confidenceSum / confidenceCount : 0.0;
    metrics.pitchMean = accepted > 0 ? pitchSum / accepted : 0.0;
    double pitchVar = 0.0;
    for (int seg = 0; seg < kSegments; ++seg) {
        if (metrics.pitch[seg] > 0.0) {
            const double delta = metrics.pitch[seg] - metrics.pitchMean;
            pitchVar += delta * delta;
        }
    }
    metrics.pitchStd = accepted > 0 ? std::sqrt(pitchVar / accepted) : 0.0;
    return metrics;
}

/// The same rule as tools/sfx_design.py's classify(); keep them in step.
const char* classify(const Metrics& metrics) {
    const double cvLimit = metrics.duration < 0.040 ? 0.60 : 0.45;
    if (metrics.centroidMean >= 1850.0 && metrics.centroidCv <= cvLimit) {
        return "metallic";
    }
    const bool tonal = metrics.pitchFraction >= 0.5 && metrics.pitchConfidence >= 0.85;
    if (!tonal) {
        return "noise";
    }
    const double ratio = metrics.pitchMean > 0.0
        ? metrics.centroidMean / metrics.pitchMean : 0.0;
    const double pitchCv = metrics.pitchMean > 0.0
        ? metrics.pitchStd / metrics.pitchMean : 1.0;
    if (metrics.centroidMean < 1000.0 && metrics.flatness >= 0.55) {
        return "triangle";
    }
    if (pitchCv <= 0.05 && metrics.flatness >= 0.80 && ratio >= 2.5) {
        return "saw";
    }
    return "pulse";
}

void perturb(SfxCue* cue) {
    cue->frequencyHz = std::min(5000.0f, cue->frequencyHz * 1.5f);
    if (cue->sweepEndHz > 0.0f) cue->sweepEndHz = std::min(5000.0f, cue->sweepEndHz * 1.5f);
    for (std::uint8_t index = 0; index < cue->pitchCount; ++index) {
        cue->pitch[index].value = std::min(5000.0f, cue->pitch[index].value * 1.5f);
    }
}

}  // namespace

int main(int argc, char** argv) {
    bool doPerturb = false;
    bool doStub = false;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--perturb") == 0) doPerturb = true;
        if (std::strcmp(argv[index], "--stub") == 0) doStub = true;
    }

    static const InstrumentPreset kStubPreset = [] {
        InstrumentPreset preset{};
        preset.baseVolume = 0.8f;
        preset.duty = 0.5f;
        preset.defaultOctave = 4u;
        preset.defaultDuration = 0.25f;
        preset.attackTime = 0.001f;
        preset.decayTime = 0.0f;
        preset.sustainLevel = 1.0f;
        preset.releaseTime = 0.005f;
        return preset;
    }();

    for (std::int32_t id = 1; id < static_cast<std::int32_t>(sfxBankSize()) + 1; ++id) {
        const SfxCue* source = sfxBankCue(id);
        if (source == nullptr) continue;
        SfxCue cue = *source;
        const InstrumentPreset* preset = doom::doomc::sfxBankPreset(source);
        if (doPerturb) {
            perturb(&cue);
        }
        if (doStub) {
            cue = SfxCue{};
            cue.wave = doom::doomc::SfxWave::Pulse;
            cue.frequencyHz = 440.0f;
            cue.durationSec = 0.25f;
            cue.volume = 0.8f;
            cue.duty = 0.5f;
            cue.attackSec = 0.001f;
            cue.decaySec = 0.0f;
            cue.sustain = 1.0f;
            cue.releaseSec = 0.005f;
            cue.priority = doom::doomc::SfxClass::Normal;
            preset = &kStubPreset;
        }

        SfxVoiceScratch scratch{};
        const AudioEvent event = sfxCueEventWithPreset(cue, preset, 127, 128, scratch);

        ApuCore apu;
        apu.init(kRate);
        AudioCommand command{};
        command.type = AudioCommandType::PLAY_EVENT;
        command.event = event;
        apu.submitCommand(command);
        const int rendered = static_cast<int>(
            std::ceil((cue.durationSec + 0.12f) * static_cast<float>(kRate)));
        std::vector<int16_t> pcm(static_cast<std::size_t>(rendered), 0);
        apu.generateSamples(pcm.data(), rendered);
        const int analysed = std::max(1, static_cast<int>(
            std::lround(cue.durationSec * static_cast<float>(kRate))));
        if (analysed < rendered) pcm.resize(static_cast<std::size_t>(analysed));

        const Metrics metrics = analyse(pcm);
        std::printf("id=%d name=%s dur=%.4f env=", static_cast<int>(id),
                    doom::doomc::sfxBankName(id), metrics.duration);
        for (int seg = 0; seg < kSegments; ++seg) {
            std::printf("%s%.4f", seg ? "," : "", metrics.envelope[seg]);
        }
        std::printf(" cen=");
        for (int seg = 0; seg < kSegments; ++seg) {
            std::printf("%s%.1f", seg ? "," : "", metrics.centroid[seg]);
        }
        std::printf(" pfr=%.3f pconf=%.3f pmean=%.1f class=%s\n",
                    metrics.pitchFraction, metrics.pitchConfidence,
                    metrics.pitchMean, classify(metrics));
    }
    return 0;
}
