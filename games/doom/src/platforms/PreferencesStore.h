#pragma once

// The board's SettingsStore: the Arduino-ESP32 `Preferences` API over the `nvs`
// partition the project's table already carries (0x9000, 24 KB). No new
// partition. This is the only file that names Arduino for the settings; the
// game code and the seam in doomc/SettingsStore.h know nothing about it.
//
// One namespace ("doom"), one blob key ("cfg"). Every call opens and closes the
// namespace, so nothing stays mapped between menu changes.
#if defined(ARDUINO)

#include <Preferences.h>

#include "doomc/SettingsStore.h"

namespace doom {
namespace platforms {

class PreferencesStore : public doomc::SettingsStore {
public:
    std::size_t read(std::uint8_t* out, std::size_t capacity) override {
        Preferences preferences;
        // Read-only open: a namespace that has never been written does not exist
        // yet, and begin() answers false instead of creating it.
        if (!preferences.begin(kNamespace, true)) {
            return 0u;
        }
        const std::size_t held = preferences.getBytesLength(kKey);
        std::size_t copied = 0u;
        if (held != 0u) {
            copied = preferences.getBytes(kKey, out, capacity < held ? capacity : held);
        }
        preferences.end();
        (void)copied;
        return held;
    }

    bool write(const std::uint8_t* data, std::size_t size) override {
        Preferences preferences;
        if (!preferences.begin(kNamespace, false)) {
            return false;
        }
        const std::size_t written = preferences.putBytes(kKey, data, size);
        preferences.end();
        return written == size;
    }

private:
    static constexpr const char* kNamespace = "doom";
    static constexpr const char* kKey = "cfg";
};

}  // namespace platforms
}  // namespace doom

#endif  // ARDUINO
