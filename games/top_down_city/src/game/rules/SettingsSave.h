#pragma once

/**
 * When a changed audio setting is written to the store.
 *
 * A Start press changes the settings in memory at once (the player hears it),
 * but a settings save stalls an NVS write 4-13 ms, so it must never happen per
 * press or while driving. Rule: a press marks the gate dirty and restarts an
 * idle count; the write is due when the count has reached `kIdleMs` AND the
 * caller says the moment is safe (on foot and standing still). A failed write
 * stays dirty and is retried after another idle period, never next frame.
 *
 * Pure and engine-free; the scene feeds it real milliseconds.
 */
namespace top_down_city::settings_save {

/// Quiet time after the last press before the write is attempted.
constexpr int kIdleMs = 1000;

struct Gate {
    bool dirty;   ///< a change the store has not seen
    int  idleMs;  ///< ms since the last press or failed save (saturates)
};

/// A clean gate.
Gate idle();

/// A press changed the settings: dirty, idle count restarts.
void pressed(Gate& gate);

/// Advance by `deltaMs` and say whether to write now.
/// @param safe true only when a write cannot cost a visible frame.
bool due(Gate& gate, int deltaMs, bool safe);

/// Report the write's outcome: success clears; failure stays dirty and waits
/// another `kIdleMs`.
void saved(Gate& gate, bool ok);

}  // namespace top_down_city::settings_save
