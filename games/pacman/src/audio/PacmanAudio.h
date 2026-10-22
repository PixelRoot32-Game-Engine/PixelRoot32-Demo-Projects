#pragma once

#include <cstdint>

#include <audio/AudioEngine.h>
#include <audio/MusicPlayer.h>

namespace pacman::audio {

/**
 * @class PacmanAudio
 * @brief Everything the game sounds like: the theme, the sound effects and
 *        the hunters' siren.
 *
 * The class owns the sequencer, so the theme keeps playing for as long as
 * the scene lives, and it keeps the little state the effects need: which of
 * the two "waka" notes comes next, and when the siren is due to sound again.
 *
 * The rules layer never talks to this class. The scene reads what the rules
 * reported and asks for the matching sound, so the game would still play
 * correctly with the whole audio subsystem compiled out.
 */
class PacmanAudio {
public:
    /**
     * @brief Binds the facade to the engine's audio.
     *
     * @param engine The engine whose audio the facade drives.
     */
    explicit PacmanAudio(pixelroot32::audio::AudioEngine& engine);

    /**
     * @brief Starts the jingle that plays while the title screen waits.
     *
     * This is the arcade's attract-mode tune: the same melody as the level
     * theme, at full speed and a comfortable volume.
     */
    void startIntroJingle();

    /**
     * @brief Starts the background music that plays under the level.
     *
     * The arcade's background music is the same tune, slower and quieter, so
     * that it stays out of the way of the sound effects.
     */
    void startBackgroundMusic();

    /**
     * @brief Stops the theme.
     */
    void stopTheme();

    /**
     * @brief Plays the "waka" for a dot.
     *
     * The arcade alternates between two notes, one for each dot, which is
     * what makes the rhythm of the eating audible.
     */
    void playDot();

    /**
     * @brief Plays the sound of picking up a power pellet.
     */
    void playPowerPellet();

    /**
     * @brief Plays the sound of defeating a hunter.
     */
    void playEnemyDefeated();

    /**
     * @brief Plays the sound of losing a life.
     */
    void playLifeLost();

    /**
     * @brief Plays the sound of finishing the level.
     */
    void playLevelCompleted();

    /**
     * @brief Plays the sound of earning an extra life.
     */
    void playExtraLife();

    /**
     * @brief Advances the hunters' siren.
     *
     * @param deltaTimeMs Milliseconds since the last call.
     * @param nearestHunter Index of the hunter closest to the player, or -1
     *        when no hunter is on the board.
     * @param frightened true while the power effect lasts.
     */
    void updateHunterVoice(unsigned long deltaTimeMs, int nearestHunter, bool frightened);

private:
    /// The engine the facade drives.
    pixelroot32::audio::AudioEngine& engine_;

    /// The sequencer that plays the theme.
    pixelroot32::audio::MusicPlayer player_;

    /// Which of the two "waka" notes comes next.
    bool wakaHigh_ = true;

    /// Milliseconds until the siren sounds again.
    unsigned long voiceTimer_ = 0;

    /// The hunter whose pitch the siren is currently using.
    int voiceHunter_ = -1;

    /// Whether the siren is currently the frightened warble.
    bool voiceFrightened_ = false;

    /**
     * @brief Milliseconds between two siren wails while the hunters chase.
     */
    static constexpr unsigned long kHunterVoicePeriodMs = 280;

    /**
     * @brief Milliseconds between two siren wails while the power effect
     *        lasts.
     */
    static constexpr unsigned long kFrightenedVoicePeriodMs = 170;
};

}  // namespace pacman::audio
