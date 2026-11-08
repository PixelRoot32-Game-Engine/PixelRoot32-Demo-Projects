#include "game/rules/SettingsSave.h"

namespace top_down_city::settings_save {

Gate idle() {
    Gate gate;
    gate.dirty = false;
    gate.idleMs = 0;
    return gate;
}

void pressed(Gate& gate) {
    gate.dirty = true;
    gate.idleMs = 0;
}

bool due(Gate& gate, int deltaMs, bool safe) {
    if (!gate.dirty) {
        return false;
    }
    if (deltaMs > 0) {
        // Saturate at kIdleMs: nothing needs a longer count, and it cannot wrap.
        gate.idleMs = (deltaMs >= kIdleMs - gate.idleMs) ? kIdleMs
                                                         : gate.idleMs + deltaMs;
    }
    return safe && gate.idleMs >= kIdleMs;
}

void saved(Gate& gate, bool ok) {
    gate.idleMs = 0;
    if (ok) {
        gate.dirty = false;
    }
}

}  // namespace top_down_city::settings_save
