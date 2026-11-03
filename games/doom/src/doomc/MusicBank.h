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
 * WHAT SHIPS, AND WHY EVERY LEVEL PLAYS IT (4 MB)
 *
 * MUS is converted offline. `D_E1M1` is 2,145 `MusicNote`s = 34,320 B and fits
 * the board's flash headroom (57,847 B free); `D_INTER` would be 3,975 = 63,600 B
 * and does not. The app cannot hold the other eight level themes (another
 * ~270 KB), and the container's spare is not enough for them at 16 B/note
 * either. So the 4 MB build answers **E1M1's track for every level id**
 * (`mus_e1m1`..`mus_e1m9`): a level change restarts the same music instead of
 * going silent. The dummy row, the intermission, the title and past `NUMMUSIC`
 * still answer nullptr, so `S_ChangeMusic` leaves those quiet rather than
 * holding a request that can never play.
 *
 * That is the 4 MB limitation. An ESP32-S3 16 MB profile is where each id
 * gets its own track; the bank is the only place that has to change, because
 * nothing else knows which asset an id maps to.
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
