#pragma once

/*
 * The off-board music bank: music id -> the APU track the build ships.
 *
 * WHY THIS HEADER HAS NO ENGINE OR APU TYPE
 *
 * `AudioMusicTypes.h` (the APU's `MusicTrack`/`MusicNote`) is not on every
 * build's include path, and this header is included by host tests that link
 * `lib/doom` without the engine. Only a pointer to the forward-declared track
 * crosses it; `MusicBank.cpp` includes the generated assets, so the real types
 * stay in one translation unit.
 *
 * WHAT SHIPS, AND WHY THE 4 MB BUILD PLAYS ONE THEME (4 MB)
 *
 * MUS is converted offline. `D_E1M1` is 2,145 `MusicNote`s = 34,320 B and fits
 * the board's flash headroom (57,847 B free); `D_INTER` would be 3,975 = 63,600 B
 * and does not. The app cannot hold the other eight level themes (another
 * 209,696 B), and the container's spare is not enough for them at 16 B/note
 * either. So a build without `DOOM_FULL_PROFILE` answers **E1M1's track for
 * every level id** (`mus_e1m1`..`mus_e1m9`): a level change restarts the same
 * music instead of going silent. The dummy row, the intermission, the title and
 * past `NUMMUSIC` still answer nullptr, so `S_ChangeMusic` leaves those quiet
 * rather than holding a request that can never play.
 *
 * WHAT THE FULL PROFILE SHIPS
 *
 * `DOOM_FULL_PROFILE` (defined by the native environment only) commits all nine
 * level themes -- `src/assets/music/MusE1M1.inc` .. `MusE1M9.inc` -- and each id
 * selects its own track. Nothing else changes: the gate is additive, so every
 * 4 MB build keeps the one-theme lookup and never links the other eight arrays.
 * The bank is the only place that knows which asset an id maps to, which is why
 * the gate lives here and nowhere else.
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
 * With `DOOM_FULL_PROFILE` every level theme `mus_e1m1`..`mus_e1m9` resolves to
 * its own track; without it they all resolve to E1M1's. Any other id (the dummy
 * row, the intermission, the title, past `NUMMUSIC`) answers nullptr. The
 * pointer is to `const` flash data and lives for the process's life, so
 * `MusicPlayer::play()` may keep it.
 *
 * @param music_id A `musicenum_t` value.
 * @return The track for that id, or nullptr when the build ships none.
 */
const pixelroot32::audio::MusicTrack* musicBankTrack(int music_id);

}  // namespace doomc
}  // namespace doom
