#pragma once

/*
 * The off-board music bank: music id -> the APU track the build ships.
 *
 * WHY THIS HEADER HAS NO ENGINE OR APU TYPE
 *
 * `AudioMusicTypes.h` (the APU's `MusicTrack`/`MusicNote`) is not on every
 * build's include path, and this header is included by host tests that link
 * `lib/doom` without the engine. Only a pointer to the forward-declared track
 * crosses it; `MusicBank.cpp` includes the generated asset, so the real types
 * stay in one translation unit.
 *
 * WHAT SHIPS, AND WHY ONLY ONE TRACK
 *
 * T47 converts MUS offline. `D_E1M1` is 2,145 `MusicNote`s = 34,320 B and
 * fits the board's flash headroom; `D_INTER` would be 3,975 = 63,600 B and
 * does not. The title (`D_INTRO`, 1,280 B) fits but no scene requests
 * `mus_intro`, so it is checked by the offline gate and not linked. Every id
 * other than `mus_e1m1` therefore answers nullptr, and `S_ChangeMusic` leaves
 * the level quiet rather than holding a request that can never play.
 */

namespace pixelroot32 {
namespace audio {
struct MusicTrack;
}  // namespace audio
}  // namespace pixelroot32

namespace doom {
namespace doomc {

/**
 * @brief The shipped track for a `musicenum_t` id, or nullptr.
 *
 * Only `mus_e1m1` resolves. The pointer is to `const` flash data and lives for
 * the process's life, so `MusicPlayer::play()` may keep it.
 */
const pixelroot32::audio::MusicTrack* musicBankTrack(int music_id);

}  // namespace doomc
}  // namespace doom
