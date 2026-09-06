/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#include "MusicSequencerScene.h"

#include <core/Engine.h>
#include <core/Log.h>

#include <cstdio>

namespace pr32 = pixelroot32;

extern pr32::core::Engine engine;

namespace music_sequencer {

namespace gfx = pr32::graphics;
namespace snd = pr32::audio;
using gfx::Color;

namespace {

/// Lane names, in sequencer track order. Lane 3 is the percussion pointer, so
/// it is labelled for what MusicTrack calls it rather than for what our two
/// patterns happen to put there.
constexpr const char* kLaneLabels[] = {"LEAD", "SUB", "BASS", "DRUM"};

/// One colour per lane, so a block on screen names its voice without a legend.
constexpr Color kLaneColors[] = {Color::Cyan, Color::Green, Color::Yellow, Color::Magenta};

/// Vertical span of the playhead: two pixels above the first lane, one below
/// the last, so the line reads as crossing all four.
constexpr int kPlayheadTop = 44;
constexpr int kPlayheadBottom = 90;

} // namespace

void MusicSequencerScene::init() {
    Scene::init();

    gfx::setPalette(gfx::PaletteType::PR32);

    // MusicPlayer needs an AudioEngine reference, and that reference only
    // becomes valid once the Engine global in the platform header has been
    // constructed and engine.init() has run. A function-local static defers
    // construction to exactly here and still costs no heap: one guard variable
    // and one object in .bss.
    static snd::MusicPlayer musicPlayer(engine.getAudioEngine());
    player_ = &musicPlayer;

    // setMasterVolume() forwards straight through to the AudioEngine, so this
    // is the global mix level, not a per-track one.
    player_->setMasterVolume(kMasterVolume);

    // Start on the first pattern, already running: an audio demo that opens
    // silent is a demo whose first bug report is "no sound".
    selectPattern(0);

    pr32::core::logging::log("MusicSequencerScene: sequencer armed");
}

void MusicSequencerScene::update(unsigned long deltaTime) {
    Scene::update(deltaTime);

    auto& input = engine.getInputManager();

    // isButtonPressed() is edge triggered: one press is one transport action,
    // one tempo step, one pattern change.
    if (input.isButtonPressed(kButtonA)) {
        toggleTransport();
    }
    if (input.isButtonPressed(kButtonB)) {
        stopTransport();
    }
    if (input.isButtonPressed(kButtonLeft)) {
        nudgeBPM(-kBPMStep);
    }
    if (input.isButtonPressed(kButtonRight)) {
        nudgeBPM(kBPMStep);
    }
    if (input.isButtonPressed(kButtonUp)) {
        selectPattern(patternIndex_ == 0 ? tracks::PATTERN_COUNT - 1 : patternIndex_ - 1);
    }
    if (input.isButtonPressed(kButtonDown)) {
        selectPattern((patternIndex_ + 1) % tracks::PATTERN_COUNT);
    }

    advancePlayhead(deltaTime);
}

void MusicSequencerScene::draw(gfx::Renderer& renderer) {
    // Background first, then the base call: Scene::draw() paints the scene's
    // entities, so filling after it would overpaint them.
    renderer.drawFilledRectangle(0, 0, renderer.getLogicalWidth(), renderer.getLogicalHeight(), Color::Black);

    Scene::draw(renderer);

    renderer.drawTextCentered("MUSIC SEQUENCER", kTitleY, Color::White, 1);
    renderer.drawText(patternText_, kMargin, kPatternY, Color::White, 1);
    renderer.drawText(tempoText_, kMargin, kTempoY, Color::Cyan, 1);
    renderer.drawText(stateText_, kMargin, kStateY, stateColor_, 1);

    for (std::size_t lane = 0; lane < kLaneCount; ++lane) {
        drawLane(renderer, lane, kLaneTop + static_cast<int>(lane) * kLanePitch);
    }

    // The playhead is the scene's own estimate of where in the loop the
    // sequencer is, not a reading of the audio clock — see advancePlayhead().
    if (transport_ != Transport::Stopped && loopBeats_ > 0.0f) {
        const int inner = kLaneWidth - 2;
        const int x = kLaneX + 1 + static_cast<int>((playheadBeats_ / loopBeats_) * static_cast<float>(inner));
        renderer.drawLine(x, kPlayheadTop, x, kPlayheadBottom,
                          transport_ == Transport::Playing ? Color::White : Color::Gray);
    }

    renderer.drawText("A PLAY/PAUSE B STOP", kMargin, kHintOneY, Color::Gray, 1);
    renderer.drawText("L/R BPM  U/D PATTERN", kMargin, kHintTwoY, Color::Gray, 1);
    renderer.drawText("0-3 MUSIC 4-7 SFX", kMargin, kLegendY, Color::DarkGreen, 1);
}

void MusicSequencerScene::selectPattern(std::size_t index) {
    if (index >= tracks::PATTERN_COUNT) {
        return;
    }

    patternIndex_ = index;
    pattern_ = &tracks::PATTERNS[index];
    loopBeats_ = trackBeats(*pattern_->track);
    if (loopBeats_ <= 0.0f) {
        loopBeats_ = 1.0f;
    }

    // Each pattern carries the tempo it was written at. BPM is a property of
    // the sequencer, not of the track, so it survives play() and has to be
    // pushed again by hand whenever the pattern changes.
    bpm_ = pattern_->startBPM;
    player_->setBPM(bpm_);

    // play() takes a reference and keeps a bare pointer to it. PATTERNS[] and
    // everything it points at have static storage duration, which is the only
    // reason handing the audio thread this address is safe.
    player_->play(*pattern_->track);

    transport_ = Transport::Playing;
    playheadBeats_ = 0.0f;
    refreshHud();
}

void MusicSequencerScene::toggleTransport() {
    switch (transport_) {
    case Transport::Playing:
        // pause() keeps the sequencer's position; the voices are gated off.
        player_->pause();
        transport_ = Transport::Paused;
        break;
    case Transport::Paused:
        player_->resume();
        transport_ = Transport::Playing;
        break;
    case Transport::Stopped:
        // After stop() the player has no track, so resume() would do nothing:
        // the only way back is a fresh play().
        player_->play(*pattern_->track);
        transport_ = Transport::Playing;
        playheadBeats_ = 0.0f;
        break;
    }
    refreshHud();
}

void MusicSequencerScene::stopTransport() {
    if (transport_ == Transport::Stopped) {
        return;
    }
    player_->stop();
    transport_ = Transport::Stopped;
    playheadBeats_ = 0.0f;
    refreshHud();
}

void MusicSequencerScene::nudgeBPM(float delta) {
    float target = bpm_ + delta;
    if (target < kMinBPM) {
        target = kMinBPM;
    } else if (target > kMaxBPM) {
        target = kMaxBPM;
    }
    if (target == bpm_) {
        return;
    }

    bpm_ = target;
    // Takes effect on the audio thread's next command drain, mid-loop and
    // without restarting the pattern: BPM only resizes the sequencer tick.
    player_->setBPM(bpm_);
    refreshHud();
}

void MusicSequencerScene::advancePlayhead(unsigned long deltaTime) {
    if (transport_ != Transport::Playing) {
        return;
    }

    unsigned long step = deltaTime;
    if (step > kMaxStepMs) {
        step = kMaxStepMs;
    }

    // Beats elapsed = seconds * BPM / 60, scaled by the tempo factor the
    // sequencer applies on top of BPM. This is a render-side estimate re-anchored
    // to zero on every play(): the authoritative clock lives on the audio
    // thread and the engine exposes no beat position to read back.
    playheadBeats_ += (static_cast<float>(step) / 1000.0f) *
                      (bpm_ / 60.0f) * player_->getTempoFactor();

    while (playheadBeats_ >= loopBeats_) {
        playheadBeats_ -= loopBeats_;
    }

    const uint8_t beat = static_cast<uint8_t>(playheadBeats_) + 1u;
    if (beat != beatShown_) {
        refreshHud();
    }
}

void MusicSequencerScene::refreshHud() {
    // snprintf into fixed members: no std::string, no heap, and only on the
    // frames where something on screen actually changed.
    snprintf(patternText_, sizeof(patternText_), "P%u %s",
             static_cast<unsigned>(patternIndex_ + 1),
             pattern_ != nullptr ? pattern_->name : "-");

    beatShown_ = static_cast<uint8_t>(playheadBeats_) + 1u;
    snprintf(tempoText_, sizeof(tempoText_), "BPM %u  BEAT %u/%u",
             static_cast<unsigned>(bpm_ + 0.5f),
             static_cast<unsigned>(beatShown_),
             static_cast<unsigned>(loopBeats_ + 0.5f));

    switch (transport_) {
    case Transport::Playing:
        snprintf(stateText_, sizeof(stateText_), "PLAYING");
        stateColor_ = Color::Green;
        break;
    case Transport::Paused:
        snprintf(stateText_, sizeof(stateText_), "PAUSED");
        stateColor_ = Color::Yellow;
        break;
    case Transport::Stopped:
        snprintf(stateText_, sizeof(stateText_), "STOPPED");
        stateColor_ = Color::Red;
        break;
    }
}

float MusicSequencerScene::trackBeats(const snd::MusicTrack& track) {
    float total = 0.0f;
    for (std::size_t i = 0; i < track.count; ++i) {
        total += track.notes[i].duration;
    }
    return total;
}

const snd::MusicTrack* MusicSequencerScene::laneTrack(std::size_t lane) const {
    if (pattern_ == nullptr) {
        return nullptr;
    }
    const snd::MusicTrack* main = pattern_->track;
    switch (lane) {
    case 0: return main;
    case 1: return main->secondVoice;
    case 2: return main->thirdVoice;
    case 3: return main->percussion;
    default: return nullptr;
    }
}

void MusicSequencerScene::drawLane(gfx::Renderer& renderer, std::size_t lane, int y) const {
    renderer.drawText(kLaneLabels[lane], kMargin, y, Color::White, 1);
    renderer.drawRectangle(kLaneX, y, kLaneWidth, kLaneHeight, Color::Gray);

    const snd::MusicTrack* track = laneTrack(lane);
    if (track == nullptr || track->notes == nullptr || loopBeats_ <= 0.0f) {
        return;
    }

    const int inner = kLaneWidth - 2;
    const int lastX = kLaneX + inner;
    float beat = 0.0f;

    for (std::size_t i = 0; i < track->count; ++i) {
        const snd::MusicNote& note = track->notes[i];

        // The same test the sequencer runs: on a NOISE track, Note::Rest plus a
        // noise preset (duty == 0) is a drum hit; anywhere else a Rest is
        // silence and gets no block.
        const bool percussionHit = track->channelType == snd::WaveType::NOISE &&
                                   note.preset != nullptr && note.preset->duty == 0.0f;
        const bool audible = note.note != snd::Note::Rest || percussionHit;

        if (audible) {
            const int x = kLaneX + 1 +
                          static_cast<int>((beat / loopBeats_) * static_cast<float>(inner));
            // A stacked hit has no length, so it is drawn as a 2 px tick — the
            // narrowest mark that still reads as "something fired here".
            int width = static_cast<int>((note.duration / loopBeats_) * static_cast<float>(inner)) - 1;
            if (width < 2) {
                width = 2;
            }
            if (x + width > lastX) {
                width = lastX - x;
            }
            if (width > 0) {
                renderer.drawFilledRectangle(x, y + 1, width, kLaneHeight - 2, kLaneColors[lane]);
            }
        }

        beat += note.duration;
    }
}

} // namespace music_sequencer
