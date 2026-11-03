#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "m_menu.h"

/*
 * The settings record on the engine's persistence layer.
 *
 * The engine keeps a small game record behind `IKeyValueStore`, and its ESP32
 * adapter is byte-compatible with Arduino `Preferences` (the engine's own README
 * proves it on the board). That is the whole migration: the same eight bytes,
 * the same namespace and key, written through the engine's interface instead of
 * `Preferences`, so a record a board already holds keeps decoding without a
 * version bump or a rewrite.
 *
 * The engine's `RecordCodec` envelope is deliberately NOT used here. The record
 * predates the engine and its bytes are a contract (two golden records are
 * pinned by tools/menu_check.py); wrapping it in a 16-byte envelope would change
 * what the board already stores. The slot store in DoomPersistence.cpp does use
 * the envelope, because those records are new.
 */
#if DOOM_PERSISTENCE
#include "persistence/KeyValueStore.h"

#if !PIXELROOT32_ENABLE_PERSISTENCE
#error "DOOM_PERSISTENCE needs PIXELROOT32_ENABLE_PERSISTENCE=1 (the engine's persistence module)"
#endif
#endif

namespace doom {
namespace doomc {

/**
 * @brief Where the settings live between boots: the persistence seam.
 *
 * The C game code (`lib/doom/m_menu.c`) knows the settings and their stored
 * form -- 8 checksummed bytes -- and nothing about storage. This interface is
 * the only thing between those bytes and the medium, so the same
 * `loadSettings()` / `saveSettings()` run against the board's NVS
 * (`KeyValueSettingsStore` over the engine's `Esp32NvsKeyValueStore`), against
 * `MemorySettingsStore` in a host test, and against the desktop mirror's RAM.
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

#if DOOM_PERSISTENCE

/**
 * @brief The settings record on the engine's `IKeyValueStore`.
 *
 * One source for both platforms: the board's `Esp32NvsKeyValueStore("doom", 64)`
 * and the host's `HostFileKeyValueStore("save_data", "doom", 64)` are both an
 * `IKeyValueStore`, and this class is the only thing between that interface and
 * `loadSettings()`/`saveSettings()`. The host and the board therefore run the
 * same encode/decode and only the medium differs.
 *
 * Namespace `doom`, key `cfg`: exactly what the Arduino `Preferences` store wrote,
 * so the record on a board that already has settings survives the switch byte for
 * byte. A medium that cannot be initialised answers `Unavailable` from every
 * call; `read()` then reports 0 and `write()` false, which is what makes the game
 * run on the defaults instead of failing to boot. The owner reports that once as
 * `SHOW,persist,status=...`.
 *
 * A write commits: the store's own `commit()` is the durability point, and the
 * settings record is written once, when the player leaves the options screen, so
 * there is no batch to group (the engine's guide asks for a commit at a safe
 * point, not for one per set).
 */
class KeyValueSettingsStore final : public SettingsStore {
public:
    /**
     * @brief Wraps an already-initialised store.
     * @param store The medium; it must outlive this object and `init()` must have
     *              returned Ok before a write can succeed.
     */
    explicit KeyValueSettingsStore(pixelroot32::persistence::IKeyValueStore& store)
        : store_(store) {}

    std::size_t read(std::uint8_t* out, std::size_t capacity) override {
        std::size_t held = 0u;
        const pixelroot32::persistence::StoreResult result =
            store_.getBlob(kKey, out, capacity, held);
        // Ok and TooLarge both report the stored size: the record's own decoder
        // is what judges a size it did not write (see loadSettings()).
        if (result == pixelroot32::persistence::StoreResult::Ok
            || result == pixelroot32::persistence::StoreResult::TooLarge) {
            return held;
        }
        return 0u;
    }

    bool write(const std::uint8_t* data, std::size_t size) override {
        if (store_.setBlob(kKey, data, size) != pixelroot32::persistence::StoreResult::Ok) {
            return false;
        }
        return store_.commit() == pixelroot32::persistence::StoreResult::Ok;
    }

private:
    static constexpr const char* kKey = "cfg";
    pixelroot32::persistence::IKeyValueStore& store_;
};

#endif  // DOOM_PERSISTENCE

}  // namespace doomc
}  // namespace doom
