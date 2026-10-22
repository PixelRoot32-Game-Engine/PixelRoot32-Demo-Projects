#include "PacmanAudio.h"

#include "PacmanMusic.h"
#include "PacmanSounds.h"

namespace pacman::audio {

PacmanAudio::PacmanAudio(pixelroot32::audio::AudioEngine& engine) : engine_(engine), player_(engine) {}

void PacmanAudio::startIntroJingle() {
    player_.setTempoFactor(1.0f);
    player_.setMasterVolume(kIntroVolume);
    player_.setBPM(kThemeBpm);
    player_.play(themeTrack());
}

void PacmanAudio::startBackgroundMusic() {
    player_.setTempoFactor(kThemeQuietTempoFactor);
    player_.setMasterVolume(kThemeQuietVolume);
    player_.setBPM(kThemeBpm);
    player_.play(themeTrack());
}

void PacmanAudio::stopTheme() {
    player_.stop();
}

void PacmanAudio::playDot() {
    engine_.playEvent(wakaHigh_ ? wakaHigh() : wakaLow());
    wakaHigh_ = !wakaHigh_;
}

void PacmanAudio::playPowerPellet() {
    engine_.playEvent(powerPelletSound());
}

void PacmanAudio::playEnemyDefeated() {
    engine_.playEvent(enemyDefeatedSound());
}

void PacmanAudio::playLifeLost() {
    engine_.playEvent(lifeLostSound());
}

void PacmanAudio::playLevelCompleted() {
    engine_.playEvent(levelCompletedSound());
}

void PacmanAudio::playExtraLife() {
    engine_.playEvent(extraLifeSound());
}

void PacmanAudio::updateHunterVoice(unsigned long deltaTimeMs, int nearestHunter, bool frightened) {
    // No hunter on the board: the siren goes quiet.
    if (nearestHunter < 0 && !frightened) {
        voiceTimer_ = 0;
        voiceHunter_ = -1;
        voiceFrightened_ = false;
        return;
    }

    voiceTimer_ += deltaTimeMs;
    const unsigned long period = frightened ? kFrightenedVoicePeriodMs : kHunterVoicePeriodMs;
    if (voiceTimer_ < period) {
        return;
    }
    voiceTimer_ = 0;

    if (frightened) {
        voiceFrightened_ = true;
        voiceHunter_ = -1;
        engine_.playEvent(frightenedVoice());
        return;
    }

    voiceFrightened_ = false;
    voiceHunter_ = nearestHunter;
    engine_.playEvent(hunterVoice(kHunterVoiceHz[nearestHunter]));
}

}  // namespace pacman::audio
