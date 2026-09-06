/*
 * Copyright (c) 2026 PixelRoot32
 * Licensed under the MIT License
 */
#pragma once

#include <core/Scene.h>
#include <graphics/Color.h>
#include <graphics/Renderer.h>

#include <cstddef>
#include <cstdint>

#if !PIXELROOT32_ENABLE_AUDIO
#error "music_sequencer needs -D PIXELROOT32_ENABLE_AUDIO=1 (see lib/platformio.ini)"
#endif

#include <audio/MusicPlayer.h>

#include "assets/SequencerTracks.h"

namespace music_sequencer {

/**
 * @enum Transport
 * @brief The three transport states this demo can be in.
 *
 * MusicPlayer exposes isPlaying() but no isPaused(), and isPlaying() returns
 * false while paused — so "paused" and "stopped" look identical from outside
 * the player. The scene therefore owns the transport state, and every call it
 * makes into MusicPlayer is decided by this enum rather than by polling.
 */
enum class Transport : uint8_t {
    Stopped, ///< Nothing loaded in the sequencer. A is play().
    Playing, ///< A is pause().
    Paused   ///< Position kept. A is resume().
};

/**
 * @class MusicSequencerScene
 * @brief Drives MusicPlayer over two four-track patterns and draws what it does.
 *
 * The screen is the sequencer's score: one lane per track, one block per note,
 * and a playhead sweeping the eight-beat loop. Nothing here is a sprite —
 * every pixel comes from Renderer primitives and the engine's built-in font.
 */
class MusicSequencerScene : public pixelroot32::core::Scene {
public:
    void init() override;
    void update(unsigned long deltaTime) override;
    void draw(pixelroot32::graphics::Renderer& renderer) override;

private:
    /// Button indices, in the order the platform headers pass them to InputConfig.
    static constexpr uint8_t kButtonUp = 0;
    static constexpr uint8_t kButtonDown = 1;
    static constexpr uint8_t kButtonLeft = 2;
    static constexpr uint8_t kButtonRight = 3;
    static constexpr uint8_t kButtonA = 4;
    static constexpr uint8_t kButtonB = 5;

    // --- Tempo range ------------------------------------------------------
    // MusicPlayer::setBPM() already clamps to [30, 300]; this narrower window
    // is a musical choice, so neither pattern turns into a drone or a buzz.
    static constexpr float kMinBPM = 60.0f;
    static constexpr float kMaxBPM = 200.0f;
    static constexpr float kBPMStep = 4.0f;

    /// Master volume. One global setting: MusicPlayer::setMasterVolume()
    /// forwards straight to AudioEngine::setMasterVolume().
    static constexpr float kMasterVolume = 0.7f;

    /// The sequencer's four parallel note streams (MAX_MUSIC_TRACKS).
    static constexpr std::size_t kLaneCount = 4;

    // --- Screen layout (128x128, built-in 5x7 font: 6 px per character) ----
    static constexpr int kMargin = 3;
    static constexpr int kTitleY = 2;
    static constexpr int kPatternY = 13;
    static constexpr int kTempoY = 23;
    static constexpr int kStateY = 33;
    static constexpr int kLaneTop = 46;
    static constexpr int kLanePitch = 12;
    static constexpr int kLaneHeight = 7;
    static constexpr int kLaneX = 34;
    static constexpr int kLaneWidth = 91;
    static constexpr int kHintOneY = 101;
    static constexpr int kHintTwoY = 111;
    static constexpr int kLegendY = 120;

    /// Longest single integration step honoured, so a stall (a breakpoint, the
    /// first frame after init) cannot fling the playhead around the loop.
    static constexpr unsigned long kMaxStepMs = 100;

    void selectPattern(std::size_t index);
    void toggleTransport();
    void stopTransport();
    void nudgeBPM(float delta);
    void advancePlayhead(unsigned long deltaTime);
    void refreshHud();

    /// Total length of a track in beats: the sum of its note durations.
    /// Stacked hits (duration 0.0) contribute nothing, which is the point.
    static float trackBeats(const pixelroot32::audio::MusicTrack& track);

    /// The sub-track feeding sequencer lane @p lane, or nullptr when the
    /// pattern leaves that lane empty.
    const pixelroot32::audio::MusicTrack* laneTrack(std::size_t lane) const;

    void drawLane(pixelroot32::graphics::Renderer& renderer,
                  std::size_t lane,
                  int y) const;

    /// Created once in init() as a function-local static: the AudioEngine
    /// reference it needs only exists after the Engine global is constructed,
    /// and a member would force this scene to be built after it. No heap.
    pixelroot32::audio::MusicPlayer* player_ = nullptr;

    const tracks::Pattern* pattern_ = nullptr;
    std::size_t patternIndex_ = 0;

    Transport transport_ = Transport::Stopped;
    float bpm_ = 120.0f;

    /// Playhead position in beats within the loop, and the loop's own length.
    /// This is the scene's own clock, not the audio thread's: it is re-anchored
    /// to zero on every play() and only ever drives the drawing.
    float playheadBeats_ = 0.0f;
    float loopBeats_ = 1.0f;

    /// Beat number last written into tempoText_. The HUD strings are rebuilt
    /// only when this changes, so snprintf runs on beat boundaries and on
    /// button presses, not on every frame.
    uint8_t beatShown_ = 0;

    char patternText_[24] = {0};
    char tempoText_[24] = {0};
    char stateText_[16] = {0};
    pixelroot32::graphics::Color stateColor_ = pixelroot32::graphics::Color::Gray;
};

} // namespace music_sequencer
