#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "m_menu.h"

namespace doom {
namespace doomc {

/**
 * @brief Where the settings live between boots (D5.2): the persistence seam.
 *
 * The C game code (`lib/doom/m_menu.c`) knows the settings and their stored
 * form -- 8 checksummed bytes -- and nothing about storage. This interface is
 * the only thing between those bytes and the medium, so the same
 * `loadSettings()` / `saveSettings()` run against the board's NVS
 * (`platforms/PreferencesStore.h`, the Arduino `Preferences` API) and against
 * `MemorySettingsStore` in a host test.
 *
 * A store moves opaque bytes. It does not validate them: a missing record, a
 * short read and a record someone else wrote are all `loadSettings()`'s to
 * reject, through `m_settings_decode()`.
 */
class SettingsStore {
public:
    virtual ~SettingsStore() {}

    /**
     * @brief Reads the stored record.
     * @param out      Receives up to @p capacity bytes.
     * @param capacity Size of @p out.
     * @return The number of bytes the store holds for the record (not clipped to
     *         @p capacity, so an oversized record is visible), or 0 when there is
     *         none.
     */
    virtual std::size_t read(std::uint8_t* out, std::size_t capacity) = 0;

    /**
     * @brief Replaces the stored record.
     * @return true when the medium accepted all @p size bytes.
     */
    virtual bool write(const std::uint8_t* data, std::size_t size) = 0;
};

/// A store in RAM, for the host test: it loses its record when it is destroyed,
/// which is exactly what a "reboot" is in that test (a second object over the
/// same bytes is how the test models the medium surviving it).
class MemorySettingsStore : public SettingsStore {
public:
    std::size_t read(std::uint8_t* out, std::size_t capacity) override {
        const std::size_t copy = size_ < capacity ? size_ : capacity;
        if (copy != 0u) {
            std::memcpy(out, bytes_, copy);
        }
        return size_;
    }

    bool write(const std::uint8_t* data, std::size_t size) override {
        if (failWrites_ || size > sizeof bytes_) {
            return false;
        }
        std::memcpy(bytes_, data, size);
        size_ = size;
        ++writes_;
        return true;
    }

    /// Test hooks: a record the medium holds, a failing medium, a write counter.
    void poke(const std::uint8_t* data, std::size_t size) {
        std::memcpy(bytes_, data, size < sizeof bytes_ ? size : sizeof bytes_);
        size_ = size < sizeof bytes_ ? size : sizeof bytes_;
    }
    void setFailWrites(bool fail) { failWrites_ = fail; }
    unsigned writes() const { return writes_; }

private:
    std::uint8_t bytes_[16] = {};
    std::size_t size_ = 0u;
    bool failWrites_ = false;
    unsigned writes_ = 0u;
};

/**
 * @brief Loads the settings, or the defaults when the store has nothing usable.
 *
 * @return true when a valid record was read. false means @p out holds the
 *         defaults -- no record, a wrong size, a bad magic/version/checksum or a
 *         value out of range. Nothing is half-trusted and nothing is clamped.
 */
inline bool loadSettings(SettingsStore& store, m_settings_t* out) {
    std::uint8_t blob[M_SETTINGS_BLOB_SIZE + 1u] = {};
    const std::size_t held = store.read(blob, sizeof blob);
    if (held != M_SETTINGS_BLOB_SIZE) {
        m_settings_default(out);
        return false;
    }
    return m_settings_decode(blob, held, out) != 0;
}

/// Encodes and stores @p settings. false when the medium refused.
inline bool saveSettings(SettingsStore& store, const m_settings_t& settings) {
    std::uint8_t blob[M_SETTINGS_BLOB_SIZE];
    m_settings_encode(&settings, blob);
    return store.write(blob, sizeof blob);
}

}  // namespace doomc
}  // namespace doom
