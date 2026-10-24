#pragma once

#include <cstdint>

namespace spaceinvaders::rules {

/**
 * @file MarchSound.h
 * @brief The four-voice march beat and its live-count interval table (S16).
 *
 * The reference does not run the march as a music track. Its sound task fires
 * one short pulse, holds it for four ticks, rotates to the next of four voices,
 * and waits for an interval chosen from the number of live aliens. This class
 * is that mechanism and nothing else: no audio API, no allocation, so the host
 * test can drive it tick by tick.
 *
 * The beat is deliberately independent of the rack's own sweep (D5). Both are
 * keyed on the live count, but the sweep length is the count itself while the
 * beat reads the table below, so at fifty-five aliens the tones are faster than
 * the formation and at fifty they are slower.
 */

/// Number of entries in the key and interval tables.
constexpr int MarchTableEntries = 16;
/// Number of rotating fleet voices.
constexpr int MarchToneCount = 4;
/// Ticks one tone is held before the voice stops until the next beat
/// (`fleetSndHold = 0x04` in the documented disassembly).
constexpr uint16_t MarchHoldTicks = 4;

/**
 * @brief Live-alien key of each interval entry.
 *
 * The interval is the first entry whose key is less than or equal to the live
 * count, so 55 and 50 both use entry 0 and 1 uses entry 15. Counts above 50
 * clamp to entry 0; zero is not in the table because a cleared rack has no
 * march at all.
 */
constexpr uint8_t MarchKeyAliens[MarchTableEntries] = {
    50, 43, 36, 28, 22, 17, 13, 10, 8, 7, 6, 5, 4, 3, 2, 1,
};

/// Ticks between beats, paired with MarchKeyAliens (S16).
constexpr uint16_t MarchIntervalTicks[MarchTableEntries] = {
    52, 46, 39, 34, 28, 24, 21, 19, 16, 14, 13, 12, 11, 9, 7, 5,
};

/**
 * @brief Pitch of each rotating fleet voice, in Hz.
 *
 * PROVISIONAL, NOT MEASURED. The reviewed documentation gives the four-voice
 * rotation, the two tables and the four-tick hold; it does not give the
 * absolute pitch of each analog fleet voice. These four descending values are
 * a stated placeholder until a reference recording is measured (T6.4). Do not
 * cite them as transcribed.
 */
constexpr float MarchToneHz[MarchToneCount] = {110.0f, 104.0f, 98.0f, 92.0f};

/**
 * @class MarchSound
 * @brief Beat countdown, rotating tone and the live-count interval table (S16).
 *
 * The caller advances it once per 60 Hz tick with the current live alien count.
 * A beat is reported on the tick the countdown expires; the tone that beat uses
 * is then available from toneIndex(), which stays fixed until the next beat so
 * the caller can read it after the tick returns.
 */
class MarchSound {
public:
    /**
     * @brief Returns the beat to its start-of-run and start-of-wave state.
     *
     * The countdown reloads with the interval for a full fifty-five-alien rack
     * and the next beat is tone 0, so a new wave opens on the first band and the
     * first voice rather than continuing the previous wave's cycle.
     */
    void reset();

    /**
     * @brief Advances the beat by one 60 Hz tick.
     *
     * A zero count means the rack is gone: no beat fires and the tone does not
     * rotate, so clearing the formation cannot leave a tone behind. Otherwise
     * the countdown is decremented and a beat fires when it reaches zero; the
     * countdown then reloads from intervalTicks(aliveAliens), so a count change
     * takes effect from the next beat and never shortens the gap in progress.
     *
     * @param aliveAliens Current live aliens in the rack.
     * @return true when a beat is due on this tick.
     */
    bool tick(uint8_t aliveAliens);

    /**
     * @brief Tone the most recent beat used.
     *
     * Stays at its last value between beats, so the caller reads it on the beat
     * tick and gets the voice that just fired. It is 0 before the first beat.
     *
     * @return Index into MarchToneHz, 0..MarchToneCount-1.
     */
    int toneIndex() const;

    /**
     * @brief Ticks between beats for a live alien count.
     *
     * Pure and independent of the countdown, so the table can be tested on its
     * own. The index is the first key that is less than or equal to the count.
     *
     * @param aliveAliens Live aliens; 0 reports no interval.
     * @return Ticks to the next beat, or 0 when the rack is empty.
     */
    static uint16_t intervalTicks(uint8_t aliveAliens);

    /**
     * @brief Interval that loaded the countdown currently running.
     * @return Ticks between beats chosen at the last beat or reset.
     */
    uint16_t lastIntervalTicks() const;

private:
    /// Ticks left before the next beat; zero only mid-reload.
    uint16_t countdown = MarchIntervalTicks[0];
    /// Interval the current countdown was loaded with.
    uint16_t interval = MarchIntervalTicks[0];
    /// Voice the next beat will use.
    uint8_t nextTone = 0;
    /// Voice the last beat used.
    uint8_t lastTone = 0;
};

}  // namespace spaceinvaders::rules
